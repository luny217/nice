/* This file is part of the Nice GLib ICE library */

#ifndef _RANDOM_H
#define _RANDOM_H

#include <stdint.h>
//#include <glib.h>

typedef struct _n_rng_st n_rng_t;

struct _n_rng_st
{
    void (*seed)(n_rng_t * src, uint32_t seed);
    void (*generate_bytes)(n_rng_t * src, uint32_t len, char * buf);
	uint32_t(*generate_int)(n_rng_t * src, uint32_t low, uint32_t high);
    void (*free)(n_rng_t * src);
    void * priv;
};

n_rng_t * nice_rng_new(void);

void nice_rng_set_new_func(n_rng_t * (*func)(void));

void nice_rng_seed(n_rng_t * rng, uint32_t seed);

void nice_rng_generate_bytes(n_rng_t * rng, uint32_t len, char * buf);

void nice_rng_generate_bytes_print(n_rng_t * rng, uint32_t len, char * buf);

uint32_t n_rng_gen_int(n_rng_t * rng, uint32_t low, uint32_t high);

void nice_rng_free(n_rng_t * rng);

#endif // _RANDOM_H

