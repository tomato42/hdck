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
#include "ioprio.h"
#define TIMER_TYPE CLOCK_REALTIME 

int pagesize = 4096;
int sectors = 256;
int verbosity = 0;

int exclusive = 0; ///< use exclusive file access (O_EXCL)
int nodirect = 0; ///< don't use O_DIRECT access
int noaffinity = 0; ///< don't set CPU affinity
int nortio = 0; ///< don't change process scheduling to RT
int write_individual_times_to_file = 1; ///< TODO - cmd option

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
    short int samples_len; ///< number of samples taken
    short int valid; ///< 0 if data is invalid (because read was interrupted)
};

/// list of sectors to read
struct block_list_t {
    off_t off;
    off_t len;
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
  printf("-o, --outfile       output file for the detailed statistics \n");
  printf("--nodirect          don't use O_DIRECT\n");
  printf("--noflush           don't flush system buffers before reading\n");
  printf("--nosync            don't use O_SYNC\n");
  printf("--noaffinity        don't set CPU affinity to 0th core/CPU\n");
  printf("--nortio            don't change IO priority to real-time\n");
  printf("--sector-symbols    print symbols representing read time of each"
                                                       " group of sectors\n");
  printf("--sector-times      print time it takes to read each group of"
                                                        " sectors (in µs)\n");
  printf("--min-reads         minimal number of valid reads for a sector\n");
  printf("--max-reads         maximal number of re-reads for a sector\n");
  printf("--max-std-deviation maximal stdandard deviation for a sector to be "
                                                        "considered valid\n");
  printf("--max-sectors NUM   read at most NUM sectors\n");
  printf("--disk-cache NUM    size of the on-board disk cache (32MiB default)\n");
  printf("--noverbose         reduce verbosity\n");
  printf("-v, --verbose       be more verbose\n");
  printf("-h, -?              print this message\n");
  printf("\n");
  printf("This program can be run on both files and devices, though running it on top of\n");
  printf("a file is quite pointless. In most cases default settings should be OK. Things to\n");
  printf("check are --nodirect and --noflush.\n");
  printf("When using -x, the program uses different algorithm that trusts the times more\n");
  printf("and as a result, should achive minimum confidence in less time (by not using re-\n");
  printf("-reads, much)\n");
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
sum_time(struct timespec *sum, struct timespec adder)
{
  sum->tv_sec += adder.tv_sec;
  sum->tv_nsec += adder.tv_nsec;
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

/**
 * convert struct timespec to double
 */
double
time_double(struct timespec ts)
{
  return ts.tv_sec + ts.tv_nsec * 1.0 / 1E9;
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

  return;
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


/**
 * reset the block_info struct
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
}

/** check if block_info is valid
 */
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
double*
bi_get_times(struct block_info_t* block_info)
{
  return block_info->samples;
}

/** return standard deviation for samples
 */
double
bi_stdev(struct block_info_t* block_info)
{
  int n = 0;
  long double mean = 0.0;
  long double M2 = 0.0;
  long double delta;

  for (int i=0; i < block_info->samples_len; i++)
    {
      n++;
      delta = block_info->samples[i] - mean;
      mean += delta/n;
      M2 += delta * (block_info->samples[i] - mean); 
    }
  
  return sqrt(M2 / n);
}

/** return relative standard deviation for samples (stdev/mean)
 */
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

/** return truncated standard deviation for samples
 * @param percent how much data is to be thrown off
 */
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
double
bi_int_rel_stdev(struct block_info_t* block_info)
{
  if(block_info->samples_len < 5)
    return bi_rel_stdev(block_info);

  return bi_trunc_rel_stdev(block_info, 0.25);
}


double
calculate_std_dev(struct timespec sumtime, struct timespec sumsqtime, long long n)
{
  double ro = 
      ( time_double(sumsqtime) - 
        (time_double(sumtime) * time_double(sumtime) / n) 
      ) * 1.0 / n;

  if (ro <= 0)
    return 0;
  else
    return sqrt(ro);
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

  static char sys_path[] = "/sys/block/";
  static char sys_stat[] = "/stat";

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

struct block_info_t*
read_blocks(int fd, char* stat_path, off_t offset, off_t len)
{
  struct timespec time_start; ///< start of read
  struct timespec time_end; ///< end of read
  struct timespec res; ///< temporary result
  long long read_start = 0, read_sectors_s = 0, write_start = 0,
            read_end = 0, read_sectors_e = 0, write_end = 0;
  struct block_info_t* block_info;
  char* buffer;
  char* buffer_free;
  off_t nread;
  off_t no_blocks = 0;

  block_info = calloc(sizeof(struct block_info_t), len);
  if (block_info == NULL)
    err(1, "read_blocks");

  buffer = malloc(sectors*512+pagesize);
  if (buffer == NULL)
    err(1, "read_blocks");
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

      if (nread != sectors*512)
        goto interrupted;
    }

  // read additional block before the main data to exclude seek time
  if (offset-disk_cache-1 < 0)
    if ( lseek(fd, (offset-1>=0)?(offset-1)*sectors*512:0, SEEK_SET) < 0)
      goto interrupted;

  nread = read(fd, buffer, sectors*512);
  if (nread != sectors*512)
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
            err(1, "read_blocks");

          write(2, "E", 1);

          // omit block
          if (lseek(fd, 
                    (off_t)512*sectors*(no_blocks+1 + offset), 
                    SEEK_SET) < 0)
            goto interrupted;

        }
      else if (nread != sectors*512)
        goto interrupted;

      bi_make_valid(&block_info[no_blocks]);

      diff_time(&res, time_start, time_end);
      times_time(&res, 1000); // block_info is in ms, not ns

      bi_add_time(&block_info[no_blocks], time_double(res));

      no_blocks++;
    }

  // read additional two blocks to exclude the probability that there were 
  // unfinished reads or writes in the mean time while the main was run
  nread = read(fd, buffer, sectors*512);
  nread = read(fd, buffer, sectors*512);

  if (stat_path != NULL)
    get_read_writes(stat_path, &read_end, &read_sectors_e, &write_end);

  if ((read_end-read_start != disk_cache + 1 + 2 + len && 
        nodirect == 0 && 
        stat_path != NULL
      )
      || 
      (read_end-read_start > 4 * (disk_cache + 1 + 2 + len) && 
        nodirect == 1 && 
        stat_path != NULL
      )
     )
    goto interrupted;

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
 * @param block_info block statistics
 * @param block_info_len block_info length
 * @param min_std_dev minimal standard deviation for a block to be cosidered 
 * uncertain
 * @param min_reads minimal number of reads for a block to be considered
 * checked
 * @param glob how close sectors have to be near them to be bundled together
 */
struct block_list_t*
find_uncertain_blocks(struct block_info_t* block_info, size_t block_info_len,
    float min_std_dev, int min_reads, int glob)
{
  struct block_list_t* block_list;
  struct block_list_t* ret;
  size_t uncertain = 0;
  size_t ret_len = 0;

  block_list = calloc(sizeof(struct block_list_t), block_info_len);
  if (block_list == NULL)
    err(1, "find_uncertain_blocks");

  // find uncertain blocks
  for (size_t block_no=0; block_no < block_info_len; block_no++)
    {
      if (bi_is_valid(&block_info[block_no]) == 0 || 
          bi_num_samples(&block_info[block_no]) < min_reads ||
          bi_int_rel_stdev(&block_info[block_no]) > min_std_dev)
        {
          block_list[uncertain].off = block_no;
          block_list[uncertain].len = 1;
          uncertain++;
        }
    }

  if (uncertain == 0)
    {
      free(block_list);
      return NULL;
    }

  if (glob == 1)
    return block_list;

  ret = calloc(sizeof(struct block_list_t), uncertain+1);
  if (ret == NULL)
    err(1, "find_uncertain_blocks");

  // compact the list
  ret[0].off = block_list[0].off;
  ret[0].len = block_list[0].len;
  ret_len = 1;
  for (size_t i=1; i< uncertain; i++)
    {
      if (block_list[i].off <= ret[ret_len-1].off + ret[ret_len-1].len)
        continue;

      if (block_list[i].off <= ret[ret_len-1].off + ret[ret_len-1].len + glob
         && ret[ret_len-1].len < glob)
        {
          ret[ret_len-1].len += block_list[i].off 
                                - (ret[ret_len-1].off + ret[ret_len-1].len)
                                + 1;

          if (ret[ret_len-1].len + ret[ret_len-1].off >= block_info_len)
            ret[ret_len-1].len = block_info_len - ret[ret_len-1].off;
          continue;
        }

      ret[ret_len].off = block_list[i].off;
      ret[ret_len].len = block_list[i].len;
      ret_len++;
    }

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

int
main(int argc, char **argv)
{
  int c;
  char* filename = NULL;
  /// path to the `stat' file for the corresponding hardware device
  char* dev_stat_path;
  char* output = NULL; ///< output file name
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
        {"outfile", 1, 0, 'o'}, // 12
        {"min-reads", 1, 0, 0}, // 13
        {"max-std-deviation", 1, 0, 0}, // 14
        {"max-reads", 1, 0, 0}, // 15
        {"disk-cache", 1, 0, 0}, // 16
        {0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "f:xhvo:?",
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
        min_reads = 2;
      if (max_reads == 0)
        max_reads = 5;
      if (max_std_dev == 0)
        max_std_dev = 0.25;
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

  struct timespec time1, time2, /**< time it takes to read single block */
                  sumtime, /**< sum of all times */
                  res, /**< temp result */
                  times, timee, /**< wall clock start and end */
                  sumsqtime; /**< sum of squares (for std. deviation) */
  size_t blocks = 0;
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
  number_of_blocks = lrintl(ceil(filesize*1.0l/512/sectors));
  block_info = calloc(number_of_blocks, 
      sizeof(struct block_info_t));
  if (!block_info)
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
  long long read_s=0, 
       write_s=0, 
       read_e=1, 
       write_e=0, 
       read_sec_s=0, 
       read_sec_e=0;
  int next_is_valid=1;
  int loop=0;

  if (verbosity > 2)
    {
      fprintf(stderr, "min-reads: %lli, max re-reads: %lli, max rel std dev %f, "
          "disk cache size: %ziMiB\n",
         min_reads,
         max_reads,
         max_std_dev,
         disk_cache_size); 
    }

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
          // TODO save that an error occured when reading the block
          if (errno != EIO)
            err(1, NULL);
          else
            {
              diff_time(&res, time1, time2); 
              nread = 1; // don't exit loop
              write(2, "E", 1);
              ++errors;

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
          (read_sec_e-read_sec_s != 256 && 
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
        printf("%li r:%lli rs: %lli w:%lli\n",
            res.tv_nsec/1000+res.tv_sec*1000000, 
            read_s, 
            read_sec_s, 
            write_s); 

      blocks++;
      abs_blocks++;
      sum_time(&sumtime, res);
      sqr_time(&res, res);
      sum_time(&sumsqtime, res);

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
              if (lseek(dev_fd, (off_t)0, SEEK_SET) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
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
  for(int tries=0; tries < max_reads; tries++)
    {
      struct block_list_t* block_list;
      struct block_info_t* block_data;

      // TODO block re-reading should back-off for a while if there are many
      // interruptions and decrease the length of blocks read
      // TODO block re-reading should provide ntermitent expected time and 
      // status for the re-reading process

      if (verbosity >= 0)
        {

          block_list = find_uncertain_blocks(
              block_info, blocks, max_std_dev, min_reads, 1);

          if (block_list == NULL)
            {
              if (verbosity >2)
                fprintf(stderr, "no uncertain blocks found\n");
              break;
            }

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


      block_list = find_uncertain_blocks(
          block_info, blocks, max_std_dev, min_reads, 10);

      if (block_list == NULL)
        break;

      size_t block_number=0;
      while (!(block_list[block_number].off == 0 && 
          block_list[block_number].len == 0))
        {
          size_t offset, length;
          offset = block_list[block_number].off;
          length = block_list[block_number].len;
          if (verbosity > 3)
            fprintf(stderr, "processing block no %zi of length %zi\n", 
                offset, length);

          block_data = read_blocks(dev_fd, dev_stat_path, offset, length);
          
          if (block_data == NULL)
            {
              if (verbosity > 2)
                fprintf(stderr, 
                    "\nre-read of block %zi (length %zi) interrupted\n", 
                    offset, length);
              else fprintf(stderr, "!");// interrupted

              block_number++;
              continue;
            }
          else if (verbosity <= 3 && verbosity >= 0)
            fprintf(stderr, "."); // OK

          for (int i=0; i < length; i++)
            {
              if(bi_is_valid(&block_data[i]) == 1)
                {
                  if (bi_is_valid(&block_info[offset+i]) == 0)
                    {
                      bi_clear(&block_info[offset+i]);
                      bi_make_valid(&block_info[offset+i]);
                    }
                  bi_add(&block_info[offset+i],
                      &block_data[i]);
                }
            }


          // free the block_data structure
          for (int i=0; i< length; i++)
            bi_clear(&block_data[i]);

          free(block_data);
          block_number++;
        }
      if (verbosity <= 3 && verbosity >= 0)
        fprintf(stderr, "\n");

      free(block_list);
    }

  // print uncertain blocks
  struct block_list_t* block_list;

  block_list = find_uncertain_blocks(
      block_info, blocks, max_std_dev, min_reads, 1);

  if (block_list == NULL)
    {
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

      free(block_list);
    }

  clock_gettime(TIMER_TYPE, &timee);
  if (verbosity > 0)
    {
      diff_time(&res, times, timee);
      fprintf(stderr, "wall time: %lis.%lims.%liµs.%lins\n", res.tv_sec,
          res.tv_nsec/1000000, res.tv_nsec/1000%1000,
          res.tv_nsec%1000);
    }
  if (verbosity > 0)
    {
      long double sum = 0.0;
      long long reads = 0;
      struct block_info_t single_block;

      bi_init(&single_block);

      for (size_t i=0; i < blocks; i++)
        {
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

      fprintf(stderr, "read %zi blocks (%lli errors, %lli samples)\n", 
          blocks, errors, reads);

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
  for (int i=0; i< blocks; i++)
    {
      if (bi_is_valid(&block_info[i]) == 0)
        sum_invalid++;

      double avg;

      if (bi_num_samples(&block_info[i]) < 5)
        avg = bi_average(&block_info[i]);
      else
        avg = bi_trunc_average(&block_info[i], 0.25);

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
          "<25ms: %lli\n<50ms: %lli\n<80ms: %lli\n>80ms: %lli\n",
        vvfast, vfast, fast, normal, slow, vslow, vvslow);
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

      long double raw_sum = 0.0;
      long long samples = 0;

      for (int i=0; i< blocks; i++)
        {
          long double partial_sum = 0.0;

          for (int j=0; j< bi_num_samples(&block_info[i]); j++)
            {

              double avg = bi_get_times(&block_info[i])[j];
              partial_sum += avg;
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
      write_to_file(output, block_info, blocks);
    }

  free(dev_stat_path);
  for(int i=0; i< number_of_blocks; i++)
    bi_clear(&block_info[i]);
  free(block_info);
  free(ibuf_free);
  return 0;
}
