/** hdck - hard drive low-level errors checking 
 * Compile with GCC, and -lrt
 * (c) 2009 Hubert Kario
 * uses parts of code from GNU dd:
 *  * Copyright (C) 2002-9 Bruce Allen <smartmontools-support@lists.sourceforge.net>
 *  * Copyright (C) 2008-9 Christian Franke <smartmontools-support@lists.sourceforge.net>
 *  * Copyright (C) 2000 Michael Cornwell <cornwell@acm.org>
 *  *
 *  * This program is free software; you can redistribute it and/or modify
 *  * it under the terms of the GNU General Public License as published by
 *  * the Free Software Foundation; either version 2, or (at your option)
 *  * any later version.
 *  *
 *  * You should have received a copy of the GNU General Public License
 *  * (for example COPYING); if not, write to the Free
 *  * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *  *
 *  * This code was originally developed as a Senior Thesis by Michael Cornwell
 *  * at the Concurrent Systems Laboratory (now part of the Storage Systems
 *  * Research Center), Jack Baskin School of Engineering, University of
 *  * California, Santa Cruz. http://ssrc.soe.ucsc.edu/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free software Foundation; wither version 3, or (at your opinion)
 * any later version.
 */

#define _GNU_SOURCE 1
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sched.h>
#include <sys/syscall.h>
#include <getopt.h>
#include "ioprio.h"
#define TIMER_TYPE CLOCK_REALTIME 

int
pagesize = 4096;
int
sectors = 255;
int
verbosity = 0;

/** Print usage information
 */
usage()
{
  printf("Usage: hdck [OPTIONS]\n");
  printf("Test hard drive for latent and hidden bad sectors\n");
  printf("\n");
  printf("-f, --file FILE     device file to test\n");
  printf("-x, --exclusive     use exclusive access\n");
  printf("                    (runs faster, but partitions must be umounted)\n");
  printf("--nodirect          don't use O_DIRECT and don't flush buffers before reading\n");
  printf("--noaffinity        don't set CPU affinity to 0th core/CPU\n");
  printf("--nort              don't change priority to real-time\n");
  printf("-v, --verbose       be more verbose\n");
  printf("-h, -?              print this message\n");
}

/**Return PTR, aligned upward to the next multiple of ALIGNMENT.
   ALIGNMENT must be nonzero.  The caller must arrange for ((char *)
   PTR) through ((char *) PTR + ALIGNMENT - 1) to be addressable
   locations.  */
static inline void *
ptr_align (void const *ptr, size_t alignment)
{
  char const *p0 = ptr;
  char const *p1 = p0 + alignment - 1;
  return (void *) (p1 - (size_t) p1 % alignment);
}

/** calculate the difference between start and end
  */
void
diff_time(struct timespec *res, struct timespec start, struct timespec end)
{
  if ((end.tv_nsec-start.tv_nsec)<0)
    {
      res->tv_sec = end.tv_sec-start.tv_sec-1;
      res->tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } 
  else 
    {
      res->tv_sec = end.tv_sec-start.tv_sec;
      res->tv_nsec = end.tv_nsec-start.tv_nsec;
    }
  return;
}

void
div_time(struct timespec *res, struct timespec divisor, long long divider)
{
  res->tv_sec = divisor.tv_sec / divider;
  res->tv_nsec = ((divisor.tv_sec % divider * 1.0l) / divider) * 1000000000 +
    divisor.tv_nsec / divider;
}

void
sum_time(struct timespec *sum, struct timespec *adder)
{
  sum->tv_sec += adder->tv_sec;
  sum->tv_nsec += adder->tv_nsec;
  if (sum->tv_nsec >= 1000000000)
    {
      sum->tv_nsec -= 1000000000;
      sum->tv_sec += 1;
    }
  return;
}

int
main(int argc, char **argv)
{
   int c;
   int digit_optind = 0;
   char* filename = NULL;
   int exclusive = 0;
   int nodirect = 0; ///< don't use O_DIRECT access
   int noaffinity = 0; ///< don't set CPU affinity
   int nort = 0; ///< don't change process scheduling to RT

  if (argc == 1)
    {
      usage();
      exit(EXIT_FAILURE);
    }

   while (1) {
       int this_option_optind = optind ? optind : 1;
       int option_index = 0;
       struct option long_options[] = {
           {"file", 1, 0, 'f'}, // 0
           {"exclusive", 0, 0, 'x'}, // 1
           {"nodirect", 0, &nodirect, 1}, // 2
           {"verbose", 0, 0, 'v'}, // 3
           {"noaffinity", 0, &noaffinity, 1}, // 4
           {"nort", 0, &nort, 1}, // 5
           {0, 0, 0, 0}
       };

       c = getopt_long(argc, argv, "f:xhv?",
                long_options, &option_index);
       if (c == -1)
           break;

       switch (c) {
       case 0:
           if (verbosity > 5)
             {
               printf("option %s", long_options[option_index].name);
               if (optarg)
                   printf(" with arg %s", optarg);
               printf("\n");
             }
           if (option_index == 2)
             {
               nodirect = 1;
               break;
             }
           break;

       case 'v':
           if (verbosity > 5 ) printf("option v\n");
           verbosity++;
           break;

       case 'x':
           if (verbosity > 5) printf("option x\n");
           exclusive = 1;
           break;

       case 'f':
           filename = optarg;
           if (verbosity > 5) printf("option f with value '%s'\n", optarg);
           break;

       case 'h':
       case '?':
           usage();
           exit(EXIT_FAILURE);
           break;

       default:
           printf("?? getopt returned character code 0%o ??\n", c);
           exit(EXIT_FAILURE);
       }
   }

   if (optind < argc)
    {
       printf("trailing options: ");
       while (optind < argc)
           printf("%s ", argv[optind++]);
       printf("\n");
       usage();
       exit(EXIT_FAILURE);
    }

   if (filename == NULL)
     {
       printf("Missing -f parameter!\n");
       usage();
       exit(EXIT_FAILURE);
     }

  struct timespec time1, time2, 
                  sumtime, /* sumaric time */
                  res, /* temp result */
                  times, timee;
  long long blocks = 0;

  int dev_fd = 0;
  char *ibuf;
  ssize_t nread;

  // make the process real-time
  struct sched_param sp;
  sp.sched_priority = 31;
  if (sched_setscheduler(0,SCHED_FIFO,&sp) < 0)
    err(1, "scheduler");

  // make the process run on single core
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set); // zero the CPU set
  CPU_SET(0, &cpu_set); // add first cpu to the set
  if (sched_setaffinity(0,sizeof(cpu_set_t), &cpu_set) <0)
    err(1, "affinity");

  if (!nort)
    {
      // make the process' IO prio highest 
      if (ioprio_set(IOPRIO_WHO_PROCESS, 
                     0, 
                     IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 0)
            ) != 0)
        err(1, "ioprio: can't make process real-time");
    }

  // open the file with disabled caching
  // (O_EXCL works on devices too)
  dev_fd = open(filename, O_RDONLY | O_DIRECT | O_LARGEFILE | O_SYNC);
  if (dev_fd < 0)
    {
      err(1, NULL);
    }

  // get memory alligned pointer
  ibuf = malloc(sectors*512+pagesize);
  ibuf = ptr_align(ibuf, pagesize);

  // Attempt to free all cached pages related to the opened file
  fsync(dev_fd);
  if (posix_fadvise(dev_fd, 0, 0, POSIX_FADV_DONTNEED) < 0)
    err(1, NULL);

  sumtime.tv_sec = 0;
  sumtime.tv_nsec = 0;
 
  long long errors = 0,
    vvfast = 0,
    vfast = 0,
    fast = 0,
    normal = 0,
    slow = 0,
    vslow = 0,
    vvslow = 0;  
  clock_gettime(TIMER_TYPE, &times);
  while (1)
    {
      clock_gettime(TIMER_TYPE, &time1);
      nread = read(dev_fd, ibuf, sectors*512);
      clock_gettime(TIMER_TYPE, &time2);
      if (nread < 0)
        {
          if (errno != EIO)
            err(1, NULL);
          else
            {
              diff_time(&res, time1, time2); 
              nread = 1; // don't exit loop
              write(0, "E", 1);
              ++errors;

              // omit sector
              if (lseek(dev_fd, (off_t)512*sectors, SEEK_CUR) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
            }
        }
      else
        diff_time(&res, time1, time2);

      if (res.tv_nsec < 2000000) // very very fast read
        //write(0,"_",1), 
        ++vvfast;
      else if (res.tv_nsec < 5000000) // very fast read
        //write(0,".",1), 
        ++vfast;
      else if (res.tv_nsec < 10000000) // fast read
        //write(0,",",1), 
        ++fast;
      else if (res.tv_nsec < 25000000) // normal read
        //write(0,"-",1), 
        ++normal;
      else if (res.tv_nsec < 50000000) // slow read
        //write(0,"+",1), 
        ++slow;
      else if (res.tv_nsec < 80000000) // very slow read
        //write(0,"#",1), 
        ++vslow;
      else // very very slow read
        {
          //write(0,"!",1);
          ++vvslow;
          //fprintf(stderr, "%lli\n", blocks * sectors * 1ll);
        }
      printf("%li\n",res.tv_nsec/1000+res.tv_sec*1000000); 
//      fsync(0);
      blocks++;
      sum_time(&sumtime, &res);
      if (nread == 0)
        break;

      if (blocks % 1000 == 0)
        {
          clock_gettime(TIMER_TYPE, &timee);
          diff_time(&res, times, timee);
          float speed;
          speed = blocks * sectors * 512 / 1024 * 1.0f / 1024 / res.tv_sec;
          fprintf(stderr,"read %lli sectors (%.3fMiB/s)\n",
             blocks*sectors,
             speed);
        }

 /*     printf("%lis:%lims:%liµs:%lins\n", res.tv_sec,
         res.tv_nsec/1000000, res.tv_nsec/1000%1000,
         res.tv_nsec%1000, res.tv_nsec); */
    }
  clock_gettime(TIMER_TYPE, &timee);
  fprintf(stderr, "sum time: %lis.%lims.%liµs.%lins\n", sumtime.tv_sec,
      sumtime.tv_nsec/1000000,
      sumtime.tv_nsec/1000%1000,
      sumtime.tv_nsec%1000);
  div_time(&res, sumtime, blocks);
  fprintf(stderr, "read %lli blocks (%lli errors)\n", blocks, errors);
  fprintf(stderr, "mean block time: %lis.%lims.%liµs.%lins\n", res.tv_sec,
      res.tv_nsec/1000000, res.tv_nsec/1000%1000,
      res.tv_nsec%1000);
  diff_time(&res, times, timee);
  fprintf(stderr, "wall time: %lis.%lims.%liµs.%lins\n", res.tv_sec,
      res.tv_nsec/1000000, res.tv_nsec/1000%1000,
      res.tv_nsec%1000);
  fprintf(stderr, "ERR: %lli\n2ms: %lli\n5ms: %lli\n10ms: %lli\n25ms: %lli\n"
      "50ms: %lli\n80ms: %lli\n80+ms: %lli\n",
      errors, vvfast, vfast, fast, normal, slow, vslow, vvslow);
  return 1;
}
