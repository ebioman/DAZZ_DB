/********************************************************************************************
 *
 *  Recreate all the .fasta files that have been loaded into a specified database.
 *
 *  Author:  Gene Myers
 *  Date  :  May 2014
 *
 ********************************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "DB.h"

static char *Usage = "[-vU] [-w<int(80)>] <path:db>";

int main(int argc, char *argv[])
{ HITS_DB    _db, *db = &_db;
  FILE       *dbfile;
  int         nfiles;
  int         VERBOSE, UPPER, WIDTH;

  //  Process arguments

  { int   i, j, k;
    int   flags[128];
    char *eptr;

    ARG_INIT("DB2fasta")

    WIDTH = 80;

    j = 1;
    for (i = 1; i < argc; i++)
      if (argv[i][0] == '-')
        switch (argv[i][1])
        { default:
            ARG_FLAGS("vU")
            break;
          case 'w':
            ARG_NON_NEGATIVE(WIDTH,"Line width")
            break;
        }
      else
        argv[j++] = argv[i];
    argc = j;

    UPPER   = 1 + flags['U'];
    VERBOSE = flags['v'];

    if (argc != 2)
      { fprintf(stderr,"Usage: %s %s\n",Prog_Name,Usage);
        exit (1);
      }
  }

  //  Open db and also db image file (dbfile)

  if (Open_DB(argv[1],db))
    { fprintf(stderr,"%s: Database %s.db could not be opened\n",Prog_Name,argv[1]);
      exit (1);
    }
  if (db->part > 0)
    { fprintf(stderr,"%s: Cannot be called on a block: %s.db\n",Prog_Name,argv[1]);
      exit (1);
    }

  { char *pwd, *root;

    pwd    = PathTo(argv[1]);
    root   = Root(argv[1],".db");
    dbfile = Fopen(Catenate(pwd,"/",root,".db"),"r");
    free(pwd);
    free(root);
    if (dbfile == NULL)
      exit (1);
  }

  //  nfiles = # of files in data base

  fscanf(dbfile,DB_NFILE,&nfiles);

  //  For each file do:

  { HITS_READ  *reads;
    char       *read;
    int         f, first;

    reads = db->reads;
    read  = New_Read_Buffer(db);
    first = 0;
    for (f = 0; f < nfiles; f++)
      { int   i, last;
        FILE *ofile;
        char  prolog[MAX_NAME], fname[MAX_NAME];

        //  Scan db image file line, create .fasta file for writing

        fscanf(dbfile,DB_FDATA,&last,fname,prolog);

        if ((ofile = Fopen(Catenate(".","/",fname,".fasta"),"w")) == NULL)
          exit (1);

        if (VERBOSE)
          { fprintf(stderr,"Creating %s.fasta ...\n",fname);
            fflush(stdout);
          }

        //   For the relevant range of reads, write each to the file
        //     recreating the original headers with the index meta-data about each read

        for (i = first; i < last; i++)
          { int        j, len;
            int        flags, qv;
            HITS_READ *r;

            r     = reads + i;
            len   = r->end - r->beg;
            flags = r->flags;
            qv    = (flags & DB_QV);
            fprintf(ofile,">%s/%d/%d_%d",prolog,r->origin,r->beg,r->end);
            if (qv > 0)
              fprintf(ofile," RQ=0.%3d",qv);
            fprintf(ofile,"\n");

            Load_Read(db,i,read,UPPER);

            for (j = 0; j+WIDTH < len; j += WIDTH)
              fprintf(ofile,"%.*s\n",WIDTH,read+j);
            if (j < len)
              fprintf(ofile,"%s\n",read+j);
          }

        first = last;
      }
  }

  fclose(dbfile);
  Close_DB(db);

  exit (0);
}
