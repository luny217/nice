/* */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>

#include "base.h"
#include "nlist.h"
#include "random-glib.h"


/* Period parameters */
#define N 624
#define M 397
#define MATRIX_A 0x9908b0df   /* constant vector a */
#define UPPER_MASK 0x80000000 /* most significant w-r bits */
#define LOWER_MASK 0x7fffffff /* least significant r bits */

/* Tempering parameters */
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000
#define TEMPERING_SHIFT_U(y)  (y >> 11)
#define TEMPERING_SHIFT_S(y)  (y << 7)
#define TEMPERING_SHIFT_T(y)  (y << 15)
#define TEMPERING_SHIFT_L(y)  (y >> 18)

typedef struct _n_rand_st n_rand_t;

struct _n_rand_st
{
	uint32_t mt[N]; /* the array for the state vector  */
	uint32_t mti;
};

n_rand_t * n_rand_new_with_seed_array(const uint32_t *seed, uint32_t  seed_length);

void n_rand_set_seed(n_rand_t * rand, uint32_t  seed);

void n_rand_set_seed_array(n_rand_t  *rand,	const uint32_t *seed, uint32_t seed_length)
{
	uint32_t i, j, k;
	
	n_rand_set_seed(rand, 19650218UL);

	i = 1; j = 0;
	k = (N > seed_length ? N : seed_length);
	for (; k; k--)
	{
		rand->mt[i] = (rand->mt[i] ^
			((rand->mt[i - 1] ^ (rand->mt[i - 1] >> 30)) * 1664525UL))
			+ seed[j] + j; /* non linear */
		rand->mt[i] &= 0xffffffffUL; /* for WORDSIZE > 32 machines */
		i++; j++;
		if (i >= N)
		{
			rand->mt[0] = rand->mt[N - 1];
			i = 1;
		}
		if (j >= seed_length)
			j = 0;
	}
	for (k = N - 1; k; k--)
	{
		rand->mt[i] = (rand->mt[i] ^
			((rand->mt[i - 1] ^ (rand->mt[i - 1] >> 30)) * 1566083941UL))
			- i; /* non linear */
		rand->mt[i] &= 0xffffffffUL; /* for WORDSIZE > 32 machines */
		i++;
		if (i >= N)
		{
			rand->mt[0] = rand->mt[N - 1];
			i = 1;
		}
	}

	rand->mt[0] = 0x80000000UL; /* MSB is 1; assuring non-zero initial array */
}

n_rand_t * n_rand_new_with_seed_array(const uint32_t *seed, uint32_t  seed_length)
{
	n_rand_t *rand = malloc(sizeof(n_rand_t));
	n_rand_set_seed_array(rand, seed, seed_length);
	return rand;
}


/**
* g_rand_new:
*
* Creates a new random number generator initialized with a seed taken
* either from `/dev/urandom` (if existing) or from the current time
* (as a fallback).
*
* On Windows, the seed is taken from rand_s().
*
* Returns: the new #GRand
*/
n_rand_t * n_rand_new(void)
{
	uint32_t seed[4];

#ifndef _WIN32
	static int dev_urandom_exists = TRUE;
	n_timeval_t now;

	if (dev_urandom_exists)
	{
		FILE* dev_urandom;

		do
		{
			dev_urandom = fopen("/dev/urandom", "rb");
		} while G_UNLIKELY(dev_urandom == NULL && errno == EINTR);

		if (dev_urandom)
		{
			int r;

			setvbuf(dev_urandom, NULL, _IONBF, 0);
			do
			{
				errno = 0;
				r = fread(seed, sizeof(seed), 1, dev_urandom);
			} while G_UNLIKELY(errno == EINTR);

			if (r != 1)
				dev_urandom_exists = FALSE;

			fclose(dev_urandom);
		}
		else
			dev_urandom_exists = FALSE;
	}

	if (!dev_urandom_exists)
	{
		get_current_time(&now);
		seed[0] = now.tv_sec;
		seed[1] = now.tv_usec;
		seed[2] = getpid();
		seed[3] = getppid();
	}
#else /* _linux */

	n_timeval_t now;

	get_current_time(&now);
	seed[0] = now.tv_sec;
	seed[1] = now.tv_usec;
	seed[2] = 0;
	seed[3] = 0;

#endif

	return n_rand_new_with_seed_array(seed, 4);
}
/**
* g_rand_set_seed:
* @rand_: a #GRand
* @seed: a value to reinitialize the random number generator
*
* Sets the seed for the random number generator #GRand to @seed.
*/
void n_rand_set_seed(n_rand_t * rand, uint32_t  seed)
{	
		/* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
		/* In the previous version (see above), MSBs of the    */
		/* seed affect only MSBs of the array mt[].            */

		rand->mt[0] = seed;
		for (rand->mti = 1; rand->mti<N; rand->mti++)
			rand->mt[rand->mti] = 1812433253UL *
			(rand->mt[rand->mti - 1] ^ (rand->mt[rand->mti - 1] >> 30)) + rand->mti;
}

n_rand_t * n_rand_new_with_seed(uint32_t seed)
{
	n_rand_t *rand = malloc(sizeof(n_rand_t));
	n_rand_set_seed(rand, seed);
	return rand;
}

static n_rand_t * n_get_global_random(void)
{
	static n_rand_t *global_random;

	/* called while locked */
	if (!global_random)
		global_random = n_rand_new();

	return global_random;
}

uint32_t n_rand_int(n_rand_t * rand)
{
	uint32_t y;
	static const uint32_t mag01[2] = { 0x0, MATRIX_A };
	/* mag01[x] = x * MATRIX_A  for x=0,1 */

	if (rand->mti >= N) { /* generate N words at one time */
		int kk;

		for (kk = 0; kk < N - M; kk++) {
			y = (rand->mt[kk] & UPPER_MASK) | (rand->mt[kk + 1] & LOWER_MASK);
			rand->mt[kk] = rand->mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1];
		}
		for (; kk < N - 1; kk++) {
			y = (rand->mt[kk] & UPPER_MASK) | (rand->mt[kk + 1] & LOWER_MASK);
			rand->mt[kk] = rand->mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1];
		}
		y = (rand->mt[N - 1] & UPPER_MASK) | (rand->mt[0] & LOWER_MASK);
		rand->mt[N - 1] = rand->mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1];

		rand->mti = 0;
	}

	y = rand->mt[rand->mti++];
	y ^= TEMPERING_SHIFT_U(y);
	y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
	y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
	y ^= TEMPERING_SHIFT_L(y);

	return y;
}

/**
* g_random_boolean:
*
* Returns a random #gboolean.
* This corresponds to a unbiased coin toss.
*
* Returns: a random #gboolean
*/
/**
* g_random_int:
*
* Return a random #guint32 equally distributed over the range
* [0..2^32-1].
*
* Returns: a random number
*/
uint32_t n_random_int(void)
{
	uint32_t result;
	result = n_rand_int(n_get_global_random());
	return result;
}

int32_t n_rand_int_range(n_rand_t  *rand, int32_t  begin, int32_t  end)
{
	uint32_t dist = end - begin;
	uint32_t random;
	
	if (dist == 0)
		random = 0;
	else
	{
		/* maxvalue is set to the predecessor of the greatest
		* multiple of dist less or equal 2^32.
		*/
		uint32_t maxvalue;
		if (dist <= 0x80000000u) /* 2^31 */
		{
			/* maxvalue = 2^32 - 1 - (2^32 % dist) */
			uint32_t leftover = (0x80000000u % dist) * 2;
			if (leftover >= dist) leftover -= dist;
			maxvalue = 0xffffffffu - leftover;
		}
		else
			maxvalue = dist - 1;

		do
			random = n_rand_int(rand);
		while (random > maxvalue);

		random %= dist;
	}

	return begin + random;
}

/**
* g_random_int_range:
* @begin: lower closed bound of the interval
* @end: upper open bound of the interval
*
* Returns a random #gint32 equally distributed over the range
* [@begin..@end-1].
*
* Returns: a random number
*/
int32_t n_random_int_range(int32_t begin, int32_t end)
{
	int32_t result;
	result = n_rand_int_range(n_get_global_random(), begin, end);
	return result;
}


/**
* g_random_set_seed:
* @seed: a value to reinitialize the global random number generator
*
* Sets the seed for the global random number generator, which is used
* by the g_random_* functions, to @seed.
*/
void n_random_set_seed(uint32_t seed)
{
	n_rand_set_seed(n_get_global_random(), seed);
}

static void rng_seed(n_rng_t * rng, uint32_t seed)
{
    (void)rng;
    n_random_set_seed(seed);
}

static void rng_generate_bytes(n_rng_t * rng, uint32_t len, char * buf)
{
    uint32_t i;

    (void)rng;

    for (i = 0; i < len; i++)
        buf[i] = n_random_int_range(0, 256);
}

static uint32_t rng_generate_int(n_rng_t * rng, uint32_t low, uint32_t high)
{
    (void)rng;
    return n_random_int_range(low, high);
}

static void rng_free(n_rng_t * rng)
{
    n_slice_free(n_rng_t, rng);
}

n_rng_t * nice_rng_glib_new(void)
{
	n_rng_t * ret;

    ret = n_slice_new0(n_rng_t);
    ret->seed = rng_seed;
    ret->generate_bytes = rng_generate_bytes;
    ret->generate_int = rng_generate_int;
    ret->free = rng_free;
    return ret;
}

n_rng_t * nice_rng_glib_new_predictable(void)
{
	n_rng_t * rng;

    rng = nice_rng_glib_new();
    rng->seed(rng, 0);
    return rng;
}

