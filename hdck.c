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
#include "sg_cmds_extra.h"
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
} version = {0, 5, 0};

/** structure representing program status */
struct status_t {
    size_t sectors; /**< number of sectors read per sample */
    int verbosity; /**< verbosity level */
    /* 
     * runtime options
     */
    int exclusive; /**< whatever exclusive disk access is used */
    int noaffinity; /**< whatever CPU affinity is set */
    int nortio; /**< whatevr the process scheduling is set to real-time IO */
    off_t max_sectors; /**< maximal number of sectors to read */
    int no_rt; /**< whatever to change to real-time scheduling */
    double vvfast_lvl; /**< block read speed considered very very fast */
    double vfast_lvl;  /**< block read speed considered very fast */
    double fast_lvl;   /**< block read speed considered fast */
    double normal_lvl; /**< block read speed considered normal */
    double slow_lvl;   /**< block read speed considered slow */
    double vslow_lvl;  /**< block read speed considered very slow */
    int sector_times;  /**< how to display individual block times */
    int quick; /**< quick mode */
    int usb_mode; /**< disk is behind USB bridge */
    int ata_verify; /**< use ATA VERIFY to test disk */
    /*
     * device access modes and device parameters
     */
    int nodirect; /**< whatever the O_DIRECT is used */
    int nosync; /**< whatever to use O_SYNC flag */
    int noflush; /**< whatever to flush all buffers before run */
    /** minimal number of reads for a sector to be considered valid */
    size_t min_reads;
    /** maximal number of additional samples taken before giving up */
    size_t max_reads;
    /** maximal standard deviation accepted for a block */
    double max_std_dev;
    size_t disk_cache_size; /** size of onboard disk cache */
    double rotational_delay; /** device rotational delay */
    /** name of the device file tested */
    char* filename;
    /** path to the stat file for above device */
    char* dev_stat_path;
    /** device size */
    off_t filesize;
    /** device size in hdck blocks */
    off_t number_of_blocks;
    /* 
     * logging options 
     */
    /** whatever to write sample times to output file */
    int write_individual_times; // TODO - make it a option
    /** whatever a bad sector warning has been issued */
    int bad_sector_warning;
    /** log file handle */
    FILE* flog;
    char* output; /**< name of file for detailed sector statistics */
    /** name of file for saving uncertain sectors to file */
    char* write_uncertain_to_file;
    /** name of file to read uncertain sectors from file */
    /*
     * run statistics
     */
    long long tot_errors; /**< total number of read errors encountered */
    long long tot_vvfast; /**< total number of very very fast reads (<RD/4) */
    long long tot_vfast;  /**< total number of very fast reads      (<RD/2) */
    long long tot_fast;   /**< total number of fast reads           (<RD)   */
    long long tot_normal; /**< total number of normal reads         (<RD*2) */
    long long tot_slow;   /**< total number of slow reads           (<RD*4) */
    long long tot_vslow;  /**< total number of very slow reads      (<RD*6) */
    long long tot_vvslow; /**< total number of very very slow reads (>RD*6) */
    long double tot_sum;  /**< sum of all valid samples */
    long long tot_samples; /**< number of all samples taken */
    long long errors;     /**< number of blocks with read errors */
    long long vvfast;     /**< number of very very fast blocks */
    long long vfast;      /**< number of very fast blocks */
    long long fast;       /**< number of fast blocks */
    long long normal;     /**< number of normal blocks */
    long long slow;       /**< numebr of slow blocks */
    long long vslow;      /**< number of very slow blocks */
    long long vvslow;     /**< number of very very slow blocks */
    long long tot_interrupts; /**< total number of read interruptions */
    long long invalid;    /**< number of blocks with useless data */
    struct timespec time_end; /**< wall clock end time */
    struct timespec time_start; /**< wall clock end time */
};

// page size of this architecture
const int pagesize = 4096;

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
usage(struct status_t *st)
{
  printf("Usage: hdck [OPTIONS]\n");
  printf("Test hard drive for latent and hidden bad sectors\n");
  printf("\n");
  printf("-f, --file FILE     device file to test\n");
  printf("-x, --exclusive     use exclusive access\n");
  printf("                    (runs faster, but all partitions must be"
                                                              " unmounted)\n");
  printf("-b, --background    shorthand for --noaffinity, --nortio, --nort\n");
  printf("-o, --outfile FILE  output file for block level detailed statistics\n");
  printf("-w, --bad-sectors FILE output file for the uncertain sectors\n");
  printf("-r, --read-sectors FILE list of ranges to scan instead of whole disk\n");
  printf("-l, --log FILE      log file to use\n");
  printf("--quick             quick mode\n");
  printf("--nodirect          don't use O_DIRECT\n");
  printf("--noflush           don't flush system buffers before reading\n");
  printf("--nosync            don't use O_SYNC\n");
  printf("--noaffinity        don't set CPU affinity to 0th core/CPU\n");
  printf("--nortio            don't change IO priority to real-time\n");
  printf("--nort              don't make the process real-time\n");
  printf("--sector-symbols    print symbols representing read time of each"
                                                       " block\n");
  printf("--sector-times      print time it takes to read each group of"
                                                        " sectors (in µs)\n");
  printf("--min-reads NUM     minimal number of valid reads for a sector\n");
  printf("--max-reads NUM     maximal number of re-reads for a sector\n");
  printf("--max-std-deviation NUM minimal relative stdandard deviation for "
      "a sector to be\n");
  printf("                    considered valid (ignored)\n");
  printf("--max-sectors NUM   read at most NUM sectors\n");
  printf("--disk-cache NUM    size of the on-board disk cache in MiB (default"
      " 32)\n");
  printf("--disk-rpm NUM      disk RPM (7200 by default)\n");
  printf("--noverbose         reduce verbosity\n");
  printf("--no-usb            not testing over USB bridge\n");
  printf("--ata-verify        use ATA VERIFY command to reduce bandwidth"
      " utilisation\n");
  printf("                    (for use with USB and FireWire disks)\n");
  printf("--no-ata-verify     don\'t use ATA VERIFY command (default)\n");
  printf("-v, --verbose       be more verbose\n");
  printf("--version           write version information\n");
  printf("-h, -?              print this message\n");
  printf("\n");
  printf("This program can be run on both files and devices, though running it"
      " on top of\n");
  printf("a file is quite pointless. In most cases default settings should be"
      " OK. If your\n");
  printf("computer slows to a crawl while testing, try --nodirect and --noflush.\n");
  printf("When using -x, the program trusts the sector times more\n");
  printf("and as a result, should achive minimum confidence in less time "
      "(by using\n");
  printf("less re-reads)\n");
  printf("\n");
  printf("Default settings:\n");
  printf("min-reads: 3, max-reads: 30, max-std-deviation: 0.5\n\n");
  printf("Exclusive settings:\n");
  printf("min-reads: 1, max-reads: 20, max-std-deviation: 0.75\n\n");
  printf("Background settings:\n");
  printf("min-reads: 3, max-reads: 50, max-std-deviation: 0.5\n\n");
  printf("Quick settings:\n");
  printf("min-reads: 1, max-reads: 50, max-std-deviation: 0.75\n");
  printf("ignore blocks with time less than twice the rotational delay and"
      " validate only\n");
  printf("64 worst blocks\n");
  printf("\n");
  printf("Format for the -o option is presented in the first line of file. "
      "Block is\n");
  printf("a group of %zi sectors (%zi bytes). Consecutive lines in files for "
      "-r and\n", st->sectors, st->sectors * 512);
  printf("-w are ranges of LBAs to scan.\n");
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
    }

  res->tv_sec = tmp_s; 
  res->tv_nsec = tmp_ns; 

  return;
}

static int
__off_t_compare(const void *a, const void *b)
{
  off_t x,y;
  x = ((struct block_list_t*)a)->off;
  y = ((struct block_list_t*)b)->off;

  if (x<y)
    return -1;
  else if (x==y)
    return 0;
  else
    return 1;
}

struct block_info_t *_block_compare_block_info;

static int
_block_compare(const void *a, const void *b)
{
  off_t off_a, off_b;
  off_a = ((struct block_list_t*)a)->off;
  off_b = ((struct block_list_t*)b)->off;

  struct block_info_t *x, *y;

  x = &_block_compare_block_info[off_a];
  y = &_block_compare_block_info[off_b];

  double dec_x, dec_y;

  dec_x = bi_quantile(x, 9, 10);
  dec_y = bi_quantile(y, 9, 10);

  int val_x, val_y;

  val_x = bi_is_valid(x);
  val_y = bi_is_valid(y);

  if (val_x == 0 && val_y == 0)
    return 0;
  else if (val_x > val_y)
    return -1;
  else if (val_x < val_y)
    return 1;
  else if (dec_x < dec_y)
    return -1;
  else if (dec_x > dec_y)
    return 1;
  else // (dec_x == dec_y
    return 0;
}

/**
 * Sort passed block_list based on 9th decile of the samples from
 * block_info
 *
 * @note This function is NOT thread safe!
 */
void
sort_worst_block_list(struct status_t *st, 
    struct block_info_t *block_info, size_t block_info_len, 
    struct block_list_t *block_list, size_t block_list_len)
{
  _block_compare_block_info = block_info;

  qsort(block_list, block_list_len, sizeof(struct block_list_t), _block_compare);

  /*
  for (size_t i=0; i<block_list_len; i++)
    {
      for (size_t j=0; j<block_list_len-1; j++)
        {
          if ((!bi_is_initialised(&block_info[block_list[j].off]) &&
               bi_is_initialised(&block_info[block_list[j+1].off]))
             ||
             (!bi_is_valid(&block_info[block_list[j].off]) &&
               bi_is_valid(&block_info[block_list[j+1].off]))
             ||
             (bi_quantile(&block_info[block_list[j].off],9,10) >
                bi_quantile(&block_info[block_list[j+1].off],9,10)))
            {
              off_t tmp;
              tmp = block_list[j].off;
              block_list[j].off = block_list[j+1].off;
              block_list[j+1].off = tmp;
            }
        }
    }
    */
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
update_block_stats(struct status_t *st, struct block_info_t *block_info)
{
  st->invalid=0;
  st->vvfast=0; /* less than one fourth the rotational delay */
  st->vfast=0;  /* less than half the rotational delay */
  st->fast=0;   /* less than rotational delay */
  st->normal=0; /* less than 2 * rotational delay */
  st->slow=0;   /* less than 4 * rotational delay */
  st->vslow=0;  /* less than 6 * rotational delay */
  st->vvslow=0; /* more than 6 * rotational delay */
  st->errors=0;
  for (size_t i=0; i< st->number_of_blocks; i++)
    {
      if (!bi_is_initialised(&block_info[i]))
        {
          break;
        }

      if (bi_is_valid(&block_info[i]) == 0)
        {
          st->invalid++;
          continue;
        }

      double avg;

      avg = bi_quantile(&block_info[i],9,10);

      st->errors += bi_get_error(&block_info[i]);

      if (avg < st->vvfast_lvl) // very very fast read
        {
          ++st->vvfast;
        }
      else if (avg < st->vfast_lvl) // very fast read
        {
          ++st->vfast;
        }
      else if (avg < st->fast_lvl) // fast read
        {
          ++st->fast;
        }
      else if (avg < st->normal_lvl) // normal read
        {
          ++st->normal;
        }
      else if (avg < st->slow_lvl) // slow read
        {
          ++st->slow;
        }
      else if (avg < st->vslow_lvl) // very slow read
        {
          ++st->vslow;
        }
      else // very very slow read
        {
          ++st->vvslow;
        }
    }
}

void
remove_block_from_stats(struct status_t *st, double time)
{
  if (time < st->vvfast_lvl)
    st->vvfast--;
  else if (time < st->vfast_lvl)
    st->vfast--;
  else if (time < st->fast_lvl)
    st->fast--;
  else if (time < st->normal_lvl)
    st->normal--;
  else if (time < st->slow_lvl)
    st->slow--;
  else if (time < st->vslow_lvl)
    st->vslow--;
  else
    st->vvslow--;
}

void
add_block_to_stats(struct status_t *st, double time)
{
  if (time < st->vvfast_lvl)
    st->vvfast++;
  else if (time < st->vfast_lvl)
    st->vfast++;
  else if (time < st->fast_lvl)
    st->fast++;
  else if (time < st->normal_lvl)
    st->normal++;
  else if (time < st->slow_lvl)
    st->slow++;
  else if (time < st->vslow_lvl)
    st->vslow++;
  else
    st->vvslow++;
}

void
add_block(struct status_t *st, struct block_info_t *block, double new_time)
{
  if (bi_is_initialised(block))
    {
      if (!bi_is_valid(block))
        st->invalid--;
      else
        {
          remove_block_from_stats(st, bi_quantile(block,9,10));
        }
    }

  bi_add_time(block, new_time);

  if (bi_is_valid(block))
    add_block_to_stats(st, bi_quantile(block,9,10));
  else
    st->invalid++;
}

void
add_sample_to_stats(struct status_t *st, double time)
{
  if (time < st->vvfast_lvl)
    {
      if (st->sector_times == PRINT_SYMBOLS)
        printf("_%s", CLEAR_LINE_END);
      st->tot_vvfast++;
    }
  else if (time < st->vfast_lvl)
    {
      if (st->sector_times == PRINT_SYMBOLS)
        printf(".%s", CLEAR_LINE_END);
      st->tot_vfast++;
    }
  else if (time < st->fast_lvl)
    {
      if (st->sector_times == PRINT_SYMBOLS)
        printf(",%s", CLEAR_LINE_END);
      st->tot_fast++;
    }
  else if (time < st->normal_lvl)
    {
      if (st->sector_times == PRINT_SYMBOLS)
        printf("-%s", CLEAR_LINE_END);
      st->tot_normal++;
    }
  else if (time < st->slow_lvl)
    {
      if (st->sector_times == PRINT_SYMBOLS)
        printf("+%s", CLEAR_LINE_END);
      st->tot_slow++;
    }
  else if (time < st->vslow_lvl)
    {
      if (st->sector_times == PRINT_SYMBOLS)
        printf("#%s", CLEAR_LINE_END);
      st->tot_vslow++;
    }
  else
    {
      if (st->sector_times == PRINT_SYMBOLS)
        printf("!%s", CLEAR_LINE_END);
      st->tot_vvslow++;
    }

  if (st->sector_times == PRINT_SYMBOLS)
    fflush(stdout);

  st->tot_sum += time;
  st->tot_samples++;
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
get_file_size(struct status_t *st, int dev_fd)
{
  struct stat file_stat;
  off_t filesize;
  if (fstat(dev_fd, &file_stat) == -1)
    err(EXIT_FAILURE, "fstat");
  if (S_ISREG(file_stat.st_mode))
    {
      filesize = file_stat.st_size;
      if (st->verbosity > 2)
        {
          printf("file size: %lli bytes\n", (long long)file_stat.st_size);
        }
      if (st->flog != NULL)
        fprintf(st->flog, "device size: %lli bytes\n", (long long)file_stat.st_size);
    }
  else if (S_ISBLK(file_stat.st_mode))
    {
      if (ioctl(dev_fd, BLKGETSIZE64, &filesize) == -1)
        err(EXIT_FAILURE, "ioctl: BLKGETSIZE64");
      if (st->verbosity > 2)
        printf("file size: %lli bytes\n", (long long)filesize);
      if (st->flog != NULL)
        fprintf(st->flog, "device size: %lli bytes\n", (long long)filesize);
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
      printf("%zi: %lli %lli\n", i, (long long)block_list[i].off, (long long)block_list[i].len);
      i++;
    }
}

char*
get_file_stat_sys_name(struct status_t *st, char* filename)
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
  
  if (st->verbosity > 2)
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

  if (st->verbosity > 2)
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
read_blocks(struct status_t *st, int fd, char* stat_path, off_t offset, off_t len)
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
  unsigned int info;
  int int_res = 0;

  assert(len>0);

  block_info = calloc(sizeof(struct block_info_t), len);
  if (block_info == NULL)
    err(EXIT_FAILURE, "read_blocks1: len=%lli", (long long)len);

  buffer = malloc(st->sectors*512+pagesize);
  if (buffer == NULL)
    err(EXIT_FAILURE, "read_blocks2");
  buffer_free = buffer;
  buffer = ptr_align(buffer, pagesize);

  if (stat_path != NULL)
    get_read_writes(stat_path, &read_start, &read_sectors_s, &write_start);

  off_t disk_cache = 16;

  // read additional blocks before the main data to reduce seek noise seen
  // over USB bridges
  if (st->usb_mode)
    disk_cache = 16;
  else
    disk_cache = 1;

  off_t beggining_pos = ((offset-disk_cache-1)*st->sectors>=0)?
                          (offset-disk_cache-1)*st->sectors:0;
    
  if(lseek(fd, beggining_pos*512, SEEK_SET) < 0)
    goto interrupted;

  for (size_t i=0; i < disk_cache; i++)
    {

      if (!st->ata_verify)
        nread = read(fd, buffer, st->sectors*512);
      else
        {
          int_res = sg_ll_verify10(fd, 0, 0, 0, 
              (unsigned int)beggining_pos+i*st->sectors,
              st->sectors, NULL, 0, &info, 1, st->verbosity);
          if (int_res != 0)
            {
              errno = EIO;
              nread = -1;
            }
          else
            nread = st->sectors*512;
        }

      if (nread < 0)
        {
          fprintf(stderr, "E");
          bad_sectors = 1;

          st->tot_errors++;
          
          if (st->bad_sector_warning)
            {
              fprintf(stderr, "%s\nBAD SECTORS! Reads may not be accurate!%s\n",
                  CLEAR_LINE_END, CLEAR_LINE_END);
              st->bad_sector_warning = 0;
            }

          // omit block
          if (lseek(fd, (off_t)st->sectors*512, SEEK_CUR) < 0)
            goto interrupted;
        }
      else if (nread != st->sectors*512)
        goto interrupted;
    }

  // read additional block before the main data to exclude seek time
  if (offset-disk_cache-1 < 0)
    if ( lseek(fd, (offset-1>=0)?(offset-1)*st->sectors*512:0, SEEK_SET) < 0)
      goto interrupted;

  if (!st->ata_verify)
    nread = read(fd, buffer, st->sectors*512);
  else
    {
      int_res = sg_ll_verify10(fd, 0, 0, 0, (unsigned int)(offset-1>=0)?
          (offset-1)*st->sectors:0,
          st->sectors, NULL, 0, &info, 1, st->verbosity);
      if (int_res != 0)
        {
          errno = EIO;
          nread = -1;
        }
      else
        nread = st->sectors*512;
    }

  if (nread < 0)
    {
      fprintf(stderr, "E");
      bad_sectors = 1;

      st->tot_errors++;

      if (st->bad_sector_warning)
        {
          fprintf(stderr, "%s\nBAD SECTORS! Reads may not be accurate!%s\n",
              CLEAR_LINE_END, CLEAR_LINE_END);
          st->bad_sector_warning = 0;
        }

      // omit block
      if (lseek(fd, st->sectors*512, SEEK_CUR) < 0)
        goto interrupted;
    }
  else if (nread != st->sectors*512)
    goto interrupted;

  if (offset == 0)
    if( lseek(fd, (off_t) 0, SEEK_SET) < 0)
      goto interrupted;

  // check if current position is correct (assert)
  if ( !st->ata_verify && lseek(fd, (off_t)0, SEEK_CUR) != offset * st->sectors * 512)
    {
      fprintf(stderr, "hdck: read_blocks: wrong offset: got %lli expected %lli\n",
          (long long)lseek(fd, (off_t)0, SEEK_CUR),
          (long long)offset * st->sectors * 512);
      exit(EXIT_FAILURE);
    }

  // start reading main block
  clock_gettime(TIMER_TYPE, &time_end);

  while (no_blocks < len)
    {
      time_start.tv_sec = time_end.tv_sec;
      time_start.tv_nsec = time_end.tv_nsec;

      if (!st->ata_verify)
        nread = read(fd, buffer, st->sectors*512);
      else
        {
          int_res = sg_ll_verify10(fd, 0, 0, 0, 
              (unsigned int)offset*st->sectors+no_blocks*st->sectors,
              st->sectors, NULL, 0, &info, 1, st->verbosity);
          if (int_res != 0)
            {
              errno = EIO;
              nread = -1;
            }
          else
            nread = st->sectors*512;
        }

      clock_gettime(TIMER_TYPE, &time_end);

      if (nread < 0)
        {
          if (errno != EIO)
            err(EXIT_FAILURE, "read_blocks3");

          write(2, "E", 1);

          st->tot_errors++;

          bi_add_error(&block_info[no_blocks]);
          bad_sectors = 1;

          if (st->bad_sector_warning)
            {
              fprintf(stderr, "\nBAD SECTORS! Reads may not be accurate!%s\n",
                  CLEAR_LINE_END);
              st->bad_sector_warning = 0;
            }

          // omit block
          if (lseek(fd, 
                    (off_t)512*st->sectors*(no_blocks+1 + offset), 
                    SEEK_SET) < 0)
            goto interrupted;

          no_blocks++;

        }
      else if (nread != st->sectors*512)
        {
          bad_sectors = 1;
          if (lseek(fd, (off_t)512*st->sectors*(no_blocks+1 + offset),
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
  if (!st->ata_verify)
    nread = read(fd, buffer, st->sectors*512);
  else
    {
      int_res = sg_ll_verify10(fd, 0, 0, 0, 
          (unsigned int)st->sectors*(offset+no_blocks+1),
          st->sectors, NULL, 0, &info, 1, st->verbosity);
      if (int_res != 0)
        {
          errno = EIO;
          nread = -1;
        }
      else
        nread = st->sectors*512;
    }
  if (!st->ata_verify)
    nread = read(fd, buffer, st->sectors*512);
  else
    {
      int_res = sg_ll_verify10(fd, 0, 0, 0, 
          (unsigned int)st->sectors*(offset+no_blocks+2),
          st->sectors, NULL, 0, &info, 1, st->verbosity);
      if (int_res != 0)
        {
          errno = EIO;
          nread = -1;
        }
      else
        nread = st->sectors*512;
    }

  if (stat_path != NULL)
    get_read_writes(stat_path, &read_end, &read_sectors_e, &write_end);

  if (((!st->ata_verify && read_end-read_start != disk_cache + 1 + 2 + len && 
        st->nodirect == 0 && 
        stat_path != NULL
      )||
      (st->ata_verify && read_end-read_start != 0 && 
        st->nodirect == 0 && 
        stat_path != NULL
      )
      || 
      (!st->ata_verify && read_end-read_start > 4 * (disk_cache + 1 + 2 + len) && 
       st->nodirect == 1 && 
        stat_path != NULL
      )
      ||
      (st->ata_verify && read_end-read_start != 0 && 
       st->nodirect == 1 && 
        stat_path != NULL
      ))
      && bad_sectors == 0
     )
    {
      goto interrupted;
    }

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
find_bad_blocks(struct status_t *st, struct block_info_t* block_info, 
    size_t block_info_len,
    float min_std_dev, size_t min_reads, size_t glob, off_t offset, double delay,
    int soft_delay, int certain_bad)
{
  struct block_list_t* block_list;
  struct block_list_t* ret;
  size_t uncertain = 0;
  size_t invalid = 0;
  size_t very_slow = 0;

  if (offset > block_info_len || offset < 0)
    return NULL;

  block_list = calloc(sizeof(struct block_list_t), block_info_len + 1);
  if (block_list == NULL)
    err(EXIT_FAILURE, "find_uncertain_blocks");

  // first thing to do in quick mode, is to get rid of invalid blocks
  if (st->quick && !certain_bad)
    {
      for (size_t block_no=offset; block_no < block_info_len; block_no++)
        {
          if (!bi_is_initialised(&block_info[block_no]))
            continue;

          // re-read blocks that didn't receive their share of proper reads
          if (bi_num_samples(&block_info[block_no]) < min_reads ||
              !bi_is_valid(&block_info[block_no]))
            {
              block_list[uncertain].off = block_no;
              block_list[uncertain].len = 1;
              uncertain++;
              invalid++;
              continue;
            }
        }
    }

  if (st->quick && !invalid)
    for(size_t block_no=offset; block_no < block_info_len; block_no++)
      {
        if (bi_quantile(&block_info[block_no],9,10) >= st->slow_lvl 
            && bi_num_samples(&block_info[block_no]) < 20)
          {
            block_list[uncertain].off = block_no;
            block_list[uncertain].len = 1;
            uncertain++;
            very_slow++;
            continue;
          }
      }

  // find uncertain blocks
  if (!invalid && very_slow < 64)
    {
      double blk_decile;
      size_t blk_n_sampl;

      if (very_slow)
        uncertain = 0; // we don't want duplicates...
      for (size_t block_no=offset; block_no < block_info_len; block_no++)
        {
          if (!bi_is_initialised(&block_info[block_no]))
            continue;

          blk_n_sampl = bi_num_samples(&block_info[block_no]);

          // re-read blocks that didn't receive their share of proper reads
          if (blk_n_sampl < min_reads ||
              !bi_is_valid(&block_info[block_no]))
            {
              block_list[uncertain].off = block_no;
              block_list[uncertain].len = 1;
              uncertain++;
              continue;
            }

          blk_decile = bi_quantile(&block_info[block_no],9,10);

          // ignore fast sectors
          if (blk_decile < st->fast_lvl)
            continue;

          // check if a single out-of-ordinary result is not a fluke
          if (blk_n_sampl <= 2 &&
              blk_decile > st->fast_lvl)
            {
              block_list[uncertain].off = block_no;
              block_list[uncertain].len = 1;
              uncertain++;
              continue;
            }

          // big claims need big evidence
          if (blk_decile >= st->normal_lvl 
              && blk_n_sampl < 15)
            {
              block_list[uncertain].off = block_no;
              block_list[uncertain].len = 1;
              uncertain++;
              continue;
            }

          if (blk_decile >= st->slow_lvl 
              && blk_n_sampl < 20)
            {
              block_list[uncertain].off = block_no;
              block_list[uncertain].len = 1;
              uncertain++;
              continue;
            }
          
          if (blk_decile >= st->vslow_lvl
              && blk_n_sampl < 30)
            {
              block_list[uncertain].off = block_no;
              block_list[uncertain].len = 1;
              uncertain++;
              continue;
            }

          // process only sectors with slow sectors
          if (blk_decile >= st->fast_lvl)
            {
              double lq, max;
              size_t num_samples;
              num_samples = blk_n_sampl;
              lq = bi_quantile_exact(&block_info[block_no],1,4);
              max = bi_max(&block_info[block_no]);
              if (num_samples == 3)
                {
                  double low, med, high;
                  low = bi_quantile_exact(&block_info[block_no],0,num_samples);
                  med = bi_quantile_exact(&block_info[block_no],1,num_samples);
                  high = max;

                  high = high - st->fast_lvl * floor(high/st->fast_lvl);

                  // check if it's not a fluke
                  if ( low < st->fast_lvl && med < st->fast_lvl 
                      && abs((low+med)/2-high) > st->fast_lvl/4 )
                    continue;

                  // if the difference is big, the sector is probably shot, 
                  // check to make sure
                  if ( max > st->normal_lvl)
                    {
                      block_list[uncertain].off = block_no;
                      block_list[uncertain].len = 1;
                      uncertain++;
                      continue;
                    }
                  else // single re-read only, ignore
                    continue;
                }

              if (num_samples <= 5)
                {
                  if (lq > st->fast_lvl)
                    {
                      // if more than 4 reads show the sector as slower than
                      // rotational delay, the sector is ceratainly shot
                      if (certain_bad == 1)
                        {
                          block_list[uncertain].off = block_no;
                          block_list[uncertain].len = 1;
                          uncertain++;
                          continue;
                        }
                      else
                        continue;
                    }
                }

              if (num_samples < 20)
                {
                  double high,med;
                  high = bi_quantile_exact(
                      &block_info[block_no],num_samples-1,num_samples);
                  med = bi_quantile_exact(
                      &block_info[block_no],num_samples-2,num_samples);

                  if ((max - high) < st->fast_lvl/8) 
                  // if two slowest are very similar
                    {
                      if (certain_bad == 1)
                        {
                          block_list[uncertain].off = block_no;
                          block_list[uncertain].len = 1;
                          uncertain++;
                          continue;
                        }
                      else
                        continue; // certain bad
                    }

                  // if difference is greater than 2 rotational delays 
                  // more reads are needed
                  if (max/st->fast_lvl - high/st->fast_lvl >= 2 
                      && num_samples < 15)
                    {
                      block_list[uncertain].off = block_no;
                      block_list[uncertain].len = 1;
                      uncertain++;
                      continue;
                    }

                  if (high > st->fast_lvl)
                    {
                      high = high - st->fast_lvl * floor(high/st->fast_lvl);
                      max = max - st->fast_lvl * floor(high/st->fast_lvl);
                      if (abs(high-max) < st->fast_lvl/8)
                        {
                          // the reads are not a fluke
                          if (certain_bad == 1)
                            {
                              block_list[uncertain].off = block_no;
                              block_list[uncertain].len = 1;
                              uncertain++;
                              continue;
                            }
                          else 
                            continue;
                        }
                      else
                        {
                          block_list[uncertain].off = block_no;
                          block_list[uncertain].len = 1;
                          uncertain++;
                          continue;
                        }
                    }

                  if (bi_quantile_exact(&block_info[block_no],num_samples-2
                        ,num_samples) > st->fast_lvl)
                    {
                      if (certain_bad == 1)
                        {
                          block_list[uncertain].off = block_no;
                          block_list[uncertain].len = 1;
                          uncertain++;
                          continue;
                        }
                      else
                        continue;
                    }

                  // looks like only one sample with re-read, don't bother
                  continue;
                }

              if (num_samples >= 20 && certain_bad == 1)
                {
                  block_list[uncertain].off = block_no;
                  block_list[uncertain].len = 1;
                  uncertain++;
                  continue;
                }
              else
                continue;
            }
        }
    }

  if (uncertain == 0)
    {
      free(block_list);
      return NULL;
    }

  // recheck only the worst sectors in quick mode but all invalid and all very slow
  if (st->quick && !invalid && very_slow < 64)
    {
      static int first = 2;
      sort_worst_block_list(st, block_info, block_info_len, block_list, 
          uncertain);
      if (first)
        {
          if (uncertain > 1024)
            {
              // move 1024 worst blocks from the bottom 
              size_t from, dest;
              assert(uncertain - 1 > 1023);
              for (from = uncertain - 1024, dest = 0;
                  dest < 1024;
                  from++, dest++)
                {
                  block_list[dest].off = block_list[from].off;
                }
              block_list[1024].off = 0;
              block_list[1024].len = 0;
              uncertain = 1024;
            }
          first--;
        }
      else
        {
          if (uncertain > 64)
            {
              // move 64 worst blocks up
              size_t from, dest;
              assert(uncertain - 1 > 63);
              for (from = uncertain - 64, dest = 0;
                  dest < 64;
                  from++, dest++)
                {
                  block_list[dest].off = block_list[from].off;
                }
              block_list[64].off = 0;
              block_list[64].len = 0;
              uncertain = 64;
            }
        }
      
      qsort(block_list, uncertain, sizeof(struct block_list_t), __off_t_compare);
    }

  // check if need to do some globbing
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
find_uncertain_blocks(struct status_t *st, struct block_info_t* block_info, 
    size_t block_info_len,
    float min_std_dev, size_t min_reads, size_t glob, off_t offset, double delay,
    int soft_delay)
{
  return find_bad_blocks(st, block_info, block_info_len, min_std_dev,
      min_reads, glob, offset, delay, soft_delay, 0);
}

struct block_list_t*
find_worst_blocks(struct status_t *st, struct block_info_t *block_info, 
    size_t block_info_len, size_t number)
{
  struct block_list_t *block_list = calloc(sizeof(struct block_list_t), 
                                                  block_info_len + 1);
  if (block_list == NULL)
    err(EXIT_FAILURE, "find_worst_blocks");

  // assert
  if (number >= block_info_len)
    {
      fprintf(stderr, "find_worst_blocks: number of blocks to find greater than"
          " total blocks\n");
      exit(EXIT_FAILURE);
    }

  for (size_t i=0; i < number; i++)
    {
      block_list[i].off = i;
      block_list[i].len = 1;
    }

  sort_worst_block_list(st, block_info, block_info_len, 
      block_list, number);

  for (size_t block_no = number; block_no < block_info_len; block_no++)
    {
      if (bi_quantile(&block_info[block_list[0].off],9,10) <
          bi_quantile(&block_info[block_no],9,10))
          {
            block_list[0].off = block_no;
            sort_worst_block_list(st, block_info, block_info_len, 
                block_list, number);
          }
    }

  return block_list;
}

void
write_to_file(struct status_t *st, char *file,
    struct block_info_t* block_info, size_t len)
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
      if (st->write_individual_times)
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
write_list_to_file(struct status_t *st, char* file, 
    struct block_list_t* block_list)
{
  FILE* handle;

  handle = fopen(file, "w+");
  if (handle == NULL)
    err(EXIT_FAILURE,"write_list_to_file");

  for(size_t i=0; !(block_list[i].off == 0 && block_list[i].len == 0); i++)
    if(fprintf(handle, "%lli %lli\n", block_list[i].off * (long long)st->sectors,
        (block_list[i].off + block_list[i].len) * (long long)st->sectors) == 0)
      err(EXIT_FAILURE, "write_list_to_file");

  fclose(handle);
}

struct block_list_t*
read_list_from_file(struct status_t *st, char* file)
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

      re = fscanf(handle, "%lli %lli\n", (long long *)&off, (long long *)&len);
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

      block_list[read_blocks].off = off/st->sectors; // round down
      block_list[read_blocks].len = ceill((len - off)*1.0L/st->sectors); //round up
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
read_block_list(struct status_t *st, int dev_fd, struct block_list_t* block_list,
    struct block_info_t* block_info, char* dev_stat_path, off_t number_of_blocks)
{
  uint16_t correct_reads = 0xffff;
  off_t total_blocks = 0; ///< total number of blocks to be read (with overhead)
  off_t blocks_read = 0; ///< number of blocks read (with overhead)
  struct block_list_t* tmp_block_list; ///< compacted block_list
  static size_t max_len = 4; ///< globbing param for compacting
  /// disk cache size in blocks
  off_t disk_cache = st->disk_cache_size * 1024 * 1024 / st->sectors / 512;
  struct timespec start_time, end_time, res; ///< expected time calculation
  size_t block_number=0; ///< position in the block_list
  struct block_info_t* block_data; ///< stats for sectors read
  int int_res;
  unsigned int info;

  if (st->verbosity > 6)
    print_block_list(block_list);
  tmp_block_list = compact_block_list(block_list, max_len * 2);
  if (st->verbosity > 6)
    {
      printf("after compacting:\n");
      print_block_list(tmp_block_list);
    }

  // count the total number of blocks that will be read
  for (size_t i=0; !(tmp_block_list[i].off==0 && tmp_block_list[i].len==0); i++)
    // 16 blocks are needed for meaningful results through USB bridges
    total_blocks += tmp_block_list[i].len + 1 + 15 * st->usb_mode + 3;

  // empty internal disk cache by reading twice the size of cache
  // but only when reads by themselves won't do it
  if (total_blocks <= disk_cache *2)
    {
      if(st->ata_verify)
        {
          int_res = sg_ll_verify10(dev_fd, 0, 0, 0, 
              (unsigned int)0, st->sectors*disk_cache*2, NULL, 0, &info, 1, st->verbosity);
          // XXX ignore errors
          if(lseek(dev_fd, st->sectors*disk_cache*2*512, SEEK_SET) < 0)
            err(1,"read_block_list:can't seek");
        }
      else
        {
          if(lseek(dev_fd, 0, SEEK_SET) < 0)
            err(1,"read_block_list:can't seek");

          char *buffer, *buffer_free;
          buffer = malloc(st->sectors*512+pagesize);
          if (buffer == NULL)
            err(EXIT_FAILURE, "read_block_list");
          buffer_free = buffer;
          buffer = ptr_align(buffer, pagesize);
          for (size_t i=0; i < disk_cache*2; i++)
            {
              int nread;
              nread = read(dev_fd, buffer, st->sectors*512);
              //XXX ignore errors
            }
          free(buffer_free);
        }
    }

  clock_gettime(TIMER_TYPE, &start_time);
  while (!(tmp_block_list[block_number].off == 0 && 
      tmp_block_list[block_number].len == 0))
    {
      size_t offset, length;
      offset = tmp_block_list[block_number].off;
      length = tmp_block_list[block_number].len;
      if (st->verbosity > 3)
        printf("processing block no %zi of length %zi\n", 
            offset, length);

      block_data = read_blocks(st, dev_fd, dev_stat_path, offset, length);

      blocks_read += length + 1 + disk_cache*st->usb_mode + 3;
      
      if (block_data == NULL || 
          (block_data != NULL && !bi_is_valid(&block_data[0])))
        {
          if (st->verbosity > 0)
            printf("re-read of block %zi (length %zi) interrupted%s\n", 
                offset, length, CLEAR_LINE_END);
          else if (st->verbosity > 0)
            printf("!%s", CLEAR_LINE_END);// interrupted

          st->tot_interrupts++;

          block_number++;
        }
      else if (st->verbosity <= 3 && st->verbosity > 2)
        printf(".%s", CLEAR_LINE_END); // OK
      fflush(stdout);

      if (block_data != NULL)
        {

          for (size_t i=0; i < length; i++)
            {
              if (bi_is_initialised(&block_info[offset+i]))
                {
                  if (!bi_is_valid(&block_info[offset+i]))
                    st->invalid--;
                  else
                    remove_block_from_stats(st, 
                        bi_quantile(&block_info[offset+i],9,10));
                }

              bi_add_valid(&block_info[offset+i], &block_data[i]);
              
              if (bi_is_valid(&block_info[offset+1]))
                add_block_to_stats(st, bi_quantile(&block_info[offset+i],9,10));
              else
                st->invalid++;
            }

          if (st->sector_times == PRINT_SYMBOLS)
            printf("====>");
          // add values from block_data to statistics
          for (size_t i=0; i < length; i++)
            {
              double *times;
              size_t len;
              times = bi_get_times(&block_data[i]);
              len = bi_num_samples(&block_data[i]);
              for (size_t j=0; j < len; j++)
                {
                  add_sample_to_stats(st, times[j]);
                }
            }
        }

      // print statistics
      if (st->verbosity >= 0 ) 
        {
          clock_gettime(TIMER_TYPE, &end_time);
          diff_time(&res, start_time, end_time);

          double percent;
          percent = blocks_read * 1.0/total_blocks;

          long long time_to_go;
          time_to_go = time_double(res) / percent;
          if (st->verbosity > 1)
            printf("\n");

          printf("reread %.2f%% done "
              "in %02li:%02li:%02li, expected time:"
              "%02lli:%02lli:%02lli%s\n",
              percent * 100,
              res.tv_sec/3600, res.tv_sec/60%60, res.tv_sec%60,
              time_to_go/3600, time_to_go/60%60, time_to_go%60,
              CLEAR_LINE_END);
          printf("         Samples:             Blocks (9th decile):%s\n",
              CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->vvfast_lvl, st->tot_vvfast,
              st->vvfast, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->vfast_lvl, st->tot_vfast,
              st->vfast, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->fast_lvl, st->tot_fast,
              st->fast, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->normal_lvl, st->tot_normal,
              st->normal, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->slow_lvl, st->tot_slow,
              st->slow, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->vslow_lvl, st->tot_vslow,
              st->vslow, CLEAR_LINE_END);
          printf(">%4.1fms: %20lli %20lli%s\n", st->vslow_lvl, st->tot_vvslow,
              st->vvslow, CLEAR_LINE_END);
          printf("ERR    : %20lli %20lli%s\n", st->tot_errors,
              st->errors, CLEAR_LINE_END);
          printf("Intrrpt: %20lli %20lli%s\n", st->tot_interrupts,
              st->invalid, CLEAR_LINE_END);
          printf("\r%s", cursor_up(11));
          fflush(stdout);
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

         // update_block_stats(st, block_info);
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
              if (st->verbosity > 7)
                print_block_list(tmp_block_list);
              free(tmp_block_list);

              tmp_block_list = compact_block_list(block_list, max_len);
              if (st->verbosity > 7)
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
          if (max_len < 64 * 1024 * 1024 / st->sectors / 512) 
            {
              max_len *= 2;
              off_t beginning = tmp_block_list[block_number].off;
              if (st->verbosity > 7)
                print_block_list(tmp_block_list);
              free(tmp_block_list);

              tmp_block_list = compact_block_list(block_list, max_len);
              if (st->verbosity > 7)
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

      // free the block_data structure
      for (size_t i=0; i< length; i++)
        bi_clear(&block_data[i]);

      free(block_data);
      block_number++;
    }

  if (tmp_block_list)
    free(tmp_block_list);

  printf("\n");
}

void
perform_re_reads(struct status_t *st, int dev_fd, char* dev_stat_path,
    struct block_info_t* block_info, size_t block_info_size, size_t re_reads,
    double max_std_dev, size_t min_reads, double delay)
{
  struct block_list_t* block_list;

  for(size_t tries=0; tries < re_reads; tries++)
    {
      // print statistics before processing
      if (st->verbosity >= 0)
        {
          if (min_reads == 1)
            block_list = find_uncertain_blocks(st,
                block_info, block_info_size, max_std_dev, min_reads, 1, 0, delay,
                1);
          else
            block_list = find_uncertain_blocks(st,
                block_info, block_info_size, max_std_dev, min_reads, 1, 0, delay,
                1);

          if (block_list == NULL)
            {
              if (st->verbosity >2)
                printf("no uncertain blocks found%s\n", CLEAR_LINE_END);
              break;
            }

          size_t block_number=0;
          if (st->verbosity > 2)
            printf("current uncertain blocks:%s\n", CLEAR_LINE_END);

          // end of list is marked by NULL, NULL
          while (!(block_list[block_number].off == 0 && 
              block_list[block_number].len == 0))
            {
              if (st->verbosity > 2)
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
        block_list = find_uncertain_blocks(st,
            block_info, block_info_size, max_std_dev, min_reads, 1, 0, delay, 1);
      else
        block_list = find_uncertain_blocks(st,
            block_info, block_info_size, max_std_dev, min_reads, 1, 0, delay, 1);

      if (block_list == NULL)
        break;

      read_block_list(st, dev_fd, block_list, block_info, dev_stat_path, 
          block_info_size);

      if (st->verbosity <= 3 && st->verbosity >= 0)
        printf("%s\n", CLEAR_LINE_END);

      if (block_list)
        free(block_list);

      if (tries % 16 == 0)
        update_block_stats(st, block_info);
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
read_whole_disk(struct status_t *st, int dev_fd, struct block_info_t* block_info, 
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
  int next_is_valid=1; ///< whatever an erronous read occured and next sector
                       ///< can contain seek time
  size_t loop=0; ///< loop number
  struct timespec time1, time2, /**< time it takes to read single block */
                  res; /**< temp result */
  int int_res;
  off_t nread; ///< number of bytes the read() managed to read
  size_t blocks = 0; ///< number of blocks read in this run
  long long abs_blocks = 0; ///< number of blocks read in all runs
  struct timespec times, timee; ///< wall clock start and end
  off_t number_of_blocks; ///< filesize in blocks

  fesetround(2); // round UP
  number_of_blocks = lrintl(ceil(filesize*1.0l/512/st->sectors));

  // get memory alligned pointer (needed for O_DIRECT access)
  ibuf = malloc(st->sectors*512+pagesize);
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
  off_t last_invalid = 0;
  unsigned int info = 0;
  while (1)
    {
      read_s = read_e;
      write_s = write_e;
      read_sec_s = read_sec_e;
      time1.tv_sec=time2.tv_sec;
      time1.tv_nsec=time2.tv_nsec;

      // assertion
      if (!st->ata_verify && 
          lseek(dev_fd, (off_t)0, SEEK_CUR) != 
          ((off_t)blocks) * st->sectors * 512 )
        {
          fprintf(stderr, "hdck: main: wrong offset, got %lli expected %lli\n",
              (long long)lseek(dev_fd, (off_t)0, SEEK_CUR),
              ((off_t)blocks) * (long long)st->sectors * 512);
          exit(EXIT_FAILURE);
        }

      //clock_gettime(TIMER_TYPE, &time1);
      if (!st->ata_verify)
        nread = read(dev_fd, ibuf, st->sectors*512);
      else
        {
          int_res = sg_ll_verify10(dev_fd, 0, 0, 0, (unsigned int)(blocks * st->sectors),
                 st->sectors, NULL, 0, &info, 1, st->verbosity);

          if (int_res != 0)
            {
              errno = EIO;
              nread = -1;
            }
          else
            nread = st->sectors*512;
        
        }

      clock_gettime(TIMER_TYPE, &time2);

      if (dev_stat_path != NULL)
        get_read_writes(dev_stat_path, &read_e, &read_sec_e, &write_e);

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

              st->tot_errors++;

              if (st->bad_sector_warning)
                {
                  printf("BAD SECTORS! Reads may not be accurate!\n");
                  st->bad_sector_warning = 0;
                }

              // omit block
              if (lseek(dev_fd, (off_t)512*st->sectors, SEEK_CUR) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
            }
        }
      // when the read was incomplete or interrupted
      else if (nread != st->sectors*512 || 
          (st->ata_verify && read_e-read_s != 0 && st->nodirect == 0 && dev_stat_path != NULL) || 
          (!st->ata_verify && read_e-read_s != 1 && st->nodirect == 0 && dev_stat_path != NULL) || 
          (st->ata_verify && read_e-read_s != 0 && st->nodirect == 1 && dev_stat_path != NULL) || 
          (!st->ata_verify && read_e-read_s > 4 && st->nodirect == 1 && dev_stat_path != NULL) || 
          (st->ata_verify && read_sec_e-read_sec_s != 0 && 
                st->nodirect == 0 && dev_stat_path != NULL) || 
          (!st->ata_verify && read_sec_e-read_sec_s != st->sectors && 
                st->nodirect == 0 && dev_stat_path != NULL) || 
          (write_e != write_s && dev_stat_path != NULL))
        {
          if (st->verbosity > 0)
            printf("block %zi (LBA: %lli-%lli) interrupted%s\n", blocks,
               ((off_t)blocks) * (long long)st->sectors, 
               ((off_t)blocks+1)*(long long)st->sectors-1,
               CLEAR_LINE_END);

          if (bi_is_initialised(&block_info[blocks]))
            st->invalid++;

          st->tot_interrupts++;

          diff_time(&res, time1, time2);
          times_time(&res, 1000); // in ms not ns
          if (bi_is_valid(&block_info[blocks]) == 0)
            {
              bi_add_time(&block_info[blocks], time_double(res));
            }
          diff_time(&res, time1, time2);
          if (nread != st->sectors*512)
            {
              // seek to start of next block
              if (lseek(dev_fd, (off_t)512*st->sectors-nread, SEEK_CUR) < 0)
                {
                  nread = -1; // exit loop, end of device
                }
            }
          // invalidate next read block (to ignore seeking)
          next_is_valid = 0;

          // invalidate last 8 read blocks
          for(int i=1; blocks > i && i <= 8 && blocks > last_invalid + i; i++)
            if (bi_is_valid(&block_info[blocks-i]))
              {
                remove_block_from_stats(st, 
                    bi_quantile(&block_info[blocks-i],9,10));
                bi_remove_last(&block_info[blocks-i]);
                if (bi_is_valid(&block_info[blocks-i]))
                  add_block_to_stats(st, 
                      bi_quantile(&block_info[blocks-i],9,10));
                else
                  st->invalid++;
              }
          
          last_invalid = blocks;

          //update_block_stats(st, block_info);
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
                  add_block(st, &block_info[blocks], time_double(res));
                  bi_make_valid(&block_info[blocks]);
                  st->invalid--;
                  add_block_to_stats(st, time_double(res));

                  if (st->verbosity > 10)
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
                  add_block(st, &block_info[blocks], time_double(res));

                  if (st->verbosity > 10)
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

          add_sample_to_stats(st, time_double(res) * 1000);
        }

      if (st->sector_times == PRINT_TIMES)
        printf("%li r:%lli rs: %lli w:%lli%s\n",
            res.tv_nsec/1000+res.tv_sec*1000000, 
            read_s, 
            read_sec_s, 
            write_s,
            CLEAR_LINE_END); 

      blocks++;
      abs_blocks++;

      if (blocks % 500 == 0 && st->verbosity >= 0)
        {
          clock_gettime(TIMER_TYPE, &timee);
          diff_time(&res, time1, time2);

          float cur_speed;
          cur_speed = st->sectors * 512 / 1024 * 1.0f / 1024 / 
            (res.tv_sec * 1.0f + res.tv_nsec / 1000000000.0);

          diff_time(&res, times, timee);

          float speed;
          speed = abs_blocks * st->sectors * 512 / 1024 * 1.0f / 1024 / 
            (res.tv_sec * 1.0f + res.tv_nsec / 1000000000.0);

          float percent;
          if (st->max_sectors == 0)
            percent = (blocks * st->sectors * 512.0f) / (filesize * 1.0f);
          else
            percent = (blocks * st->sectors * 512.0f) / 
              (st->max_sectors * st->sectors * 2.0f);

          long long time_to_go;
          time_to_go = (res.tv_sec*1.0) / 
                            (percent/st->min_reads + loop*1.0/st->min_reads);

          printf("hdck status:%s\n", CLEAR_LINE_END);
          printf("============%s\n", CLEAR_LINE_END);
          printf("Loop:          %zi of %zi%s\n", loop+1, st->min_reads, 
              CLEAR_LINE_END);
          printf("Progress:      %.2f%%, %.2f%% total%s\n",
              percent*100, (percent/st->min_reads + loop*1.0/st->min_reads) * 100,
              CLEAR_LINE_END);
          printf("Read:          %lli sectors of %lli%s\n", 
              ((off_t)blocks)*(long long)st->sectors,
              (long long)filesize, CLEAR_LINE_END);
          printf("Speed:         %.3fMiB/s, average: %.3fMiB/s%s\n", cur_speed,
              speed, CLEAR_LINE_END);
          printf("Elapsed time:  %02li:%02li:%02li%s\n",
              res.tv_sec/3600, res.tv_sec/60%60, res.tv_sec%60,
              CLEAR_LINE_END);
          printf("Expected time: %02lli:%02lli:%02lli%s\n",
              time_to_go/3600, time_to_go/60%60, time_to_go%60,
              CLEAR_LINE_END);
          printf("         Samples:             Blocks (9th decile):%s\n",
              CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->vvfast_lvl, st->tot_vvfast,
              st->vvfast, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->vfast_lvl, st->tot_vfast,
              st->vfast, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->fast_lvl, st->tot_fast,
              st->fast, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->normal_lvl, st->tot_normal,
              st->normal, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->slow_lvl, st->tot_slow,
              st->slow, CLEAR_LINE_END);
          printf("<%4.1fms: %20lli %20lli%s\n", st->vslow_lvl, st->tot_vslow,
              st->vslow, CLEAR_LINE_END);
          printf(">%4.1fms: %20lli %20lli%s\n", st->vslow_lvl, st->tot_vvslow,
              st->vvslow, CLEAR_LINE_END);
          printf("ERR    : %20lli %20lli%s\n", st->tot_errors,
              st->errors, CLEAR_LINE_END);
          printf("Intrrpt: %20lli %20lli%s\n", st->tot_interrupts,
              st->invalid, CLEAR_LINE_END);
          printf("\r%s", cursor_up(18));
          fflush(stdout);
        }

      // check whatever we have to leave the loop
      if (nread == 0 || nread == -1 || blocks >= number_of_blocks
          || (st->max_sectors != 0 && blocks * st->sectors >= st->max_sectors ))
        {
          long long high_dev=0;
          long long sum_invalid=0;
          loop++;

          update_block_stats(st, block_info);

          // check standard deviation for blocks
          for (size_t i =0; i < blocks; i++)
            {
              if (bi_int_rel_stdev(&block_info[i]) > st->max_std_dev)
                high_dev++;
              
              if (bi_is_valid(&block_info[i]) == 0)
                sum_invalid++;
            }
          if (loop < st->min_reads || 
              high_dev/(blocks*1.0) > 0.25 || 
              sum_invalid/(blocks*1.0) > 0.10)
            {
              if (
                  st->verbosity >= 0 && 
                  !(loop < st->min_reads) && 
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
              // try to place first sector in cache
              if (!st->ata_verify)
                {
                  nread = read(dev_fd, ibuf, 512);
                  if (lseek(dev_fd, (off_t)0, SEEK_SET) < 0)
                    {
                      nread = -1; // exit loop, end of device
                    }
                }
              clock_gettime(TIMER_TYPE, &time2);
              // TODO: flush system buffers when no direct 
            }
          else
            break;

          if (loop > st->max_reads + st->min_reads)
            {
              printf("Warning: read whole disk %zi times, still "
                  "can't get high confidence%s\n", st->max_reads, CLEAR_LINE_END);
              break;
            }
        }
    }
 free(ibuf_free); 
}

int
main(int argc, char **argv)
{
  struct status_t st; /**< program status */
  
  /*
   * initialize program
   */
  st.sectors = 256;
  st.verbosity = 0;
  st.exclusive = 0;
  st.noaffinity = 0;
  st.nortio = 0;
  st.max_sectors = 0;
  st.no_rt = 0;
  st.vvfast_lvl = -1.0;
  st.vfast_lvl = -1.0;
  st.fast_lvl = -1.0;
  st.normal_lvl = -1.0;
  st.slow_lvl = -1.0;
  st.vslow_lvl = -1.0;
  st.nodirect = 0;
  st.nosync = 0;
  st.noflush = 0;
  st.min_reads = 0;
  st.max_reads = 0;
  st.max_std_dev = 0.0;
  st.sector_times = 0;
  st.usb_mode = 1;
  st.ata_verify = 0;
  st.disk_cache_size = 32; // in MiB
  st.rotational_delay = 60.0/7200*1000; // in ms
  st.filename = NULL;
  st.dev_stat_path = NULL;
  st.filesize = 0;
  st.number_of_blocks = 0;
  st.write_individual_times = 1;
  st.bad_sector_warning = 1;
  st.flog = NULL;
  st.output = NULL;
  st.write_uncertain_to_file = NULL;
  st.tot_errors = 0;
  st.tot_vvfast = 0;
  st.tot_vfast = 0;
  st.tot_fast = 0;
  st.tot_normal = 0;
  st.tot_slow = 0;
  st.tot_vslow = 0;
  st.tot_vvslow = 0;
  st.tot_sum = 0.0;
  st.tot_samples = 0;
  st.errors = 0;
  st.vvfast = 0;
  st.vfast = 0;
  st.fast = 0;
  st.normal = 0;
  st.slow = 0;
  st.vslow = 0;
  st.vvslow = 0;
  st.tot_interrupts = 0;
  st.invalid = 0;
  st.quick = 0;
  //st.time_end;
  //st.time_start;

  int c;
  /// path to the `stat' file for the corresponding hardware device
  char* read_sectors_from_file = NULL; ///< file with sectors to scan to
  char* log_path = NULL; ///< path to file to write log to
  struct block_info_t* block_info = NULL;
  struct timespec res; /// temporary timespec result

  if (argc == 1)
    {
      usage(&st);
      exit(EXIT_FAILURE);
    }

  /*
   * parse command line options
   */
  while (1) {
    int option_index = 0;
    struct option long_options[] = {
        {"file", 1, 0, 'f'}, // 0
        {"exclusive", 0, 0, 'x'}, // 1
        {"nodirect", 0, &st.nodirect, 1}, // 2
        {"verbose", 0, 0, 'v'}, // 3
        {"noaffinity", 0, &st.noaffinity, 1}, // 4
        {"nortio", 0, &st.nortio, 1}, // 5
        {"sector-times", 0, &st.sector_times, PRINT_TIMES}, // 6
        {"sector-symbols", 0, &st.sector_times, PRINT_SYMBOLS}, // 7
        {"nosync", 0, &st.nosync, 1}, // 8
        {"noverbose", 0, 0, 0}, // 9
        {"noflush", 0, &st.noflush, 1}, // 10
        {"max-sectors", 1, 0, 0}, // 11
        {"outfile", 1, 0, 'o'}, // 12
        {"min-reads", 1, 0, 0}, // 13
        {"max-std-deviation", 1, 0, 0}, // 14
        {"max-reads", 1, 0, 0}, // 15
        {"disk-cache", 1, 0, 0}, // 16
        {"nort", 0, &st.no_rt, 1}, // 17
        {"background", 0, 0, 'b'}, // 18
        {"disk-rpm", 1, 0, 0}, // 19
        {"bad-sectors", 1, 0, 'w'}, // 20
        {"read-sectors", 1, 0, 'r'}, // 21
        {"version", 0, 0, 0}, // 22
        {"log", 1, 0, 'l'}, // 23
        {"quick", 0, &st.quick, 1}, // 24
        {"no-usb", 0, 0, 0}, // 25
        {"ata-verify", 0, 0, 0}, // 26
        {"no-ata-verify", 0, 0, 0}, // 27
        {0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "f:xhbvo:?w:r:l:",
             long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 0:
        if (st.verbosity > 5)
          {
            printf("option %s%s\n", long_options[option_index].name,
                CLEAR_LINE_END);
            if (optarg)
                printf(" with arg %s%s\n", optarg, CLEAR_LINE_END);
          }
        if (option_index == 9)
          {
            st.verbosity--;
            break;
          }
        if (option_index == 11)
          {
            st.max_sectors = atoll(optarg);
            break;
          }
        if (option_index == 13)
          {
            st.min_reads = atoll(optarg);
            break;
          }
        if (option_index == 14)
          {
            st.max_std_dev = atof(optarg);
            break;
          }
        if (option_index == 15)
          {
            st.max_reads = atoll(optarg);
            break;
          }
        if (option_index == 16)
          {
            st.disk_cache_size = atoll(optarg);
            break;
          }
        if (option_index == 19)
          {
            if (atoll(optarg) == 0)
              {
                usage(&st);
                exit(EXIT_FAILURE);
              }
            st.rotational_delay = 60.0/atoll(optarg) * 1000;
            break;
          }
        if (option_index == 22)
          {
            print_version();
            exit(EXIT_SUCCESS);
          }
        if (option_index == 25)
          {
            st.usb_mode = 0;
            break;
          }
        if (option_index == 26)
          {
            st.ata_verify = 1;
            break;
          }
        if (option_index == 27)
          {
            st.ata_verify = 0;
            break;
          }
        break;

    case 'v':
        if (st.verbosity > 5 ) printf("option v%s\n", CLEAR_LINE_END);
        st.verbosity++;
        break;

    case 'x':
        if (st.verbosity > 5) printf("option x%s\n", CLEAR_LINE_END);
        st.exclusive = 1;
        break;

    case 'f':
        st.filename = optarg;
        if (st.verbosity > 5) printf("option f with value '%s'%s\n", optarg,
            CLEAR_LINE_END);
        break;

    case 'o':
        st.output = optarg;
        if (st.verbosity > 5) printf("option o with value '%s'%s\n", optarg,
            CLEAR_LINE_END);
        break;

    case 'b':
        st.max_reads = 50;
        st.noaffinity = 1;
        st.nortio = 1;
        st.no_rt = 1;
        break;
        
    case 'w':
        st.write_uncertain_to_file = optarg;
        break;

    case 'r':
        read_sectors_from_file = optarg;
        break;

    case 'l':
        log_path = optarg;
        break;

    case 'h':
    case '?':
        usage(&st);
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
      usage(&st);
      exit(EXIT_FAILURE);
    }

  if (st.filename == NULL)
    {
      printf("Missing -f parameter!%s\n", CLEAR_LINE_END);
      usage(&st);
      exit(EXIT_FAILURE);
    }

  if (st.exclusive)
    {
      if (st.min_reads == 0)
        st.min_reads = 1;
      if (st.max_reads == 0)
        st.max_reads = 20;
      if (st.max_std_dev == 0)
        st.max_std_dev = 0.75;
    }
  else if (st.quick)
    {
      if (st.min_reads == 0)
        st.min_reads = 1;
      if (st.max_reads == 0)
        st.max_reads = 50;
      if (st.max_std_dev == 0)
        st.max_std_dev = 0.75;
    }
  else
    {
      if (st.min_reads == 0)
        st.min_reads = 3;
      if (st.max_reads == 0)
        st.max_reads = 30;
      if (st.max_std_dev == 0)
        st.max_std_dev = 0.5;
    }

  if (st.vvfast_lvl < 0.0)
    st.vvfast_lvl = st.rotational_delay / 4;
  if (st.vfast_lvl < 0.0)
    st.vfast_lvl = st.rotational_delay / 2;
  if (st.fast_lvl < 0.0)
    st.fast_lvl = st.rotational_delay;
  if (st.normal_lvl < 0.0)
    st.normal_lvl = st.rotational_delay * 2;
  if (st.slow_lvl < 0.0)
    st.slow_lvl = st.rotational_delay * 4;
  if (st.vslow_lvl < 0.0)
    st.vslow_lvl = st.rotational_delay * 6;

  if (log_path != NULL)
    {
      st.flog = fopen(log_path, "w+");
      if (st.flog == NULL)
        err(EXIT_FAILURE, "log: open");

      fprintf(st.flog, "hdck v.%i.%i.%i log start\n", 
          version.major, version.minor, version.revision);
      fprintf(st.flog, "=========================\n");
      fprintf(st.flog, "Test parameters:\n");
      fprintf(st.flog, "min reads: %zi\n", st.min_reads);
      fprintf(st.flog, "max reads: %zi\n", st.max_reads);
      fprintf(st.flog, "max standard deviation: %f\n", st.max_std_dev);
      if(st.exclusive)
        {
          fprintf(st.flog, "Exclusive access specified\n");
        }
      if(st.quick)
        {
          fprintf(st.flog, "Quick mode!\n");
        }
      if(read_sectors_from_file != NULL)
        {
          fprintf(st.flog, "Testing only ranges specified in file %s\n", 
              read_sectors_from_file);
        }
      if(st.max_sectors != 0)
        {
          fprintf(st.flog, "Limiting device size to %lli sectors\n", 
              (long long)st.max_sectors);
        }
      fprintf(st.flog, "Testing device at %s\n", st.filename);
      fprintf(st.flog, "Assuming %.0frpm disk with %ziMiB cache\n", 
          1000/st.rotational_delay*60,
          st.disk_cache_size);
      fprintf(st.flog, "Block thresholds: %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, "
          "\n",
          st.vvfast_lvl,
          st.vfast_lvl,
          st.fast_lvl,
          st.normal_lvl,
          st.slow_lvl,
          st.vslow_lvl
          );
      fprintf(st.flog, "\n");
      fprintf(st.flog, "Runtime options: \n");
      fprintf(st.flog, "CPU affinity: %s\n", (st.noaffinity)?"off":"on");
      fprintf(st.flog, "RT IO: %s\n", (st.nortio)?"off":"on");
      fprintf(st.flog, "real time: %s\n", (st.no_rt)?"off":"on");
      fprintf(st.flog, "O_DIRECT: %s\n", (st.nodirect)?"off":"on");
      fprintf(st.flog, "O_SYNC: %s\n", (st.nosync)?"off":"on");
      fprintf(st.flog, "flush: %s\n", (st.noflush)?"off":"on");
      fprintf(st.flog, "\n");
      fflush(st.flog);
    }

  if (st.min_reads > st.max_reads)
    {
      fprintf(stderr, "Warning: min_reads bigger than max_reads, "
          "correcting%s\n", CLEAR_LINE_END);
      if (st.flog != NULL)
        fprintf(st.flog, "min reads bigger than max reads, correcting\n");
      st.max_reads = st.min_reads;
    }

  st.max_reads -= st.min_reads;

  struct timespec times, timee; /**< wall clock start and end */

  int dev_fd = 0;

  // make the process real-time
  if (!st.no_rt)
    make_real_time();

  // make the process run on single core
  if (!st.noaffinity)
    {
      set_affinity();
    }

  // make the process' IO prio highest 
  if (!st.nortio)
    {
      set_rt_ioprio();
    }
 
  int flags = O_RDONLY | O_LARGEFILE; 
  if (st.verbosity > 5)
    printf("setting O_RDONLY flag on file\n");
  if (st.verbosity > 5)
    printf("setting O_LARGEFILE flag on file\n");

  // open the file with disabled caching
  if (!st.nodirect)
    {
      if (st.verbosity > 5)
        printf("setting O_DIRECT flag on file\n");
      flags = flags | O_DIRECT;
    }
  else
    {
      if (st.verbosity > 5)
        printf("NOT setting O_DIRECT on file\n");
    }

  // no sync on file
  if (!st.nosync)
    {
      if (st.verbosity > 5)
        printf("setting O_SYNC flag on file\n");
      flags = flags | O_SYNC;
    }
  else
    {
      if (st.verbosity > 5)
        printf("NOT setting O_SYNC on file\n");
    }

  // use exclusive mode
  if (st.exclusive)
    {
      if (st.verbosity > 5)
        printf("setting O_EXCL on file\n");
      flags = flags | O_EXCL;
    }
  else
    {
      if (st.verbosity > 5)
        printf("NOT setting O_EXCL on file\n");
    }

  dev_fd = open(st.filename, flags);
  if (dev_fd < 0)
    {
      err(EXIT_FAILURE, "open");
    }

  st.filesize = get_file_size(&st, dev_fd);

  // we can't realiably read last sector anyway, so round the disk size down
  st.filesize = floorl(st.filesize*1.L/512/st.sectors)*512*st.sectors;
  if (!st.filesize)
    {
      fprintf(stderr, "Device too small, needs to be at least %lli bytes in "
          "size\n", ((off_t)512)*(long long)st.sectors);
      exit(EXIT_FAILURE);
    }

  if (st.filesize / 512 / st.sectors * 2 > (off_t)SIZE_MAX)
    {
      fprintf(stderr, "File too big, devices this big are supported only on "
          "64 bit OSs\n");
      exit(EXIT_FAILURE);
    }

  st.dev_stat_path = get_file_stat_sys_name(&st, st.filename);

  fesetround(2); // interger rounding rounds UP
  if (st.max_sectors == 0)
    st.number_of_blocks = lrintl(ceill(st.filesize*1.0L/512/st.sectors));
  else
    st.number_of_blocks = lrintl(ceill(st.max_sectors*1.0L/st.sectors));
  block_info = calloc(st.number_of_blocks, 
      sizeof(struct block_info_t));
  if (!block_info)
    {
      fprintf(stderr, "Allocation error, tried to allocate %lli bytes:", 
          (long long)st.number_of_blocks * sizeof(struct block_info_t));
      err(EXIT_FAILURE, "calloc");
    }

  fsync(dev_fd);

  if (!st.noflush)
    {
      // Attempt to free all cached pages related to the opened file
      if (posix_fadvise(dev_fd, 0, 0, POSIX_FADV_DONTNEED) < 0)
        err(EXIT_FAILURE, NULL);
      if (posix_fadvise(dev_fd, 0, 0, POSIX_FADV_NOREUSE) < 0)
        err(EXIT_FAILURE, NULL);
    }

  if (st.verbosity > 2)
    {
      printf("min-reads: %zi, max re-reads: %zi, max rel std dev %f, "
          "disk cache size: %ziMiB\n",
         st.min_reads,
         st.max_reads,
         st.max_std_dev,
         st.disk_cache_size); 
    }

  clock_gettime(TIMER_TYPE, &times);

  /*
   * MAIN LOOP
   */
  time_t current_time;
  current_time = time(NULL);
  if (st.flog != NULL)
    fprintf(st.flog, "\nbegin testing: %s\n", asctime(localtime(&current_time)));
  if(read_sectors_from_file == NULL)
    {
      read_whole_disk(&st, dev_fd, block_info, st.dev_stat_path, st.min_reads, 
          st.sector_times, st.max_sectors, st.filesize);
    }
  else
    {
      struct block_list_t* block_list;

      block_list = read_list_from_file(&st, read_sectors_from_file);

      if(block_list == NULL)
        {
          printf("File \'%s\' is empty\n", read_sectors_from_file);
          exit(EXIT_FAILURE);
        }

      for (size_t i=0; i < st.min_reads; i++)
        read_block_list(&st, dev_fd, block_list, block_info, st.dev_stat_path,
            st.number_of_blocks);

      free(block_list);
    }

  if (st.verbosity >= 0)
    printf("\r%s\n", cursor_down(18));

  current_time = time(NULL);
  if(st.flog != NULL)
    fprintf(st.flog, "end of main loop: %s\n", asctime(localtime(&current_time)));

  /*
   * REREADS
   */
  perform_re_reads(&st, dev_fd, st.dev_stat_path, block_info, st.number_of_blocks,
      st.max_reads, st.max_std_dev, st.min_reads, st.rotational_delay);

  current_time = time(NULL);
  if(st.flog != NULL)
    fprintf(st.flog, "end of rereads: %s\n", asctime(localtime(&current_time)));

  /*
   * RAPORTING
   * print uncertain and bad blocks
   */
  struct block_list_t* block_list;

  block_list = find_bad_blocks(&st,
      block_info, st.number_of_blocks, st.max_std_dev, st.min_reads, 1, 0,
      st.rotational_delay, 0, 1);

  if (st.verbosity >= 0)
    printf("%s\nhdck results:%s\n"
               "=============%s\n", CLEAR_LINE, CLEAR_LINE_END, CLEAR_LINE_END);
  if(st.flog != NULL)
    fprintf(st.flog, "results:\n");

  if (block_list == NULL)
    {

      if (st.write_uncertain_to_file != NULL)
        {
          // zero out the file
          block_list = calloc(sizeof(struct block_list_t), 1);

          write_list_to_file(&st, st.write_uncertain_to_file, block_list);

          free(block_list);
          block_list = NULL;
        }

      if (st.verbosity >= 0)
        printf("no problematic blocks found!%s\n", CLEAR_LINE_END);
      if(st.flog != NULL)
        fprintf(st.flog, "no problematic blocks found!\n");
    }
  else
    {
      if (st.verbosity >= 0)
        printf("possible latent bad sectors or silent realocations:%s\n", 
            CLEAR_LINE_END);
      if (st.flog != NULL)
        fprintf(st.flog, "possible latent bad sectors or silent realocations:\n");

      size_t block_number=0;
      while (!(block_list[block_number].off == 0 && 
          block_list[block_number].len == 0))
        {
          size_t start = block_list[block_number].off,
                end = start + block_list[block_number].len;

          for(size_t i= start; i< end; i++)
            {
              double stdev = bi_int_rel_stdev(&block_info[i]);

              if (st.verbosity >= 0)
                printf("block %zi (LBA: %lli-%lli) rel std dev: %5.2f"
                  ", avg: %5.2f, valid: %s, samples: %zi, 9th decile: %5.2f%s\n", 
                  i,
                  ((off_t)i)*(long long)st.sectors,
                  ((off_t)i+1)*(long long)st.sectors-1,
                  stdev,
                  bi_average(&block_info[i]),
                  (bi_is_valid(&block_info[i]))?"yes":"no",
                  bi_num_samples(&block_info[i]),
                  bi_quantile(&block_info[i],9,10),
                  CLEAR_LINE_END);

              if (st.flog != NULL)
                fprintf(st.flog, "block %zi (LBA: %lli-%lli) rel std dev: %5.2f"
                  ", avg: %5.2f, valid: %s, samples: %zi, 9th decile: %5.2f\n", 
                  i,
                  ((off_t)i)*(long long)st.sectors,
                  ((off_t)i+1)*(long long)st.sectors-1,
                  stdev,
                  bi_average(&block_info[i]),
                  (bi_is_valid(&block_info[i]))?"yes":"no",
                  bi_num_samples(&block_info[i]),
                  bi_quantile(&block_info[i],9,10));
            }
          block_number++;
        }

      fflush(stdout);

      if (st.verbosity >= 0)
        printf("%zi uncertain blocks found%s\n", block_number,
            CLEAR_LINE_END);
      if (st.flog != NULL)
        fprintf(st.flog, "%zi uncertain blocks found\n", block_number);

      if (st.write_uncertain_to_file != NULL)
        write_list_to_file(&st, st.write_uncertain_to_file, block_list);

      free(block_list);
    }

  clock_gettime(TIMER_TYPE, &timee);

  diff_time(&res, times, timee);
  if (st.verbosity >= 0)
    printf("%s\nwall time: %lis.%lims.%liµs.%lins%s\n", CLEAR_LINE, res.tv_sec,
        res.tv_nsec/1000000, res.tv_nsec/1000%1000,
        res.tv_nsec%1000, CLEAR_LINE_END);

  if (st.flog != NULL)
    fprintf(st.flog, "\nwall time: %lis.%lims.%liµs.%lins\n", res.tv_sec,
        res.tv_nsec/1000000, res.tv_nsec/1000%1000,
        res.tv_nsec%1000);

  long double sum = 0.0;
  long long reads = 0;
  struct block_info_t single_block;

  bi_init(&single_block);

  for (size_t i=0; i < st.number_of_blocks; i++)
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

  if (st.verbosity >= 0)
    printf("sum time: %.0fs.%.0fms.%.0fµs%s\n",
      sec,
      msec,
      usec, 
      CLEAR_LINE_END);
  if (st.flog != NULL)
    fprintf(st.flog, "sum time: %.0fs.%.0fms.%.0fµs\n",
      sec,
      msec,
      usec);

  if (st.verbosity >= 0)
    printf("tested %lli blocks (%lli errors, %lli samples)%s\n", 
        (long long)st.number_of_blocks, st.errors, reads, CLEAR_LINE_END);
  if (st.flog != NULL)
    fprintf(st.flog, "tested %lli blocks (%lli errors, %lli samples)\n", 
        (long long)st.number_of_blocks, st.errors, reads);

  sum = bi_average(&single_block);

  sec = floor(sum / 1000);
  msec = floor(sum - sec * 1000);
  usec = floor((sum - sec * 1000 - msec)*1000);

  if (st.verbosity >= 0)
    printf("mean block time: %.0fs.%.0fms.%.0fµs%s\n",
      sec,
      msec,
      usec, CLEAR_LINE_END);
  if (st.flog != NULL)
    fprintf(st.flog, "mean block time: %.0fs.%.0fms.%.0fµs\n",
      sec,
      msec,
      usec);

  if (st.verbosity >= 0)
    printf("std dev: %.9f(ms)%s\n",
        bi_stdev(&single_block), CLEAR_LINE_END);
  if (st.flog != NULL)
    fprintf(st.flog, "std dev: %.9f(ms)\n",
        bi_stdev(&single_block));

  bi_clear(&single_block);

  update_block_stats(&st, block_info);

  if (st.verbosity >= 0)
    printf("Number of invalid blocks because of detected "
      "interrupted reads: %lli\n", st.invalid);
  if (st.flog != NULL)
    fprintf(st.flog, "Number of invalid blocks because of detected "
        "interrupted reads: %lli\n", st.invalid);

  if (st.verbosity >= 0)
    printf("Number of interrupted reads: %lli\n", st.tot_interrupts);
  if (st.flog != NULL)
    fprintf(st.flog, "Number of interrupted reads: %lli\n", st.tot_interrupts);

  if (st.verbosity >= 0)
    printf("Individual block statistics:\n<%02.2fms: %lli\n"
        "<%02.2fms: %lli\n<%2.2fms: %lli\n<%2.2fms: %lli\n<%2.2fms: %lli\n"
        "<%2.2fms: %lli\n>%2.2fms: %lli\nERR: %lli\n",
      st.vvfast_lvl, st.vvfast, st.vfast_lvl, st.vfast,
      st.fast_lvl, st.fast, st.normal_lvl, st.normal,
      st.slow_lvl, st.slow, st.vslow_lvl, st.vslow, 
      st.vslow_lvl, st.vvslow, st.errors);
  if (st.flog != NULL)
    fprintf(st.flog, "Individual block statistics:\n<%02.2fms: %lli\n"
        "<%02.2fms: %lli\n<%2.2fms: %lli\n<%2.2fms: %lli\n<%2.2fms: %lli\n"
        "<%2.2fms: %lli\n>%2.2fms: %lli\nERR: %lli\n",
      st.vvfast_lvl, st.vvfast, st.vfast_lvl, st.vfast,
      st.fast_lvl, st.fast, st.normal_lvl, st.normal,
      st.slow_lvl, st.slow, st.vslow_lvl, st.vslow, 
      st.vslow_lvl, st.vvslow, st.errors);

  if (st.verbosity >= 0)
    printf("%s\n", CLEAR_LINE_END);
  if (st.flog != NULL)
    fprintf(st.flog, "\n");

  struct block_list_t *worst_blocks;
  worst_blocks = find_worst_blocks(&st, block_info, st.number_of_blocks,
      10);

  if (st.verbosity >= 0)
    printf("Worst blocks:%s\n", CLEAR_LINE_END);
  if (st.flog != NULL)
    fprintf(st.flog, "Worst blocks:\n");
  if (st.verbosity >= 0)
    printf("block no      st.dev  avg   1stQ    med     3rdQ   valid "
        "samples 9th decile%s\n", CLEAR_LINE_END);
  if (st.flog != NULL)
    fprintf(st.flog, "block no      st.dev  avg   1stQ     med     3rdQ  valid "
        "samples 9th decile\n");

  size_t block_number=0;
  while (!(worst_blocks[block_number].off == 0 && 
      worst_blocks[block_number].len == 0))
    {
      size_t start = worst_blocks[block_number].off,
            end = start + worst_blocks[block_number].len;

      for(size_t i= start; i< end; i++)
        {
          double stdev = bi_int_rel_stdev(&block_info[i]);
          stdev = bi_stdev(&block_info[i]);

          if (st.verbosity >= 0)
            printf("%12zi %7.4f %6.2f %7.2f %7.2f %7.2f  %s %3zi %9.2f%s\n", 
                i, 
                stdev,
                bi_average(&block_info[i]),
                bi_quantile(&block_info[i],1,4),
                bi_quantile(&block_info[i],2,4),
                bi_quantile(&block_info[i],3,4),
                (bi_is_valid(&block_info[i]))?"yes":"no ",
                bi_num_samples(&block_info[i]),
                bi_quantile(&block_info[i],9,10),
                CLEAR_LINE_END);

          if (st.flog != NULL)
            fprintf(st.flog, "%12zi %7.4f %6.2f %7.2f %7.2f %7.2f  %s %3zi %9.2f\n", 
                i, 
                stdev,
                bi_average(&block_info[i]),
                bi_quantile(&block_info[i],1,4),
                bi_quantile(&block_info[i],2,4),
                bi_quantile(&block_info[i],3,4),
                (bi_is_valid(&block_info[i]))?"yes":"no ",
                bi_num_samples(&block_info[i]),
                bi_quantile(&block_info[i],9,10)
                );
        }
      block_number++;
    }

  free(worst_blocks);

  if (st.verbosity >= 0)
    printf("%s\n", CLEAR_LINE_END);
  if (st.flog != NULL)
    fprintf(st.flog, "\n");

  printf("Disk status: ");

  if (st.flog != NULL)
    fprintf(st.flog, "\nDisk status: ");

  if (st.errors != 0)
    {
      printf("FAILED\n"
          "CAUTION! Bad sectors detected, copy data off this "
          "disk AS SOON AS POSSIBLE!\n");
      if (st.flog != NULL)
        fprintf(st.flog, "FAILED\n"
            "CAUTION! Bad sectors detected, copy data off this "
            "disk AS SOON AS POSSIBLE!\n");
    }
  else if (st.vvslow != 0)
    {
      printf("CRITICAL\n"
          "CAUTION! Sectors that required more than 6 read "
          "attempts detected, drive may be ALREADY FAILING!\n");
      if (st.flog != NULL)
        fprintf(st.flog, "CRITICAL\n"
            "CAUTION! Sectors that required more than 6 read "
            "attempts detected, drive may be ALREADY FAILING!\n");
    }
  else if (st.vslow != 0)
    {
      printf("very bad\n"
          "sectors that required more than 4 read attempts "
          "detected!\n");
      if (st.flog != NULL)
        fprintf(st.flog, "very bad\n"
            "sectors that required more than 4 read attempts "
            "detected!\n");
    }
  else if (st.slow != 0)
    {
      if (!st.quick || st.exclusive)
        {
          printf("bad\n"
              "sectors that required more than 2 read attempts "
              "detected\n");
          if (st.flog != NULL)
            fprintf(st.flog, "bad\n"
                "sectors that required more than 2 read attempts "
                "detected\n");
        }
      else
        {
          printf("moderate\n"
              "sectors that required more than 2 read attempts "
              "detected\n");
          if (st.flog != NULL)
            fprintf(st.flog, "moderate\n"
                "sectors that required more than 2 read attempts "
                "detected\n");
        }
    }
  else if (((st.normal * 1.0) / (st.number_of_blocks * 1.0) > 0.001 && !st.quick)
      || ((st.normal * 1.0) / (st.number_of_blocks * 1.0) > 0.25 && st.quick))
    {
      printf("moderate\n"
          "high number of blocks that required more than 1 "
          "read attempt detected\n");
      if (st.flog != NULL) 
        fprintf(st.flog, "moderate\n"
            "high number of blocks that required more than 1 "
            "read attempt detected\n");
    }
  else if (st.normal == 0)
    {
      if ((st.fast * 1.0) / (st.number_of_blocks * 1.0) < 0.1)
        {
          printf("excellent\n");
          if (st.flog != NULL)
            fprintf(st.flog, "excellent\n");
        }
      else
        {
          printf("very good\n"
            "no blocks that required constant re-reads "
            "detected\n");
      
          if (st.flog != NULL)
            fprintf(st.flog, "very good\n"
              "no blocks that required constant re-reads "
              "detected\n");
        }
    }
  else
    {
      printf("good\n"
          "few blocks that required more than 1 read attempt "
          "detected\n");
      if (st.flog != NULL)
        fprintf(st.flog, "good\n"
          "few blocks that required more than 1 read attempt "
          "detected\n");
    }

  if (st.verbosity > 2)
    {
      printf("\nraw read statistics:\n"); 
      printf("ERR: %lli\n<%.2fms:  %lli\n<%.2fms:  %lli\n<%.2fms: %lli\n"
          "<%.2fms: %lli\n"
          "<%.2fms: %lli\n<%.2fms: %lli\n>%.2fms: %lli\n",
          st.tot_errors, st.vvfast_lvl, st.tot_vvfast, st.vfast_lvl,
          st.tot_vfast, st.fast_lvl, st.tot_fast, st.normal_lvl,
          st.tot_normal, st.slow_lvl, st.tot_slow, st.vslow_lvl, 
          st.tot_vslow, st.vslow_lvl, st.tot_vvslow);

      double sec = floor(st.tot_sum / 1000);
      double msec = floor(st.tot_sum - sec * 1000);
      double usec = floor((st.tot_sum - sec * 1000 - msec)*1000);

      printf("sum time: %.0fs.%.0fms.%.0fµs\n",
        sec,
        msec,
        usec);

      long double avg = st.tot_sum / st.tot_samples;

      sec = floor(avg / 1000);
      msec = floor(avg - sec * 1000);
      usec = floor((avg - sec * 1000 - msec)*1000);

      printf("mean block time: %.0fs.%.0fms.%.0fµs\n",
        sec,
        msec,
        usec);
    }

  if (st.output != NULL)
    {
      write_to_file(&st, st.output, block_info, st.number_of_blocks);
    }

  free(st.dev_stat_path);
  for(size_t i=0; i< st.number_of_blocks; i++)
    bi_clear(&block_info[i]);
  free(block_info);
  if (st.verbosity >= 0)
    printf("\n");
  if (st.flog != NULL)
    fprintf(st.flog, "\nhdck log end");
  if (st.flog != NULL)
    fclose(st.flog);
  return EXIT_SUCCESS;
}
