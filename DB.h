/*******************************************************************************************
 *
 *  Compressed data base module.  Auxiliary routines to open and manipulate a data base for
 *    which the sequence and read information are separated into two separate files, and the
 *    sequence is compressed into 2-bits for each base.  Support for tracks of additional
 *    information, and trimming according to the current partition.  Eventually will also
 *    support compressed quality information. 
 *
 *  Author :  Gene Myers
 *  Date   :  July 2013
 *  Revised:  April 2014
 *
 ********************************************************************************************/

#ifndef _HITS_DB

#define _HITS_DB

#include <stdio.h>

#include "QV.h"

typedef unsigned char      uint8;
typedef unsigned short     uint16;
typedef unsigned int       uint32;
typedef unsigned long long uint64;
typedef signed char        int8;
typedef signed short       int16;
typedef signed int         int32;
typedef signed long long   int64;
typedef float              float32;
typedef double             float64;

#define HIDE_FILES          //  Auxiliary DB files start with a . so they are "hidden"
                            //    Undefine if you don't want this

#define READMAX  65535      //  Maximum # of reads in a DB partition block

typedef uint16   READIDX;   //  Reads can be no longer than 2^16


/*******************************************************************************************
 *
 *  COMMAND LINE INTERPRETATION MACROS
 *
 ********************************************************************************************/

extern char *Prog_Name;   //  Name of program

#define ARG_INIT(name)                  \
  Prog_Name = Strdup(name,"");          \
  for (i = 0; i < 128; i++)             \
    flags[i] = 0;

#define ARG_FLAGS(set)                                                                  \
  for (k = 1; argv[i][k] != '\0'; k++)                                                  \
    { if (index(set,argv[i][k]) == NULL)                                                \
        { fprintf(stderr,"%s: -%c is an illegal option\n",Prog_Name,argv[i][k]);        \
          exit (1);                                                                     \
        }                                                                               \
      flags[(int) argv[i][k]] = 1;                                                      \
    }

#define ARG_POSITIVE(var,name)                                                          \
  var = strtol(argv[i]+2,&eptr,10);                                                     \
  if (*eptr != '\0' || argv[i][2] == '\0')                                              \
    { fprintf(stderr,"%s: -%c argument is not an integer\n",Prog_Name,argv[i][1]);      \
      exit (1);                                                                         \
    }                                                                                   \
  if (var <= 0)                                                                         \
    { fprintf(stderr,"%s: %s must be positive (%d)\n",Prog_Name,name,var);              \
      exit (1);                                                                         \
    }

#define ARG_NON_NEGATIVE(var,name)                                                      \
  var = strtol(argv[i]+2,&eptr,10);                                                     \
  if (*eptr != '\0' || argv[i][2] == '\0')                                              \
    { fprintf(stderr,"%s: -%c argument is not an integer\n",Prog_Name,argv[i][1]);      \
      exit (1);                                                                         \
    }                                                                                   \
  if (var < 0)	                                                                        \
    { fprintf(stderr,"%s: %s must be non-negative (%d)\n",Prog_Name,name,var);          \
      exit (1);                                                                         \
    }

#define ARG_REAL(var)                                                                   \
  var = strtod(argv[i]+2,&eptr);                                                        \
  if (*eptr != '\0' || argv[i][2] == '\0')                                              \
    { fprintf(stderr,"%s: -%c argument is not a real number\n",Prog_Name,argv[i][1]);   \
      exit (1);                                                                         \
    }

/*******************************************************************************************
 *
 *  UTILITIES
 *
 ********************************************************************************************/

//  The following general utilities return NULL if any of their input pointers are NULL, or if they
//    could not perform their function (in which case they also print an error to stderr).

void *Malloc(int64 size, char *mesg);                    //  Guarded versions of malloc, realloc
void *Realloc(void *object, int64 size, char *mesg);     //  and strdup, that output "mesg" to
char *Strdup(char *string, char *mesg);                  //  stderr if out of memory

FILE *Fopen(char *path, char *mode);     // Open file path for "mode"
char *PathTo(char *path);                // Return path portion of file name "path"
char *Root(char *path, char *suffix);    // Return the root name, excluding suffix, of "path"

// Catenate returns concatenation of path.sep.root.suffix in a *temporary* buffer
// Numbered_Suffix returns concatenation of left.<num>.right in a *temporary* buffer

char *Catenate(char *path, char *sep, char *root, char *suffix);
char *Numbered_Suffix(char *left, int num, char *right);


// DB-related utilities

void Print_Number(int64 num, int width, FILE *out);   //  Print readable big integer

#define COMPRESSED_LEN(len)  (((len)+3) >> 2)

void   Compress_Read(int len, char *s);   //  Compress read in-place into 2-bit form
void Uncompress_Read(int len, char *s);   //  Uncompress read in-place into numeric form
void      Print_Read(char *s, int width);

void Lower_Read(char *s);     //  Convert read from numbers to lowercase letters (0-3 to acgt)
void Upper_Read(char *s);     //  Convert read from numbers to uppercase letters (0-3 to ACGT)
void Number_Read(char *s);    //  Convert read from letters to numbers


/*******************************************************************************************
 *
 *  DB IN-CORE DATA STRUCTURES
 *
 ********************************************************************************************/

#define DB_QV   0x03ff   //  Mask for 3-digit quality value
#define DB_CSS  0x0400   //  This is the second or later of a group of reads from a given insert
#define DB_BEST 0x0800   //  This is the longest read of a given insert (may be the only 1)

typedef struct
  { int     origin; //  Well #
    READIDX beg;    //  First pulse
    READIDX end;    //  Last pulse
    int64   boff;   //  Offset (in bytes) of compressed read in 'bases' file, or offset of
                    //    uncompressed bases in memory block
    int64   coff;   //  Offset (in bytes) of compressed quiva streams in 'quiva' file
    int     flags;  //  QV of read + flags above
  } HITS_READ;

//  A track can be of 3 types:
//    data == NULL: there are nreads+1 'anno' records of size 'size'.
//    data != NULL && size == 4: anno is an array of nreads+1 int's and data[anno[i]..anno[i+1])
//                                    contains the variable length data
//    data != NULL && size == 8: anno is an array of nreads+1 int64's and data[anno[i]..anno[i+1])
//                                    contains the variable length data

typedef struct _track
  { struct _track *next;  //  Link to next track
    char          *name;  //  Symbolic name of track
    int            size;  //  Size in bytes of anno records
    void          *anno;  //  over [0,nreads]: read i annotation: int, int64, or 'size' records 
    void          *data;  //     data[anno[i] .. anno[i+1]-1] is data if data != NULL
  } HITS_TRACK;

//  The information for accessing QV streams is in a HITS_QV record that is a "pseudo-track"
//    named ".@qvs" and is always the first track record in the list (if present).  Since normal
//    track names cannot begin with a . (this is enforced), this pseudo-track is never confused
//    with a normal track.

typedef struct
  { struct _track *next;
    char          *name;
    int            ncodes;  //  # of coding tables
    QVcoding      *coding;  //  array [0..ncodes-1] of coding schemes (see QV.h)
    uint16        *table;   //  for i in [0,db->nreads-1]: read i should be decompressed with
                            //    scheme coding[table[i]]
    FILE          *quiva;   //  the open file pointer to the .qvs file
  } HITS_QV;

//  The DB record holds all information about the current state of an active DB including an
//    array of HITS_READS, one per read, and a linked list of HITS_TRACKs the first of which
//    is always a HITS_QV pseudo-track (if the QVs have been loaded).

typedef struct
  { int         oreads;     //  Total number of reads in DB
    int         breads;     //  Total number of reads in trimmed DB (if trimmed set)
    int         cutoff;     //  Minimum read length in block (-1 if not yet set)
    int         all;        //  Consider multiple reads from a given well
    float       freq[4];    //  frequency of A, C, G, T, respectively

    //  Set with respect to "active" part of DB (all vs block, untrimmed vs trimmed)

    int         maxlen;     //  length of maximum read (initially over all DB)
    int64       totlen;     //  total # of bases (initially over all DB)

    int         nreads;     //  # of reads in actively loaded portion of DB
    int         trimmed;    //  DB has been trimmed by cutoff/all
    int         part;       //  DB block (if > 0), total DB (if == 0)
    int         ofirst;     //  Index of first read in block (without trimming)
    int         bfirst;     //  Index of first read in block (with trimming)

    char       *path;       //  Root name of DB for .bps and tracks
    int         loaded;     //  Are reads loaded in memory?
    void       *bases;      //  file pointer for bases file (to fetch reads from),
                            //    or memory pointer to uncompressed block of all sequences.
    HITS_READ  *reads;      //  Array [0..nreads] of HITS_READ
    HITS_TRACK *tracks;     //  Linked list of loaded tracks
  } HITS_DB; 


/*******************************************************************************************
 *
 *  DB STUB FILE FORMAT = NFILE FDATA^nfile NBLOCK PARAMS BDATA^nblock
 *
 ********************************************************************************************/

#define MAX_NAME 10000      //  Longest file name or fasta header line

#define DB_NFILE  "files = %9d\n"   //  number of files
#define DB_FDATA  "  %9d %s %s\n"   //  last read index + 1, fasta prolog, file name
#define DB_NBLOCK "blocks = %9d\n"  //  number of blocks
#define DB_PARAMS "size = %9lld cutoff = %9d all = %1d\n"  //  block size, len cutoff, all in well
#define DB_BDATA  " %9d %9d\n"      //  First read index (untrimmed), first read index (trimmed)


/*******************************************************************************************
 *
 *  DB ROUTINES
 *
 ********************************************************************************************/

  // Suppose DB is the name of an original database.  Then there will be files .DB.idx, .DB.bps,
  //    .DB.qvs, and files .DB.<track>.anno and DB.<track>.data where <track> is a track name
  //    (not containing a . !).

  // Open the given database "path" into the supplied HITS_DB record "db", return nonzero
  //   if the file could not be opened for any reason.  If the name has a part # in it then
  //   just the part is opened.  The index array is allocated (for all or just the part) and
  //   read in.

int Open_DB(char *path, HITS_DB *db);

  // Trim the DB or part thereof and all loaded tracks according to the cuttof and all settings
  //   of the current DB partition.  Reallocate smaller memory blocks for the information kept
  //   for the retained reads.

void Trim_DB(HITS_DB *db);

  // Shut down an open 'db' by freeing all associated space, including tracks and QV structures,
  //   and any open file pointers.  The record pointed at by db however remains (the user
  //   supplied it and so should free it).

void Close_DB(HITS_DB *db);

  // If QV pseudo track is not already in db's track list, then set it up and return a pointer
  //   to it.  The database must not have been trimmed yet.

void Load_QVs(HITS_DB *db);

  // Remove the QV pseudo track, all space associated with it, and close the .qvs file.

void Close_QVs(HITS_DB *db);

  // If track is not already in the db's track list, then allocate all the storage for it,
  //   read it in from the appropriate file, add it to the track list, and return a pointer
  //   to the newly created HITS_TRACK record.  If the track does not exist or cannot be
  //   opened for some reason, then NULL is returned.

HITS_TRACK *Load_Track(HITS_DB *db, char *track);

  // If track is on the db's track list, then it is removed and all storage associated with it
  //   is freed.

void Close_Track(HITS_DB *db, char *track);

  // Allocate and return a buffer big enough for the largest read in 'db'.
  // **NB** free(x-1) if x is the value returned as *prefix* and suffix '\0'(4)-byte
  // are needed by the alignment algorithms.

char *New_Read_Buffer(HITS_DB *db);

  // Load into 'read' the i'th read in 'db'.  As a lower case ascii string if ascii is 1, an
  //   upper case ascii string if ascii is 2, and a numeric string over 0(A), 1(C), 2(G), and 3(T)
  //   otherwise.  A '\0' (or 4) is prepended and appended to the string so it has a delimeter
  //   for traversals in either direction.

void Load_Read(HITS_DB *db, int i, char *read, int ascii);

  // Allocate a set of 5 vectors large enough to hold the longest QV stream that will occur
  //   in the database.  

#define DEL_QV  0   //  The deletion QVs are x[DEL_QV] if x is the buffer returned by New_QV_Buffer
#define DEL_TAG 1   //  The deleted characters
#define INS_QV  2   //  The insertion QVs
#define SUB_QV  3   //  The substitution QVs
#define MRG_QV  4   //  The merge QVs

char **New_QV_Buffer(HITS_DB *db);

  // Load into 'entry' the 5 QV vectors for i'th read in 'db'.  The deletion tag or characters
  //   are converted to a numeric or upper/lower case ascii string as per ascii.

void   Load_QVentry(HITS_DB *db, int i, char **entry, int ascii);

  // Allocate a block big enough for all the uncompressed sequences, read them into it,
  //   reset the 'off' in each read record to be its in-memory offset, and set the
  //   bases pointer to point at the block after closing the bases file.  If ascii is
  //   1 then the reads are converted to lowercase ascii, if 2 then uppercase ascii, and
  //   otherwise the reads are left as numeric strings over 0(A), 1(C), 2(G), and 3(T).

void Read_All_Sequences(HITS_DB *db, int ascii);

  // For the DB "path" = "prefix/root[.db]", find all the files for that DB, i.e. all those
  //   of the form "prefix/[.]root.part" and call foreach with the complete path to each file
  //   pointed at by path, and the suffix of the path by extension.  The . proceeds the root
  //   name if the defined constant HIDE_FILES is set.  Always the first call is with the
  //   path "prefix/root.db" and extension "db".  There will always be calls for
  //   "prefix/[.]root.idx" and "prefix/[.]root.bps".  All other calls are for *tracks* and
  //   so this routine gives one a way to know all the tracks associated with a given DB.
  //   Return non-zero iff path could not be opened for any reason.

int List_DB_Files(char *path, void foreach(char *path, char *extension));

#endif // _HITS_DB
