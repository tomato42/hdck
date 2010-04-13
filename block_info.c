#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <stdint.h>
#include <err.h>
#include <string.h>
#include <assert.h>
#include "block_info.h"


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

/**
 * initalises the block_info_t struct
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

/** 
 * add anouther block_info 
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

/** 
 * add one block_info to another, only if their state matches
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

/** 
 * removes last sample
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
 * check if block_info is valid
 */
int
bi_is_valid(struct block_info_t* block_info)
{
  return block_info->valid;
}

/** 
 * set block info to be valid
 */
void
bi_make_valid(struct block_info_t* block_info)
{
  block_info->valid = 1;
}

/** 
 * set block info to be invalid
 */
void
bi_make_invalid(struct block_info_t* block_info)
{
  block_info->valid = 0;
}

/** 
 * returns individual sample times
 */
double*
bi_get_times(struct block_info_t* block_info)
{
  return block_info->samples;
}

/** 
 * return standard deviation for samples
 */
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

/**
 * return relative standard deviation for samples (stdev/mean)
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

/** 
 * return arithemtic average for samples
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

/**
 * return sum of samples
 */
double
bi_sum(struct block_info_t* block_info)
{
  long double sum = 0.0;

  for (int i=0; i< block_info->samples_len; i++)
    sum += block_info->samples[i];

  return sum;
}

/** 
 * return number of collected samples
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

/** 
 * return truncated average for samples
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

/** 
 * return inteligent mean for samples
 */
double
bi_int_average(struct block_info_t* block_info)
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

/** 
 * return truncated standard deviation for samples
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

/** 
 * return truncated relative standard deviation (stdev/mean) for samples 
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

/** 
 * "inteligent" truncated rel_st_dev
 */
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

/** 
 * get number of errors that occured while reading the block
 */
int
bi_get_error(struct block_info_t* block_info)
{
  return block_info->error;
}

