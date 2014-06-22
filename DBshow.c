/*******************************************************************************************
 *
 *  Display a specified set of reads of a database in fasta format.
 *
 *  Author:  Gene Myers
 *  Date  :  September 2013
 *  Mod   :  With DB overhaul, made this a routine strictly for printing a selected subset
 *             and created DB2fasta for recreating all the fasta files of a DB
 *  Date  :  April 2014
 *
 ********************************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "DB.h"

static char *Usage = "[-du] <path:db> [ <reads:range> ... ]";

int main(int argc, char *argv[])
{ HITS_DB    _db, *db = &_db;
  HITS_TRACK *dust;
  int         reps, *pts;
  int         DUST;
  int         TRIM;

  //  Process arguments

  { int i, j, k;
    int flags[128];

    ARG_INIT("DBread")

    j = 1;
    for (i = 1; i < argc; i++)
      if (argv[i][0] == '-')
        { ARG_FLAGS("du") }
      else
        argv[j++] = argv[i];
    argc = j;

    DUST = flags['d'];
    TRIM = 1-flags['u'];

    if (argc <= 1)
      { fprintf(stderr,"Usage: %s %s\n",Prog_Name,Usage);
        exit (1);
      }
  }

  //  Open DB, Dust track if requested, and then trim unless -u set

  if (Open_DB(argv[1],db))
    exit (1);

  if (DUST)
    dust = Load_Track(db,"dust");
  else
    dust = NULL;

  if (TRIM)
    Trim_DB(db);

  //  Process read index arguments into a list of read ranges

  pts  = (int *) Malloc(sizeof(int)*2*(argc-1),"Allocating read parameters");
  if (pts == NULL)
    exit (1);

  reps = 0;
  if (argc > 2)
    { int   c, b, e;
      char *eptr, *fptr;

      for (c = 2; c < argc; c++)
        { if (argv[c][0] == '#')
            { b = db->nreads;
              eptr = argv[c]+1;
            }
          else
            b = strtol(argv[c],&eptr,10);
          if (eptr > argv[c])
            { if (*eptr == 0)
                { pts[reps++] = b;
                  pts[reps++] = b;
                  continue;
                }
              else if (*eptr == '-')
                { if (eptr[1] == '#')
                    { e = db->nreads;
                      fptr = eptr+2;
                    }
                  else
                    e = strtol(eptr+1,&fptr,10);
                  if (fptr > eptr+1 && *fptr == 0 && eptr[1] != '-')
                    { pts[reps++] = b;
                      pts[reps++] = e;
                      if (b > e)
                        { fprintf(stderr,"%s: Empty range '%s'\n",Prog_Name,argv[c]);
                          exit (1);
                        }
                      continue;
                    }
                }
            }
          fprintf(stderr,"%s: argument '%s' is not an integer range\n",Prog_Name,argv[c]);
          exit (1);
        }
    }
  else
    { pts[reps++] = 1;
      pts[reps++] = db->nreads;
    }

  //  Display each read in the active DB according to the range pairs in pts[0..reps)

  { HITS_READ  *reads;
    int        *anno, *data;
    char       *read;
    int         c, b, e, i;

    read = New_Read_Buffer(db);

    if (dust != NULL)
      { anno = (int *) dust->anno;
        data = (int *) dust->data;
      }

    reads = db->reads;
    for (c = 0; c < reps; c += 2)
      { b = pts[c]-1;
        e = pts[c+1];
        for (i = b; i < e; i++)
          { int        j, len;
            int        flags, qv;
            HITS_READ *r;

            r   = reads + i;
            len = r->end - r->beg;

            flags = r->flags;
            qv    = (flags & DB_QV);
            printf(">Prolog/%d/%d_%d",r->origin,r->beg,r->end);
            if (qv > 0)
              printf(" RQ=0.%3d",qv);
            printf("\n");

            Load_Read(db,i,read,1);

            if (dust != NULL)
              { int  s, f, b, e, m;

                s = (anno[i] >> 2);
                f = (anno[i+1] >> 2);
                if (s < f)
                  { for (j = s; j < f; j += 2)
                      { b = data[j];
                        e = data[j+1];
                        for (m = b; m <= e; m++)
                          read[m] = (read[m]-'a')+'A';
                        if (j == s)
                          printf("> ");
                        printf(" [%d,%d]",b,e);
                      }
                    printf("\n");
                  }
              }

            for (j = 0; j+70 < len; j += 70)
              printf("%.70s\n",read+j);
            if (j < len)
              printf("%s\n",read+j);
          }
      }
  }

  Close_DB(db);

  exit (0);
}