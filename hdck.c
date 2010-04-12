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

#define _XOPEN_SOURCE 500
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
#include <string.h>
#include "ioprio.h"
#define TIMER_TYPE CLOCK_REALTIME 

int
pagesize = 4096;
int
sectors = 256;
int
verbosity = 0;

/// information about a single block (256 sectors by default)
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
  long double temp=0.0;
  temp = val.tv_sec + (val.tv_nsec * 1.0L) / 1000000000.0L;
  temp = temp * temp;
  
  res->tv_sec = floorl(temp);
  temp = temp - floorl(temp);
  res->tv_nsec = floorl(temp * 1000000000LL);
/*  res->tv_sec = 1000000000 * val.tv_sec * val.tv_sec + 
                2 * val.tv_nsec * val.tv_sec + 
                ((val.tv_nsec * val.tv_nsec) / 1000000000);
  res->tv_nsec = ((val.tv_nsec * 1LL) * (val.tv_nsec * 1LL)) % 1000000000;*/
}

/** take square root out of time 
 */
void
sqrt_time(struct timespec *res, struct timespec val)
{
  res->tv_sec = floor(sqrt(val.tv_sec));
  res->tv_nsec = (sqrt(val.tv_sec) - res->tv_sec) * 1000000000LL + 
    floor(sqrt(val.tv_nsec));
  if (res->tv_nsec >= 1000000000LL)
    {
      res->tv_nsec -= 1000000000LL;
      res->tv_sec += 1;
    }
}

void
div_time(struct timespec *res, struct timespec divisor, long long divider)
{
  res->tv_sec = divisor.tv_sec / divider;
  res->tv_nsec = ((divisor.tv_sec % divider * 1.0l) / divider) * 1000000000LL +
    divisor.tv_nsec / divider;
}

void
sum_time(struct timespec *sum, struct timespec *adder)
{
  sum->tv_sec += adder->tv_sec;
  sum->tv_nsec += adder->tv_nsec;
  if (sum->tv_nsec >= 1000000000LL)
    {
      sum->tv_nsec -= 1000000000LL;
      sum->tv_sec += 1;
    }
  return;
}

void
times_time(struct timespec *res, long long multiplicator)
{
  // fields in timespec are time_t and long, so they are too small for large
  // multiplications
  long long tmp_s;
  long long tmp_ns;
  
  tmp_s = res->tv_sec * multiplicator;
  tmp_ns = res->tv_nsec * multiplicator;

  if (tmp_ns >= 1000000000LL)
    {
      tmp_s += tmp_ns / 1000000000LL;
      tmp_ns %= 1000000000LL;
//      res->tv_nsec -= 1000000000;
//      res->tv_sec += 1;
    }

  res->tv_sec = tmp_s; 
  res->tv_nsec = tmp_ns; 

  return;
}

void
make_real_time(void)
{
  struct sched_param sp;
  sp.sched_priority = 31;
  if (sched_setscheduler(0,SCHED_FIFO,&sp) < 0)
    err(1, "scheduler");
}

void
set_affinity(void)
{
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set); // zero the CPU set
  CPU_SET(0, &cpu_set); // add first cpu to the set
  if (sched_setaffinity(0,sizeof(cpu_set_t), &cpu_set) <0)
    err(1, "affinity");
}

void
set_rt_ioprio(void)
{
  if (ioprio_set(IOPRIO_WHO_PROCESS, 
               0, 
               IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 0)
      ) != 0)
  err(1, "ioprio: can't make process IO class real-time");
}


/// get file size
long long
get_file_size(int dev_fd)
{
  struct stat file_stat;
  off_t filesize;
  if (fstat(dev_fd, &file_stat) == -1)
    err(1, "fstat");
  if (S_ISREG(file_stat.st_mode))
    {
      filesize = file_stat.st_size;
      if (verbosity > 1)
        {
          printf("file size: %lli bytes\n", file_stat.st_size);
        }
    }
  else if (S_ISBLK(file_stat.st_mode))
    {
      if (ioctl(dev_fd, BLKGETSIZE64, &filesize) == -1)
        err(1, "ioctl: BLKGETSIZE64");
      if (verbosity > 1)
        printf("file size: %lli bytes\n", filesize);
    }
  else
    {
      printf("%s: %s: File is neither device file nor regular file", __FILE__, "main");
      exit(EXIT_FAILURE);
    }
  return filesize;
}


// readlink wrapper
char *
readlink_malloc (char *filename)
{
  int size = 100;

  while (1)
    {
      char *buffer = (char *) malloc (size);
      if (buffer == NULL)
        err(1, "malloc");

      int nchars = readlink (filename, buffer, size);
      if (nchars < size)
        {
          if (nchars < 0)
            err(1, "readlink");
          else
            {
              buffer[nchars]='\0';
              return buffer;
            }
        }
      free (buffer);
      size *= 2;
    }
}

char*
read_link(char* link)
{
  char* buf;
  ssize_t bufsize=1024;
  ssize_t ret;
  while(1)
    {
      buf = malloc(bufsize);
      if (buf == NULL)
        {
          err(1, "malloc");
        }
      ret = readlink(link, buf, bufsize);

      if (ret == -1)
        err(1, "readlink");

      if (ret < bufsize-1)
        {
          buf[ret]='\0';
          return buf;
        }
      else
        {
          free(buf);
          bufsize*=2;
        }
    }
}

char*
get_file_stat_sys_name(char* filename)
{
  struct stat file_stat;
  char* name;
  char* stat_sys_name;

  if(stat(filename, &file_stat) == -1)
    err(1, "stat");

  if (S_ISLNK(file_stat.st_mode))
    {
      filename = read_link(filename);
    }

  if(stat(filename, &file_stat) == -1)
    err(1, "stat");

  if (S_ISLNK(file_stat.st_mode))
    {
      err(1, "circular reference");
    }

  if (!S_ISBLK(file_stat.st_mode))
    {
      return NULL;
    }

  name = strrchr(filename, '/');
  name++; // omit the last '/'

  
  if (verbosity > 2)
    {
      printf("device name %s\n", name);
    }
  
  stat_sys_name = malloc(strlen(name) + 17);
  if (stat_sys_name == NULL)
    err(1, "malloc");

  stat_sys_name[0]='\0';
  strcat(stat_sys_name, "/sys/block/");
  strcat(stat_sys_name, name);
  strcat(stat_sys_name, "/stat");

  if (verbosity > 2)
    {
      printf("stat device to open %s\n", stat_sys_name);
    }

  // check if file exists
  if(stat(stat_sys_name, &file_stat) == -1)
    {
      if (errno == ENOENT)
        {
          free(stat_sys_name);
          return NULL;
        }
      else
        {
          err(1, "stat");
        }
    }

  return stat_sys_name;
}

int
get_read_writes(char* filepath, long long* reads, long long* read_sec, long long* writes)
{
  int fd;
  char buf[4096];
  int read_bytes;
  fd = open(filepath, O_RDONLY);
  if (fd < 0)
    {
      err(1, "open");
    }
  read_bytes = read(fd,buf, 4096);
  if (read_bytes < 0)
    err(1, "get_read_writes: read");
  buf[read_bytes] = '\0';
  // Field 1 -- # of reads issued
  // Field 2 -- # of reads merged 
  // Field 3 -- # of sectors read
  // Field 4 -- # of milliseconds spent reading
  // Field 5 -- # of writes completed
  // Field 6 -- # of writes merged
  // Field 7 -- # of sectors written
  // Field 8 -- # of milliseconds spent writing
  // Field 9 -- # of I/Os currently in progress
  // Field 10 -- # of milliseconds spent doing I/Os
  // Field 11 -- weighted # of milliseconds spent doing I/Os
  long long tmp=0;
  read_bytes = sscanf(buf, "%lli %lli %lli %lli %lli %lli* %lli* %lli* %lli* %lli* %lli*", reads, &tmp, read_sec, &tmp, &tmp, writes);
  close(fd);
  if (read_bytes != 2)
    return 1;
  return 0;
}

int
main(int argc, char **argv)
{
  int c;
  char* filename = NULL;
  int exclusive = 0; ///< use exclusive file access (O_EXCL)
  int nodirect = 0; ///< don't use O_DIRECT access
  int noaffinity = 0; ///< don't set CPU affinity
  int nortio = 0; ///< don't change process scheduling to RT
  char* dev_stat_path; ///< path to the `stat' file for the corresponding hardware device
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

  struct timespec time1, time2, /**< time it takes to read single block */
                  sumtime, /**< sum of all times */
                  res, /**< temp result */
                  times, timee, /**< wall clock start and end */
                  sumsqtime; /**< sum of squares (for std. deviation) */
  long long blocks = 0;
  long long abs_blocks = 0;

  int dev_fd = 0;
  char *ibuf;
  char *ibuf_free;
  off_t nread;
  off_t number_of_blocks;

  // make the process real-time
  make_real_time();

  // make the process run on single core
  if (!noaffinity)
    {
      set_affinity();
    }

  // make the process' IO prio highest 
  if (!nortio)
    {
      set_rt_ioprio();
    }
 
  int flags = O_RDONLY | O_LARGEFILE; 
  if (verbosity > 5)
    printf("setting O_RDONLY flag on file\n");
  if (verbosity > 5)
    printf("setting O_LARGEFILE flag on file\n");

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

  // no sync on file
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

  // open in exclusive mode
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
      err(1, "open");
    }

  filesize = get_file_size(dev_fd);

  dev_stat_path = get_file_stat_sys_name(filename);

  fesetround(2); // round UP
  number_of_blocks = lrintl(filesize*1.0l/512/sectors);
  block_info = calloc(number_of_blocks, 
      sizeof(struct block_info_t));
  if (block_info == NULL)
    {
      printf("Allocation error, tried to allocate %lli bytes:\n", 
          number_of_blocks* sizeof(struct block_info_t));
      err(1, "calloc");
    }

  // get memory alligned pointer (needed for O_DIRECT access)
  ibuf = malloc(sectors*512+pagesize);
  ibuf_free = ibuf;
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
  long long read_s=0, write_s=0, read_e=1, write_e=0, read_sec_s=0, read_sec_e=0;
  int next_is_valid=1;
  int loop=0, loops=3;

  // position the disk head
  lseek(dev_fd, (off_t)0, SEEK_SET);
  read(dev_fd, ibuf, pagesize);
  lseek(dev_fd, (off_t)0, SEEK_SET);

  clock_gettime(TIMER_TYPE, &time2);
  clock_gettime(TIMER_TYPE, &times);
  if (dev_stat_path != NULL)
    get_read_writes(dev_stat_path, &read_e, &read_sec_e, &write_e);
  while (1)
    {
      read_s = read_e;
      write_s = write_e;
      read_sec_s = read_sec_e;
      //if (dev_stat_path != NULL)
      //  get_read_writes(dev_stat_path, &read_s, &read_sec_s, &write_s);
      time1.tv_sec=time2.tv_sec;
      time1.tv_nsec=time2.tv_nsec;

      //clock_gettime(TIMER_TYPE, &time1);
      nread = read(dev_fd, ibuf, sectors*512);
      clock_gettime(TIMER_TYPE, &time2);

      if (dev_stat_path != NULL)
        get_read_writes(dev_stat_path, &read_e, &read_sec_e, &write_e);

      if (nread < 0) // on error
        {
          // TODO save that an error occured when reading the block
          if (errno != EIO)
            err(1, NULL);
          else
            {
              diff_time(&res, time1, time2); 
              nread = 1; // don't exit loop
              write(0, "E", 1);
              ++errors;

              // omit block
              if (lseek(dev_fd, (off_t)512*sectors, SEEK_CUR) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
            }
        }
      // when the read was incomplete or interrupted
      else if (nread != sectors*512 || ( read_e-read_s != 1 && nodirect == 0) || (read_e-read_s > 4 && nodirect == 1) || write_e != write_s)
        {
          diff_time(&res, time1, time2);
          if (block_info[blocks].valid == 0)
            {
              block_info[blocks].valid = 0;
              sum_time(&(block_info[blocks].sumtime), &res);
              sqr_time(&res, res);
              sum_time(&(block_info[blocks].sumsqtime), &res);
              block_info[blocks].samples ++;
            }
          block_info[blocks].valid = 0;
          diff_time(&res, time1, time2);
          if (nread != sectors*512)
            {
              // seek to start of next block
              if (lseek(dev_fd, (off_t)512*sectors-nread, SEEK_CUR) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
            }
          // invalidate next read block (to ignore seeking)
          next_is_valid = 0;

          // TODO: should invalidate the previous block too as the interrupting
          // read/write could finish after our read
        }
      else // when the read was correct
        {
          diff_time(&res, time1, time2);
          //make the times stored in block struct in ms not in ns
          times_time(&res, 1000); 
          // update only if we can gather meaningful data
          if (block_info[blocks].valid == 0 || 
              (block_info[blocks].valid == 1 && next_is_valid == 1))
            {
              if (block_info[blocks].valid == 0 && next_is_valid == 1)
                {
                  // first valid read
                  block_info[blocks].samples = 1;
                  block_info[blocks].sumtime.tv_sec = res.tv_sec;
                  block_info[blocks].sumtime.tv_nsec = res.tv_nsec;
                  sqr_time(&res, res);
                  block_info[blocks].sumsqtime.tv_sec = res.tv_sec;
                  block_info[blocks].sumsqtime.tv_nsec = res.tv_nsec;
                  if (verbosity > 8)
                    fprintf(stderr, "block: %lli, samples: %i, sumtime: %li.%09li, sumsqtime: %li.%09li\n", 
                        blocks, 
                        block_info[blocks].samples,
                        block_info[blocks].sumtime.tv_sec,
                        block_info[blocks].sumtime.tv_nsec,
                        block_info[blocks].sumsqtime.tv_sec,
                        block_info[blocks].sumsqtime.tv_nsec);
                }
              else
                {
                  // subsequent valid reads
                  block_info[blocks].samples += 1;
                  sum_time(&(block_info[blocks].sumtime), &res);
                  sqr_time(&res, res);
                  sum_time(&(block_info[blocks].sumsqtime), &res);
                  if (verbosity > 8)
                    fprintf(stderr, "block: %lli, samples: %i, sumtime: %li.%09li, sumsqtime: %li.%09li\n", 
                        blocks, 
                        block_info[blocks].samples,
                        block_info[blocks].sumtime.tv_sec,
                        block_info[blocks].sumtime.tv_nsec,
                        block_info[blocks].sumsqtime.tv_sec,
                        block_info[blocks].sumsqtime.tv_nsec);
                }
              
              diff_time(&res, time1, time2);

              block_info[blocks].valid = next_is_valid;
            }
            
          next_is_valid = 1;
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
        printf("%li r:%lli rs: %lli w:%lli\n",res.tv_nsec/1000+res.tv_sec*1000000, read_s, read_sec_s, write_s); 
//      fsync(0);
      blocks++;
      abs_blocks++;
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
          speed = abs_blocks * sectors * 512 / 1024 * 1.0f / 1024 / (res.tv_sec * 1.0f + res.tv_nsec / 1000000000.0);
          float percent;
          if (max_sectors == 0)
            percent = (blocks * sectors * 512.0f) / (filesize * 1.0f);
          else
            percent = (blocks * sectors * 512.0f) / (max_sectors * sectors * 2.0f);
          long long time_to_go;
          time_to_go = (res.tv_sec*1.0) / percent;
          fprintf(stderr,"read %lli sectors, %.3fMiB/s (%.3fMiB/s), %.2f%%, "
              "in %02li:%02li:%02li, expected time: %02lli:%02lli:%02lli\n",
             blocks*sectors,
             cur_speed,
             speed,
             percent*100,
             res.tv_sec/3600, res.tv_sec/60%60, res.tv_sec%60,
             time_to_go/3600, time_to_go/60%60, time_to_go%60);
        }

      if (nread == 0 || (max_sectors != 0 && blocks * sectors >= max_sectors ))
        {
          if (loop < loops)
            {
              loop++;
              blocks=0;
              if (lseek(dev_fd, (off_t)0, SEEK_SET) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
              // TODO: flush system buffers
            }
          else
            break;
        }
    }
  for(long long i=0; i<blocks; i++)
    {
      sqrt_time(&res, block_info[i].sumsqtime);
      div_time(&res, res, block_info[i].samples);
      struct timespec avg;
      div_time(&avg, block_info[i].sumtime, block_info[i].samples);
      if (res.tv_sec > 1)
        {
          fprintf(stderr, "high std dev for block %lli:%3li.%09li  sumsqtime:%5li.%09li, average: %li.%09li\n", 
              i, 
              res.tv_sec, 
              res.tv_nsec,
              block_info[i].sumsqtime.tv_sec, 
              block_info[i].sumsqtime.tv_nsec,
              avg.tv_sec,
              avg.tv_nsec);
        }
    }
  // TODO: check statistical deviation of sectors
  // TODO: re-read sectors with high deviation
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
  fprintf(stderr, "std dev: %li.%09li(s)\n", res.tv_sec, res.tv_nsec);
  fprintf(stderr, "ERR: %lli\n2ms: %lli\n5ms: %lli\n10ms: %lli\n25ms: %lli\n"
      "50ms: %lli\n80ms: %lli\n80+ms: %lli\n",
      errors, vvfast, vfast, fast, normal, slow, vslow, vvslow);
  long long sum_invalid=0;
  for (int i=0; i< blocks; i++)
    {
      if (block_info[i].valid == 0)
        sum_invalid++;
    }
  fprintf(stderr, "Number of invalid measures because of interrupted reads: %lli\n", sum_invalid);

  free(dev_stat_path);
  free(block_info);
  free(ibuf_free);
  return 0;
}
