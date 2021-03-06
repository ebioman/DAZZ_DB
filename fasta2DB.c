/*******************************************************************************************
 *
 *  Add .fasta files to a DB:
 *     Adds the given fasta files in the given order to <path>.db.  If the db does not exist
 *     then it is created.  All .fasta files added to a given data base must have the same
 *     header format and follow Pacbio's convention.  A file cannot be added twice and this
 *     is enforced.  The command either builds or appends to the .<path>.idx and .<path>.bps
 *     files, where the index file (.idx) contains information about each read and their offsets
 *     in the base-pair file (.bps) that holds the sequences where each base is compessed
 *     into 2-bits.  The two files are hidden by virtue of their names beginning with a '.'.
 *     <path>.db is effectively a stub file with given name that contains an ASCII listing
 *     of the files added to the DB and possibly the block partitioning for the DB if DBsplit
 *     has been called upon it.
 *
 *  Author:  Gene Myers
 *  Date  :  May 2013
 *  Modify:  DB upgrade: now *add to* or create a DB depending on whether it exists, read
 *             multiple .fasta files (no longer a stdin pipe).
 *  Date  :  April 2014
 *
 ********************************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "DB.h"

#ifdef HIDE_FILES
#define PATHSEP "/."
#else
#define PATHSEP "/"
#endif

static char *Usage = "[-v] <path:string> <input:fasta> ...";

static char number[128] =
    { 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 1, 0, 0, 0, 2,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 3, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 1, 0, 0, 0, 2,
      0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 3, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
    };

int main(int argc, char *argv[])
{ FILE  *istub, *ostub;
  char  *dbname;
  char  *root, *pwd;

  FILE  *bases, *indx;
  int64  boff, ioff;

  int    ifiles, ofiles;
  char **flist;

  HITS_DB db;
  int     oreads;
  int64   offset;

  int     VERBOSE;

  //   Usage: <path:string> <input:fasta> ...

  { int   i, j, k;
    int   flags[128];

    ARG_INIT("fasta2DB")

    j = 1;
    for (i = 1; i < argc; i++)
      if (argv[i][0] == '-')
        { ARG_FLAGS("v") }
      else
        argv[j++] = argv[i];
    argc = j;

    VERBOSE = flags['v'];

    if (argc <= 2)
      { fprintf(stderr,"Usage: %s %s\n",Prog_Name,Usage);
        exit (1);
      }
  }

  //  Try to open DB file, if present then adding to DB, otherwise creating new DB.  Set up
  //  variables as follows:
  //    dbname = full name of db = <pwd>/<root>.db
  //    istub  = open db file (if adding) or NULL (if creating)
  //    ostub  = new image of db file (will overwrite old image at end)
  //    bases  = .bps file positioned for appending
  //    indx   = .idx file positioned for appending
  //    oreads = # of reads currently in db
  //    offset = offset in .bps at which to place next sequence
  //    ioff   = offset in .idx file to truncate to if command fails
  //    boff   = offset in .bps file to truncate to if command fails
  //    ifiles = # of .fasta files to add
  //    ofiles = # of .fasta files already in db
  //    flist  = [0..ifiles+ofiles] list of file names (root only) added to db so far

  { int     i;

    root   = Root(argv[1],".db");
    pwd    = PathTo(argv[1]);
    dbname = Strdup(Catenate(pwd,"/",root,".db"),"Allocating db name");
    if (dbname == NULL)
      exit (1);

    istub  = fopen(dbname,"r");
    ifiles = argc-2;

    if (istub == NULL)
      { ofiles = 0;

        bases = Fopen(Catenate(pwd,PATHSEP,root,".bps"),"w+");
        indx  = Fopen(Catenate(pwd,PATHSEP,root,".idx"),"w+");
        if (bases == NULL || indx == NULL)
          exit (1);

        fwrite(&db,sizeof(HITS_DB),1,indx);

        oreads  = 0;
        offset  = 0;
        boff    = 0;
        ioff    = 0;
      }
    else
      { fscanf(istub,DB_NFILE,&ofiles);

        bases = Fopen(Catenate(pwd,PATHSEP,root,".bps"),"r+");
        indx  = Fopen(Catenate(pwd,PATHSEP,root,".idx"),"r+");
        if (bases == NULL || indx == NULL)
          exit (1);

        fread(&db,sizeof(HITS_DB),1,indx);
        fseeko(bases,0,SEEK_END);
        fseeko(indx, 0,SEEK_END);

        oreads = db.oreads;
        offset = ftello(bases);
        boff   = offset;
        ioff   = ftello(indx);
      }

    flist  = (char **) Malloc(sizeof(char *)*(ofiles+ifiles),"Allocating file list");
    ostub  = Fopen(Catenate(pwd,"/",root,".dbx"),"w");
    if (ostub == NULL || flist == NULL)
      exit (1);

    fprintf(ostub,DB_NFILE,ofiles+ifiles);
    for (i = 0; i < ofiles; i++)
      { int  last;
        char prolog[MAX_NAME], fname[MAX_NAME];

        fscanf(istub,DB_FDATA,&last,fname,prolog);
        if ((flist[i] = Strdup(fname,"Adding to file list")) == NULL)
          goto error;
        fprintf(ostub,DB_FDATA,last,fname,prolog);
      }
  }

  { int        maxlen;
    int64      totlen, count[4];
    int        pmax, rmax;
    HITS_READ *prec;
    char      *read;
    int        c;

    //  Buffer for reads all in the same well

    pmax = 100;
    prec = (HITS_READ *) Malloc(sizeof(HITS_READ)*pmax,"Allocating record buffer");
    if (prec == NULL)
      goto error;

    //  Buffer for accumulating .fasta sequence over multiple lines

    rmax  = MAX_NAME + 60000;
    read  = (char *) Malloc(rmax+1,"Allocating line buffer");
    if (read == NULL)
      goto error;

    totlen = 0;              //  total # of bases in new .fasta files
    maxlen = 0;              //  longest read in new .fasta files
    for (c = 0; c < 4; c++)  //  count of acgt in new .fasta files
      count[c] = 0;

    //  For each new .fasta file do:

    for (c = 2; c < argc; c++)
      { FILE *input;
        char *path, *core, *prolog;
        int   nline, eof, rlen, pcnt;
        int   pwell;

        //  Open it: <path>/<core>.fasta, check that core is not too long, and add it to
        //           list of added files, flist[0..ofiles), after checking that it is not
        //           already in the list.

        path  = PathTo(argv[c]);
        core  = Root(argv[c],".fasta");
        if ((input = Fopen(Catenate(path,"/",core,".fasta"),"r")) == NULL)
          goto error;
        if (strlen(core) >= MAX_NAME)
          { fprintf(stderr,"%s: File name over %d chars: '%.200s'\n",
                           Prog_Name,MAX_NAME,core);
            goto error;
          }

        { int j;

          for (j = 0; j < ofiles; j++)
            if (strcmp(core,flist[j]) == 0)
              { fprintf(stderr,"%s: File %s.fasta is already in database %s.db\n",
                               Prog_Name,core,Root(argv[1],".db"));
                goto error;
              }
          flist[ofiles++] = core;
          free(path);
        }

        if (VERBOSE)
          { fprintf(stderr,"Adding '%s' ...\n",core);
            fflush(stderr);
          }

        //  Get the header of the first line, check that it has PACBIO format, and record
        //    prolog in 'prolog'.

        pcnt  = 0;
        rlen  = 0;
        nline = 1;
        eof   = (fgets(read,MAX_NAME,input) == NULL);
        if (read[strlen(read)-1] != '\n')
          { fprintf(stderr,"File %s.fasta, Line 1: Fasta line is too long (> %d chars)\n",
                           core,MAX_NAME-2);
            goto error;
          }
        if (!eof && read[0] != '>')
          { fprintf(stderr,"File %s.fasta, Line 1: First header in fasta file is missing\n",core);
            goto error;
          }

        { char *find;
          int   well, beg, end, qv;

          find = index(read+1,'/');
          if (find != NULL && sscanf(find+1,"%d/%d_%d RQ=0.%d\n",&well,&beg,&end,&qv) >= 3)
            { *find = '\0';
              prolog = Strdup(read+1,"Extracting prolog");
              *find = '/';
              if (prolog == NULL) goto error;
            }
          else
            { fprintf(stderr,"File %s.fasta, Line %d: Pacbio header line format error\n",
                             core,nline);
              goto error;
            }
        }

        //  Read in all the sequences until end-of-file

        { int i, x;

          pwell = -1;
          while (!eof)
            { int   beg, end, clen;
              int   well, qv;
              char *find;

              find = index(read+(rlen+1),'/');
              if (find == NULL)
                { fprintf(stderr,"File %s.fasta, Line %d: Pacbio header line format error\n",
                                 core,nline);
                  goto error;
                }
              *find = '\0';
              if (strcmp(read+(rlen+1),prolog) != 0)
                { fprintf(stderr,"File %s.fasta, Line %d: Pacbio header line name inconsisten\n",
                                 core,nline);
                  goto error;
                }
              *find = '/';
              x = sscanf(find+1,"%d/%d_%d RQ=0.%d\n",&well,&beg,&end,&qv);
              if (x < 3)
                { fprintf(stderr,"File %s.fasta, Line %d: Pacbio header line format error\n",
                                 core,nline);
                  goto error;
                }
              else if (x == 3)
                qv = 0;

              rlen = 0;
              while (1)
                { eof = (fgets(read+rlen,MAX_NAME,input) == NULL);
                  nline += 1;
                  x = strlen(read+rlen)-1;
                  if (read[rlen+x] != '\n')
                    { fprintf(stderr,"File %s.fasta, Line %d:",core,nline);
                      fprintf(stderr," Fasta line is too long (> %d chars)\n",MAX_NAME-2);
                      goto error;
                    }
                  if (eof || read[rlen] == '>')
                    break;
                  rlen += x;
                  if (rlen + MAX_NAME > rmax)
                    { rmax = 1.2 * rmax + 1000 + MAX_NAME;
                      read = (char *) realloc(read,rmax+1);
                      if (read == NULL)
                        { fprintf(stderr,"File %s.fasta, Line %d:",core,nline);
                          fprintf(stderr," Out of memory (Allocating line buffer)\n");
                          goto error;
                        }
                    }
                }
              read[rlen] = '\0';

              for (i = 0; i < rlen; i++)
                { x = number[(int) read[i]];
                  count[x] += 1;
                  read[i]   = x;
                }
              oreads += 1;
              totlen += rlen;
              if (rlen > maxlen)
                maxlen = rlen;

              prec[pcnt].origin = well;
              prec[pcnt].beg    = beg;
              prec[pcnt].end    = end;
              prec[pcnt].boff   = offset;
              prec[pcnt].coff   = 0;
              prec[pcnt].flags  = qv;

              Compress_Read(rlen,read);
              clen = COMPRESSED_LEN(rlen);
              fwrite(read,1,clen,bases);
              offset += clen;

              if (pwell == well)
                { prec[pcnt].flags |= DB_CSS;
                  pcnt += 1;
                  if (pcnt >= pmax)
                    { pmax = pcnt*1.2 + 100;
                      prec = (HITS_READ *) realloc(prec,sizeof(HITS_READ)*pmax);
                      if (prec == NULL)
                        { fprintf(stderr,"File %s.fasta, Line %d: Out of memory",core,nline);
                          fprintf(stderr," (Allocating read records)\n");
                          goto error;
                        }
                    }
                }
              else if (pcnt == 0)
                pcnt += 1;
              else
                { x = 0;
                  for (i = 1; i < pcnt; i++)
                    if (prec[i].end - prec[i].beg > prec[x].end - prec[x].beg)
                      x = i;
                  prec[x].flags |= DB_BEST;
                  fwrite(prec,sizeof(HITS_READ),pcnt,indx);
                  prec[0] = prec[pcnt];
                  pcnt = 1;
                }
              pwell = well;
            }

          //  Complete processing of .fasta file: flush last well group, write file line
          //      in db image, free prolog, and close file

          x = 0;
          for (i = 1; i < pcnt; i++)
            if (prec[i].end - prec[i].beg > prec[x].end - prec[i].beg)
              x = i;
          prec[x].flags |= DB_BEST;
          fwrite(prec,sizeof(HITS_READ),pcnt,indx);

          fprintf(ostub,DB_FDATA,oreads,core,prolog);

	  free(prolog);
          fclose(input);
        }
      }

    //  Finished loading all sequences: update relevant fields in db record

    db.oreads = oreads;
    if (istub == NULL)
      { for (c = 0; c < 4; c++)
          db.freq[c] = (1.*count[c])/totlen;
        db.totlen = totlen;
        db.maxlen = maxlen;
        db.cutoff = -1;
      }
    else
      { for (c = 0; c < 4; c++)
          db.freq[c] = (db.freq[c]*db.totlen + (1.*count[c]))/(db.totlen + totlen);
        db.totlen += totlen;
        if (maxlen > db.maxlen)
          db.maxlen = maxlen;
      }
  }

  //  If db has been previously partitioned then calculate additional partition points and
  //    write to new db file image

  if (db.cutoff >= 0)
    { int64      totlen, dbpos, size;
      int        nblock, ireads, bfirst, rlen;
      int        ofirst, cutoff, allflag;
      HITS_READ  record;
      int        i;

      if (VERBOSE)
        { fprintf(stderr,"Updating block partition ...\n");
          fflush(stderr);
        }

      //  Read the block portion of the existing db image getting the indices of the first
      //    read in the last block of the exisiting db as well as the partition parameters.
      //    Copy the old image block information to the new block information (except for
      //    the indices of the last partial block)

      fscanf(istub,DB_NBLOCK,&nblock); 
      dbpos = ftello(ostub);
      fprintf(ostub,DB_NBLOCK,0);
      fscanf(istub,DB_PARAMS,&size,&cutoff,&allflag); 
      fprintf(ostub,DB_PARAMS,size,cutoff,allflag); 
      if (allflag)
        allflag = 0;
      else
        allflag = DB_BEST;
      size *= 1000000ll;

      nblock -= 1;
      for (i = 0; i <= nblock; i++)
        { fscanf(istub,DB_BDATA,&ofirst,&bfirst);
          fprintf(ostub,DB_BDATA,ofirst,bfirst);
        }

      //  Seek the first record of the last block of the existing db in .idx, and then
      //    compute and record partition indices for the rest of the db from this point
      //    forward.

      fseeko(indx,sizeof(HITS_DB)+sizeof(HITS_READ)*ofirst,SEEK_SET);
      totlen = 0;
      ireads = 0;
      for (i = ofirst; i < oreads; i++)
        { fread(&record,sizeof(HITS_READ),1,indx);
          rlen = record.end - record.beg;
          if (rlen >= cutoff && (record.flags & DB_BEST) >= allflag)
            { ireads += 1;
              bfirst += 1;
              totlen += rlen;
              if (totlen >= size || ireads >= READMAX)
                { fprintf(ostub," %9d %9d\n",i+1,bfirst);
                  totlen = 0;
                  ireads = 0;
                  nblock += 1;
                }
            }
        }

      if (ireads > 0)
        { fprintf(ostub,DB_BDATA,oreads,bfirst);
          nblock += 1;
        }

      db.breads = bfirst;

      fseeko(ostub,dbpos,SEEK_SET);
      fprintf(ostub,DB_NBLOCK,nblock);    //  Rewind and record the new number of blocks
    }
  else
    db.breads = oreads;

  rewind(indx);
  fwrite(&db,sizeof(HITS_DB),1,indx);   //  Write the finalized db record into .idx

  if (istub != NULL)
    fclose(istub);
  fclose(ostub);
  fclose(indx);
  fclose(bases);

  rename(Catenate(pwd,"/",root,".dbx"),dbname);   //  New image replaces old image

  exit (0);

  //  Error exit:  Either truncate or remove the .idx and .bps files as appropriate.
  //               Remove the new image file <pwd>/<root>.dbx

error:
  if (ioff != 0)
    { fseeko(indx,0,SEEK_SET);
      ftruncate(fileno(indx),ioff);
    }
  if (boff != 0)
    { fseeko(bases,0,SEEK_SET);
      ftruncate(fileno(bases),boff);
    }
  fclose(indx);
  fclose(bases);
  if (ioff == 0)
    unlink(Catenate(pwd,PATHSEP,root,".idx"));
  if (boff == 0)
    unlink(Catenate(pwd,PATHSEP,root,".bps"));

  if (istub != NULL)
    fclose(istub);
  fclose(ostub);
  unlink(Catenate(pwd,"/",root,".dbx"));

  exit (1);
}
