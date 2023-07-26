/***************************************************************************
 * mseed2esync.c - miniSEED to Enhanced SYNC Listing
 *
 * Opens a user specified file, parses the miniSEED records and prints
 * an Enhanced SYNC Listing.
 *
 * In general critical error messages are prefixed with "ERROR:" and
 * the return code will be 1.  On successfull operation the return
 * code will be 0.
 *
 * Written by Chad Trabant, EarthScope Data Services.
 ***************************************************************************/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libmseed.h>

#include "md5.h"

static void trimsegments (MS3TraceList *mstl);
static void printesynclist (MS3TraceList *mstl, char *dccid);
static void comparetraces (MS3TraceList *mstl);
static int processparam (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static int addfile (char *filename);
static int addlistfile (char *filename);
static int my_globmatch (const char *string, const char *pattern);
static void usage (void);

#define VERSION "0.9"
#define PACKAGE "mseed2esync"

static int retval         = 0;
static flag verbose       = 0;
static flag compare       = 0;
static flag splitversion  = 1; /* Controls consideration of publication version */
static flag dataflag      = 1; /* Controls decompression of data and production of MD5 */
static char *dccidstr     = 0;
static nstime_t starttime = NSTUNSET; /* Limit to records containing or after starttime */
static nstime_t endtime   = NSTUNSET; /* Limit to records containing or before endtime */
static char *match        = 0; /* Glob match pattern */
static char *reject       = 0; /* Glob reject pattern */

static double timetol;     /* Time tolerance for continuous traces */
static double sampratetol; /* Sample rate tolerance for continuous traces */
static MS3Tolerance tolerance = {.time = NULL, .samprate = NULL};
double timetol_callback (const MS3Record *msr) { return timetol; }
double samprate_callback (const MS3Record *msr) { return sampratetol; }

struct filelink
{
  char *filename;
  struct filelink *next;
};

struct filelink *filelist     = 0;
struct filelink *filelisttail = 0;

int
main (int argc, char **argv)
{
  struct filelink *flp;
  MS3Record *msr     = 0;
  MS3TraceList *mstl = 0;
  int retcode        = MS_NOERROR;
  uint32_t flags     = 0;
  char stime[30];

  /* Set default error message prefix */
  ms_loginit (NULL, NULL, NULL, "ERROR: ");

  /* Process given parameters (command line and parameter file) */
  if (processparam (argc, argv) < 0)
    return 1;

  if (dataflag)
    flags |= MSF_UNPACKDATA;

  flags |= MSF_PNAMERANGE;

  mstl = mstl3_init (NULL);

  flp = filelist;

  while (flp)
  {
    /* Loop over the input file */
    while ((retcode = ms3_readmsr (&msr, flp->filename, flags, verbose)) == MS_NOERROR)
    {
      /* Check if record matches start/end time criteria */
      if (starttime != NSTUNSET || endtime != NSTUNSET)
      {
        nstime_t recendtime = msr3_endtime (msr);

        if (starttime != NSTUNSET && (msr->starttime < starttime && !(msr->starttime <= starttime && recendtime >= starttime)))
        {
          if (verbose >= 3)
          {
            ms_nstime2timestr (msr->starttime, stime, SEEDORDINAL, NANO_MICRO);
            ms_log (1, "Skipping (starttime) %s, %s\n", msr->sid, stime);
          }
          continue;
        }

        if (endtime != NSTUNSET && (recendtime > endtime && !(msr->starttime <= endtime && recendtime >= endtime)))
        {
          if (verbose >= 3)
          {
            ms_nstime2timestr (msr->starttime, stime, SEEDORDINAL, NANO_MICRO);
            ms_log (1, "Skipping (starttime) %s, %s\n", msr->sid, stime);
          }
          continue;
        }
      }

      if (match || reject)
      {
        /* Check if record is matched by the match pattern */
        if (match)
        {
          if (my_globmatch (msr->sid, match) == 0)
          {
            if (verbose >= 3)
            {
              ms_nstime2timestr (msr->starttime, stime, ISOMONTHDAY, NANO);
              ms_log (1, "Skipping (match) %s, %s\n", msr->sid, stime);
            }
            continue;
          }
        }

        /* Check if record is rejected by the reject pattern */
        if (reject)
        {
          if (my_globmatch (msr->sid, reject) != 0)
          {
            if (verbose >= 3)
            {
              ms_nstime2timestr (msr->starttime, stime, ISOMONTHDAY, NANO);
              ms_log (1, "Skipping (reject) %s, %s\n", msr->sid, stime);
            }
            continue;
          }
        }
      }

      /* Add to TraceList */
      mstl3_addmsr (mstl, msr, splitversion, flags, 1, &tolerance);
    }

    /* Print error if not EOF */
    if (retcode != MS_ENDOFFILE)
    {
      ms_log (2, "Cannot read %s: %s\n", flp->filename, ms_errorstr (retcode));
      ms3_readmsr (&msr, NULL, 0, 0);
      exit (1);
    }

    /* Make sure everything is cleaned up */
    ms3_readmsr (&msr, NULL, 0, 0);

    flp = flp->next;
  } /* End of looping over file list */

  /* Trim each segment to specified time range */
  if (starttime != NSTUNSET || endtime != NSTUNSET)
    trimsegments (mstl);

  /* Print the ESYNC listing */
  printesynclist (mstl, dccidstr);

  if (compare)
    comparetraces (mstl);

  if (mstl)
    mstl3_free (&mstl, 0);

  return retval;
} /* End of main() */

/***************************************************************************
 * trimsegments():
 *
 * Trim data segments to specified start and end times.  The specified
 * or default time tolerance is used to liberally match sample times:
 *
 *   The first sample can be at start time minus the tolerance value
 *   The last sample can be at the end time plus the tolerance value
 *
 * If timetol is -1 the default tolerance of 1/2 sample period is used.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
trimsegments (MS3TraceList *mstl)
{
  MS3TraceID *id   = 0;
  MS3TraceSeg *seg = 0;

  nstime_t sampletime;
  nstime_t nsdelta;
  nstime_t nstimetol = 0;
  int64_t trimcount;
  int samplesize;
  void *datasamples;

  if (!mstl)
    return;

  /* Loop through trace list */
  id = mstl->traces.next[0];
  while (id)
  {
    /* Loop through segment list */
    seg = id->first;
    while (seg)
    {
      /* Skip segments that do not have integer, float or double types */
      if (seg->sampletype != 'i' && seg->sampletype != 'f' && seg->sampletype != 'd')
      {
        seg = seg->next;
        continue;
      }

      /* Calculate high-precision sample period */
      nsdelta = (nstime_t)((seg->samprate) ? (NSTMODULUS / seg->samprate) : 0.0);

      /* Calculate high-precision time tolerance */
      if (timetol == -1.0)
        nstimetol = (nstime_t)(0.5 * nsdelta); /* Default time tolerance is 1/2 sample period */
      else if (timetol >= 0.0)
        nstimetol = (nstime_t)(timetol * NSTMODULUS);

      samplesize = ms_samplesize (seg->sampletype);

      /* Trim samples from beginning of segment if earlier than starttime */
      if (starttime != NSTUNSET && seg->starttime < starttime)
      {
        trimcount  = 0;
        sampletime = seg->starttime;
        while (sampletime < (starttime - nstimetol))
        {
          sampletime += nsdelta;
          trimcount++;
        }

        if (trimcount > 0 && trimcount < seg->numsamples)
        {
          if (verbose)
            ms_log (1, "Trimming %lld samples from beginning of trace for %s\n",
                    (long long)trimcount, id->sid);

          memmove (seg->datasamples,
                   (char *)seg->datasamples + (trimcount * samplesize),
                   (seg->numsamples - trimcount) * samplesize);

          datasamples = realloc (seg->datasamples, (seg->numsamples - trimcount) * samplesize);

          if (!datasamples)
          {
            ms_log (2, "Cannot reallocate sample buffer\n");
            return;
          }

          seg->datasamples = datasamples;
          seg->starttime += MS_EPOCH2NSTIME ((trimcount / seg->samprate));
          seg->numsamples -= trimcount;
          seg->samplecnt -= trimcount;
        }
      }

      /* Trim samples from end of segment if later than endtime */
      if (endtime != NSTUNSET && seg->endtime > endtime)
      {
        trimcount  = 0;
        sampletime = seg->endtime;
        while (sampletime > (endtime + nstimetol))
        {
          sampletime -= nsdelta;
          trimcount++;
        }

        if (trimcount > 0 && trimcount < seg->numsamples)
        {
          if (verbose)
            ms_log (1, "Trimming %lld samples from end of trace for %s\n",
                    (long long)trimcount, id->sid);

          datasamples = realloc (seg->datasamples, (seg->numsamples - trimcount) * samplesize);

          if (!datasamples)
          {
            ms_log (2, "Cannot reallocate sample buffer\n");
            return;
          }

          seg->datasamples = datasamples;
          seg->endtime -= MS_EPOCH2NSTIME ((trimcount / seg->samprate));
          seg->numsamples -= trimcount;
          seg->samplecnt -= trimcount;
        }
      }

      seg = seg->next;
    }

    id = id->next[0];
  }

  return;
} /* End of trimsegments() */

/***************************************************************************
 * printesynclist():
 *
 * Print the MS3TraceList as an Enhanced SYNC Listing.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
printesynclist (MS3TraceList *mstl, char *dccid)
{
  MS3TraceID *id   = 0;
  MS3TraceSeg *seg = 0;
  char starttime[30];
  char endtime[30];
  char yearday[30];
  char network[11];
  char station[11];
  char location[11];
  char channel[11];
  char quality[10];
  time_t now;
  struct tm *nt;

  md5_state_t pms;
  md5_byte_t digest[16];
  int samplesize;
  int idx;
  char digeststr[33];

  if (!mstl)
    return;

  /* Generate current time stamp */
  now = time (NULL);
  nt  = localtime (&now);
  nt->tm_year += 1900;
  nt->tm_yday += 1;
  snprintf (yearday, sizeof (yearday), "%04d,%03d", nt->tm_year, nt->tm_yday);

  /* Print SYNC header line */
  ms_log (0, "%s|%s\n", (dccid) ? dccid : "DCC", yearday);

  /* Loop through trace list */
  id = mstl->traces.next[0];
  while (id)
  {
    /* Split SID into network, station, location and channel */
    ms_sid2nslc (id->sid, network, station, location, channel);

    /* Loop through segment list */
    seg = id->first;
    while (seg)
    {
      ms_nstime2timestr (seg->starttime, starttime, SEEDORDINAL, NANO_MICRO);
      ms_nstime2timestr (seg->endtime, endtime, SEEDORDINAL, NANO_MICRO);

      /* Calculate MD5 hash of sample values if samples present */
      if (seg->datasamples)
      {
        samplesize = ms_samplesize (seg->sampletype);
        memset (&pms, 0, sizeof (md5_state_t));
        md5_init (&pms);
        md5_append (&pms, (const md5_byte_t *)seg->datasamples, (seg->numsamples * samplesize));
        md5_finish (&pms, digest);

        for (idx = 0; idx < 16; idx++)
          sprintf (digeststr + (idx * 2), "%02x", digest[idx]);
      }

      /* Set quality flag, mapping to legacy codes for backwards compatibility */
      switch (id->pubversion)
      {
      case 1:
        quality[0] = 'D';
        quality[1] = 0;
        break;
      case 2:
        quality[0] = 'R';
        quality[1] = 0;
        break;
      case 3:
        quality[0] = 'Q';
        quality[1] = 0;
        break;
      case 4:
        quality[0] = 'M';
        quality[1] = 0;
        break;
      default:
        snprintf (quality, sizeof (quality), "%d", id->pubversion);
        break;
      }

      /* Print SYNC line */
      ms_log (0, "%s|%s|%s|%s|%s|%s||%.10g|%lld|||%s|%.32s|||%s\n",
              network, station, location, channel,
              starttime, endtime, seg->samprate, (long long int)seg->samplecnt,
              (id->pubversion) ? quality : "",
              (seg->datasamples) ? digeststr : "",
              yearday);

      seg = seg->next;
    }

    id = id->next[0];
  }

  return;
} /* End of printesynclist() */

/***************************************************************************
 * comparetraces():
 *
 * Compare sample values for each segment
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
comparetraces (MS3TraceList *mstl)
{
  MS3TraceID *id    = 0;
  MS3TraceID *tid   = 0;
  MS3TraceSeg *seg  = 0;
  MS3TraceSeg *tseg = 0;
  char start[30];
  char end[30];
  char tstart[30];
  char tend[30];
  int64_t idx = 0;

  if (!mstl)
    return;

  /* Loop through trace list */
  id = mstl->traces.next[0];
  while (id)
  {
    /* Loop through segment list */
    seg = id->first;
    while (seg)
    {
      ms_nstime2timestr (seg->starttime, start, SEEDORDINAL, NANO_MICRO);
      ms_nstime2timestr (seg->endtime, end, SEEDORDINAL, NANO_MICRO);

      if (!seg->datasamples)
      {
        ms_log (2, "%s, %s, %s :: No data samples\n", id->sid, start, end);
        seg = seg->next;
        continue;
      }

      /* Loop through trace list for targets */
      tid = id;
      while (tid)
      {
        /* Loop through target segment list */
        tseg = (tid == id) ? seg->next : tid->first;
        while (tseg)
        {
          if (!tseg->datasamples)
          {
            ms_log (1, "%s, %s, %s :: No data samples\n", tid->sid, tstart, tend);
            tseg = tseg->next;
            continue;
          }

          ms_nstime2timestr (tseg->starttime, tstart, SEEDORDINAL, NANO_MICRO);
          ms_nstime2timestr (tseg->endtime, tend, SEEDORDINAL, NANO_MICRO);

          if (seg->sampletype != tseg->sampletype)
          {
            ms_log (1, "%s and %s :: Sample type mismatch\n", id->sid, tid->sid);
            tseg = tseg->next;
            continue;
          }

          if (seg->numsamples != tseg->numsamples)
          {
            ms_log (1, "%s (%lld) and %s (%lld) :: Sample count mismatch\n",
                    id->sid, (long long int)seg->numsamples,
                    tid->sid, (long long int)tseg->numsamples);
            tseg = tseg->next;
            continue;
          }

          if (seg->sampletype == 'i')
          {
            int32_t *data  = (int32_t *)seg->datasamples;
            int32_t *tdata = (int32_t *)tseg->datasamples;

            for (idx = 0; idx < seg->numsamples; idx++)
              if (data[idx] != tdata[idx])
              {
                ms_log (0, "Time series are NOT the same, differing at sample %lld (%d versus %d)\n",
                        (long long int)idx + 1, data[idx], tdata[idx]);
                retval = 1;
                break;
              }
          }
          else if (seg->sampletype == 'f')
          {
            float *data  = (float *)seg->datasamples;
            float *tdata = (float *)tseg->datasamples;

            for (idx = 0; idx < seg->numsamples; idx++)
              if (data[idx] != tdata[idx])
              {
                ms_log (0, "Time series are NOT the same, differing at sample %lld (%f versus %f)\n",
                        (long long int)idx + 1, data[idx], tdata[idx]);
                retval = 1;
                break;
              }
          }
          else if (seg->sampletype == 'd')
          {
            double *data  = (double *)seg->datasamples;
            double *tdata = (double *)tseg->datasamples;

            for (idx = 0; idx < seg->numsamples; idx++)
              if (data[idx] != tdata[idx])
              {
                ms_log (0, "Time series are NOT the same, differing at sample %lld (%f versus %f)\n",
                        (long long int)idx + 1, data[idx], tdata[idx]);
                retval = 1;
                break;
              }
          }
          else if (seg->sampletype == 'a')
          {
            char *data  = (char *)seg->datasamples;
            char *tdata = (char *)tseg->datasamples;

            for (idx = 0; idx < seg->numsamples; idx++)
              if (data[idx] != tdata[idx])
              {
                ms_log (0, "Time series are NOT the same, differing at sample %lld (%c versus %c)\n",
                        (long long int)idx + 1, data[idx], tdata[idx]);
                retval = 1;
                break;
              }
          }

          if (idx == seg->numsamples)
          {
            ms_log (0, "Time series are the same, %lld samples compared\n",
                    (long long int)idx);
          }

          ms_log (0, "  %s  %s  %s\n", id->sid, start, end);
          ms_log (0, "  %s  %s  %s\n", tid->sid, tstart, tend);

          tseg = tseg->next;
        }

        tid = tid->next[0];
      }

      seg = seg->next;
    }

    id = id->next[0];
  }

  return;
} /* End of comparetraces() */

/***************************************************************************
 * parameter_proc():
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static int
processparam (int argcount, char **argvec)
{
  int optind;
  char *match_pattern  = 0;
  char *reject_pattern = 0;
  char *tptr;

  /* Process all command line arguments */
  for (optind = 1; optind < argcount; optind++)
  {
    if (strcmp (argvec[optind], "-V") == 0)
    {
      ms_log (1, "%s version: %s\n", PACKAGE, VERSION);
      exit (0);
    }
    else if (strcmp (argvec[optind], "-h") == 0)
    {
      usage ();
      exit (0);
    }
    else if (strncmp (argvec[optind], "-v", 2) == 0)
    {
      verbose += strspn (&argvec[optind][1], "v");
    }
    else if (strcmp (argvec[optind], "-D") == 0)
    {
      dccidstr = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-C") == 0)
    {
      compare = 1;
    }
    else if (strcmp (argvec[optind], "-ts") == 0)
    {
      starttime = ms_timestr2nstime (getoptval (argcount, argvec, optind++));
      if (starttime == NSTUNSET)
        return -1;
    }
    else if (strcmp (argvec[optind], "-te") == 0)
    {
      endtime = ms_timestr2nstime (getoptval (argcount, argvec, optind++));
      if (endtime == NSTUNSET)
        return -1;
    }
    else if (strcmp (argvec[optind], "-m") == 0)
    {
      match_pattern = strdup (getoptval (argcount, argvec, optind++));
    }
    else if (strcmp (argvec[optind], "-r") == 0)
    {
      reject_pattern = strdup (getoptval (argcount, argvec, optind++));
    }
    else if (strcmp (argvec[optind], "-tt") == 0)
    {
      timetol        = strtod (getoptval (argcount, argvec, optind++), NULL);
      tolerance.time = timetol_callback;
    }
    else if (strcmp (argvec[optind], "-rt") == 0)
    {
      sampratetol        = strtod (getoptval (argcount, argvec, optind++), NULL);
      tolerance.samprate = samprate_callback;
    }
    else if (strncmp (argvec[optind], "-", 1) == 0 &&
             strlen (argvec[optind]) > 1)
    {
      ms_log (2, "Unknown option: %s\n", argvec[optind]);
      exit (1);
    }
    else
    {
      tptr = argvec[optind];

      /* Check for an input file list */
      if (tptr[0] == '@')
      {
        if (addlistfile (tptr + 1) < 0)
        {
          ms_log (2, "Error adding list file %s", tptr + 1);
          exit (1);
        }
      }
      /* Otherwise this is an input file */
      else
      {
        /* Add file to global file list */
        if (addfile (tptr))
        {
          ms_log (2, "Error adding file to input list %s", tptr);
          exit (1);
        }
      }
    }
  }

  /* Make sure input file were specified */
  if (filelist == 0)
  {
    ms_log (2, "No input files were specified\n\n");
    ms_log (1, "%s version %s\n\n", PACKAGE, VERSION);
    ms_log (1, "Try %s -h for usage\n", PACKAGE);
    exit (1);
  }

  /* Add wildcards to match pattern for logical "contains" */
  if (match_pattern)
  {
    if ((match = malloc (strlen (match_pattern) + 3)) == NULL)
    {
      ms_log (2, "Error allocating memory\n");
      exit (1);
    }

    snprintf (match, strlen (match_pattern) + 3, "*%s*", match_pattern);
  }

  /* Add wildcards to reject pattern for logical "contains" */
  if (reject_pattern)
  {
    if ((reject = malloc (strlen (reject_pattern) + 3)) == NULL)
    {
      ms_log (2, "Error allocating memory\n");
      exit (1);
    }

    snprintf (reject, strlen (reject_pattern) + 3, "*%s*", reject_pattern);
  }

  /* Report the program version */
  if (verbose)
    ms_log (1, "%s version: %s\n", PACKAGE, VERSION);

  return 0;
} /* End of parameter_proc() */

/***************************************************************************
 * getoptval:
 * Return the value to a command line option; checking that the value is
 * itself not an option (starting with '-') and is not past the end of
 * the argument list.
 *
 * argcount: total arguments in argvec
 * argvec: argument list
 * argopt: index of option to process, value is expected to be at argopt+1
 *
 * Returns value on success and exits with error message on failure
 ***************************************************************************/
static char *
getoptval (int argcount, char **argvec, int argopt)
{
  if (argvec == NULL || argvec[argopt] == NULL)
  {
    ms_log (2, "getoptval(): NULL option requested\n");
    exit (1);
    return 0;
  }

  /* Special case of '-o -' usage */
  if ((argopt + 1) < argcount && strcmp (argvec[argopt], "-o") == 0)
    if (strcmp (argvec[argopt + 1], "-") == 0)
      return argvec[argopt + 1];

  if ((argopt + 1) < argcount && *argvec[argopt + 1] != '-')
    return argvec[argopt + 1];

  ms_log (2, "Option %s requires a value, try -h for usage\n", argvec[argopt]);
  exit (1);
  return 0;
} /* End of getoptval() */

/***************************************************************************
 * addfile:
 *
 * Add file to end of the global file list (filelist).
 *
 * Returns 0 on success and -1 on error.
 ***************************************************************************/
static int
addfile (char *filename)
{
  struct filelink *newlp;

  if (filename == NULL)
  {
    ms_log (2, "addfile(): No file name specified\n");
    return -1;
  }

  newlp = (struct filelink *)calloc (1, sizeof (struct filelink));

  if (!newlp)
  {
    ms_log (2, "addfile(): Cannot allocate memory\n");
    return -1;
  }

  newlp->filename = strdup (filename);

  if (!newlp->filename)
  {
    ms_log (2, "addfile(): Cannot duplicate string\n");
    return -1;
  }

  /* Add new file to the end of the list */
  if (filelisttail == 0)
  {
    filelist     = newlp;
    filelisttail = newlp;
  }
  else
  {
    filelisttail->next = newlp;
    filelisttail       = newlp;
  }

  return 0;
} /* End of addfile() */

/***************************************************************************
 * addlistfile:
 *
 * Add files listed in the specified file to the global input file list.
 *
 * Returns count of files added on success and -1 on error.
 ***************************************************************************/
static int
addlistfile (char *filename)
{
  FILE *fp;
  char filelistent[1024];
  int filecount = 0;

  if (verbose >= 1)
    ms_log (1, "Reading list file '%s'\n", filename);

  if (!(fp = fopen (filename, "rb")))
  {
    ms_log (2, "Cannot open list file %s: %s\n", filename, strerror (errno));
    return -1;
  }

  while (fgets (filelistent, sizeof (filelistent), fp))
  {
    char *cp;

    /* End string at first newline character */
    if ((cp = strchr (filelistent, '\n')))
      *cp = '\0';

    /* Skip empty lines */
    if (!strlen (filelistent))
      continue;

    /* Skip comment lines */
    if (*filelistent == '#')
      continue;

    if (verbose > 1)
      ms_log (1, "Adding '%s' from list file\n", filelistent);

    if (addfile (filelistent))
      return -1;

    filecount++;
  }

  fclose (fp);

  return filecount;
} /* End of addlistfile() */

/***********************************************************************
 * robust glob pattern matcher
 * ozan s. yigit/dec 1994
 * public domain
 *
 * glob patterns:
 *	*	matches zero or more characters
 *	?	matches any single character
 *	[set]	matches any character in the set
 *	[^set]	matches any character NOT in the set
 *		where a set is a group of characters or ranges. a range
 *		is written as two characters seperated with a hyphen: a-z denotes
 *		all characters between a to z inclusive.
 *	[-set]	set matches a literal hypen and any character in the set
 *	[]set]	matches a literal close bracket and any character in the set
 *
 *	char	matches itself except where char is '*' or '?' or '['
 *	\char	matches char, including any pattern character
 *
 * examples:
 *	a*c		ac abc abbc ...
 *	a?c		acc abc aXc ...
 *	a[a-z]c		aac abc acc ...
 *	a[-a-z]c	a-c aac abc ...
 *
 * Revision 1.4  2004/12/26  12:38:00  ct
 * Changed function name (amatch -> globmatch), variables and
 * formatting for clarity.  Also add matching header globmatch.h.
 *
 * Revision 1.3  1995/09/14  23:24:23  oz
 * removed boring test/main code.
 *
 * Revision 1.2  94/12/11  10:38:15  oz
 * charset code fixed. it is now robust and interprets all
 * variations of charset [i think] correctly, including [z-a] etc.
 *
 * Revision 1.1  94/12/08  12:45:23  oz
 * Initial revision
 ***********************************************************************/

#define GLOBMATCH_TRUE 1
#define GLOBMATCH_FALSE 0
#define GLOBMATCH_NEGATE '^' /* std char set negation char */

/***********************************************************************
 * my_globmatch:
 *
 * Check if a string matches a globbing pattern.
 *
 * Return 0 if string does not match pattern and non-zero otherwise.
 **********************************************************************/
static int
my_globmatch (const char *string, const char *pattern)
{
  int negate;
  int match;
  int c;

  while (*pattern)
  {
    if (!*string && *pattern != '*')
      return GLOBMATCH_FALSE;

    switch (c = *pattern++)
    {

    case '*':
      while (*pattern == '*')
        pattern++;

      if (!*pattern)
        return GLOBMATCH_TRUE;

      if (*pattern != '?' && *pattern != '[' && *pattern != '\\')
        while (*string && *pattern != *string)
          string++;

      while (*string)
      {
        if (my_globmatch (string, pattern))
          return GLOBMATCH_TRUE;
        string++;
      }
      return GLOBMATCH_FALSE;

    case '?':
      if (*string)
        break;
      return GLOBMATCH_FALSE;

      /* set specification is inclusive, that is [a-z] is a, z and
       * everything in between. this means [z-a] may be interpreted
       * as a set that contains z, a and nothing in between.
       */
    case '[':
      if (*pattern != GLOBMATCH_NEGATE)
        negate = GLOBMATCH_FALSE;
      else
      {
        negate = GLOBMATCH_TRUE;
        pattern++;
      }

      match = GLOBMATCH_FALSE;

      while (!match && (c = *pattern++))
      {
        if (!*pattern)
          return GLOBMATCH_FALSE;

        if (*pattern == '-') /* c-c */
        {
          if (!*++pattern)
            return GLOBMATCH_FALSE;
          if (*pattern != ']')
          {
            if (*string == c || *string == *pattern ||
                (*string > c && *string < *pattern))
              match = GLOBMATCH_TRUE;
          }
          else
          { /* c-] */
            if (*string >= c)
              match = GLOBMATCH_TRUE;
            break;
          }
        }
        else /* cc or c] */
        {
          if (c == *string)
            match = GLOBMATCH_TRUE;
          if (*pattern != ']')
          {
            if (*pattern == *string)
              match = GLOBMATCH_TRUE;
          }
          else
            break;
        }
      }

      if (negate == match)
        return GLOBMATCH_FALSE;

      /* If there is a match, skip past the charset and continue on */
      while (*pattern && *pattern != ']')
        pattern++;
      if (!*pattern++) /* oops! */
        return GLOBMATCH_FALSE;
      break;

    case '\\':
      if (*pattern)
        c = *pattern++;
    default:
      if (c != *string)
        return GLOBMATCH_FALSE;
      break;
    }

    string++;
  }

  return !*string;
} /* End of my_globmatch() */

/***************************************************************************
 * usage():
 * Print the usage message.
 ***************************************************************************/
static void
usage (void)
{
  fprintf (stderr, "%s - miniSEED to Enhanced SYNC version: %s\n\n", PACKAGE, VERSION);
  fprintf (stderr, "Usage: %s [options] file1 [file2] [file3] ...\n\n", PACKAGE);
  fprintf (stderr,
           " ## General options ##\n"
           " -V           Report program version\n"
           " -h           Show this usage message\n"
           " -v           Be more verbose, multiple flags can be used\n"
           " -D DCCID     Specify the DCC identifier for SYNC header\n"
           " -C           Compare sample values of time series, to diagnose mismatches\n"
           "\n"
           " ## Data selection options ##\n"
           " -ts time     Limit to samples that start on or after time\n"
           " -te time     Limit to samples that end on or before time\n"
           "                time format: 'YYYY[,DDD,HH,MM,SS,FFFFFF]' delimiters: [,:.]\n"
           " -m match     Limit to records containing the specified pattern\n"
           " -r reject    Limit to records not containing the specfied pattern\n"
           "                Patterns are applied to: 'FDSN:NET_STA_LOC_BAND_SOURCE_SS'\n"
           " -tt secs     Specify a time tolerance for continuous traces\n"
           " -rt diff     Specify a sample rate tolerance for continuous traces\n"
           "\n"
           " files        File(s) of miniSEED records, list files prefixed with '@'\n"
           "\n");
} /* End of usage() */
