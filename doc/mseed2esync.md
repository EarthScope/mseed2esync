# <p >Create an Enhanced SYNC Listing from Mini-SEED file(s)</p>

1. [Name](#)
1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [Input Files](#input-files)
1. [Input List File](#input-list-file)
1. [Match Or Reject List File](#match-or-reject-list-file)
1. [Author](#author)

## <a id='synopsis'>Synopsis</a>

<pre >
mseed2esync [options] file1 [file2 file3 ...]
</pre>

## <a id='description'>Description</a>

<p ><b>mseed2esync</b> reads Mini-SEED, optionally subsetting to specific time ranges or channels, and prints an Enhanced SYNC listing.</p>

<p >An Enhanced SYNC Listing includes the SEED quality indicator and an MD5 hash of the sample values.  The MD5 hash of the sample values allows time series segments to be compared without directly comparing the sample values.</p>

<p >IMPORTANT: the MD5 hashing employed is dependent on the binary representation of the sample values on a given architecture.  Most architectures should generate comparable values but some may not, particularly for float type samples.</p>

<p >The SEED quality code is included in the SYNC listing in the field historically used for "DCC Tape Number".</p>

<p >The MD5 sample value hash is included in the SYNC listing in the field historically used for "DMC Volume Number".</p>

<p >If '-' is specified standard input will be read.  Multiple input files will be processed in the order specified.</p>

<p >Files on the command line prefixed with a '@' character are input list files and are expected to contain a simple list of input files, see \fBINPUT LIST FILE\fR for more details.</p>

<p >When a input file is full SEED including both SEED headers and data records all of the headers will be skipped.</p>

## <a id='options'>Options</a>

<b>-V</b>

<p style="padding-left: 30px;">Print program version and exit.</p>

<b>-h</b>

<p style="padding-left: 30px;">Print program usage and exit.</p>

<b>-v</b>

<p style="padding-left: 30px;">Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.</p>

<b>-r </b><i>reclen</i>

<p style="padding-left: 30px;">Specify the input record length in bytes.  By default the record length of each input Mini-SEED record is automatically detected, this option forces the record length.</p>

<b>-e </b><i>encoding</i>

<p style="padding-left: 30px;">Specify the data encoding format.  These encoding values are the same as those specified in the SEED 1000 Blockette.</p>

<b>-D </b><i>DCCID</i>

<p style="padding-left: 30px;">Specify the a data center identifier to place in the SYNC file header. If not specified it will be left empty in the header.</p>

<b>-C</b>

<p style="padding-left: 30px;">Compare the sample values between each segment of data being processed, useful for diagnosing differences.  If all of the segments processed match the exit value of the program will be 0, otherwise 1.</p>

<b>-ts </b><i>time</i>

<p style="padding-left: 30px;">Limit processing to Mini-SEED records that contain or start after <i>time</i>.  The format of the <i>time</i> arguement is: 'YYYY[,DDD,HH,MM,SS,FFFFFF]' where valid delimiters are either commas (,), colons (:) or periods (.).</p>

<b>-te </b><i>time</i>

<p style="padding-left: 30px;">Limit processing to Mini-SEED records that contain or end before <i>time</i>.  The format of the <i>time</i> arguement is: 'YYYY[,DDD,HH,MM,SS,FFFFFF]' where valid delimiters are either commas (,), colons (:) or periods (.).</p>

<b>-M </b><i>match</i>

<p style="padding-left: 30px;">Limit processing to Mini-SEED records that match the <i>match</i> regular expression.  For each input record a source name string composed of 'NET_STA_LOC_CHAN_QUAL' is created and compared to the regular expression.  If the match expression begins with an '@' character it is assumed to indicate a file containing a list of expressions to match, see the \fBMATCH OR REJECT LIST FILE\fR section below.</p>

<b>-R </b><i>reject</i>

<p style="padding-left: 30px;">Limit processing to Mini-SEED records that do not match the <i>reject</i> regular expression.  For each input record a source name string composed of 'NET_STA_LOC_CHAN_QUAL' is created and compared to the regular expression.  If the reject expression begins with an '@' character it is assumed to indicate a file containing a list of expressions to reject, see the \fBMATCH OR REJECT LIST FILE\fR section below.</p>

<b>-tt </b><i>secs</i>

<p style="padding-left: 30px;">Specify a time tolerance for constructing continous trace segments. The tolerance is specified in seconds.  The default tolerance is 1/2 of the sample period.</p>

<b>-rt </b><i>diff</i>

<p style="padding-left: 30px;">Specify a sample rate tolerance for constructing continous trace segments. The tolerance is specified as the difference between two sampling rates.  The default tolerance is tested as: (abs(1-sr1/sr2) < 0.0001).</p>

## <a id='input-files'>Input Files</a>

<p >An input file name may be followed by an <b>@</b> charater followed by a byte offset.  In this case the program will interpret the byte offset as the starting offset into the file.  Useful for diagnosing specific records.  As an example an input file specified as <b>ANMO.mseed@8192</b> would result in the file <b>ANMO.mseed</b> being read starting at byte 8192.</p>

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

## <a id='match-or-reject-list-file'>Match Or Reject List File</a>

<p >A list file used with either the <b>-M</b> or <b>-R</b> options contains a list of regular expressions (one on each line) that will be combined into a single compound expression.  The initial '@' character indicating a list file is not considered part of the file name.  As an example, if the following command line option was used:</p>

<pre >
<b>-M @match.list</b>
</pre>

<p >The 'match.list' file might look like this:</p>

<pre >
IU_ANMO_.*
IU_ADK_00_BHZ.*
II_BFO_00_BHZ_Q
</pre>

## <a id='author'>Author</a>

<pre >
Chad Trabant
IRIS Data Management Center
</pre>


(man page 2012/04/27)
