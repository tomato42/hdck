/** hdck - hard drive low-level error and badsector checking 
 * 
 * Copyright (C) 2010  Hubert Kario
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 * uses parts of code from GNU dd 
 * Copyright (C) 2002-9 Bruce Allen <smartmontools-support@lists.sourceforge.net>
 * Copyright (C) 2008-9 Christian Franke <smartmontools-support@lists.sourceforge.net>
 * Copyright (C) 2000 Michael Cornwell <cornwell@acm.org>
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
#include "block_info.h"
#define TIMER_TYPE CLOCK_REALTIME 
#ifdef __GNUC__
#define PURE_FUNCTION  __attribute__ ((pure))
#else
#define PURE FUNCTION
#endif
#define CLEAR_LINE "\033[2K"
#define CLEAR_LINE_END "\033[K"

/// struct for holding current version number in computer readable form
const struct version_t {
    const int major;
    const int minor;
    const int revision;
} version = {0, 2, 6};

int pagesize = 4096;
size_t sectors = 256; ///< number of sectors read per sample
int verbosity = 0;
int detailed_uncertain = 1;

int exclusive = 0; ///< use exclusive file access (O_EXCL)
int nodirect = 0; ///< don't use O_DIRECT access
int noaffinity = 0; ///< don't set CPU affinity
int nortio = 0; ///< don't change process scheduling to RT
int write_individual_times_to_file = 1; ///< TODO - cmd option
int bad_sector_warning = 1; ///< whatever the user was warned about badsectors

/// minimal number of reads for a sector to be qualified as valid
size_t min_reads = 0; 
/// maximal number of re-read tries before the algorith gives up
size_t max_reads = 0; 
/// maximal standard deviation accepted for a block
double max_std_dev = 0;
/// handle for log file
FILE* flog = NULL;

size_t disk_cache_size = 32;

/// list of sectors to read
struct block_list_t {
    off_t off;
    off_t len;
};

enum {
    PRINT_TIMES = 1,
    PRINT_SYMBOLS
};

/** Move cursor up */
char*
cursor_up(int x)
{
  static char ret[16];
  int error;
  error = snprintf(ret, 16, "\033[%iA", x);
  if (error > 16 || error < 0)
    {
      fprintf(stderr, "cursor_up: parameter too large\n");
      exit(EXIT_FAILURE);
    }
  return ret;
}

/** Move cursor down */
char*
cursor_down(int x)
{
  static char ret[16];
  int error;
  error = snprintf(ret, 16, "\033[%iB", x);
  if (error > 16 || error < 0)
    {
      fprintf(stderr, "cursor_down: parameter too large\n");
      exit(EXIT_FAILURE);
    }
  return ret;
}

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
  printf("-l, --log FILE      log file to use\n");
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
  printf("--disk-cache NUM    size of the on-board disk cache in MiB (default"
      " 32)\n");
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
print_version(void)
{
  printf("hdck %i.%i.%i\n", version.major, version.minor, version.revision);
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
    err(EXIT_FAILURE, "scheduler");
}

void
set_affinity(void)
{
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set); // zero the CPU set
  CPU_SET(0, &cpu_set); // add first cpu to the set
  if (sched_setaffinity(0,sizeof(cpu_set_t), &cpu_set) <0)
    err(EXIT_FAILURE, "affinity");
}

void
set_rt_ioprio(void)
{
  if (ioprio_set(IOPRIO_WHO_PROCESS, 
               0, 
               IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, 0)
      ) != 0)
  err(EXIT_FAILURE, "ioprio: can't make process IO class real-time");
}


/// get file size
off_t
get_file_size(int dev_fd)
{
  struct stat file_stat;
  off_t filesize;
  if (fstat(dev_fd, &file_stat) == -1)
    err(EXIT_FAILURE, "fstat");
  if (S_ISREG(file_stat.st_mode))
    {
      filesize = file_stat.st_size;
      if (verbosity > 2)
        {
          printf("file size: %lli bytes\n", file_stat.st_size);
        }
      if (flog != NULL)
        fprintf(flog, "device size: %lli bytes\n", file_stat.st_size);
    }
  else if (S_ISBLK(file_stat.st_mode))
    {
      if (ioctl(dev_fd, BLKGETSIZE64, &filesize) == -1)
        err(EXIT_FAILURE, "ioctl: BLKGETSIZE64");
      if (verbosity > 2)
        printf("file size: %lli bytes\n", filesize);
      if (flog != NULL)
        fprintf(flog, "device size: %lli bytes\n", filesize);
    }
  else
    {
      fprintf(stderr, "%s: %s: File is neither device file nor regular file\n", 
          __FILE__, "main");
      exit(EXIT_FAILURE);
    }
  return filesize;
}


// readlink wrapper
char *
readlink_malloc(char *filename)
{
  size_t size = 100;

  while (1)
    {
      char *buffer = (char *) malloc (size);
      if (buffer == NULL)
        err(EXIT_FAILURE, "malloc");

      size_t nchars = readlink (filename, buffer, size);
      if (nchars < size)
        {
          if (nchars < 0)
            err(EXIT_FAILURE, "readlink");
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
          err(EXIT_FAILURE, "malloc");
        }
      ret = readlink(link, buf, bufsize);

      if (ret == -1)
        err(EXIT_FAILURE, "readlink");

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

void
print_block_list(struct block_list_t* block_list)
{
  size_t i = 0;

  while (!(block_list[i].off == 0 && block_list[i].len == 0))
    {
      printf("%zi: %lli %lli\n", i, block_list[i].off, block_list[i].len);
      i++;
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
    err(EXIT_FAILURE, "stat");

  if (S_ISLNK(file_stat.st_mode))
    {
      filename = read_link(filename);
    }

  if(stat(filename, &file_stat) == -1)
    err(EXIT_FAILURE, "stat");

  if (S_ISLNK(file_stat.st_mode))
    {
      err(EXIT_FAILURE, "circular reference");
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
    err(EXIT_FAILURE, "malloc");

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
          err(EXIT_FAILURE, "stat");
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
  size_t read_bytes;
  fd = open(filepath, O_RDONLY);
  if (fd < 0)
    {
      err(EXIT_FAILURE, "open");
    }
  read_bytes = read(fd,buf, 4096);
  if (read_bytes < 0)
    err(EXIT_FAILURE, "get_read_writes: read");
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
    err(EXIT_FAILURE, "read_blocks1: len=%lli", len);

  buffer = malloc(sectors*512+pagesize);
  if (buffer == NULL)
    err(EXIT_FAILURE, "read_blocks2");
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

  for (size_t i=0; i < disk_cache; i++)
    {
      nread = read(fd, buffer, sectors*512);

      if (nread < 0)
        {
          fprintf(stderr, "E");
          bad_sectors = 1;
          
          if (bad_sector_warning)
            {
              fprintf(stderr, "%s\nBAD SECTORS! Reads may not be accurate!%s\n",
                  CLEAR_LINE_END, CLEAR_LINE_END);
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
          fprintf(stderr, "%s\nBAD SECTORS! Reads may not be accurate!%s\n",
              CLEAR_LINE_END, CLEAR_LINE_END);
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
      exit(EXIT_FAILURE);
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
            err(EXIT_FAILURE, "read_blocks3");

          write(2, "E", 1);

          bi_add_error(&block_info[no_blocks]);
          bad_sectors = 1;

          if (bad_sector_warning)
            {
              fprintf(stderr, "\nBAD SECTORS! Reads may not be accurate!%s\n",
                  CLEAR_LINE_END);
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
  for(size_t i=0; i < len; i++)
    bi_clear(&block_info[i]);
  free(block_info);
  return NULL;
}

/**
 * @param block_list list to compact
 * @param glob globbing parameter -- how far can blocks be to be bound together
 */
struct block_list_t*
compact_block_list(struct block_list_t* block_list, size_t glob)
{
  struct block_list_t* ret = NULL;
  size_t block_list_len = 0;
  size_t ret_len = 0;

  while (!(block_list[block_list_len].off == 0 &&
           block_list[block_list_len].len == 0))
    block_list_len++;

  ret = calloc(sizeof(struct block_list_t), block_list_len+1);
  if (ret == NULL)
    err(EXIT_FAILURE, "compact_block_list");

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
      if (block_list[i].off <= ret[ret_len-1].off + glob)
        {
          ret[ret_len-1].len = block_list[i].off + block_list[i].len
              - ret[ret_len-1].off; 
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
 * @param soft_delay (flag) ignore blocks that don't have a single read above 
 *      the delay (are unlikely to be really bad)
 * @param certain_bad (flag) add blocks that are certainly bad
 * @return null if there were no blocks meeting the criteria, null terimated list
 * otherwise
 */
struct block_list_t*
find_bad_blocks(struct block_info_t* block_info, size_t block_info_len,
    float min_std_dev, size_t min_reads, size_t glob, off_t offset, double delay,
    int soft_delay, int certain_bad)
{
  struct block_list_t* block_list;
  struct block_list_t* ret;
  size_t uncertain = 0;

  if (offset > block_info_len || offset < 0)
    return NULL;

  block_list = calloc(sizeof(struct block_list_t), block_info_len);
  if (block_list == NULL)
    err(EXIT_FAILURE, "find_uncertain_blocks");

  // find uncertain blocks
  for (size_t block_no=offset; block_no < block_info_len; block_no++)
    {
      if (!bi_is_initialised(&block_info[block_no]))
        continue;

      // re-read blocks that didn't recive their share of proper reads
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
            bi_max(&block_info[block_no]) < delay / 2.0
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

      // do not re-read blocks if we look for uncertain only and standard 
      // deviation is low
      if (certain_bad == 0 && 
          bi_int_rel_stdev(&block_info[block_no]) <= min_std_dev)
            continue;

      if (!soft_delay &&
          bi_max(&block_info[block_no]) < delay)
        continue;

      if (bi_int_average(&block_info[block_no]) > delay ||
          bi_int_rel_stdev(&block_info[block_no]) > min_std_dev)
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

          if (sum/5.0*4 < bi_average(&block_info[block_no]))
            {
              block_list[uncertain].off = block_no;
              block_list[uncertain].len = 1;
              uncertain++;
              continue;
            }
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

/** Find blocks that could be bad
 * @param block_info block statistics
 * @param block_info_len block_info length
 * @param min_std_dev minimal standard deviation for a block to be cosidered 
 * uncertain
 * @param min_reads minimal number of reads for a block to be considered
 * checked
 * @param glob how close sectors have to be near them to be bundled together
 * @param offset minimal address to consider
 * @param delay read delay for the block to be considered uncertain 
 * @param soft_delay (flag) ignore blocks that don't have a single read above 
 *      the delay (are unlikely to be really bad)
 * @return null if there were no blocks meeting the criteria, null terimated list
 * otherwise
 */
struct block_list_t*
find_uncertain_blocks(struct block_info_t* block_info, size_t block_info_len,
    float min_std_dev, size_t min_reads, size_t glob, off_t offset, double delay,
    int soft_delay)
{
  return find_bad_blocks(block_info, block_info_len, min_std_dev,
      min_reads, glob, offset, delay, soft_delay, 0);
}

void
write_to_file(char *file, struct block_info_t* block_info, size_t len)
{
  FILE* handle;

  handle = fopen(file, "w+");
  if (handle == NULL)
    err(EXIT_FAILURE, "write_to_file");

  fprintf(handle, "# sector_number, avg, trunc_avg, std_dev, rel_st_dev, "
      "trunc_st_dev, num_of_samples, samples\n");

  for(size_t i=0; i< len; i++)
    {
      if (!bi_is_initialised(&block_info[i]))
        continue;
      double trunc_avg;
      if (bi_num_samples(&block_info[i]) < 5)
        trunc_avg = bi_average(&block_info[i]);
      else
        trunc_avg = bi_trunc_average(&block_info[i], 0.25);

      fprintf(handle, "%zi\t%f\t%f\t%f\t%f\t%f\t%zi",
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
          for(size_t l=0; l<bi_num_samples(&block_info[i]); l++)
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
    err(EXIT_FAILURE,"write_list_to_file");

  for(size_t i=0; !(block_list[i].off == 0 && block_list[i].len == 0); i++)
    if(fprintf(handle, "%lli %lli\n", block_list[i].off * sectors,
        (block_list[i].off + block_list[i].len) * sectors) == 0)
      err(EXIT_FAILURE, "write_list_to_file");

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
    err(EXIT_FAILURE, "read_list_from_file");

  block_list = calloc(sizeof(struct block_list_t), alloc_elements);
  if (block_list == NULL)
    err(EXIT_FAILURE, "read_list_from_file");

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
            err(EXIT_FAILURE, "read_list_from_file");
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
  static size_t max_len = 4; ///< globbing param for compacting
  /// disk cache size in blocks
  off_t disk_cache = disk_cache_size * 1024 * 1024 / sectors / 512;
  struct timespec start_time, end_time, res; ///< expected time calculation
  size_t block_number=0; ///< position in the block_list
  struct block_info_t* block_data; ///< stats for sectors read

  if (verbosity > 6)
    print_block_list(block_list);
  tmp_block_list = compact_block_list(block_list, max_len * 2);
  if (verbosity > 6)
    {
      printf("after compacting:\n");
      print_block_list(tmp_block_list);
    }

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
        printf("processing block no %zi of length %zi\n", 
            offset, length);

      block_data = read_blocks(dev_fd, dev_stat_path, offset, length);

      blocks_read += length + disk_cache + 3;
      
      if (block_data == NULL || 
          (block_data != NULL && !bi_is_valid(&block_data[0])))
        {
          if (verbosity > 2)
            printf("re-read of block %zi (length %zi) interrupted\n", 
                offset, length);
          else if (verbosity > 0)
            printf("!%s", CLEAR_LINE_END);// interrupted
          fflush(stdout);

          block_number++;
        }
      else if (verbosity <= 3 && verbosity > 1)
        printf(".%s", CLEAR_LINE_END); // OK
      fflush(stdout);

      // print statistics
      if (verbosity >= 0 && (block_number % 10 == 0 || blocks_read % 32 == 0 
          || blocks_read == total_blocks || blocks_read == 2))
        {
          clock_gettime(TIMER_TYPE, &end_time);
          diff_time(&res, start_time, end_time);

          double percent;
          percent = blocks_read * 1.0/total_blocks;

          long long time_to_go;
          time_to_go = time_double(res) / percent;
          if (verbosity > 1)
            printf("\n");

          printf("reread %.2f%% done "
              "in %02li:%02li:%02li, expected time:"
              "%02lli:%02lli:%02lli%s\r",
              percent * 100,
              res.tv_sec/3600, res.tv_sec/60%60, res.tv_sec%60,
              time_to_go/3600, time_to_go/60%60, time_to_go%60,
              CLEAR_LINE_END);
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
              if (verbosity > 7)
                print_block_list(tmp_block_list);
              free(tmp_block_list);

              tmp_block_list = compact_block_list(block_list, max_len);
              if (verbosity > 7)
                {
                  printf("after compacting:\n");
                  print_block_list(tmp_block_list);
                }

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
              if (verbosity > 7)
                print_block_list(tmp_block_list);
              free(tmp_block_list);

              tmp_block_list = compact_block_list(block_list, max_len);
              if (verbosity > 7)
                {
                  printf("after compacting:\n");
                  print_block_list(tmp_block_list);
                }

              if (tmp_block_list == NULL)
                break;

              if (tmp_block_list == NULL)
                err(EXIT_FAILURE, "re_read");

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
      for (size_t i=0; i< length; i++)
        bi_clear(&block_data[i]);

      free(block_data);
      block_number++;
    }

  if (tmp_block_list)
    free(tmp_block_list);
}

void
perform_re_reads(int dev_fd, char* dev_stat_path, struct block_info_t* block_info,
    size_t block_info_size, size_t re_reads, double max_std_dev, size_t min_reads,
    double delay)
{
  struct block_list_t* block_list;

  for(size_t tries=0; tries < re_reads; tries++)
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
                block_info, block_info_size, max_std_dev, min_reads, 1, 0, delay,
                1);


          if (block_list == NULL)
            {
              if (verbosity >2)
                printf("no uncertain blocks found\n");
              break;
            }

          size_t block_number=0;
          // end of list is marked by NULL, NULL
          if (verbosity > 2)
            printf("current uncertain blocks:\n");

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

                      printf("rel std dev for block %zi: %3.9f"
                          ", average: %f, valid: %s, samples: %zi%s\n", 
                          i, 
                          stdev,
                          bi_average(&block_info[i]),
                          (bi_is_valid(&block_info[i]))?"yes":"no",
                          bi_num_samples(&block_info[i]),
                          CLEAR_LINE_END);
                    }

                }
              block_number++;
            }

          printf("re-reading %zi uncertain blocks%s\n", block_number, 
              CLEAR_LINE_END);
          free(block_list);
        }


      if (min_reads == 1)
        block_list = find_uncertain_blocks(
            block_info, block_info_size, max_std_dev, min_reads, 1, 0, delay, 1);
      else
        block_list = find_uncertain_blocks(
            block_info, block_info_size, max_std_dev, min_reads, 1, 0, delay, 1);

      if (block_list == NULL)
        break;

      read_block_list(dev_fd, block_list, block_info, dev_stat_path, 
          block_info_size);

      if (verbosity <= 3 && verbosity >= 0)
        printf("\n");

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
    char* dev_stat_path, size_t loops, int sector_times, off_t max_sectors,
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
  size_t loop=0; ///< loop number
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
          fprintf(stderr, "hdck: main: wrong offset, got %lli expected %lli\n",
              lseek(dev_fd, (off_t)0, SEEK_CUR),
              ((off_t)blocks) * sectors * 512);
          exit(EXIT_FAILURE);
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
            err(EXIT_FAILURE, NULL);
          else
            {
              diff_time(&res, time1, time2); 
              nread = 1; // don't exit loop
              write(2, "E", 1);
              bi_add_error(&block_info[blocks]);

              if (bad_sector_warning)
                {
                  printf("BAD SECTORS! Reads may not be accurate!\n");
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
            printf("block %zi (LBA: %lli-%lli) interrupted%s\n", blocks,
               ((off_t)blocks) * sectors, ((off_t)blocks+1)*sectors-1,
               CLEAR_LINE_END);
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
                    printf("block: %zi, samples: %zi, average: "
                        "%f, rel stdev: %f, trunc rel stdev: %f%s\n", 
                        blocks,
                        bi_num_samples(&block_info[blocks]),
                        bi_average(&block_info[blocks]),
                        bi_rel_stdev(&block_info[blocks]),
                        bi_int_rel_stdev(&block_info[blocks]),
                        CLEAR_LINE_END);
                }
              else
                {
                  // subsequent valid or invalid reads
                  bi_add_time(&block_info[blocks], time_double(res));

                  if (verbosity > 10)
                    printf("block: %zi, samples: %zi, average: "
                        "%f, rel stdev: %f, trunc rel stdev: %f%s\n", 
                        blocks,
                        bi_num_samples(&block_info[blocks]),
                        bi_average(&block_info[blocks]),
                        bi_rel_stdev(&block_info[blocks]),
                        bi_int_rel_stdev(&block_info[blocks]),
                        CLEAR_LINE_END);
                }
              
              diff_time(&res, time1, time2);

            }
            
          next_is_valid = 1;
        }

      if (res.tv_nsec < 2000000 && res.tv_sec == 0) // very very fast read
        {
          if (sector_times == PRINT_SYMBOLS) write(1,"_",1);
        }
      else if (res.tv_nsec < 5000000 && res.tv_sec == 0) // very fast read
        {
          if (sector_times == PRINT_SYMBOLS) write(1,".",1);
        }
      else if (res.tv_nsec < 10000000 && res.tv_sec == 0) // fast read
        {
          if (sector_times == PRINT_SYMBOLS) write(1,",",1);
        }
      else if (res.tv_nsec < 25000000 && res.tv_sec == 0) // normal read
        {
          if (sector_times == PRINT_SYMBOLS) write(1,"-",1);
        }
      else if (res.tv_nsec < 50000000 && res.tv_sec == 0) // slow read
        {
          if (sector_times == PRINT_SYMBOLS) write(1,"+",1);
        }
      else if (res.tv_nsec < 80000000 && res.tv_sec == 0) // very slow read
        {
          if (sector_times == PRINT_SYMBOLS) write(1,"#",1);
        }
      else // very very slow read
        {
          if (sector_times == PRINT_SYMBOLS) write(1,"!",1);
        }

      if (sector_times == PRINT_TIMES)
        printf("%li r:%lli rs: %lli w:%lli%s\n",
            res.tv_nsec/1000+res.tv_sec*1000000, 
            read_s, 
            read_sec_s, 
            write_s,
            CLEAR_LINE_END); 

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
          time_to_go = (res.tv_sec*1.0) / 
                            (percent/min_reads + loop*1.0/min_reads);

          printf("hdck status:%s\n", CLEAR_LINE_END);
          printf("============%s\n", CLEAR_LINE_END);
          printf("Loop:          %i of %i%s\n", loop+1, min_reads, 
              CLEAR_LINE_END);
          printf("Progress:      %.2f%%, %.2f%% total%s\n",
              percent*100, (percent/min_reads + loop*1.0/min_reads) * 100,
              CLEAR_LINE_END);
          printf("Read:          %lli sectors of %lli%s\n", ((off_t)blocks)*sectors,
              filesize, CLEAR_LINE_END);
          printf("Speed:         %.3fMiB/s, average: %.3fMiB/s%s\n", cur_speed,
              speed, CLEAR_LINE_END);
          printf("Elapsed time:  %02li:%02li:%02li%s\n",
              res.tv_sec/3600, res.tv_sec/60%60, res.tv_sec%60,
              CLEAR_LINE_END);
          printf("Expected time: %02lli:%02lli:%02lli%s",
              time_to_go/3600, time_to_go/60%60, time_to_go%60,
              CLEAR_LINE_END);
          printf("\r%s", cursor_up(7));
          fflush(stdout);
/*          fprintf(stderr,"\033[2Kread %lli sectors, %.3fMiB/s (%.3fMiB/s), "
              "%.2f%% (%.2f%%), "
              "in %02li:%02li:%02li, loop %zi of %zi, "
              "expected time: %02lli:%02lli:%02lli\r",
             ((off_t)blocks)*sectors,
             cur_speed,
             speed,
             percent*100, (percent/min_reads + loop*1.0/min_reads) * 100,
             res.tv_sec/3600, res.tv_sec/60%60, res.tv_sec%60,
             loop+1, min_reads,
             time_to_go/3600, time_to_go/60%60, time_to_go%60);*/
        }

      if (nread == 0 || nread == -1 || blocks >= number_of_blocks
          || (max_sectors != 0 && blocks * sectors >= max_sectors ))
        {
          long long high_dev=0;
          long long sum_invalid=0;
          loop++;
          // check standard deviation for blocks
          for (size_t i =0; i < blocks; i++)
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
                  verbosity >= 0 && 
                  !(loop < min_reads) && 
                  ( high_dev/(blocks*1.0) > 0.25 
                    || sum_invalid/(blocks*1.0) > 0.10)
                 )
                printf("low confidance for the results, "
                    "re-reading whole disk%s\n", CLEAR_LINE_END);

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
              printf("Warning: read whole disk %zi times, still "
                  "can't get high confidence%s\n", max_reads, CLEAR_LINE_END);
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
  char* log_path = NULL; ///< path to file to write log to
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
        {"log", 0, 0, 'l'}, // 23
        {0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "f:xhbvo:?w:r:l:",
             long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 0:
        if (verbosity > 5)
          {
            printf("option %s%s\n", long_options[option_index].name,
                CLEAR_LINE_END);
            if (optarg)
                printf(" with arg %s%s\n", optarg, CLEAR_LINE_END);
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
            print_version();
            exit(EXIT_SUCCESS);
          }
        break;

    case 'v':
        if (verbosity > 5 ) printf("option v%s\n", CLEAR_LINE_END);
        verbosity++;
        break;

    case 'x':
        if (verbosity > 5) printf("option x%s\n", CLEAR_LINE_END);
        exclusive = 1;
        break;

    case 'f':
        filename = optarg;
        if (verbosity > 5) printf("option f with value '%s'%s\n", optarg,
            CLEAR_LINE_END);
        break;

    case 'o':
        output = optarg;
        if (verbosity > 5) printf("option o with value '%s'%s\n", optarg,
            CLEAR_LINE_END);
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

    case 'l':
        log_path = optarg;
        break;

    case 'h':
    case '?':
        usage();
        exit(EXIT_SUCCESS);
        break;

    default:
        printf("?? getopt returned character code 0%o ??%s\n", c, 
            CLEAR_LINE_END);
        exit(EXIT_FAILURE);
    }
  }

  if (optind < argc)
    {
      printf("trailing options: ");
      while (optind < argc)
          printf("%s ", argv[optind++]);
      printf("%s\n", CLEAR_LINE_END);
      usage();
      exit(EXIT_FAILURE);
    }

  if (filename == NULL)
    {
      printf("Missing -f parameter!%s\n", CLEAR_LINE_END);
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

  if (log_path != NULL)
    {
      flog = fopen(log_path, "w+");
      if (flog == NULL)
        err(EXIT_FAILURE, "log: open");

      fprintf(flog, "hdck v.%i.%i.%i log start\n", 
          version.major, version.minor, version.revision);
      fprintf(flog, "=========================\n");
      fprintf(flog, "Test parameters:\n");
      fprintf(flog, "min reads: %zi\n", min_reads);
      fprintf(flog, "max reads: %zi\n", max_reads);
      fprintf(flog, "max standard deviation: %f\n", max_std_dev);
      if(exclusive)
        {
          fprintf(flog, "Exclusive access specified\n");
        }
      if(read_sectors_from_file != NULL)
        {
          fprintf(flog, "Testing only ranges specified in file %s\n", 
              read_sectors_from_file);
        }
      fprintf(flog, "Testing device at %s\n", filename);
      fprintf(flog, "Assuming %.0frpm with %iMiB cache\n", 
          1000/rotational_delay*60,
          disk_cache_size);
    }

  if (min_reads > max_reads)
    {
      fprintf(stderr, "Warning: min_reads bigger than max_reads, "
          "correcting%s\n", CLEAR_LINE_END);
      if (flog != NULL)
        fprintf(flog, "min reads bigger than max reads, correcting\n");
      max_reads = min_reads;
    }

  max_reads -= min_reads;

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
      err(EXIT_FAILURE, "open");
    }

  filesize = get_file_size(dev_fd);

  // we can't realiably read last sector anyway, so round the disk size down
  filesize = floorl(filesize*1.L/512/sectors)*512*sectors;
  if (!filesize)
    {
      fprintf(stderr, "Device too small, needs to be at least %lli bytes in "
          "size\n", ((off_t)512)*sectors);
      exit(EXIT_FAILURE);
    }

  if (filesize / 512 / sectors * 2 > (off_t)SIZE_MAX)
    {
      fprintf(stderr, "File too big, devices this big are supported only on "
          "64 bit OSs\n");
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
      fprintf(stderr, "Allocation error, tried to allocate %lli bytes:", 
          number_of_blocks * sizeof(struct block_info_t));
      err(EXIT_FAILURE, "calloc");
    }

  fsync(dev_fd);

  if (!noflush)
    {
      // Attempt to free all cached pages related to the opened file
      if (posix_fadvise(dev_fd, 0, 0, POSIX_FADV_DONTNEED) < 0)
        err(EXIT_FAILURE, NULL);
      if (posix_fadvise(dev_fd, 0, 0, POSIX_FADV_NOREUSE) < 0)
        err(EXIT_FAILURE, NULL);
    }

  if (verbosity > 2)
    {
      printf("min-reads: %zi, max re-reads: %zi, max rel std dev %f, "
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
  time_t current_time;
  current_time = time(NULL);
  if (flog != NULL)
    fprintf(flog, "\nbegin testing: %s\n", asctime(localtime(&current_time)));
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

      for (size_t i=0; i<min_reads; i++)
        read_block_list(dev_fd, block_list, block_info, dev_stat_path,
            number_of_blocks);

      free(block_list);
    }
  if (verbosity >= 0)
    printf("\r%s\n", cursor_down(7));
  current_time = time(NULL);
  if(flog != NULL)
    fprintf(flog, "end of main loop: %s\n", asctime(localtime(&current_time)));

  perform_re_reads(dev_fd, dev_stat_path, block_info, number_of_blocks,
      max_reads, max_std_dev, min_reads, rotational_delay);

  current_time = time(NULL);
  if(flog != NULL)
    fprintf(flog, "end of rereads: %s\n", asctime(localtime(&current_time)));


  // print uncertain and bad blocks
  struct block_list_t* block_list;

  block_list = find_bad_blocks(
      block_info, number_of_blocks, max_std_dev, min_reads, 1, 0,
      rotational_delay, 0, 1);

  if (verbosity >= 0)
    printf("\nhdck results:%s\n"
             "=============%s\n", CLEAR_LINE_END, CLEAR_LINE_END);
  if(flog != NULL)
    fprintf(flog, "results:\n");

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

      if (verbosity >= 0)
        printf("no problematic blocks found!%s\n", CLEAR_LINE_END);
      if(flog != NULL)
        fprintf(flog, "no problematic blocks found!\n");
    }
  else
    {
      if (verbosity >= 0)
        printf("possible latent bad sectors or silent realocations:%s\n", 
            CLEAR_LINE_END);
      if (flog != NULL)
        fprintf(flog, "possible latent bad sectors or silent realocations:\n");

      size_t block_number=0;
      while (!(block_list[block_number].off == 0 && 
          block_list[block_number].len == 0))
        {
          size_t start = block_list[block_number].off,
                end = start + block_list[block_number].len;

          for(size_t i= start; i< end; i++)
            {
              double stdev = bi_int_rel_stdev(&block_info[i]);

              if (verbosity >= 0)
                printf("block %zi (LBA: %lli-%lli) rel std dev: %3.9f"
                  ", average: %f, valid: %s, samples: %zi%s\n", 
                  i,
                  ((off_t)i)*sectors,((off_t)i+1)*sectors-1,
                  stdev,
                  bi_average(&block_info[i]),
                  (bi_is_valid(&block_info[i]))?"yes":"no",
                  bi_num_samples(&block_info[i]),
                  CLEAR_LINE_END);
              /*else
                printf("%lli\t%lli\n", ((off_t)i)*sectors,
                  ((off_t)i+1)*sectors);*/

              if (flog != NULL)
                fprintf(flog, "block %zi (LBA: %lli-%lli) rel std dev: %3.9f"
                  ", average: %f, valid: %s, samples: %zi\n", 
                  i,
                  ((off_t)i)*sectors,((off_t)i+1)*sectors-1,
                  stdev,
                  bi_average(&block_info[i]),
                  (bi_is_valid(&block_info[i]))?"yes":"no",
                  bi_num_samples(&block_info[i]));
            }
          block_number++;
        }

      fflush(stdout);

      if (verbosity >= 0)
        printf("%zi uncertain blocks found%s\n", block_number,
            CLEAR_LINE_END);
      if (flog != NULL)
        fprintf(flog, "%zi uncertain blocks found\n", block_number);

      if (write_uncertain_to_file != NULL)
        write_list_to_file(write_uncertain_to_file, block_list);

      free(block_list);
    }

  clock_gettime(TIMER_TYPE, &timee);

  diff_time(&res, times, timee);
  if (verbosity >= 0)
    printf("\nwall time: %lis.%lims.%liµs.%lins%s\n", res.tv_sec,
        res.tv_nsec/1000000, res.tv_nsec/1000%1000,
        res.tv_nsec%1000, CLEAR_LINE_END);

  if (flog != NULL)
    fprintf(flog, "\nwall time: %lis.%lims.%liµs.%lins\n", res.tv_sec,
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

  if (verbosity >= 0)
    printf("sum time: %.0fs.%.0fms.%.0fµs\n",
      sec,
      msec,
      usec);
  if (flog != NULL)
    fprintf(flog, "sum time: %.0fs.%.0fms.%.0fµs\n",
      sec,
      msec,
      usec);

  if (verbosity >= 0)
    printf("tested %lli blocks (%lli errors, %lli samples)\n", 
        number_of_blocks, errors, reads);
  if (flog != NULL)
    fprintf(flog, "tested %lli blocks (%lli errors, %lli samples)\n", 
        number_of_blocks, errors, reads);


  sum = bi_average(&single_block);

  sec = floor(sum / 1000);
  msec = floor(sum - sec * 1000);
  usec = floor((sum - sec * 1000 - msec)*1000);

  if (verbosity >= 0)
    printf("mean block time: %.0fs.%.0fms.%.0fµs\n",
      sec,
      msec,
      usec);
  if (flog != NULL)
    fprintf(flog, "mean block time: %.0fs.%.0fms.%.0fµs\n",
      sec,
      msec,
      usec);

  if (verbosity >= 0)
    printf("std dev: %.9f(ms)\n",
        bi_stdev(&single_block));
  if (flog != NULL)
    fprintf(flog, "std dev: %.9f(ms)\n",
        bi_stdev(&single_block));

  bi_clear(&single_block);

  long long sum_invalid=0;
  vvfast=0; /* less than one fourth th rotational delay */
  vfast=0;  /* less than half the rotational delay */
  fast=0;   /* less than rotational delay */
  normal=0; /* less than 2 * rotational delay */
  slow=0;   /* less than 4 * rotational delay */
  vslow=0;  /* less than 6 * rotational delay */
  vvslow=0; /* more than 6 * rotational delay */
  errors=0;
  for (size_t i=0; i< number_of_blocks; i++)
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

      if (avg < rotational_delay / 4) // very very fast read
        {
          ++vvfast;
        }
      else if (avg < rotational_delay / 2) // very fast read
        {
          ++vfast;
        }
      else if (avg < rotational_delay) // fast read
        {
          ++fast;
        }
      else if (avg < rotational_delay * 2) // normal read
        {
          ++normal;
        }
      else if (avg < rotational_delay * 4) // slow read
        {
          ++slow;
        }
      else if (avg < rotational_delay * 6) // very slow read
        {
          ++vslow;
        }
      else // very very slow read
        {
          ++vvslow;
        }
    }

  if (verbosity >= 0)
    printf("Number of invalid measures because of detected "
      "interrupted reads: %lli\n", sum_invalid);
  if (flog != NULL)
    fprintf(flog, "Number of invalid measures because of detected "
        "interrupted reads: %lli\n", sum_invalid);

  if (verbosity >= 0)
    printf("Individual block statistics:\n<%02.2fms: %lli\n"
        "<%02.2fms: %lli\n<%2.2fms: %lli\n<%2.2fms: %lli\n<%2.2fms: %lli\n"
        "<%2.2fms: %lli\n>%2.2fms: %lli\nERR: %lli\n",
      rotational_delay / 4, vvfast, rotational_delay / 2, vfast,
      rotational_delay, fast, rotational_delay * 2, normal,
      rotational_delay * 4, slow, rotational_delay * 6, vslow, 
      rotational_delay * 6, vvslow, errors);
  if (flog != NULL)
    fprintf(flog, "Individual block statistics:\n<%02.2fms: %lli\n"
        "<%02.2fms: %lli\n<%2.2fms: %lli\n<%2.2fms: %lli\n<%2.2fms: %lli\n"
        "<%2.2fms: %lli\n>%2.2fms: %lli\nERR: %lli\n",
      rotational_delay / 4, vvfast, rotational_delay / 2, vfast,
      rotational_delay, fast, rotational_delay * 2, normal,
      rotational_delay * 4, slow, rotational_delay * 6, vslow, 
      rotational_delay * 6, vvslow, errors);

  if (verbosity >= 0)
    printf("\n");
  printf("Disk status: ");
  if (flog != NULL)
    fprintf(flog, "\nDisk status: ");
  if (errors != 0)
    {
      printf("FAILED\n"
          "CAUTION! Bad sectors detected, copy data off this "
          "disk AS SOON AS POSSIBLE!\n");
      if (flog != NULL)
        fprintf(flog, "FAILED\n"
            "CAUTION! Bad sectors detected, copy data off this "
            "disk AS SOON AS POSSIBLE!\n");
    }
  else if (vvslow != 0)
    {
      printf("CRITICAL\n"
          "CAUTION! Sectors that required more than 6 read "
          "attempts detected, drive may be ALREADY FAILING!\n");
      if (flog != NULL)
        fprintf(flog, "CRITICAL\n"
            "CAUTION! Sectors that required more than 6 read "
            "attempts detected, drive may be ALREADY FAILING!\n");
    }
  else if (vslow != 0)
    {
      printf("very bad\n"
          "sectors that required more than 4 read attempts "
          "detected!\n");
      if (flog != NULL)
        fprintf(flog, "very bad\n"
            "sectors that required more than 4 read attempts "
            "detected!\n");
    }
  else if (slow != 0)
    {
      printf("bad\n"
          "sectors that required more than 2 read attempts "
          "detected\n");
      if (flog != NULL)
        fprintf(flog, "bad\n"
            "sectors that required more than 2 read attempts "
            "detected\n");
    }
  else if ((normal * 1.0) / (number_of_blocks * 1.0) > 0.001)
    {
      printf("moderate\n"
          "high number of blocks that required more than 1 "
          "read attempt detected, drive is in moderate condition\n");
      if (flog != NULL) 
        fprintf(flog, "moderate\n"
            "high number of blocks that required more than 1 "
            "read attempt detected, drive is in moderate condition\n");
    }
  else if (normal == 0)
    {
      if ((fast * 1.0) / (number_of_blocks * 1.0) < 0.1)
        {
          printf("excellent\n");
          if (flog != NULL)
            fprintf(flog, "excellent\n");
        }
      else
        {
          printf("very good\n"
            "no blocks that required constant re-reads "
            "detected\n");
      
          if (flog != NULL)
            fprintf(flog, "very good\n"
              "no blocks that required constant re-reads "
              "detected\n");
        }
    }
  else
    {
      printf("good\n"
          "few blocks that required more than 1 read attempt "
          "detected\n");
      if (flog != NULL)
        fprintf(flog, "good\n"
          "few blocks that required more than 1 read attempt "
          "detected\n");
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

      for (size_t i=0; i< number_of_blocks; i++)
        {
          if (!bi_is_initialised(&block_info[i]))
            continue;

          long double partial_sum = 0.0;

          for (size_t j=0; j< bi_num_samples(&block_info[i]); j++)
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

      printf("\nraw read statistics:\n"); 
      printf("ERR: %lli\n2ms:  %lli\n5ms:  %lli\n10ms: %lli\n25ms: %lli\n"
          "50ms: %lli\n80ms: %lli\n80+ms: %lli\n",
          errors, vvfast, vfast, fast, normal, slow, vslow, vvslow);

      double sec = floor(raw_sum / 1000);
      double msec = floor(raw_sum - sec * 1000);
      double usec = floor((raw_sum - sec * 1000 - msec)*1000);

      printf("sum time: %.0fs.%.0fms.%.0fµs\n",
        sec,
        msec,
        usec);

      raw_sum = raw_sum / samples;

      sec = floor(raw_sum / 1000);
      msec = floor(raw_sum - sec * 1000);
      usec = floor((raw_sum - sec * 1000 - msec)*1000);

      printf("mean block time: %.0fs.%.0fms.%.0fµs\n",
        sec,
        msec,
        usec);
    }


  if (output != NULL)
    {
      write_to_file(output, block_info, number_of_blocks);
    }

  free(dev_stat_path);
  for(size_t i=0; i< number_of_blocks; i++)
    bi_clear(&block_info[i]);
  free(block_info);
  if (verbosity >= 0)
    printf("\n");
  if (flog != NULL)
    fprintf(flog, "\nhdck log end");
  if (flog != NULL)
    fclose(flog);
  return EXIT_SUCCESS;
}
