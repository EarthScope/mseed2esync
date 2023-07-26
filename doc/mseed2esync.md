# <p >Create an Enhanced SYNC Listing from miniSEED file(s)</p>

1. [Name](#)
1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [Input Files](#input-files)
1. [Input List File](#input-list-file)
1. [Author](#author)

## <a id='synopsis'>Synopsis</a>

<pre >
mseed2esync [options] file1 [file2 file3 ...]
</pre>

## <a id='description'>Description</a>

<p ><b>mseed2esync</b> reads miniSEED, optionally subsetting to specific time ranges or channels, and prints an Enhanced SYNC listing.</p>

<p >An Enhanced SYNC Listing includes the SEED quality indicator and an MD5 hash of the sample values.  The MD5 hash of the sample values allows time series segments to be compared without directly comparing the sample values.</p>

<p >IMPORTANT: the MD5 hashing employed is dependent on the binary representation of the sample values on a given architecture.  Most architectures should generate comparable values but some may not, particularly for float type samples.</p>

<p >The publication version, or data quality codes, is included in the SYNC listing in the field historically used for "DCC Tape Number".</p>

<p >The MD5 sample value hash is included in the SYNC listing in the field historically used for "DMC Volume Number".</p>

<p >If '-' is specified standard input will be read.  Multiple input files will be processed in the order specified.</p>

<p >Files on the command line prefixed with a '@' character are input list files and are expected to contain a simple list of input files, see \fBINPUT LIST FILE\fR for more details.</p>

## <a id='options'>Options</a>

<b>-V</b>

<p style="padding-left: 30px;">Print program version and exit.</p>

<b>-h</b>

<p style="padding-left: 30px;">Print program usage and exit.</p>

<b>-v</b>

<p style="padding-left: 30px;">Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.</p>

<b>-D </b><i>DCCID</i>

<p style="padding-left: 30px;">Specify the a data center identifier to place in the SYNC file header. If not specified it will be left empty in the header.</p>

<b>-C</b>

<p style="padding-left: 30px;">Compare the sample values between each segment of data being processed, useful for diagnosing differences.  If all of the segments processed match the exit value of the program will be 0, otherwise 1.</p>

<b>-ts </b><i>time</i>

<p style="padding-left: 30px;">Limit processing to miniSEED records that contain or start after <i>time</i>.  The format of the <i>time</i> arguement is: 'YYYY[-MM-DDThh:mm:ss.ffff], or 'YYYY[,DDD,HH,MM,SS,FFFFFF]', or Unix/POSIX epoch seconds.</p>

<b>-te </b><i>time</i>

<p style="padding-left: 30px;">Limit processing to miniSEED records that contain or end before <i>time</i>.  The format of the <i>time</i> arguement is: 'YYYY[-MM-DDThh:mm:ss.ffff], or 'YYYY[,DDD,HH,MM,SS,FFFFFF]', or Unix/POSIX epoch seconds.</p>

<b>-m </b><i>match</i>

<p style="padding-left: 30px;">Limit processing to miniSEED records that contain the <i>match</i> pattern, which is applied to the Source Identifier for each record, often following this pattern: 'FDSN:<network>_<station>_<location>_<band>_<source>_<subsource>'</p>

<b>-r </b><i>reject</i>

<p style="padding-left: 30px;">Limit processing to miniSEED records that do _not_ contain the <i>reject</i> pattern, which is applied to the the Source Identifier for each record, often following this pattern: 'FDSN:<network>_<station>_<location>_<band>_<source>_<subsource>'</p>

<b>-tt </b><i>secs</i>

<p style="padding-left: 30px;">Specify a time tolerance for constructing continous trace segments. The tolerance is specified in seconds.  The default tolerance is 1/2 of the sample period.</p>

<b>-rt </b><i>diff</i>

<p style="padding-left: 30px;">Specify a sample rate tolerance for constructing continous trace segments. The tolerance is specified as the difference between two sampling rates.  The default tolerance is tested as: (abs(1-sr1/sr2) < 0.0001).</p>

## <a id='input-files'>Input Files</a>

<p >An input file name may be followed by an <b>@</b> charater followed by a byte range in the pattern <b>START[-END]</b>, where the END offset is optional.  As an example an input file specified as <b>ANMO.mseed@8192</b> would result in the file <b>ANMO.mseed</b> being read starting at byte 8192.  An optional end offset can be specified, e.g. <b>ANMO.mseed@8192-12288</b> would start reading at offset 8192 and stop after offset 12288.</p>

## <a id='input-list-file'>Input List File</a>

<p >A list file can be used to specify input files, one file per line. The initial '@' character indicating a list file is not considered part of the file name.  As an example, if the following command line option was used:</p>

<pre >
<b>@files.list</b>
</pre>

<p >The 'files.list' file might look like this:</p>

<pre >
data/day1.mseed
data/day2.mseed
data/day3.mseed
</pre>

## <a id='author'>Author</a>

<pre >
Chad Trabant
EarthScope Data Services
</pre>


(man page 2023/07/25)
