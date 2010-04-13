#ifndef __BLOCK_INFO_H
#define __BLOCK_INFO_H 1

#ifdef __GNUC__
#define PURE_FUNCTION  __attribute__ ((pure))
#else
#define PURE FUNCTION
#endif

/// information about a single block (256 sectors by default)
struct block_info_t {
    double* samples; ///< measurements for the block
    size_t samples_len; ///< number of samples taken
    short int valid; ///< 0 if data is invalid (because read was interrupted)
    unsigned short int error; ///< number of IO errors that occured while reading
                              /// the block
};

/**
 * reset the block_info struct
 * does not reset number of block errors!
 */
void
bi_clear(struct block_info_t* block_info);

/**
 * initalises the block_info_t struct
 */
void
bi_init(struct block_info_t* block_info);

/**
 * checks if block_info had been initialised (whatever any data has been written
 * to it)
 */
int
bi_is_initialised(struct block_info_t* block_info) PURE_FUNCTION;

/**
 * add time (in ms) to samples inside block_info
 */
void
bi_add_time(struct block_info_t* block_info, double time);

/** 
 * add anouther block_info 
 */
void
bi_add(struct block_info_t* sum, struct block_info_t* adder);

/** 
 * add one block_info to another, only if their state matches
 */
void
bi_add_valid(struct block_info_t* sum, struct block_info_t* adder);

/** 
 * removes last sample
 */
void
bi_remove_last(struct block_info_t* block_info);

/** 
 * check if block_info is valid
 */
int
bi_is_valid(struct block_info_t* block_info) PURE_FUNCTION;

/** 
 * set block info to be valid
 */
void
bi_make_valid(struct block_info_t* block_info);

/** 
 * set block info to be invalid
 */
void
bi_make_invalid(struct block_info_t* block_info);

/** 
 * returns individual sample times
 */
double*
bi_get_times(struct block_info_t* block_info) PURE_FUNCTION;

/** 
 * return standard deviation for samples
 */
double
bi_stdev(struct block_info_t* block_info) PURE_FUNCTION;

/**
 * find longest read in samples
 */
double
bi_max(struct block_info_t* block_info) PURE_FUNCTION;

/**
 * return relative standard deviation for samples (stdev/mean)
 */
double
bi_rel_stdev(struct block_info_t* block_info) PURE_FUNCTION;

/** 
 * return arithemtic average for samples
 */
double
bi_average(struct block_info_t* block_info) PURE_FUNCTION;

/**
 * return sum of samples
 */
double
bi_sum(struct block_info_t* block_info) PURE_FUNCTION;

/** 
 * return number of collected samples
 */
int
bi_num_samples(struct block_info_t* block_info) PURE_FUNCTION;

/** 
 * return truncated average for samples
 * @parm percent how much data is to be thrown off
 */
double
bi_trunc_average(struct block_info_t* block_info, double percent) PURE_FUNCTION;

/** 
 * return inteligent mean for samples
 */
double
bi_int_average(struct block_info_t* block_info) PURE_FUNCTION;

/** 
 * return truncated standard deviation for samples
 * @param percent how much data is to be thrown off
 */
double
bi_trunc_stdev(struct block_info_t* block_info, double percent) PURE_FUNCTION;

/** 
 * return truncated relative standard deviation (stdev/mean) for samples 
 * @param percent how much data is to be thrown off
 */
double
bi_trunc_rel_stdev(struct block_info_t* block_info, double percent) PURE_FUNCTION;

/** 
 * "inteligent" truncated rel_st_dev
 */
double
bi_int_rel_stdev(struct block_info_t* block_info) PURE_FUNCTION;

/** 
 * set that an error occured while reading the block
 */
void
bi_add_error(struct block_info_t* block_info);

/** 
 * get number of errors that occured while reading the block
 */
int
bi_get_error(struct block_info_t* block_info) PURE_FUNCTION;

#endif
