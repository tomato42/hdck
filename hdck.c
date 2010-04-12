/** hdck - hard drive low-level errors checking 
 * Compile with GCC, and -lrt -lm
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
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sched.h>
#include <sys/syscall.h>
#include <getopt.h>
#include <math.h>
#include <fenv.h>
#include "ioprio.h"
#define TIMER_TYPE CLOCK_REALTIME 

int
pagesize = 4096;
int
sectors = 256;
int
verbosity = 0;

struct block_info_t {
    struct timespec sumtime; ///< sum of all the read times
    struct timespec sumsqtime; ///< sum of all squared read times
    int samples; ///< number of samples taken
    int valid; ///< 0 if data is invalid (because read was interrupted)
};

/** Print usage information
 */
void
usage()
{
  printf("Usage: hdck [OPTIONS]\n");
  printf("Test hard drive for latent and hidden bad sectors while it's still in use\n");
  printf("\n");
  printf("-f, --file FILE     device file to test\n");
  printf("-x, --exclusive     use exclusive access\n");
  printf("                    (runs faster, but all partitions must be"
                                                              " unmounted)\n");
  printf("--nodirect          don't use O_DIRECT\n");
  printf("--noflush           don't flush system buffers before reading\n");
  printf("--nosync            don't use O_SYNC\n");
  printf("--noaffinity        don't set CPU affinity to 0th core/CPU\n");
  printf("--nortio            don't change IO priority to real-time\n");
  printf("--sector-symbols    print symbols representing read time of each"
                                                       " group of sectors\n");
  printf("--sector-times      print time it takes to read each group of"
                                                        " sectors (in µs)\n");
  printf("--max-sectors NUM   read at most NUM sectors\n");
  printf("--noverbose         reduce verbosity\n");
  printf("-v, --verbose       be more verbose\n");
  printf("-h, -?              print this message\n");
  printf("\n");
  printf("This program can be run on both files and devices, though running it on top of\n");
  printf("a file is quite pointless. In most cases default settings should be OK. Things to\n");
  printf("check are --nodirect and --noflush, sometimes it works better without them.\n");
  printf("When using -x, the program uses different algorithm that trusts the times more\n");
  printf("and as a result, should achive minimum confidence in less time (by not using re-\n");
  printf("-reads) (God, I suck at English, rewrite it you moron :/)\n");
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

/** multiply time by itself
 * 
 * this works because:
 * for x = 1000*a + b
 * x^2 = (1000a)^2 + 2*1000a*b + b^2 = 1000*a_2 + b_2, where
 * a_2 = 1000*a^2 + 2ab + b^2 / 1000
 * b_2 = b^2 % 1000
 *
 * and log2(1000000000)*2=59.794... < 64
 */
void
sqr_time(struct timespec *res, struct timespec val)
{
  res->tv_sec = 1000000000 * val.tv_sec * val.tv_sec + 
                2 * val.tv_nsec * val.tv_sec + 
                val.tv_nsec * val.tv_nsec / 1000000000;
  res->tv_nsec = val.tv_nsec * val.tv_nsec % 1000000000;
}

/** take square root out of time 
 */
void
sqrt_time(struct timespec *res, struct timespec val)
{
  res->tv_sec = floor(sqrt(val.tv_sec));
  res->tv_nsec = (sqrt(val.tv_sec) - res->tv_sec) * 1000000000 + 
    floor(sqrt(val.tv_nsec));
  if (res->tv_nsec >= 1000000000)
    {
      res->tv_nsec -= 1000000000;
      res->tv_sec += 1;
    }
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
  int nortio = 0; ///< don't change process scheduling to RT
  int sector_times = 0;
  enum {
      PRINT_TIMES = 1,
      PRINT_SYMBOLS
  };
  int nosync = 0;
  int noflush = 0;
  off_t filesize = 0;
  off_t max_sectors = 0;
  struct block_info_t* block_info = NULL;

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
        {"nortio", 0, &nortio, 1}, // 5
        {"sector-times", 0, &sector_times, PRINT_TIMES}, // 6
        {"sector-symbols", 0, &sector_times, PRINT_SYMBOLS}, // 7
        {"nosync", 0, &nosync, 1}, // 8
        {"noverbose", 0, 0, 0}, // 9
        {"noflush", 0, &noflush, 1}, // 10
        {"max-sectors", 1, 0, 0}, // 11
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
        if (option_index == 9)
          {
            verbosity--;
            break;
          }
        if (option_index == 11)
          {
            max_sectors = atoll(optarg);
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
        exit(EXIT_SUCCESS);
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
                  times, timee,
                  sumsqtime; /* sum of squares (for std. deviation) */
  long long blocks = 0;

  int dev_fd = 0;
  char *ibuf;
  off_t nread;

  // make the process real-time
  struct sched_param sp;
  sp.sched_priority = 31;
  if (sched_setscheduler(0,SCHED_FIFO,&sp) < 0)
    err(1, "scheduler");

  if (!noaffinity)
    {
      // make the process run on single core
      cpu_set_t cpu_set;
      CPU_ZERO(&cpu_set); // zero the CPU set
      CPU_SET(0, &cpu_set); // add first cpu to the set
      if (sched_setaffinity(0,sizeof(cpu_set_t), &cpu_set) <0)
        err(1, "affinity");
    }

  if (!nortio)
    {
      // make the process' IO prio highest 
      if (ioprio_set(IOPRIO_WHO_PROCESS, 
                     0, 
                     IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 0)
            ) != 0)
        err(1, "ioprio: can't make process IO class real-time");
    }
 
  int flags = O_RDONLY | O_LARGEFILE; 

  // open the file with disabled caching
  if (!nodirect)
    {
      if (verbosity > 5)
        printf("setting O_DIRECT flag on file\n");
      flags = flags | O_DIRECT;
    }
  else
    {
      if (verbosity > 5)
        printf("NOT setting O_DIRECT on file\n");
    }

  if (!nosync)
    {
      if (verbosity > 5)
        printf("setting O_SYNC flag on file\n");
      flags = flags | O_SYNC;
    }
  else
    {
      if (verbosity > 5)
        printf("NOT setting O_SYNC on file\n");
    }

  if (exclusive)
    {
      if (verbosity > 5)
        printf("setting O_EXCL on file\n");
      flags = flags | O_EXCL;
    }
  else
    {
      if (verbosity > 5)
        printf("NOT setting O_EXCL on file\n");
    }

  dev_fd = open(filename, flags);
  if (dev_fd < 0)
    {
      err(1, NULL);
    }

  // get file size
  struct stat file_stat;
  char* dev_stat_path;
  if (fstat(dev_fd, &file_stat) == -1)
    err(1, "fstat");
  if (S_ISREG(file_stat.st_mode))
    {
      filesize = file_stat.st_size;
      if (verbosity > 1)
        {
          printf("file size: %lli B\n", file_stat.st_size);
        }
    }
  else if (S_ISBLK(file_stat.st_mode))
    {
      if (ioctl(dev_fd, BLKGETSIZE64, &filesize) == -1)
        err(1, "ioctl");
      if (verbosity > 1)
        printf("file size: %lli bytes\n", filesize);

      dev_stat_path = path to the stat file of device in /sys
    }
  else
    {
      printf("%s: %s: File is neither device file nor regular file", __FILE__, "main");
      exit(EXIT_FAILURE);
    }

  fesetround(2);
  block_info = calloc(lrintl(filesize*1.0l/512/sectors), sizeof(struct block_info_t));
  if (block_info == NULL)
    {
      printf("Allocation error, tried to allocate %li bytes:\n", lrintl(filesize*1.0l/512/sectors)* sizeof(struct block_info_t));
      err(1, "calloc");
    }

  // get memory alligned pointer (needed for O_DIRECT access)
  ibuf = malloc(sectors*512+pagesize);
  ibuf = ptr_align(ibuf, pagesize);

  fsync(dev_fd);

  if (!noflush)
    {
      // Attempt to free all cached pages related to the opened file
      if (posix_fadvise(dev_fd, 0, 0, POSIX_FADV_DONTNEED) < 0)
        err(1, NULL);
      if (posix_fadvise(dev_fd, 0, 0, POSIX_FADV_NOREUSE) < 0)
        err(1, NULL);
    }

  sumtime.tv_sec = 0;
  sumtime.tv_nsec = 0;
  sumsqtime.tv_sec = 0;
  sumsqtime.tv_nsec = 0;
 
  long long errors = 0,
    vvfast = 0,
    vfast = 0,
    fast = 0,
    normal = 0,
    slow = 0,
    vslow = 0,
    vvslow = 0;  
  long long read_s, write_s, read_e, write_e;
  clock_gettime(TIMER_TYPE, &times);
  while (1)
    {
      get_read_writes(dev_stat_path, &read_s, &write_s);
      clock_gettime(TIMER_TYPE, &time1);
      nread = read(dev_fd, ibuf, sectors*512);
      clock_gettime(TIMER_TYPE, &time2);
      get_read_writes(dev_stat_path, &read_e, &write_e);
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

      if(read_e-read_s-1 != 0 || write_e != write_s)
        {
          block_info[blocks].valid = 0;
        }
      else
        {
          block_info[blocks].valid = 1;
          block_info[blocks].sumtime = res;
          sqr_time(&res, res);
          block_info[blocks].sumsqtime = res;
          diff_time(&res, time1, time2);
          block_info[blocks].samples++;
        }

      if (res.tv_nsec < 2000000 && res.tv_sec == 0) // very very fast read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"_",1);
          ++vvfast;
        }
      else if (res.tv_nsec < 5000000 && res.tv_sec == 0) // very fast read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,".",1);
          ++vfast;
        }
      else if (res.tv_nsec < 10000000 && res.tv_sec == 0) // fast read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,",",1);
          ++fast;
        }
      else if (res.tv_nsec < 25000000 && res.tv_sec == 0) // normal read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"-",1);
          ++normal;
        }
      else if (res.tv_nsec < 50000000 && res.tv_sec == 0) // slow read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"+",1);
          ++slow;
        }
      else if (res.tv_nsec < 80000000 && res.tv_sec == 0) // very slow read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"#",1);
          ++vslow;
        }
      else // very very slow read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"!",1);
          ++vvslow;
          //fprintf(stderr, "%lli\n", blocks * sectors * 1ll);
        }
      if (sector_times == PRINT_TIMES)
        printf("%li\n",res.tv_nsec/1000+res.tv_sec*1000000); 
//      fsync(0);
      blocks++;
      sum_time(&sumtime, &res);
      sqr_time(&res, res);
      sum_time(&sumsqtime, &res);

      if (blocks % 1000 == 0 && verbosity >= 0)
        {
          clock_gettime(TIMER_TYPE, &timee);
          diff_time(&res, time1, time2);
          float cur_speed;
          cur_speed = sectors * 512 / 1024 * 1.0f / 1024 / (res.tv_sec * 1.0f + res.tv_nsec / 1000000000.0);
          diff_time(&res, times, timee);
          float speed;
          speed = blocks * sectors * 512 / 1024 * 1.0f / 1024 / (res.tv_sec * 1.0f + res.tv_nsec / 1000000000.0);
          float percent;
          if (max_sectors == 0)
            percent = (blocks * sectors * 512.0f) / (filesize * 1.0f);
          else
            percent = (blocks * sectors * 512.0f) / (max_sectors * sectors * 2.0f);
          long long time_to_go;
          time_to_go = (res.tv_sec*1.0) / percent;
          fprintf(stderr,"read %lli sectors, %.3fMiB/s (%.3fMiB/s), %.2f%%, in %02li:%02li:%02li, expected time: %02lli:%02lli:%02lli\n",
             blocks*sectors,
             cur_speed,
             speed,
             percent*100,
             res.tv_sec/3600, res.tv_sec/60%60, res.tv_sec%60,
             time_to_go/3600, time_to_go/60%60, time_to_go%60);
        }

      if (nread == 0 || (max_sectors != 0 && blocks * sectors >= max_sectors ))
        break;
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
  sqrt_time(&res, sumsqtime);
  div_time(&res, res, blocks);
  fprintf(stderr, "std dev: %li.%09li\n", res.tv_sec, res.tv_nsec);
  fprintf(stderr, "ERR: %lli\n2ms: %lli\n5ms: %lli\n10ms: %lli\n25ms: %lli\n"
      "50ms: %lli\n80ms: %lli\n80+ms: %lli\n",
      errors, vvfast, vfast, fast, normal, slow, vslow, vvslow);
  return 1;
}
