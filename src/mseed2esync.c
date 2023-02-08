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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <regex.h>

#include <libmseed.h>

#include "md5.h"

static void trimsegments (MSTraceList *mstl);
static void printesynclist (MSTraceList *mstl, char *dccid);
static void comparetraces (MSTraceList *mstl);
static int processparam (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static int readregexfile (char *regexfile, char **pppattern);
static int lisnumber (char *number);
static int addfile (char *filename);
static int addlistfile (char *filename);
static void usage (void);

#define VERSION "0.4"
#define PACKAGE "mseed2esync"

static int     retval       = 0;
static flag    verbose      = 0;
static flag    compare      = 0;
static flag    dataquality  = 1;    /* Controls consideration of data quality */
static flag    dataflag     = 1;    /* Controls decompression of data and production of MD5 */
static double  timetol      = -1.0; /* Time tolerance for continuous traces */
static double  sampratetol  = -1.0; /* Sample rate tolerance for continuous traces */
static int     reclen       = -1;
static char   *encodingstr  = 0;
static char   *dccidstr     = 0;
static hptime_t starttime   = HPTERROR;  /* Limit to records containing or after starttime */
static hptime_t endtime     = HPTERROR;  /* Limit to records containing or before endtime */
static regex_t *match       = 0;    /* Compiled match regex */
static regex_t *reject      = 0;    /* Compiled reject regex */

struct filelink {
  char *filename;
  uint64_t offset;
  struct filelink *next;
};

struct filelink *filelist = 0;
struct filelink *filelisttail = 0;


int
main (int argc, char **argv)
{
  struct filelink *flp;
  MSRecord *msr = 0;
  MSTraceList *mstl = 0;
  int retcode = MS_NOERROR;

  char envvariable[100];
  off_t filepos = 0;

  char srcname[50];
  char stime[30];

  /* Set default error message prefix */
  ms_loginit (NULL, NULL, NULL, "ERROR: ");

  /* Process given parameters (command line and parameter file) */
  if ( processparam (argc, argv) < 0 )
    return 1;

  /* Setup encoding environment variable if specified, ugly kludge */
  if ( encodingstr )
    {
      snprintf (envvariable, sizeof(envvariable), "UNPACK_DATA_FORMAT=%s", encodingstr);

      if ( putenv (envvariable) )
	{
	  ms_log (2, "Cannot set environment variable UNPACK_DATA_FORMAT\n");
	  return 1;
	}
    }

  mstl = mstl_init (NULL);

  flp = filelist;

  while ( flp != 0 )
    {
      if ( verbose >= 2 )
	{
	  if ( flp->offset )
	    ms_log (1, "Processing: %s (starting at byte %lld)\n", flp->filename, flp->offset);
	  else
	    ms_log (1, "Processing: %s\n", flp->filename);
	}

      /* Set starting byte offset if supplied as negative file position */
      filepos = - flp->offset;

      /* Loop over the input file */
      for (;;)
	{
	  if ( (retcode = ms_readmsr (&msr, flp->filename, reclen, &filepos,
				      NULL, 1, dataflag, verbose)) != MS_NOERROR )
	    break;

	  /* Check if record matches start/end time criteria */
	  if ( starttime != HPTERROR || endtime != HPTERROR )
	    {
	      hptime_t recendtime = msr_endtime (msr);

	      if ( starttime != HPTERROR && (msr->starttime < starttime && ! (msr->starttime <= starttime && recendtime >= starttime)) )
		{
		  if ( verbose >= 3 )
		    {
		      msr_srcname (msr, srcname, 1);
		      ms_hptime2seedtimestr (msr->starttime, stime, 1);
		      ms_log (1, "Skipping (starttime) %s, %s\n", srcname, stime);
		    }
		  continue;
		}

	      if ( endtime != HPTERROR && (recendtime > endtime && ! (msr->starttime <= endtime && recendtime >= endtime)) )
		{
		  if ( verbose >= 3 )
		    {
		      msr_srcname (msr, srcname, 1);
		      ms_hptime2seedtimestr (msr->starttime, stime, 1);
		      ms_log (1, "Skipping (starttime) %s, %s\n", srcname, stime);
		    }
		  continue;
		}
	    }

	  if ( match || reject )
	    {
	      /* Generate the srcname with the quality code */
	      msr_srcname (msr, srcname, 1);

	      /* Check if record is matched by the match regex */
	      if ( match )
		{
		  if ( regexec ( match, srcname, 0, 0, 0) != 0 )
		    {
		      if ( verbose >= 3 )
			{
			  ms_hptime2seedtimestr (msr->starttime, stime, 1);
			  ms_log (1, "Skipping (match) %s, %s\n", srcname, stime);
			}
		      continue;
		    }
		}

	      /* Check if record is rejected by the reject regex */
	      if ( reject )
		{
		  if ( regexec ( reject, srcname, 0, 0, 0) == 0 )
		    {
		      if ( verbose >= 3 )
			{
			  ms_hptime2seedtimestr (msr->starttime, stime, 1);
			  ms_log (1, "Skipping (reject) %s, %s\n", srcname, stime);
			}
		      continue;
		    }
		}
	    }

	  /* Add to TraceList */
	  mstl_addmsr (mstl, msr, dataquality, 1, timetol, sampratetol);
	}

      /* Print error if not EOF and not counting down records */
      if ( retcode != MS_ENDOFFILE )
	{
	  ms_log (2, "Cannot read %s: %s\n", flp->filename, ms_errorstr(retcode));
	  ms_readmsr (&msr, NULL, 0, NULL, NULL, 0, 0, 0);
	  exit (1);
	}

      /* Make sure everything is cleaned up */
      ms_readmsr (&msr, NULL, 0, NULL, NULL, 0, 0, 0);

      flp = flp->next;
    } /* End of looping over file list */

  /* Trim each segment to specified time range */
  if ( starttime != HPTERROR || endtime != HPTERROR )
    trimsegments (mstl);

  /* Print the ESYNC listing */
  printesynclist (mstl, dccidstr);

  if ( compare )
    comparetraces (mstl);

  if ( mstl )
    mstl_free (&mstl, 0);

  return retval;
}  /* End of main() */


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
trimsegments (MSTraceList *mstl)
{
  MSTraceID *id = 0;
  MSTraceSeg *seg = 0;

  hptime_t sampletime;
  hptime_t hpdelta;
  hptime_t hptimetol = 0;
  int64_t trimcount;
  int samplesize;
  void *datasamples;

  if ( ! mstl )
    {
      return;
    }

  /* Loop through trace list */
  id = mstl->traces;
  while ( id )
    {
      /* Loop through segment list */
      seg = id->first;
      while ( seg )
	{
	  /* Skip segments that do not have integer, float or double types */
	  if ( seg->sampletype != 'i' && seg->sampletype != 'f' && seg->sampletype != 'd' )
	    {
	      seg = seg->next;
	      continue;
	    }

	  /* Calculate high-precision sample period */
	  hpdelta = (hptime_t) (( seg->samprate ) ? (HPTMODULUS / seg->samprate) : 0.0);

	  /* Calculate high-precision time tolerance */
	  if ( timetol == -1.0 )
	    hptimetol = (hptime_t) (0.5 * hpdelta);   /* Default time tolerance is 1/2 sample period */
	  else if ( timetol >= 0.0 )
	    hptimetol = (hptime_t) (timetol * HPTMODULUS);

	  samplesize = ms_samplesize (seg->sampletype);

	  /* Trim samples from beginning of segment if earlier than starttime */
	  if ( starttime != HPTERROR && seg->starttime < starttime )
	    {
	      trimcount = 0;
	      sampletime = seg->starttime;
	      while ( sampletime < (starttime - hptimetol) )
		{
		  sampletime += hpdelta;
		  trimcount++;
		}

	      if ( trimcount > 0 && trimcount < seg->numsamples )
		{
		  if ( verbose )
		    ms_log (1, "Trimming %lld samples from beginning of trace for %s\n",
			    (long long)trimcount, id->srcname);

		  memmove (seg->datasamples,
			   (char *)seg->datasamples + (trimcount * samplesize),
			   (seg->numsamples - trimcount) * samplesize);

		  datasamples = realloc (seg->datasamples, (seg->numsamples - trimcount) * samplesize);

		  if ( ! datasamples )
		    {
		      ms_log (2, stderr, "Cannot reallocate sample buffer\n");
		      return;
		    }

		  seg->datasamples = datasamples;
		  seg->starttime += MS_EPOCH2HPTIME((trimcount / seg->samprate));
		  seg->numsamples -= trimcount;
		  seg->samplecnt -= trimcount;
		}
	    }

	  /* Trim samples from end of segment if later than endtime */
	  if ( endtime != HPTERROR && seg->endtime > endtime )
	    {
	      trimcount = 0;
	      sampletime = seg->endtime;
	      while ( sampletime > (endtime + hptimetol) )
		{
		  sampletime -= hpdelta;
		  trimcount++;
		}

	      if ( trimcount > 0 && trimcount < seg->numsamples )
		{
		  if ( verbose )
		    ms_log (1, "Trimming %lld samples from end of trace for %s\n",
			    (long long)trimcount, id->srcname);

		  datasamples = realloc (seg->datasamples, (seg->numsamples - trimcount) * samplesize);

		  if ( ! datasamples )
		    {
		      ms_log (2, stderr, "Cannot reallocate sample buffer\n");
		      return;
		    }

		  seg->datasamples = datasamples;
		  seg->endtime -= MS_EPOCH2HPTIME((trimcount / seg->samprate));
		  seg->numsamples -= trimcount;
		  seg->samplecnt -= trimcount;
		}
	    }

	  seg = seg->next;
	}

      id = id->next;
    }

  return;
}  /* End of trimsegments() */


/***************************************************************************
 * printesynclist():
 *
 * Print the MSTraceList as an Enhanced SYNC Listing.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
printesynclist (MSTraceList *mstl, char *dccid)
{
  MSTraceID *id = 0;
  MSTraceSeg *seg = 0;
  char starttime[30];
  char endtime[30];
  char yearday[30];
  time_t now;
  struct tm *nt;

  md5_state_t pms;
  md5_byte_t digest[16];
  int samplesize;
  int idx;
  char digeststr[33];

  if ( ! mstl )
    {
      return;
    }

  /* Generate current time stamp */
  now = time (NULL);
  nt = localtime ( &now );
  nt->tm_year += 1900;
  nt->tm_yday += 1;
  snprintf ( yearday, sizeof(yearday), "%04d,%03d", nt->tm_year, nt->tm_yday);

  /* Print SYNC header line */
  ms_log (0, "%s|%s\n", (dccid)?dccid:"DCC", yearday);

  /* Loop through trace list */
  id = mstl->traces;
  while ( id )
    {
      /* Loop through segment list */
      seg = id->first;
      while ( seg )
	{
	  ms_hptime2seedtimestr (seg->starttime, starttime, 1);
	  ms_hptime2seedtimestr (seg->endtime, endtime, 1);

	  /* Calculate MD5 hash of sample values if samples present */
	  if ( seg->datasamples )
	    {
	      samplesize = ms_samplesize ( seg->sampletype );
	      memset (&pms, 0, sizeof(md5_state_t));
	      md5_init(&pms);
	      md5_append(&pms, (const md5_byte_t *)seg->datasamples, (seg->numsamples * samplesize));
	      md5_finish(&pms, digest);

	      for (idx=0; idx < 16; idx++)
		sprintf (digeststr+(idx*2), "%02x", digest[idx]);
	    }

	  /* Print SYNC line */
	  ms_log (0, "%s|%s|%s|%s|%s|%s||%.10g|%lld|||%.1s|%.32s|||%s\n",
		  id->network, id->station, id->location, id->channel,
		  starttime, endtime, seg->samprate, (long long int)seg->samplecnt,
		  (id->dataquality) ? &(id->dataquality) : "",
		  (seg->datasamples) ? digeststr : "",
		  yearday);

	  seg = seg->next;
	}

      id = id->next;
    }

  return;
}  /* End of printesynclist() */


/***************************************************************************
 * comparetraces():
 *
 * Compare sample values for each segment
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static void
comparetraces (MSTraceList *mstl)
{
  MSTraceID *id = 0;
  MSTraceID *tid = 0;
  MSTraceSeg *seg = 0;
  MSTraceSeg *tseg = 0;
  char start[30];
  char end[30];
  char tstart[30];
  char tend[30];
  int64_t idx = 0;

  if ( ! mstl )
    {
      return;
    }

  /* Loop through trace list */
  id = mstl->traces;
  while ( id )
    {
      /* Loop through segment list */
      seg = id->first;
      while ( seg )
	{
	  ms_hptime2seedtimestr (seg->starttime, start, 1);
	  ms_hptime2seedtimestr (seg->endtime, end, 1);

	  if ( ! seg->datasamples )
	    {
	      ms_log (2, "%s, %s, %s :: No data samples\n", id->srcname, start, end);
	      seg = seg->next;
	      continue;
	    }

	  /* Loop through trace list for targets */
	  tid = id;
	  while ( tid )
	    {
	      /* Loop through target segment list */
	      tseg = ( tid == id ) ? seg->next : tid->first;
	      while ( tseg )
		{
		  if ( ! tseg->datasamples )
		    {
		      ms_log (1, "%s, %s, %s :: No data samples\n", tid->srcname, tstart, tend);
		      tseg = tseg->next;
		      continue;
		    }

		  ms_hptime2seedtimestr (tseg->starttime, tstart, 1);
		  ms_hptime2seedtimestr (tseg->endtime, tend, 1);

		  if ( seg->sampletype != tseg->sampletype )
		    {
		      ms_log (1, "%s and %s :: Sample type mismatch\n", id->srcname, tid->srcname);
		      tseg = tseg->next;
		      continue;
		    }

		  if ( seg->numsamples != tseg->numsamples )
		    {
		      ms_log (1, "%s (%lld) and %s (%lld) :: Sample count mismatch\n",
			      id->srcname, (long long int) seg->numsamples,
			      tid->srcname, (long long int) tseg->numsamples);
		      tseg = tseg->next;
		      continue;
		    }

		  if ( seg->sampletype == 'i' )
		    {
		      int32_t *data = (int32_t*) seg->datasamples;
		      int32_t *tdata = (int32_t*) tseg->datasamples;

		      for (idx=0; idx < seg->numsamples; idx++)
			if ( data[idx] != tdata[idx] )
			  {
			    ms_log (0, "Time series are NOT the same, differing at sample %lld (%d versus %d)\n",
				    (long long int)idx+1, data[idx], tdata[idx]);
			    retval = 1;
			    break;
			  }
		    }
		  else if ( seg->sampletype == 'f' )
		    {
		      float *data = (float*) seg->datasamples;
		      float *tdata = (float*) tseg->datasamples;

		      for (idx=0; idx < seg->numsamples; idx++)
			if ( data[idx] != tdata[idx] )
			  {
			    ms_log (0, "Time series are NOT the same, differing at sample %lld (%f versus %f)\n",
				    (long long int)idx+1, data[idx], tdata[idx]);
			    retval = 1;
			    break;
			  }
		    }
		  else if ( seg->sampletype == 'd' )
		    {
		      double *data = (double*) seg->datasamples;
		      double *tdata = (double*) tseg->datasamples;

		      for (idx=0; idx < seg->numsamples; idx++)
			if ( data[idx] != tdata[idx] )
			  {
			    ms_log (0, "Time series are NOT the same, differing at sample %lld (%f versus %f)\n",
				    (long long int)idx+1, data[idx], tdata[idx]);
			    retval = 1;
			    break;
			  }
		    }
		  else if ( seg->sampletype == 'a' )
		    {
		      char *data = (char*) seg->datasamples;
		      char *tdata = (char*) tseg->datasamples;

		      for (idx=0; idx < seg->numsamples; idx++)
			if ( data[idx] != tdata[idx] )
			  {
			    ms_log (0, "Time series are NOT the same, differing at sample %lld (%c versus %c)\n",
				    (long long int)idx+1, data[idx], tdata[idx]);
			    retval = 1;
			    break;
			  }
		    }

		  if ( idx == seg->numsamples )
		    {
		      ms_log (0, "Time series are the same, %lld samples compared\n",
			      (long long int)idx);
		    }

		  ms_log (0, "  %s  %s  %s\n", id->srcname, start, end);
		  ms_log (0, "  %s  %s  %s\n", tid->srcname, tstart, tend);

		  tseg = tseg->next;
		}

	      tid = tid->next;
	    }

	  seg = seg->next;
	}

      id = id->next;
    }

  return;
}  /* End of comparetraces() */


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
  char *matchpattern = 0;
  char *rejectpattern = 0;
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
	  usage();
	  exit (0);
	}
      else if (strncmp (argvec[optind], "-v", 2) == 0)
	{
	  verbose += strspn (&argvec[optind][1], "v");
	}
      else if (strcmp (argvec[optind], "-r") == 0)
	{
	  reclen = strtol (getoptval(argcount, argvec, optind++), NULL, 10);
	}
      else if (strcmp (argvec[optind], "-e") == 0)
	{
	  encodingstr = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-D") == 0)
	{
	  dccidstr = getoptval(argcount, argvec, optind++);
	}
      else if (strcmp (argvec[optind], "-C") == 0)
	{
	  compare = 1;
	}
      else if (strcmp (argvec[optind], "-ts") == 0)
	{
	  starttime = ms_seedtimestr2hptime (getoptval(argcount, argvec, optind++));
	  if ( starttime == HPTERROR )
	    return -1;
	}
      else if (strcmp (argvec[optind], "-te") == 0)
	{
	  endtime = ms_seedtimestr2hptime (getoptval(argcount, argvec, optind++));
	  if ( endtime == HPTERROR )
	    return -1;
	}
      else if (strcmp (argvec[optind], "-M") == 0)
	{
	  matchpattern = strdup (getoptval(argcount, argvec, optind++));
	}
      else if (strcmp (argvec[optind], "-R") == 0)
	{
	  rejectpattern = strdup (getoptval(argcount, argvec, optind++));
	}
      else if (strcmp (argvec[optind], "-tt") == 0)
	{
	  timetol = strtod (getoptval(argcount, argvec, optind++), NULL);
	}
      else if (strcmp (argvec[optind], "-rt") == 0)
	{
	  sampratetol = strtod (getoptval(argcount, argvec, optind++), NULL);
	}
      else if (strncmp (argvec[optind], "-", 1) == 0 &&
	       strlen (argvec[optind]) > 1 )
	{
	  ms_log (2, "Unknown option: %s\n", argvec[optind]);
	  exit (1);
	}
      else
	{
	  tptr = argvec[optind];

          /* Check for an input file list */
          if ( tptr[0] == '@' )
            {
              if ( addlistfile (tptr+1) < 0 )
                {
                  ms_log (2, "Error adding list file %s", tptr+1);
                  exit (1);
                }
            }
          /* Otherwise this is an input file */
          else
            {
              /* Add file to global file list */
              if ( addfile (tptr) )
                {
                  ms_log (2, "Error adding file to input list %s", tptr);
                  exit (1);
                }
            }
	}
    }

  /* Make sure input file were specified */
  if ( filelist == 0 )
    {
      ms_log (2, "No input files were specified\n\n");
      ms_log (1, "%s version %s\n\n", PACKAGE, VERSION);
      ms_log (1, "Try %s -h for usage\n", PACKAGE);
      exit (1);
    }

  /* Expand match pattern from a file if prefixed by '@' */
  if ( matchpattern )
    {
      if ( *matchpattern == '@' )
	{
	  tptr = strdup(matchpattern + 1); /* Skip the @ sign */
	  free (matchpattern);
	  matchpattern = 0;

	  if ( readregexfile (tptr, &matchpattern) <= 0 )
	    {
	      ms_log (2, "Cannot read match pattern regex file\n");
	      exit (1);
	    }

	  free (tptr);
	}
    }

  /* Expand reject pattern from a file if prefixed by '@' */
  if ( rejectpattern )
    {
      if ( *rejectpattern == '@' )
	{
	  tptr = strdup(rejectpattern + 1); /* Skip the @ sign */
	  free (rejectpattern);
	  rejectpattern = 0;

	  if ( readregexfile (tptr, &rejectpattern) <= 0 )
	    {
	      ms_log (2, "Cannot read reject pattern regex file\n");
	      exit (1);
	    }

	  free (tptr);
	}
    }

  /* Compile match and reject patterns */
  if ( matchpattern )
    {
      match = (regex_t *) malloc (sizeof(regex_t));

      if ( regcomp (match, matchpattern, REG_EXTENDED) != 0)
	{
	  ms_log (2, "Cannot compile match regex: '%s'\n", matchpattern);
	}

      free (matchpattern);
    }

  if ( rejectpattern )
    {
      reject = (regex_t *) malloc (sizeof(regex_t));

      if ( regcomp (reject, rejectpattern, REG_EXTENDED) != 0)
	{
	  ms_log (2, "Cannot compile reject regex: '%s'\n", rejectpattern);
	}

      free (rejectpattern);
    }

  /* Report the program version */
  if ( verbose )
    ms_log (1, "%s version: %s\n", PACKAGE, VERSION);

  return 0;
}  /* End of parameter_proc() */


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
  if ( argvec == NULL || argvec[argopt] == NULL ) {
    ms_log (2, "getoptval(): NULL option requested\n");
    exit (1);
    return 0;
  }

  /* Special case of '-o -' usage */
  if ( (argopt+1) < argcount && strcmp (argvec[argopt], "-o") == 0 )
    if ( strcmp (argvec[argopt+1], "-") == 0 )
      return argvec[argopt+1];

  /* Special cases of '-gmin' and '-gmax' with negative numbers */
  if ( (argopt+1) < argcount &&
       (strcmp (argvec[argopt], "-gmin") == 0 || (strcmp (argvec[argopt], "-gmax") == 0)))
    if ( lisnumber(argvec[argopt+1]) )
      return argvec[argopt+1];

  if ( (argopt+1) < argcount && *argvec[argopt+1] != '-' )
    return argvec[argopt+1];

  ms_log (2, "Option %s requires a value, try -h for usage\n", argvec[argopt]);
  exit (1);
  return 0;
}  /* End of getoptval() */


/***************************************************************************
 * readregexfile:
 *
 * Read a list of regular expressions from a file and combine them
 * into a single, compound expression which is returned in *pppattern.
 * The return buffer is reallocated as need to hold the growing
 * pattern.  When called *pppattern should not point to any associated
 * memory.
 *
 * Returns the number of regexes parsed from the file or -1 on error.
 ***************************************************************************/
static int
readregexfile (char *regexfile, char **pppattern)
{
  FILE *fp;
  char  line[1024];
  char  linepattern[1024];
  int   regexcnt = 0;
  int   lengthbase;
  int   lengthadd;

  if ( ! regexfile )
    {
      ms_log (2, "readregexfile: regex file not supplied\n");
      return -1;
    }

  if ( ! pppattern )
    {
      ms_log (2, "readregexfile: pattern string buffer not supplied\n");
      return -1;
    }

  /* Open the regex list file */
  if ( (fp = fopen (regexfile, "rb")) == NULL )
    {
      ms_log (2, "Cannot open regex list file %s: %s\n",
	      regexfile, strerror (errno));
      return -1;
    }

  if ( verbose )
    ms_log (1, "Reading regex list from %s\n", regexfile);

  *pppattern = NULL;

  while ( (fgets (line, sizeof(line), fp)) !=  NULL)
    {
      /* Trim spaces and skip if empty lines */
      if ( sscanf (line, " %s ", linepattern) != 1 )
	continue;

      /* Skip comment lines */
      if ( *linepattern == '#' )
	continue;

      regexcnt++;

      /* Add regex to compound regex */
      if ( *pppattern )
	{
	  lengthbase = strlen(*pppattern);
	  lengthadd = strlen(linepattern) + 4; /* Length of addition plus 4 characters: |()\0 */

	  *pppattern = realloc (*pppattern, lengthbase + lengthadd);

	  if ( *pppattern )
	    {
	      snprintf ((*pppattern)+lengthbase, lengthadd, "|(%s)", linepattern);
	    }
	  else
	    {
	      ms_log (2, "Cannot allocate memory for regex string\n");
	      return -1;
	    }
	}
      else
	{
	  lengthadd = strlen(linepattern) + 3; /* Length of addition plus 3 characters: ()\0 */

	  *pppattern = malloc (lengthadd);

	  if ( *pppattern )
	    {
	      snprintf (*pppattern, lengthadd, "(%s)", linepattern);
	    }
	  else
	    {
	      ms_log (2, "Cannot allocate memory for regex string\n");
	      return -1;
	    }
	}
    }

  fclose (fp);

  return regexcnt;
}  /* End of readregexfile() */


/***************************************************************************
 * lisnumber:
 *
 * Test if the string is all digits allowing an initial minus sign.
 *
 * Return 0 if not a number otherwise 1.
 ***************************************************************************/
static int
lisnumber (char *number)
{
  int idx = 0;

  while ( *(number+idx) )
    {
      if ( idx == 0 && *(number+idx) == '-' )
	{
	  idx++;
	  continue;
	}

      if ( ! isdigit ((int) *(number+idx)) )
	{
	  return 0;
	}

      idx++;
    }

  return 1;
}  /* End of lisnumber() */


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
  char *at;

  if ( filename == NULL )
    {
      ms_log (2, "addfile(): No file name specified\n");
      return -1;
    }

  newlp = (struct filelink *) calloc (1, sizeof (struct filelink));

  if ( ! newlp )
    {
      ms_log (2, "addfile(): Cannot allocate memory\n");
      return -1;
    }

  /* Check for starting offset */
  if ( (at = strrchr (filename, '@')) )
    {
      *at++ = '\0';
      newlp->offset = strtoull (at, NULL, 10);
    }
  else
    {
      newlp->offset = 0;
    }

  newlp->filename = strdup(filename);

  if ( ! newlp->filename )
    {
      ms_log (2, "addfile(): Cannot duplicate string\n");
      return -1;
    }

  /* Add new file to the end of the list */
  if ( filelisttail == 0 )
    {
      filelist = newlp;
      filelisttail = newlp;
    }
  else
    {
      filelisttail->next = newlp;
      filelisttail = newlp;
    }

  return 0;
}  /* End of addfile() */


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

  if ( verbose >= 1 )
    ms_log (1, "Reading list file '%s'\n", filename);

  if ( ! (fp = fopen(filename, "rb")) )
    {
      ms_log (2, "Cannot open list file %s: %s\n", filename, strerror(errno));
      return -1;
    }

  while ( fgets (filelistent, sizeof(filelistent), fp) )
    {
      char *cp;

      /* End string at first newline character */
      if ( (cp = strchr(filelistent, '\n')) )
        *cp = '\0';

      /* Skip empty lines */
      if ( ! strlen (filelistent) )
        continue;

      /* Skip comment lines */
      if ( *filelistent == '#' )
        continue;

      if ( verbose > 1 )
        ms_log (1, "Adding '%s' from list file\n", filelistent);

      if ( addfile (filelistent) )
        return -1;

      filecount++;
    }

  fclose (fp);

  return filecount;
}  /* End of addlistfile() */


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
	   " -r reclen    Specify record length in bytes, default is autodetection\n"
	   " -e encoding  Specify encoding format of data samples\n"
	   " -D DCCID     Specify the DCC identifier for SYNC header\n"
	   " -C           Compare sample values of time series, to diagnose mismatches\n"
	   "\n"
	   " ## Data selection options ##\n"
	   " -ts time     Limit to samples that start on or after time\n"
	   " -te time     Limit to samples that end on or before time\n"
	   "                time format: 'YYYY[,DDD,HH,MM,SS,FFFFFF]' delimiters: [,:.]\n"
	   " -M match     Limit to records matching the specified regular expression\n"
	   " -R reject    Limit to records not matching the specfied regular expression\n"
	   "                Regular expressions are applied to: 'NET_STA_LOC_CHAN_QUAL'\n"
	   " -tt secs     Specify a time tolerance for continuous traces\n"
	   " -rt diff     Specify a sample rate tolerance for continuous traces\n"
	   "\n"
	   " files        File(s) of miniSEED records, list files prefixed with '@'\n"
	   "\n");
}  /* End of usage() */
