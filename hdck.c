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
#include <assert.h>
#include <stdint.h>
#include "ioprio.h"
#define TIMER_TYPE CLOCK_REALTIME 
#ifdef __GNUC__
#define PURE_FUNCTION  __attribute__ ((pure))
#else
#define PURE FUNCTION
#endif

int pagesize = 4096;
int sectors = 256; ///< number of sectors read per sample
int verbosity = 0;

int exclusive = 0; ///< use exclusive file access (O_EXCL)
int nodirect = 0; ///< don't use O_DIRECT access
int noaffinity = 0; ///< don't set CPU affinity
int nortio = 0; ///< don't change process scheduling to RT
int write_individual_times_to_file = 1; ///< TODO - cmd option
int bad_sector_warning = 1; ///< whatever the user was warned about badsectors

/// minimal number of reads for a sector to be qualified as valid
long long min_reads = 0; 
/// maximal number of re-read tries before the algorith gives up
long long max_reads = 0; 
/// maximal standard deviation accepted for a block
double max_std_dev = 0;

size_t disk_cache_size = 32;

/// information about a single block (256 sectors by default)
struct block_info_t {
    double* samples; ///< measurements for the block
    size_t samples_len; ///< number of samples taken
    short int valid; ///< 0 if data is invalid (because read was interrupted)
    unsigned short int error; ///< number of IO errors that occured while reading
                              /// the block
};

/// list of sectors to read
struct block_list_t {
    off_t off;
    off_t len;
};

enum {
    PRINT_TIMES = 1,
    PRINT_SYMBOLS
};

/** Print usage information
 */
void
usage()
{
  printf("Usage: hdck [OPTIONS]\n");
  printf("Test hard drive for latent and hidden bad sectors\n");
  printf("\n");
  printf("-f, --file FILE     device file to test\n");
  printf("-x, --exclusive     use exclusive access\n");
  printf("                    (runs faster, but all partitions must be"
                                                              " unmounted)\n");
  printf("-b, --background    shorthand for --noaffinity, --nortio, --nort\n");
  printf("-o, --outfile FILE  output file for the detailed statistics \n");
  printf("-w, --bad-sectors FILE output file for the uncertain sectors\n");
  printf("-r, --read-sectors FILE list of ranges to scan instead of whole disk\n");
  printf("--nodirect          don't use O_DIRECT\n");
  printf("--noflush           don't flush system buffers before reading\n");
  printf("--nosync            don't use O_SYNC\n");
  printf("--noaffinity        don't set CPU affinity to 0th core/CPU\n");
  printf("--nortio            don't change IO priority to real-time\n");
  printf("--nort              don't make the process real-time\n");
  printf("--sector-symbols    print symbols representing read time of each"
                                                       " group of sectors\n");
  printf("--sector-times      print time it takes to read each group of"
                                                        " sectors (in µs)\n");
  printf("--min-reads NUM     minimal number of valid reads for a sector\n");
  printf("--max-reads NUM     maximal number of re-reads for a sector\n");
  printf("--max-std-deviation NUM minimal relative stdandard deviation for "
      "a sector to be\n");
  printf("                    considered valid\n");
  printf("--max-sectors NUM   read at most NUM sectors\n");
  printf("--disk-cache NUM    size of the on-board disk cache (32MiB default)\n");
  printf("--disk-rpm NUM      disk RPM (7200 by default)\n");
  printf("--noverbose         reduce verbosity\n");
  printf("-v, --verbose       be more verbose\n");
  printf("--version           write version information\n");
  printf("-h, -?              print this message\n");
  printf("\n");
  printf("This program can be run on both files and devices, though running it on"
      " top of\n");
  printf("a file is quite pointless. In most cases default settings should be OK."
      " Things to\n");
  printf("check are --nodirect and --noflush.\n");
  printf("When using -x, the program trusts the sector times more\n");
  printf("and as a result, should achive minimum confidence in less time "
      "(by not using re-\n");
  printf("-reads, much)\n");
  printf("\n");
  printf("Default settings:\n");
  printf("min-reads: 3, max-reads: 10, max-std-deviation: 0.5\n");
  printf("Exclusive settings:\n");
  printf("min-reads: 1, max-reads: 5, max-std-deviation: 0.75\n");
  printf("Background settings:\n");
  printf("min-reads: 3, max-reads: 20, max-std-deviation: 0.5\n");
  printf("\n");
  printf("Format for the -o option is presented in the first line of file, block "
      "is a group\n");
  printf("of %zi sectors (%zi bytes). Consecutive lines in files for -r and -w"
      " are ranges\n", sectors, sectors * 512);
  printf("of LBAs to scan to.\n");
}

void
version(void)
{
  printf("hdck 0.2.1rc1\n");
  printf("License GPLv3+: GNU GPL version 3 or later "
      "<http://gnu.org/licenses/gpl.html>.\n");
  printf("This is free software: you are free to change and redistribute it.\n");
  printf("There is NO WARRANTY, to the extent permitted by law.\n");
  printf("\n");
  printf("Written by Hubert Kario\n");
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

/**
 * convert struct timespec to double
 */
double
time_double(struct timespec ts) PURE_FUNCTION;

double
time_double(struct timespec ts)
{
  return ts.tv_sec + ts.tv_nsec * 1.0 / 1E9;
}

/**
 * reset the block_info struct
 * does not reset number of block errors!
 */
void
bi_clear(struct block_info_t* block_info)
{
  if (block_info->samples)
    free(block_info->samples);

  block_info->samples = NULL;
  block_info->samples_len = 0;
  block_info->valid = 0;
}

/** initalises the block_info_t struct
 */
void
bi_init(struct block_info_t* block_info)
{
  block_info->samples = NULL;
  block_info->samples_len = 0;
  block_info->valid = 0;
  block_info->error = 0;
}

/**
 */
PURE_FUNCTION
int
bi_is_initialised(struct block_info_t* block_info)
{
  if (block_info->samples != NULL ||
      block_info->samples_len != 0 ||
      block_info->valid != 0 ||
      block_info->error != 0)
    return 1;
  else
    return 0;
}

/**
 * add time (in ms) to samples inside block_info
 */
void
bi_add_time(struct block_info_t* block_info, double time)
{
  if (block_info->samples_len == 0)
    {
      block_info->samples = malloc(sizeof(double));
      if (!block_info->samples)
        err(1, "bi_add_time");

      block_info->samples[0] = time;
      block_info->samples_len = 1;
    }
  else
    {
      block_info->samples_len++;
      block_info->samples = realloc(block_info->samples, 
          sizeof(double) * block_info->samples_len);

      if (!block_info->samples)
        err(1, "bi_add_time");

      block_info->samples[block_info->samples_len-1] = time;
    }
}

/** add anouther block_info 
 */
void
bi_add(struct block_info_t* sum, struct block_info_t* adder)
{
  if (adder->samples_len == 0)
    return;

  if (sum->samples_len == 0)
    {
      sum->samples = malloc(sizeof(double)*adder->samples_len);
      if (!sum->samples)
        err(1, "bi_add");

      memcpy(sum->samples, adder->samples, sizeof(double) * adder->samples_len);

      sum->samples_len = adder->samples_len;
    }
  else
    {
      sum->samples = realloc(sum->samples, sizeof(double)*(
            sum->samples_len + adder->samples_len));

      if (!sum->samples)
        err(1, "bi_add");

      for(int i=0; i< adder->samples_len; i++)
        sum->samples[sum->samples_len + i] = adder->samples[i];

      sum->samples_len += adder->samples_len;
    }

  sum->error += adder->error;

  return;
}

/** add one block_info to another, only if their state matches
 */
void
bi_add_valid(struct block_info_t* sum, struct block_info_t* adder)
{
  if ((sum->valid == 1 && adder->valid == 1) || 
      ((sum->valid != 1) && (adder->valid != 1)))
    {
      bi_add(sum, adder);
    }
  else if (sum->valid == 1 && adder->valid != 1)
    {
      sum->error += adder->error;
    }
  else // (sum->valid != 1 && adder->valid == 1)
    {
      bi_clear(sum);
      bi_add(sum, adder);
      sum->valid = 1;
    }
}

/** removes last sample
 */
void
bi_remove_last(struct block_info_t* block_info)
{
  if (block_info->samples_len > 1)
    block_info->samples_len --;
  else
    {
      free(block_info->samples);
      block_info->samples = NULL;
      block_info->samples_len = 0;
    }
}


/** check if block_info is valid
 */
PURE_FUNCTION
int
bi_is_valid(struct block_info_t* block_info)
{
  return block_info->valid;
}

/** set block info to be valid
 */
void
bi_make_valid(struct block_info_t* block_info)
{
  block_info->valid = 1;
}

/** set block info to be invalid
 */
void
bi_make_invalid(struct block_info_t* block_info)
{
  block_info->valid = 0;
}

/** returns individual sample times
 */
PURE_FUNCTION
double*
bi_get_times(struct block_info_t* block_info)
{
  return block_info->samples;
}

/** return standard deviation for samples
 */
PURE_FUNCTION
double
bi_stdev(struct block_info_t* block_info)
{
  int n = 0;
  long double mean = 0.0;
  long double M2 = 0.0;
  long double delta;

  for (size_t i=0; i < block_info->samples_len; i++)
    {
      n++;
      delta = block_info->samples[i] - mean;
      mean += delta/n;
      M2 += delta * (block_info->samples[i] - mean); 
    }
  
  return sqrt(M2 / n);
}

/**
 * find longest read in samples
 */
PURE_FUNCTION
double
bi_max(struct block_info_t* block_info)
{
  double ret;

  if (block_info->samples_len == 0)
    return 0.0;

  ret = block_info->samples[0];

  for (size_t i=1; i<block_info->samples_len; i++)
    if (ret < block_info->samples[i])
      ret = block_info->samples[i];

  return ret;
}

/** return relative standard deviation for samples (stdev/mean)
 */
PURE_FUNCTION
double
bi_rel_stdev(struct block_info_t* block_info)
{
  int n = 0;
  long double mean = 0.0;
  long double M2 = 0.0;
  long double delta;
  long double sum = 0.0;

  for (int i=0; i < block_info->samples_len; i++)
    {
      n++;
      delta = block_info->samples[i] - mean;
      mean += delta/n;
      M2 += delta * (block_info->samples[i] - mean);
      sum += block_info->samples[i];
    }
  
  return (sqrt(M2 / n)) / ( sum / n);
}

/** return arithemtic average for samples
 */
PURE_FUNCTION
double
bi_average(struct block_info_t* block_info)
{
  long double sum = 0.0;
  int i;

  for( i=0; i<block_info->samples_len; i++)
    sum += block_info->samples[i];

  return sum / i;
}

/** return sum of samples
 */
PURE_FUNCTION
double
bi_sum(struct block_info_t* block_info)
{
  long double sum = 0.0;

  for (int i=0; i< block_info->samples_len; i++)
    sum += block_info->samples[i];

  return sum;
}

/** return number of collected samples
 */
PURE_FUNCTION
int
bi_num_samples(struct block_info_t* block_info)
{
  return block_info->samples_len;
}

/**
 * internal function to pass to qsort
 */
static int
__double_sort(const void* a, const void* b)
{
  double x, y;
  x = *(double *)a;
  y = *(double *)b;

  if (x < y)
    return -1;
  else if (x == y)
    return 0;
  else
    return 1;
}

/** return truncated average for samples
 * @parm percent how much data is to be thrown off
 */
PURE_FUNCTION
double
bi_trunc_average(struct block_info_t* block_info, double percent)
{
  assert(percent >= 0 || percent <= 1);

  double* tmp;

  tmp = malloc(sizeof(double) * block_info->samples_len);
  if (!tmp)
    err(1, "bi_trunc_average");

  memcpy(tmp, block_info->samples, block_info->samples_len * sizeof(double));

  qsort(tmp, block_info->samples_len, sizeof(double), __double_sort);

  int low, high;

  low = ceil(percent / 2 * block_info->samples_len);
  high = floor(block_info->samples_len - percent / 2 * block_info->samples_len);

  if (high == low)
    {
      free(tmp);
      return 0;
    }

  int i;
  long double sum = 0.0;

  for (i = low; i < high; i++)
    {
      sum += tmp[i];
    }

  free (tmp);

  return sum / (high - low);
}

/** return inteligent mean for samples
 */
PURE_FUNCTION
double bi_int_average(struct block_info_t* block_info)
{
  if (block_info->samples_len < 5)
    {
      return bi_average(block_info);
    }
  else
    {
      return bi_trunc_average(block_info, 0.25);
    }
}

/** return truncated standard deviation for samples
 * @param percent how much data is to be thrown off
 */
PURE_FUNCTION
double
bi_trunc_stdev(struct block_info_t* block_info, double percent)
{
  assert(percent >= 0 || percent <= 1);

  double* tmp;

  tmp = malloc(sizeof(double) * block_info->samples_len);
  if (!tmp)
    err(1, "bi_trunc_average");

  memcpy(tmp, block_info->samples, block_info->samples_len * sizeof(double));

  qsort(tmp, block_info->samples_len, sizeof(double), __double_sort);

  int low, high;

  low = ceil(percent / 2 * block_info->samples_len);
  high = floor(block_info->samples_len - percent / 2 * block_info->samples_len);

  if (high == low)
    {
      free(tmp);
      return 0;
    }

  int n = 0;
  long double mean = 0.0;
  long double M2 = 0.0;
  long double delta;

  for (int i=low; i < high; i++)
    {
      n++;
      delta = tmp[i] - mean;
      mean += delta/n;
      M2 += delta * (tmp[i] - mean); 
    }

  free(tmp);
  
  return sqrt(M2 / n);
}

/** return truncated relative standard deviation (stdev/mean) for samples 
 * @param percent how much data is to be thrown off
 */
PURE_FUNCTION
double
bi_trunc_rel_stdev(struct block_info_t* block_info, double percent)
{
  assert(percent >= 0 || percent <= 1);

  double* tmp;

  tmp = malloc(sizeof(double) * block_info->samples_len);
  if (!tmp)
    err(1, "bi_trunc_average");

  memcpy(tmp, block_info->samples, block_info->samples_len * sizeof(double));

  qsort(tmp, block_info->samples_len, sizeof(double), __double_sort);

  int low, high;

  low = ceil(percent / 2 * block_info->samples_len);
  high = floor(block_info->samples_len - percent / 2 * block_info->samples_len);

  if (high == low)
    {
      free(tmp);
      return 0;
    }

  int n = 0;
  long double mean = 0.0;
  long double M2 = 0.0;
  long double delta;
  long double sum = 0.0;

  for (int i=low; i < high; i++)
    {
      n++;
      delta = tmp[i] - mean;
      mean += delta/n;
      M2 += delta * (tmp[i] - mean); 
      sum += tmp[i];
    }

  free(tmp);
  
  return (sqrt(M2 / n)) / (sum / n);
}

/** "inteligent" truncated rel_st_dev
 */
PURE_FUNCTION
double
bi_int_rel_stdev(struct block_info_t* block_info)
{
  if(block_info->samples_len < 5)
    return bi_rel_stdev(block_info);

  return bi_trunc_rel_stdev(block_info, 0.25);
}

/** 
 * set that an error occured while reading the block
 */
void
bi_add_error(struct block_info_t* block_info)
{
  block_info->error++;
}

/** get number of errors that occured while reading the block
 */
PURE_FUNCTION
int
bi_get_error(struct block_info_t* block_info)
{
  return block_info->error;
}

PURE_FUNCTION
int 
bitcount(unsigned short int n) 
{
#ifdef __GNUC__
  return __builtin_popcount(n);
#else
  int count = 0;
  while (n) 
    {
      count += n & 0x1u;
      n >>= 1;
    }
  return count;
#endif
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
      printf("%s: %s: File is neither device file nor regular file", 
          __FILE__, "main");
      exit(EXIT_FAILURE);
    }
  return filesize;
}


// readlink wrapper
char *
readlink_malloc(char *filename)
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

  const char sys_path[] = "/sys/block/";
  const char sys_stat[] = "/stat";

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
  
  stat_sys_name = malloc(strlen(name) + strlen(sys_path) + strlen(sys_stat) + 1);
  if (stat_sys_name == NULL)
    err(1, "malloc");

  stat_sys_name[0]='\0';
  strcat(stat_sys_name, sys_path);
  strcat(stat_sys_name, name);
  strcat(stat_sys_name, sys_stat);

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
get_read_writes(char* filepath, 
                long long* reads, 
                long long* read_sec, 
                long long* writes)
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
  read_bytes = sscanf(buf, 
                  "%lli %lli %lli %lli %lli %lli* %lli* %lli* %lli* %lli* %lli*",
                  reads, 
                  &tmp, 
                  read_sec,
                  &tmp, 
                  &tmp, 
                  writes,
                  &tmp,
                  &tmp,
                  &tmp,
                  &tmp,
                  &tmp);
  close(fd);
  if (read_bytes != 2)
    return 1;
  return 0;
}

/**
 * reads only the blocks between offset and offset+len
 */
struct block_info_t*
read_blocks(int fd, char* stat_path, off_t offset, off_t len)
{
  struct timespec time_start; ///< start of read
  struct timespec time_end; ///< end of read
  struct timespec res; ///< temporary result
  long long read_start = 0, read_sectors_s = 0, write_start = 0,
            read_end = 0, read_sectors_e = 0, write_end = 0;
  struct block_info_t* block_info;
  int bad_sectors = 0;
  char* buffer;
  char* buffer_free;
  off_t nread;
  off_t no_blocks = 0;

  assert(len>0);

  block_info = calloc(sizeof(struct block_info_t), len);
  if (block_info == NULL)
    err(1, "read_blocks1: len=%lli", len);

  buffer = malloc(sectors*512+pagesize);
  if (buffer == NULL)
    err(1, "read_blocks2");
  buffer_free = buffer;
  buffer = ptr_align(buffer, pagesize);

  if (stat_path != NULL)
    get_read_writes(stat_path, &read_start, &read_sectors_s, &write_start);

  off_t disk_cache = disk_cache_size * 1024 * 1024 /sectors / 512;

  // read additional blocks before the main data to empty disk cache
  if(lseek(fd, ((offset-disk_cache-1)*sectors*512>=0)?
                (offset-disk_cache-1)*sectors*512:
                0, SEEK_SET) < 0)
    goto interrupted;

  for (int i=0; i < disk_cache; i++)
    {
      nread = read(fd, buffer, sectors*512);

      if (nread < 0)
        {
          fprintf(stderr, "E");
          bad_sectors = 1;
          
          if (bad_sector_warning)
            {
              fprintf(stderr, "BAD SECTORS reads may not be accurate!\n");
              bad_sector_warning = 0;
            }

          // omit block
          if (lseek(fd, (off_t)sectors*512, SEEK_CUR) < 0)
            goto interrupted;
        }
      else if (nread != sectors*512)
        goto interrupted;
    }

  // read additional block before the main data to exclude seek time
  if (offset-disk_cache-1 < 0)
    if ( lseek(fd, (offset-1>=0)?(offset-1)*sectors*512:0, SEEK_SET) < 0)
      goto interrupted;

  nread = read(fd, buffer, sectors*512);
  if (nread < 0)
    {
      fprintf(stderr, "E");
      bad_sectors = 1;

      if (bad_sector_warning)
        {
          fprintf(stderr, "BAD SECTORS reds may not be accurate!\n");
          bad_sector_warning = 0;
        }

      // omit block
      if (lseek(fd, sectors*512, SEEK_CUR) < 0)
        goto interrupted;
    }
  else if (nread != sectors*512)
    goto interrupted;

  if (offset == 0)
    if( lseek(fd, (off_t) 0, SEEK_SET) < 0)
      goto interrupted;

  // check if current position is correct (assert)
  if ( lseek(fd, (off_t)0, SEEK_CUR) != offset * sectors * 512)
    {
      fprintf(stderr, "hdck: read_blocks: wrong offset: got %lli expected %lli\n",
          lseek(fd, (off_t)0, SEEK_CUR),
          offset * sectors * 512);
      exit(1);
    }

  // start reading main block
  clock_gettime(TIMER_TYPE, &time_end);

  while (no_blocks < len)
    {
      time_start.tv_sec = time_end.tv_sec;
      time_start.tv_nsec = time_end.tv_nsec;

      nread = read(fd, buffer, sectors*512);

      clock_gettime(TIMER_TYPE, &time_end);

      if (nread < 0)
        {
          if (errno != EIO)
            err(1, "read_blocks3");

          write(2, "E", 1);

          bi_add_error(&block_info[no_blocks]);
          bad_sectors = 1;

          if (bad_sector_warning)
            {
              fprintf(stderr, "BAD SECTORS reads may not be accurate!\n");
              bad_sector_warning = 0;
            }

          // omit block
          if (lseek(fd, 
                    (off_t)512*sectors*(no_blocks+1 + offset), 
                    SEEK_SET) < 0)
            goto interrupted;

          no_blocks++;

        }
      else if (nread != sectors*512)
        {
          bad_sectors = 1;
          if (lseek(fd, (off_t)512*sectors*(no_blocks+1 + offset),
                    SEEK_SET) < 0)
            goto interrupted;
          no_blocks++;
        }
      else
        {
          bi_make_valid(&block_info[no_blocks]);

          diff_time(&res, time_start, time_end);
          times_time(&res, 1000); // block_info is in ms, not ns

          bi_add_time(&block_info[no_blocks], time_double(res));

          no_blocks++;
        }
    }

  // read additional two blocks to exclude the probability that there were 
  // unfinished reads or writes in the mean time while the main was run
  nread = read(fd, buffer, sectors*512);
  nread = read(fd, buffer, sectors*512);

  if (stat_path != NULL)
    get_read_writes(stat_path, &read_end, &read_sectors_e, &write_end);

  if (((read_end-read_start != disk_cache + 1 + 2 + len && 
        nodirect == 0 && 
        stat_path != NULL
      )
      || 
      (read_end-read_start > 4 * (disk_cache + 1 + 2 + len) && 
        nodirect == 1 && 
        stat_path != NULL
      ))
      && bad_sectors == 0
     )
    goto interrupted;

  if (bad_sectors)
    for(size_t i=0; i < len; i++)
      bi_make_invalid(&block_info[i]);

  free(buffer_free);
  return(block_info);

interrupted:
  free(buffer_free);
  for(int i=0; i < len; i++)
    bi_clear(&block_info[i]);
  free(block_info);
  return NULL;
}

/**
 * @param block_list list to compact
 * @param glob globbing parameter -- how far can blocks be to be bound together
 */
struct block_list_t*
compact_block_list(struct block_list_t* block_list, int glob)
{
  struct block_list_t* ret = NULL;
  size_t block_list_len = 0;
  size_t ret_len = 0;

  while (!(block_list[block_list_len].off == 0 &&
           block_list[block_list_len].len == 0))
    block_list_len++;

  ret = calloc(sizeof(struct block_list_t), block_list_len+1);
  if (ret == NULL)
    err(1, "compact_block_list");

  // compact the list
  ret[0].off = block_list[0].off;
  ret[0].len = block_list[0].len;
  ret_len = 1;
  for (size_t i=1; i<block_list_len; i++)
    {
      // check if the block isn't contained in the previous one
      if (block_list[i].off < ret[ret_len-1].off + ret[ret_len-1].len)
        continue;

      // check if we can extend the last block to contain current one
      if (block_list[i].off <= ret[ret_len-1].off + ret[ret_len-1].len + glob
         && ret[ret_len-1].len < glob)
        {
          ret[ret_len-1].len += block_list[i].off 
                                - (ret[ret_len-1].off + ret[ret_len-1].len)
                                + 1;

//          if (ret[ret_len-1].len + ret[ret_len-1].off >= block_list_len)
//            ret[ret_len-1].len = block_list_len - ret[ret_len-1].off;
          continue;
        }

      ret[ret_len].off = block_list[i].off;
      ret[ret_len].len = block_list[i].len;
      ret_len++;
    }

  return ret;
}

/**
 * @param block_info block statistics
 * @param block_info_len block_info length
 * @param min_std_dev minimal standard deviation for a block to be cosidered 
 * uncertain
 * @param min_reads minimal number of reads for a block to be considered
 * checked
 * @param glob how close sectors have to be near them to be bundled together
 * @param offset minimal address to consider
 * @param delay read delay for the block to be considered uncertain 
 * @return null if there were no blocks meeting the criteria, null terimated list
 * otherwise
 */
struct block_list_t*
find_uncertain_blocks(struct block_info_t* block_info, size_t block_info_len,
    float min_std_dev, int min_reads, int glob, off_t offset, double delay,
    int soft_delay)
{
  struct block_list_t* block_list;
  struct block_list_t* ret;
  size_t uncertain = 0;

  if (offset > block_info_len || offset < 0)
    return NULL;

  block_list = calloc(sizeof(struct block_list_t), block_info_len);
  if (block_list == NULL)
    err(1, "find_uncertain_blocks");

  // find uncertain blocks
  for (size_t block_no=offset; block_no < block_info_len; block_no++)
    {
      if (!bi_is_initialised(&block_info[block_no]))
        continue;

      if (bi_num_samples(&block_info[block_no]) < min_reads ||
          !bi_is_valid(&block_info[block_no]))
        {
          block_list[uncertain].off = block_no;
          block_list[uncertain].len = 1;
          uncertain++;
          continue;
        }

      // do not re-read blocks with high std dev if all reads are much
      // lower than rotational delay
      if (soft_delay &&
          (
           (
            bi_num_samples(&block_info[block_no]) > 2 &&
            bi_max(&block_info[block_no]) < delay / 2
           )
           ||
           (
            bi_num_samples(&block_info[block_no]) > 1 &&
            bi_max(&block_info[block_no]) < delay  &&
            bi_int_rel_stdev(&block_info[block_no]) < min_std_dev * 2
           )
          )
         )
        {
          continue;
        }

      if (!soft_delay &&
          bi_int_average(&block_info[block_no]) > delay)
        {
          block_list[uncertain].off = block_no;
          block_list[uncertain].len = 1;
          uncertain++;
          continue;
        }

      if (bi_num_samples(&block_info[block_no]) == 1)
        {
          double sum;
          if (block_no < 4)
            {
              sum = bi_average(&block_info[block_no+1]) +
                bi_average(&block_info[block_no+2]) +
                bi_average(&block_info[block_no+3]) +
                bi_average(&block_info[block_no+4]) +
                bi_average(&block_info[block_no+5]);
            }
          else if (block_no > block_info_len - 3)
            {
              sum = bi_average(&block_info[block_no-1]) +
                bi_average(&block_info[block_no-2]) +
                bi_average(&block_info[block_no-3]) +
                bi_average(&block_info[block_no-4]) +
                bi_average(&block_info[block_no-5]);
            }
          else
            {
              sum = bi_average(&block_info[block_no-1]) +
                bi_average(&block_info[block_no-2]) +
                bi_average(&block_info[block_no-3]) +
                bi_average(&block_info[block_no+1]) +
                bi_average(&block_info[block_no+2]);
            }

          if (sum/5.0*4 < bi_average(&block_info[block_no]) ||
              bi_average(&block_info[block_no]) > delay)
            {
              block_list[uncertain].off = block_no;
              block_list[uncertain].len = 1;
              uncertain++;
              continue;
            }
        }

      if (bi_int_rel_stdev(&block_info[block_no]) > min_std_dev ||
          bi_int_average(&block_info[block_no]) > delay)
        {
          block_list[uncertain].off = block_no;
          block_list[uncertain].len = 1;
          uncertain++;
          continue;
        }
    }

  if (uncertain == 0)
    {
      free(block_list);
      return NULL;
    }

  if (glob == 1)
    return block_list;

  ret = compact_block_list(block_list, glob);

  free(block_list);

  return ret;
}

void
write_to_file(char *file, struct block_info_t* block_info, size_t len)
{
  FILE* handle;

  handle = fopen(file, "w+");
  if (handle == NULL)
    err(1, "write_to_file");

  fprintf(handle, "# sector_number, avg, trunc_avg, std_dev, rel_st_dev, "
      "trunc_st_dev, tries, samples\n");

  for(size_t i=0; i< len; i++)
    {
      if (!bi_is_initialised(&block_info[i]))
        continue;
      double trunc_avg;
      if (bi_num_samples(&block_info[i]) < 5)
        trunc_avg = bi_average(&block_info[i]);
      else
        trunc_avg = bi_trunc_average(&block_info[i], 0.25);

      fprintf(handle, "%i\t%f\t%f\t%f\t%f\t%f\t%i",
          i,
          bi_average(&block_info[i]),
          trunc_avg,
          bi_stdev(&block_info[i]),
          bi_rel_stdev(&block_info[i]),
          bi_int_rel_stdev(&block_info[i]),
          bi_num_samples(&block_info[i]));
      if (write_individual_times_to_file)
        {
          double* times;
          times = bi_get_times(&block_info[i]);
          for(int l=0; l<bi_num_samples(&block_info[i]); l++)
            fprintf(handle, " %f", times[l]);
        }
      fprintf(handle, "\n");
    }

  fclose(handle);
}

void
write_list_to_file(char* file, struct block_list_t* block_list)
{
  FILE* handle;

  handle = fopen(file, "w+");
  if (handle == NULL)
    err(1,"write_list_to_file");

  for(size_t i=0; !(block_list[i].off == 0 && block_list[i].len == 0); i++)
    if(fprintf(handle, "%lli %lli\n", block_list[i].off * sectors,
        (block_list[i].off + block_list[i].len) * sectors) == 0)
      err(1, "write_list_to_file");

  fclose(handle);
}

struct block_list_t*
read_list_from_file(char* file)
{
  FILE* handle = NULL;
  size_t read_blocks = 0;
  size_t alloc_elements = 16;
  struct block_list_t* block_list = NULL;

  handle = fopen(file, "r");
  if (handle == NULL)
    err(1, "read_list_from_file");

  block_list = calloc(sizeof(struct block_list_t), alloc_elements);
  if (block_list == NULL)
    err(1, "read_list_from_file");

  off_t off, len;
  off_t re;

  while(1)
    {
      if (read_blocks + 2 >= alloc_elements)
        {
          alloc_elements=alloc_elements*2;
          block_list = realloc(block_list, 
              sizeof(struct block_list_t)*alloc_elements);

          if (block_list == NULL)
            err(1, "read_list_from_file");
        }
      assert(alloc_elements >= read_blocks);

      re = fscanf(handle, "%lli %lli\n", &off, &len);
      if (re == 0 || re < 0)
        break;

      if ( len <= off )
        {
          fprintf(stderr, "end LBA is bigger than start LBA on line %zi in "
              "file %s\n",
              read_blocks, file);
          exit(EXIT_FAILURE);
        }
      if (read_blocks > 0 && len < block_list[read_blocks-1].off)
        {
          fprintf(stderr, "file %s not sorted!\n", file);
          exit(EXIT_FAILURE);
        }

      block_list[read_blocks].off = off/sectors; // round down
      block_list[read_blocks].len = ceill((len - off)*1.0L/sectors); //round up
      read_blocks++;
    }
  block_list[read_blocks].off = 0;
  block_list[read_blocks].len = 0;

  fclose(handle);
  
  if (read_blocks == 0)
    {
      free(block_list);
      return NULL;
    }
  else
    return block_list;
}

void
read_block_list(int dev_fd, struct block_list_t* block_list,
    struct block_info_t* block_info, char* dev_stat_path, off_t number_of_blocks)
{
  uint16_t correct_reads = 0xffff;
  off_t total_blocks = 0; ///< total number of blocks to be read (with overhead)
  off_t blocks_read = 0; ///< number of blocks read (with overhead)
  struct block_list_t* tmp_block_list; ///< compacted block_list
  int max_len = 4; ///< globbing param for compacting
  /// disk cache size in blocks
  off_t disk_cache = disk_cache_size * 1024 * 1024 / sectors / 512;
  struct timespec start_time, end_time, res; ///< expected time calculation
  size_t block_number=0; ///< position in the block_list
  struct block_info_t* block_data; ///< stats for sectors read

  tmp_block_list = compact_block_list(block_list, max_len * 2);

  for (size_t i=0; !(tmp_block_list[i].off==0 && tmp_block_list[i].len==0); i++)
    total_blocks += tmp_block_list[i].len + disk_cache + 3;

  clock_gettime(TIMER_TYPE, &start_time);
  while (!(tmp_block_list[block_number].off == 0 && 
      tmp_block_list[block_number].len == 0))
    {
      size_t offset, length;
      offset = tmp_block_list[block_number].off;
      length = tmp_block_list[block_number].len;
      if (verbosity > 3)
        fprintf(stderr, "processing block no %zi of length %zi\n", 
            offset, length);

      block_data = read_blocks(dev_fd, dev_stat_path, offset, length);

      blocks_read += length + disk_cache + 3;
      
      if (block_data == NULL || 
          (block_data != NULL && !bi_is_valid(&block_data[0])))
        {
          if (verbosity > 2)
            fprintf(stderr, 
                "\nre-read of block %zi (length %zi) interrupted\n", 
                offset, length);
          else if (verbosity > 0)
            fprintf(stderr, "!");// interrupted

          block_number++;
        }
      else if (verbosity <= 3 && verbosity > 0)
        fprintf(stderr, "."); // OK

      // print statistics
      if (verbosity >= 0 && (block_number % 10 == 0 || blocks_read % 32 == 0))
        {
          clock_gettime(TIMER_TYPE, &end_time);
          diff_time(&res, start_time, end_time);

          double percent;
          percent = blocks_read * 1.0/total_blocks;

          long long time_to_go;
          time_to_go = time_double(res) / percent;
          if (verbosity > 0)
            fprintf(stderr, "\n");

          fprintf(stderr, "reread %.2f%% done "
              "in %02li:%02li:%02li, expected time:"
              "%02lli:%02lli:%02lli\n",
              percent * 100,
              res.tv_sec/3600, res.tv_sec/60%60, res.tv_sec%60,
              time_to_go/3600, time_to_go/60%60, time_to_go%60);
        }

      // save whatever the read was successful
      if (block_data != NULL && bi_is_valid(&block_data[0]))
        {
          correct_reads <<= 1;
          correct_reads |= 0x1u;
        }
      else
        {
          correct_reads <<= 1;
        }

      // if last reads were unsuccessful, wait a second
      if (bitcount(correct_reads) == 0)
        {
          sleep(1); // let the reads and writes finish
        }
      // if less than 12 out of 16 last reads were successful
      // reduce the amount of blocks to read
      else if (bitcount(correct_reads) < 12)
        {
          if (max_len > 2) // divide the max len by half, recreate tmp_block_list
            {
              max_len /= 2;
              off_t beginning = tmp_block_list[block_number].off;
              free(tmp_block_list);

              tmp_block_list = compact_block_list(block_list, max_len);

              if (tmp_block_list == NULL)
                break;

              total_blocks = blocks_read;

              size_t i=0;
              while(!(tmp_block_list[i].off == 0 && tmp_block_list[i].len==0) &&
                  tmp_block_list[i].off + tmp_block_list[i].len < beginning)
                i++;
              block_number = i;

              for (; 
                  !(tmp_block_list[i].off==0 && tmp_block_list[i].len==0);
                  i++)
                total_blocks += tmp_block_list[i].len + disk_cache + 3;
            } 
        }
      // if all reads were successful, double the amount of blocks read
      else if (bitcount(correct_reads) == 16)
        {
          // don't read more than 128 MiB at a time
          if (max_len < 64 * 1024 * 1024 / sectors / 512) 
            {
              max_len *= 2;
              off_t beginning = tmp_block_list[block_number].off;
              free(tmp_block_list);

              tmp_block_list = compact_block_list(block_list, max_len);

              if (tmp_block_list == NULL)
                break;

              if (tmp_block_list == NULL)
                err(1, "re_read");

              total_blocks = blocks_read;

              size_t i = 0;
              while(!(tmp_block_list[i].off == 0 && tmp_block_list[i].len==0) &&
                  tmp_block_list[i].off + tmp_block_list[i].len < beginning)
                i++;
              block_number = i;
              
              for (;
                  !(tmp_block_list[i].off==0 && tmp_block_list[i].len==0);
                  i++)
                total_blocks += tmp_block_list[i].len + disk_cache + 3;
            }
        }

      if (block_data == NULL)
        continue;

      for (size_t i=0; i < length; i++)
        {
          bi_add_valid(&block_info[offset+i], &block_data[i]);
        }

      // free the block_data structure
      for (int i=0; i< length; i++)
        bi_clear(&block_data[i]);

      free(block_data);
      block_number++;
    }

  if (tmp_block_list)
    free(tmp_block_list);
}

void
perform_re_reads(int dev_fd, char* dev_stat_path, struct block_info_t* block_info,
    size_t block_info_size, int re_reads, double max_std_dev, short int min_reads,
    double delay)
{
  struct block_list_t* block_list;

  for(int tries=0; tries < re_reads; tries++)
    {
      // print statistics before processing
      if (verbosity >= 0)
        {

          if (min_reads == 1)
            block_list = find_uncertain_blocks(
                block_info, block_info_size, max_std_dev, min_reads, 1, 0, delay,
                1);
          else
            block_list = find_uncertain_blocks(
                block_info, block_info_size, max_std_dev, min_reads, 1, 0, 1000,
                1);


          if (block_list == NULL)
            {
              if (verbosity >2)
                fprintf(stderr, "no uncertain blocks found\n");
              break;
            }

          size_t block_number=0;
          // end of list is marked by NULL, NULL
          while (!(block_list[block_number].off == 0 && 
              block_list[block_number].len == 0))
            {
              if (verbosity > 2)
                {
                  size_t start = block_list[block_number].off,
                        end = start + block_list[block_number].len;

                  for(size_t i= start; i< end; i++)
                    {
                      double stdev = bi_int_rel_stdev(&block_info[i]);

                      fprintf(stderr, "rel std dev for block %zi: %3.9f"
                          ", average: %f, valid: %i, samples: %i\n", 
                          i, 
                          stdev,
                          bi_average(&block_info[i]),
                          bi_is_valid(&block_info[i]),
                          bi_num_samples(&block_info[i]));
                    }

                }
              block_number++;
            }

          fprintf(stderr, "re-reading %zi uncertain blocks\n", block_number);
          free(block_list);
        }


      if (min_reads == 1)
        block_list = find_uncertain_blocks(
            block_info, block_info_size, max_std_dev, min_reads, 1, 0, delay, 1);
      else
        block_list = find_uncertain_blocks(
            block_info, block_info_size, max_std_dev, min_reads, 1, 0, 1000, 1);

      if (block_list == NULL)
        break;

      read_block_list(dev_fd, block_list, block_info, dev_stat_path, 
          block_info_size);

      if (verbosity <= 3 && verbosity > 0)
        fprintf(stderr, "\n");

      if (block_list)
        free(block_list);
    }
  return;
}

/**
 * @param dev_fd device file descriptor
 * @param block_info structure to which write sector data
 * @param dev_stat_path sys stat file associated with the device
 * @param loops how many times to read the device
 * @param whatever to write sector times to stdout
 * @param max_sectors maximum sector number to read, 0 if unbounded
 * @param filesize filesize
 */
void
read_whole_disk(int dev_fd, struct block_info_t* block_info, 
    char* dev_stat_path, int loops, int sector_times, off_t max_sectors,
    off_t filesize)
{
  char *ibuf; ///< input buffer for sector reading
  char *ibuf_free; ///< pointer for free'ing the buffer
  long long read_s=0,  ///< device reads (at the beginning)
       write_s=0, ///< device writes (at the beginning)
       read_e=1, ///< device reads (at the end)
       write_e=0, ///< device writes (at the end)
       read_sec_s=0, ///< device read sectors (at the beginning)
       read_sec_e=0; ///< device read sectors (at the end)
  int next_is_valid=1; ///< whatever am erronous read occured and next sector
                       ///< can contain seek time
  int loop=0; ///< loop number
  struct timespec time1, time2, /**< time it takes to read single block */
                  res; /**< temp result */
  off_t nread; ///< number of bytes the read() managed to read
  size_t blocks = 0; ///< number of blocks read in this run
  long long abs_blocks = 0; ///< number of blocks read in all runs
  struct timespec times, timee; ///< wall clock start and end
  off_t number_of_blocks; ///< filesize in blocks


  fesetround(2); // round UP
  number_of_blocks = lrintl(ceil(filesize*1.0l/512/sectors));

  // get memory alligned pointer (needed for O_DIRECT access)
  ibuf = malloc(sectors*512+pagesize);
  ibuf_free = ibuf;
  ibuf = ptr_align(ibuf, pagesize);


  // position the disk head
  lseek(dev_fd, (off_t)0, SEEK_SET);
  read(dev_fd, ibuf, pagesize);
  lseek(dev_fd, (off_t)0, SEEK_SET);

  clock_gettime(TIMER_TYPE, &time2);
  if (dev_stat_path != NULL)
    get_read_writes(dev_stat_path, &read_e, &read_sec_e, &write_e);

 clock_gettime(TIMER_TYPE, &times);
  while (1)
    {
      read_s = read_e;
      write_s = write_e;
      read_sec_s = read_sec_e;
      //if (dev_stat_path != NULL)
      //  get_read_writes(dev_stat_path, &read_s, &read_sec_s, &write_s);
      time1.tv_sec=time2.tv_sec;
      time1.tv_nsec=time2.tv_nsec;

      // assertion
      if ( lseek(dev_fd, (off_t)0, SEEK_CUR) != ((off_t)blocks) * sectors * 512 )
        {
          fprintf(stderr, "hdck: main: wrong offset got %lli expected %lli\n",
              lseek(dev_fd, (off_t)0, SEEK_CUR),
              ((off_t)blocks) * sectors * 512);
        }

      //clock_gettime(TIMER_TYPE, &time1);
      nread = read(dev_fd, ibuf, sectors*512);
      clock_gettime(TIMER_TYPE, &time2);

      if (dev_stat_path != NULL)
        get_read_writes(dev_stat_path, &read_e, &read_sec_e, &write_e);

//      fprintf(stderr, "read sec: %lli\n", read_sec_e-read_sec_s);

      if (nread < 0) // on error
        {
          if (errno != EIO)
            err(1, NULL);
          else
            {
              diff_time(&res, time1, time2); 
              nread = 1; // don't exit loop
              write(2, "E", 1);
              bi_add_error(&block_info[blocks]);

              if (bad_sector_warning)
                {
                  fprintf(stderr, "BAD SECTORS reads may not be accurate!\n");
                  bad_sector_warning = 0;
                }

              // omit block
              if (lseek(dev_fd, (off_t)512*sectors, SEEK_CUR) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
            }
        }
      // when the read was incomplete or interrupted
      else if (nread != sectors*512 || 
          (read_e-read_s != 1 && nodirect == 0 && dev_stat_path != NULL) || 
          (read_e-read_s > 4 && nodirect == 1 && dev_stat_path != NULL) || 
          (read_sec_e-read_sec_s != sectors && 
                nodirect == 0 && dev_stat_path != NULL) || 
          (write_e != write_s && dev_stat_path != NULL))
        {
          if (verbosity > 0)
            fprintf(stderr, "block %zi (LBA: %lli-%lli) interrupted\n", blocks,
               ((off_t)blocks) * sectors, ((off_t)blocks+1)*sectors);
          diff_time(&res, time1, time2);
          times_time(&res, 1000); // in ms not ns
          if (bi_is_valid(&block_info[blocks]) == 0)
            {
              bi_add_time(&block_info[blocks], time_double(res));
            }
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

          // invalidate last read block
          if (blocks > 0 && bi_is_valid(&block_info[blocks-1]))
            bi_remove_last(&block_info[blocks-1]);
        }
      else // when the read was correct
        {
          diff_time(&res, time1, time2);
          //make the times stored in block struct in ms not in ns
          times_time(&res, 1000);

          // update only if we can gather meaningful data
          if (bi_is_valid(&block_info[blocks]) == 0 || 
              (bi_is_valid(&block_info[blocks]) && next_is_valid == 1))
            {
              if (bi_is_valid(&block_info[blocks]) == 0 && next_is_valid == 1)
                {
                  // first valid read
                  bi_clear(&block_info[blocks]);
                  bi_make_valid(&block_info[blocks]);
                  bi_add_time(&block_info[blocks], time_double(res));

                  if (verbosity > 10)
                    fprintf(stderr, "block: %zi, samples: %i, average: "
                        "%f, rel stdev: %f, trunc rel stdev: %f\n", 
                        blocks,
                        bi_num_samples(&block_info[blocks]),
                        bi_average(&block_info[blocks]),
                        bi_rel_stdev(&block_info[blocks]),
                        bi_int_rel_stdev(&block_info[blocks]));
                }
              else
                {
                  // subsequent valid or invalid reads
                  bi_add_time(&block_info[blocks], time_double(res));

                  if (verbosity > 10)
                    fprintf(stderr, "block: %zi, samples: %i, average: "
                        "%f, rel stdev: %f, trunc rel stdev: %f\n", 
                        blocks,
                        bi_num_samples(&block_info[blocks]),
                        bi_average(&block_info[blocks]),
                        bi_rel_stdev(&block_info[blocks]),
                        bi_int_rel_stdev(&block_info[blocks]));
                }
              
              diff_time(&res, time1, time2);

            }
            
          next_is_valid = 1;
        }

      if (res.tv_nsec < 2000000 && res.tv_sec == 0) // very very fast read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"_",1);
        }
      else if (res.tv_nsec < 5000000 && res.tv_sec == 0) // very fast read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,".",1);
        }
      else if (res.tv_nsec < 10000000 && res.tv_sec == 0) // fast read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,",",1);
        }
      else if (res.tv_nsec < 25000000 && res.tv_sec == 0) // normal read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"-",1);
        }
      else if (res.tv_nsec < 50000000 && res.tv_sec == 0) // slow read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"+",1);
        }
      else if (res.tv_nsec < 80000000 && res.tv_sec == 0) // very slow read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"#",1);
        }
      else // very very slow read
        {
          if (sector_times == PRINT_SYMBOLS) write(0,"!",1);
        }

      if (sector_times == PRINT_TIMES)
        printf("%li r:%lli rs: %lli w:%lli\n",
            res.tv_nsec/1000+res.tv_sec*1000000, 
            read_s, 
            read_sec_s, 
            write_s); 

      blocks++;
      abs_blocks++;

      if (blocks % 1000 == 0 && verbosity >= 0)
        {
          clock_gettime(TIMER_TYPE, &timee);
          diff_time(&res, time1, time2);

          float cur_speed;
          cur_speed = sectors * 512 / 1024 * 1.0f / 1024 / 
            (res.tv_sec * 1.0f + res.tv_nsec / 1000000000.0);

          diff_time(&res, times, timee);

          float speed;
          speed = abs_blocks * sectors * 512 / 1024 * 1.0f / 1024 / 
            (res.tv_sec * 1.0f + res.tv_nsec / 1000000000.0);

          float percent;
          if (max_sectors == 0)
            percent = (blocks * sectors * 512.0f) / (filesize * 1.0f);
          else
            percent = (blocks * sectors * 512.0f) / 
              (max_sectors * sectors * 2.0f);

          long long time_to_go;
          time_to_go = (res.tv_sec*1.0) / (percent/min_reads + loop*1.0/min_reads);

          fprintf(stderr,"read %lli sectors, %.3fMiB/s (%.3fMiB/s), "
              "%.2f%% (%.2f%%), "
              "in %02li:%02li:%02li, loop %i of %lli, "
              "expected time: %02lli:%02lli:%02lli\n",
             ((off_t)blocks)*sectors,
             cur_speed,
             speed,
             percent*100, (percent/min_reads + loop*1.0/min_reads) * 100,
             res.tv_sec/3600, res.tv_sec/60%60, res.tv_sec%60,
             loop+1, min_reads,
             time_to_go/3600, time_to_go/60%60, time_to_go%60);
        }

      if (nread == 0 || nread == -1 || blocks >= number_of_blocks
          || (max_sectors != 0 && blocks * sectors >= max_sectors ))
        {
          long long high_dev=0;
          long long sum_invalid=0;
          loop++;
          // check standard deviation for blocks
          for (int i =0; i < blocks; i++)
            {
              if (bi_int_rel_stdev(&block_info[i]) > max_std_dev)
                high_dev++;
              
              if (bi_is_valid(&block_info[i]) == 0)
                sum_invalid++;
            }
          if (loop < min_reads || 
              high_dev/(blocks*1.0) > 0.25 || 
              sum_invalid/(blocks*1.0) > 0.10)
            {
              if (
                  verbosity > 0 && 
                  !(loop < min_reads) && 
                  ( high_dev/(blocks*1.0) > 0.25 
                    || sum_invalid/(blocks*1.0) > 0.10)
                 )
                fprintf(stderr, "low confidance for the results, "
                    "re-reading whole disk\n");

              blocks=0;
              // seek to first block
              if (lseek(dev_fd, (off_t)0, SEEK_SET) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
              nread = read(dev_fd, ibuf, 512);
              if (lseek(dev_fd, (off_t)0, SEEK_SET) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
              clock_gettime(TIMER_TYPE, &time2);
              // TODO: flush system buffers when no direct 
            }
          else
            break;

          if (loop > max_reads)
            {
              fprintf(stderr, "Warning; read whole disk %lli times, still "
                  "can't get high confidence\n", max_reads);
              break;
            }
        }
    }
 free(ibuf_free); 
}

int
main(int argc, char **argv)
{
  int c;
  char* filename = NULL;
  /// path to the `stat' file for the corresponding hardware device
  char* dev_stat_path;
  char* output = NULL; ///< output file name
  char* write_uncertain_to_file = NULL; ///< output file for uncertain sectors
  char* read_sectors_from_file = NULL; ///< file with sectors to scan to
  int sector_times = 0;
  int nosync = 0;
  int noflush = 0;
  off_t filesize = 0;
  off_t max_sectors = 0;
  struct block_info_t* block_info = NULL;
  int no_rt = 0;
  double rotational_delay = 60.0/7200*1000; ///< in ms
  struct timespec res; /// temporary timespec result
  long long errors = 0,
    vvfast = 0,
    vfast = 0,
    fast = 0,
    normal = 0,
    slow = 0,
    vslow = 0,
    vvslow = 0;  


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
        {"outfile", 1, 0, 'o'}, // 12
        {"min-reads", 1, 0, 0}, // 13
        {"max-std-deviation", 1, 0, 0}, // 14
        {"max-reads", 1, 0, 0}, // 15
        {"disk-cache", 1, 0, 0}, // 16
        {"nort", 0, &no_rt, 1}, // 17
        {"background", 0, 0, 'b'}, // 18
        {"disk-rpm", 1, 0, 0}, // 19
        {"bad-sectors", 0, 0, 'w'}, // 20
        {"read-sectors", 0, 0, 'r'}, // 21
        {"version", 0, 0, 0}, // 22
        {0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "f:xhbvo:?w:r:",
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
        if (option_index == 13)
          {
            min_reads = atoll(optarg);
            break;
          }
        if (option_index == 14)
          {
            max_std_dev = atof(optarg);
            break;
          }
        if (option_index == 15)
          {
            max_reads = atoll(optarg);
            break;
          }
        if (option_index == 16)
          {
            disk_cache_size = atoll(optarg);
            break;
          }
        if (option_index == 19)
          {
            if (atoll(optarg) == 0)
              {
                usage();
                exit(EXIT_FAILURE);
              }
            rotational_delay = 60.0/atoll(optarg) * 1000;
            break;
          }
        if (option_index == 22)
          {
            version();
            exit(EXIT_SUCCESS);
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

    case 'o':
        output = optarg;
        if (verbosity > 5) printf("option o with value '%s'\n", optarg);
        break;

    case 'b':
        max_reads = 20;
        noaffinity = 1;
        nortio = 1;
        no_rt = 1;
        break;
        
    case 'w':
        write_uncertain_to_file = optarg;
        break;

    case 'r':
        read_sectors_from_file = optarg;
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

  if (exclusive)
    {
      if (min_reads == 0)
        min_reads = 1;
      if (max_reads == 0)
        max_reads = 5;
      if (max_std_dev == 0)
        max_std_dev = 0.75;
    }
  else
    {
      if (min_reads == 0)
        min_reads = 3;
      if (max_reads == 0)
        max_reads = 10;
      if (max_std_dev == 0)
        max_std_dev = 0.5;
    }

  if (min_reads > max_reads)
    {
      fprintf(stderr, "Warning: min_reads bigger thatn max_reads, correcting\n");
      max_reads = min_reads;
    }

  struct timespec times, timee; /**< wall clock start and end */

  int dev_fd = 0;
  off_t number_of_blocks;

  // make the process real-time
  if (!no_rt)
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

  // use exclusive mode
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

  // we can't realiably read last sector anyway, so round the disk size down
  filesize = floorl(filesize*1.L/512/sectors)*512*sectors;
  if (!filesize)
    {
      fprintf(stderr, "Device too small, needs to be at least %i bytes in size\n",
          512*sectors);
      exit(EXIT_FAILURE);
    }

  dev_stat_path = get_file_stat_sys_name(filename);


  fesetround(2); // interger rounding rounds UP
  if (max_sectors == 0)
    number_of_blocks = lrintl(ceill(filesize*1.0L/512/sectors));
  else
    number_of_blocks = lrintl(ceill(max_sectors*1.0L/sectors));
  block_info = calloc(number_of_blocks, 
      sizeof(struct block_info_t));
  if (!block_info)
    {
      printf("Allocation error, tried to allocate %lli bytes:\n", 
          number_of_blocks* sizeof(struct block_info_t));
      err(1, "calloc");
    }

  fsync(dev_fd);

  if (!noflush)
    {
      // Attempt to free all cached pages related to the opened file
      if (posix_fadvise(dev_fd, 0, 0, POSIX_FADV_DONTNEED) < 0)
        err(1, NULL);
      if (posix_fadvise(dev_fd, 0, 0, POSIX_FADV_NOREUSE) < 0)
        err(1, NULL);
    }

  if (verbosity > 2)
    {
      fprintf(stderr, "min-reads: %lli, max re-reads: %lli, max rel std dev %f, "
          "disk cache size: %ziMiB\n",
         min_reads,
         max_reads,
         max_std_dev,
         disk_cache_size); 
    }

  clock_gettime(TIMER_TYPE, &times);

  /*
   * MAIN LOOP
   */
  if(read_sectors_from_file == NULL)
    {
      read_whole_disk(dev_fd, block_info, dev_stat_path, min_reads, sector_times,
         max_sectors, filesize);
    }
  else
    {
      struct block_list_t* block_list;

      block_list = read_list_from_file(read_sectors_from_file);

      if(block_list == NULL)
        {
          printf("File \'%s\' is empty\n", read_sectors_from_file);
          exit(EXIT_FAILURE);
        }

      for (int i=0; i<min_reads; i++)
        read_block_list(dev_fd, block_list, block_info, dev_stat_path,
            number_of_blocks);

      free(block_list);
    }

  perform_re_reads(dev_fd, dev_stat_path, block_info, number_of_blocks,
      max_reads, max_std_dev, min_reads, rotational_delay);


  // print uncertain blocks
  struct block_list_t* block_list;

  block_list = find_uncertain_blocks(
      block_info, number_of_blocks, max_std_dev, min_reads, 1, 0,
      rotational_delay, 0);

  if (block_list == NULL)
    {

      if (write_uncertain_to_file != NULL)
        {
          // zero out the file
          block_list = calloc(sizeof(struct block_list_t), 1);

          write_list_to_file(write_uncertain_to_file, block_list);

          free(block_list);
          block_list = NULL;
        }

      fprintf(stderr, "no uncertain blocks found!\n");
    }
  else
    {
      if (verbosity > 0)
        fprintf(stderr, "uncertain blocks:\n");

      size_t block_number=0;
      while (!(block_list[block_number].off == 0 && 
          block_list[block_number].len == 0))
        {
          if (verbosity > 2)
            {
              size_t start = block_list[block_number].off,
                    end = start + block_list[block_number].len;

              for(size_t i= start; i< end; i++)
                {
                  double stdev = bi_int_rel_stdev(&block_info[i]);

                  fprintf(stdout, "rel std dev for block %zi: %3.9f"
                      ", average: %f, valid: %i, samples: %zi\n", 
                      i, 
                      stdev,
                      bi_average(&block_info[i]),
                      bi_is_valid(&block_info[i]),
                      bi_num_samples(&block_info[i]));
                }

            }
          else
            {
              size_t start = block_list[block_number].off,
                    end = start + block_list[block_number].len;

              for(size_t i= start; i< end; i++)
                {
                  fprintf(stdout, "%lli\t%lli\n", ((off_t)i)*sectors,
                      ((off_t)i+1)*sectors);
                }
            }
          block_number++;
        }

      if (verbosity > 1)
        {
          fprintf(stderr, "%zi uncertain blocks found\n", block_number);
        }

      if (write_uncertain_to_file != NULL)
        write_list_to_file(write_uncertain_to_file, block_list);

      free(block_list);
    }

  clock_gettime(TIMER_TYPE, &timee);
  if (verbosity > 0)
    {
      diff_time(&res, times, timee);
      fprintf(stderr, "wall time: %lis.%lims.%liµs.%lins\n", res.tv_sec,
          res.tv_nsec/1000000, res.tv_nsec/1000%1000,
          res.tv_nsec%1000);

      long double sum = 0.0;
      long long reads = 0;
      struct block_info_t single_block;

      bi_init(&single_block);

      for (size_t i=0; i < number_of_blocks; i++)
        {
          if (!bi_is_initialised(&block_info[i]))
            continue;
          sum += bi_sum(&block_info[i]);
          reads += bi_num_samples(&block_info[i]);

          if (bi_num_samples(&block_info[i]) < 5)
            bi_add_time(&single_block, bi_average(&block_info[i]));
          else
            bi_add_time(&single_block, bi_trunc_average(&block_info[i], 0.25));
        }

      double sec = floor(sum / 1000);
      double msec = floor(sum - sec * 1000);
      double usec = floor((sum - sec * 1000 - msec)*1000);

      fprintf(stderr, "sum time: %.0fs.%.0fms.%.0fµs\n",
        sec,
        msec,
        usec);

      fprintf(stderr, "read %lli blocks (%lli errors, %lli samples)\n", 
          number_of_blocks, errors, reads);

      sum = bi_average(&single_block);

      sec = floor(sum / 1000);
      msec = floor(sum - sec * 1000);
      usec = floor((sum - sec * 1000 - msec)*1000);

      fprintf(stderr, "mean block time: %.0fs.%.0fms.%.0fµs\n",
        sec,
        msec,
        usec);

      fprintf(stderr, "std dev: %.9f(ms)\n",
          bi_stdev(&single_block));

      bi_clear(&single_block);
    }
  long long sum_invalid=0;
  vvfast=0;
  vfast=0;
  fast=0;
  normal=0;
  slow=0;
  vslow=0;
  vvslow=0;
  errors=0;
  for (int i=0; i< number_of_blocks; i++)
    {
      if (!bi_is_initialised(&block_info[i]))
        continue;

      if (bi_is_valid(&block_info[i]) == 0)
        sum_invalid++;

      double avg;

      if (bi_num_samples(&block_info[i]) < 5)
        avg = bi_average(&block_info[i]);
      else
        avg = bi_trunc_average(&block_info[i], 0.25);

      errors += bi_get_error(&block_info[i]);

      if (avg < 2) // very very fast read
        {
          ++vvfast;
        }
      else if (avg < 5) // very fast read
        {
          ++vfast;
        }
      else if (avg < 10) // fast read
        {
          ++fast;
        }
      else if (avg < 25) // normal read
        {
          ++normal;
        }
      else if (avg < 50) // slow read
        {
          ++slow;
        }
      else if (avg < 80) // very slow read
        {
          ++vslow;
        }
      else // very very slow read
        {
          ++vvslow;
        }
    }

  if (verbosity > 0)
    {
      fprintf(stderr, "Number of invalid measures because of interrupted "
          "reads: %lli\n", sum_invalid);
      fprintf(stderr, "read statistics:\n<2ms: %lli\n<5ms: %lli\n<10ms: %lli\n"
          "<25ms: %lli\n<50ms: %lli\n<80ms: %lli\n>80ms: %lli\nERR: %lli\n",
        vvfast, vfast, fast, normal, slow, vslow, vvslow, errors);
    }

  if (verbosity > 2)
    {
      vvfast=0;
      vfast=0;
      fast=0;
      normal=0;
      slow=0;
      vslow=0;
      vvslow=0;
      errors=0;

      long double raw_sum = 0.0;
      long long samples = 0;

      for (int i=0; i< number_of_blocks; i++)
        {
          if (!bi_is_initialised(&block_info[i]))
            continue;

          long double partial_sum = 0.0;

          for (int j=0; j< bi_num_samples(&block_info[i]); j++)
            {

              double avg = bi_get_times(&block_info[i])[j];
              partial_sum += avg;
              errors += bi_get_error(&block_info[i]);
              samples++;

              if (avg < 2) // very very fast read
                {
                  ++vvfast;
                }
              else if (avg < 5) // very fast read
                {
                  ++vfast;
                }
              else if (avg < 10) // fast read
                {
                  ++fast;
                }
              else if (avg < 25) // normal read
                {
                  ++normal;
                }
              else if (avg < 50) // slow read
                {
                  ++slow;
                }
              else if (avg < 80) // very slow read
                {
                  ++vslow;
                }
              else // very very slow read
                {
                  ++vvslow;
                }
            }
          raw_sum += partial_sum;
        }

      fprintf(stderr, "raw read statistics:\n"); 
      fprintf(stderr, "ERR: %lli\n2ms: %lli\n5ms: %lli\n10ms: %lli\n25ms: %lli\n"
          "50ms: %lli\n80ms: %lli\n80+ms: %lli\n",
          errors, vvfast, vfast, fast, normal, slow, vslow, vvslow);

      double sec = floor(raw_sum / 1000);
      double msec = floor(raw_sum - sec * 1000);
      double usec = floor((raw_sum - sec * 1000 - msec)*1000);

      fprintf(stderr, "sum time: %.0fs.%.0fms.%.0fµs\n",
        sec,
        msec,
        usec);

      raw_sum = raw_sum / samples;

      sec = floor(raw_sum / 1000);
      msec = floor(raw_sum - sec * 1000);
      usec = floor((raw_sum - sec * 1000 - msec)*1000);

      fprintf(stderr, "mean block time: %.0fs.%.0fms.%.0fµs\n",
        sec,
        msec,
        usec);
    }


  if (output != NULL)
    {
      write_to_file(output, block_info, number_of_blocks);
    }

  free(dev_stat_path);
  for(int i=0; i< number_of_blocks; i++)
    bi_clear(&block_info[i]);
  free(block_info);
  return 0;
}
