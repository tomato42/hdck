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
  block_info->last = 0.0;
  block_info->decile = 0.0;
}

/**
 * initialises the block_info_t struct
 */
void
bi_init(struct block_info_t* block_info)
{
  block_info->samples = NULL;
  block_info->samples_len = 0;
  block_info->valid = 0;
  block_info->error = 0;
  block_info->last = 0;
  block_info->decile = 0.0;
  block_info->initialized = 0;
}

/**
 */
inline int
bi_is_initialised(struct block_info_t* block_info)
{
  return block_info->initialized;
  /*
  if (block_info->samples != NULL ||
      block_info->samples_len != 0 ||
      block_info->valid != 0 ||
      block_info->error != 0)
    return 1;
  else
    return 0;*/
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
      block_info->last = time;
      block_info->decile = time;
      block_info->initialized = 1;
    }
  else
    {
      block_info->samples_len++;
      block_info->samples = realloc(block_info->samples,
          sizeof(double) * block_info->samples_len);

      if (!block_info->samples)
        err(1, "bi_add_time");

      block_info->samples[block_info->samples_len-1] = time;
      block_info->last = time;
      block_info->decile = 0.0;
      block_info->initialized = 1;
    }
}

/**
 * add another block_info
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
      sum->last = adder->last;
      sum->decile = adder->decile;
    }
  else
    {
      sum->samples = realloc(sum->samples, sizeof(double)*(
            sum->samples_len + adder->samples_len));

      if (!sum->samples)
        err(1, "bi_add");

      for(size_t i=0; i< adder->samples_len; i++)
        sum->samples[sum->samples_len + i] = adder->samples[i];

      sum->samples_len += adder->samples_len;
      sum->last = adder->last;
      sum->decile = 0.0;
    }

  sum->initialized &= adder->initialized;
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
    {
      // swap places of the last sample and last position in the array
      for (size_t i=0; i< block_info->samples_len; i++)
        if (block_info->samples[i] == block_info->last)
          {
            block_info->samples[i]
              = block_info->samples[block_info->samples_len-1];
            break;
          }
      block_info->samples_len--;
      block_info->decile = 0.0;
    }
  else
    {
      free(block_info->samples);
      block_info->samples = NULL;
      block_info->samples_len = 0;
      block_info->last = 0.0;
      block_info->decile = 0.0;
      block_info->valid = 0;
      // still initialized, errors are preserved
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
 * DO NOT free returned data, DO NOT modify returned data
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
  size_t n = 0;
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

  return sqrt(M2 / (n - 1));
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

  for (size_t i=0; i < block_info->samples_len; i++)
    if (ret < block_info->samples[i])
      ret = block_info->samples[i];

  return ret;
}

/**
 * find shortest read in samples
 */
double
bi_min(struct block_info_t* block_info)
{
  double ret;

  if (block_info->samples_len == 0)
    return 0.0;

  ret = block_info->samples[0];

  for (size_t i=0; i < block_info->samples_len; i++)
    if (ret > block_info->samples[i])
      ret = block_info->samples[i];

  return ret;
}

/**
 * return relative standard deviation for samples (stdev/mean)
 */
double
bi_rel_stdev(struct block_info_t* block_info)
{
  size_t n = 0;
  long double mean = 0.0;
  long double M2 = 0.0;
  long double delta;
  long double sum = 0.0;

  for (size_t i=0; i < block_info->samples_len; i++)
    {
      n++;
      delta = block_info->samples[i] - mean;
      mean += delta/n;
      M2 += delta * (block_info->samples[i] - mean);
      sum += block_info->samples[i];
    }

  return (sqrt(M2 / (n - 1))) / (sum / n);
}

/**
 * return arithmetic average for samples
 */
double
bi_average(struct block_info_t* block_info)
{
  long double sum = 0.0;
  size_t i;

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

  for (size_t i=0; i< block_info->samples_len; i++)
    sum += block_info->samples[i];

  return sum;
}

/**
 * return number of collected samples
 */
size_t
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

  size_t low, high;

  low = ceill(percent / 2 * block_info->samples_len);
  high = floorl(block_info->samples_len - percent / 2 *
    block_info->samples_len);

  if (high == low)
    {
      free(tmp);
      return 0;
    }

  size_t i;
  long double sum = 0.0;

  for (i = low; i < high; i++)
    {
      sum += tmp[i];
    }

  free (tmp);

  return sum / (high - low);
}

/**
 * return k-th quantile of q order using interpolating algorithm
 * @parm block_info block data to analyse
 * @parm k ordinal of quantile to return (q/2 for median)
 * @parm q order of quantiles (4 for quartiles, 10 for deciles)
 */
double
bi_quantile(struct block_info_t* block_info, int k, int q)
{
  assert(k<=q);

  double p = k*1.0/(q*1.0);

  if (block_info->samples_len == 0)
    return NAN;

  if (block_info->samples_len == 1)
    return block_info->samples[0];

  if (block_info->decile != 0.0 && p == 0.9)
    return block_info->decile;

  // save the sorted samples, but sort them only if they are unsorted
  double *tmp = block_info->samples;
  if (block_info->decile == 0.0)
    qsort(tmp,
      block_info->samples_len, sizeof(double), __double_sort);

  // find quantile
  double h;

  h = (block_info->samples_len-1)*p+1-1;

  size_t h_fl = floor(h);

  if (p == 0.9)
    {
      block_info->decile = tmp[h_fl] + (h-h_fl)*(tmp[h_fl+1]-tmp[h_fl]);
      return block_info->decile;
    }
  else
    {
      if (block_info->decile == 0.0)
        { // save that we sorted the data
          double k = (block_info->samples_len-1)*0.9+1-1;
          int k_fl = floor(k);
          block_info->decile = tmp[k_fl] + (k-k_fl)*(tmp[k_fl+1]-tmp[k_fl]);
        }

      return tmp[h_fl] + (h-h_fl)*(tmp[h_fl+1]-tmp[h_fl]);
    }
}

/**
 * return k-th quantile of q order using exact algorithm
 * @parm block_info block data to analyse
 * @parm k ordinal of quantile to return (q/2 for median)
 * @parm q order of quantiles (4 for quartiles, 10 for deciles)
 */
double
bi_quantile_exact(struct block_info_t* block_info, int k, int q)
{
  assert(k<=q);

  if (block_info->samples_len == 1)
    return block_info->samples[0];

  // sort samples
  double *tmp = block_info->samples;

  if (block_info->decile == 0.0)
    qsort(tmp, block_info->samples_len, sizeof(double), __double_sort);

  // find quantile
  double h;
  double p = k*1.0/(q*1.0);
  double ret;

  h = (block_info->samples_len)*p;

  int h_fl = nearbyint(h)-1;
  if (h_fl < 0) h_fl = 0;

  ret = tmp[h_fl];

  if (block_info->decile == 0.0)
    { // save that we sorted the data
      double k = (block_info->samples_len-1)*0.9+1-1;
      int k_fl = floor(k);
      block_info->decile = tmp[k_fl] + (k-k_fl)*(tmp[k_fl+1]-tmp[k_fl]);
    }
  return ret;
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

  size_t low, high;

  low = ceill(percent / 2 * block_info->samples_len);
  high = floorl(block_info->samples_len - percent / 2 * block_info->samples_len);

  if (high == low)
    {
      free(tmp);
      return 0;
    }

  size_t n = 0;
  long double mean = 0.0;
  long double M2 = 0.0;
  long double delta;

  for (size_t i=low; i < high; i++)
    {
      n++;
      delta = tmp[i] - mean;
      mean += delta/n;
      M2 += delta * (tmp[i] - mean);
    }

  free(tmp);

  return sqrt(M2 / (n - 1));
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

  size_t low, high;

  low = ceill(percent / 2 * block_info->samples_len);
  high = floorl(block_info->samples_len - percent / 2 * block_info->samples_len);

  if (high == low)
    {
      free(tmp);
      return 0;
    }

  size_t n = 0;
  long double mean = 0.0;
  long double M2 = 0.0;
  long double delta;
  long double sum = 0.0;

  for (size_t i=low; i < high; i++)
    {
      n++;
      delta = tmp[i] - mean;
      mean += delta/n;
      M2 += delta * (tmp[i] - mean);
      sum += tmp[i];
    }

  free(tmp);

  return (sqrt(M2 / (n - 1))) / (sum / n);
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
 * set that an error occurred while reading the block
 */
void
bi_add_error(struct block_info_t* block_info)
{
  if (!bi_is_initialised(block_info))
    {
        bi_init(block_info);
        block_info->initialized = 1;
    }
  block_info->error++;
}

/**
 * get number of errors that occurred while reading the block
 */
int
bi_get_error(struct block_info_t* block_info)
{
  return block_info->error;
}

