/* This file is part of the Nice GLib ICE library */

#ifndef _RANDOM_H
#define _RANDOM_H

#include <stdint.h>
#include <glib.h>

typedef struct _NiceRNG NiceRNG;

struct _NiceRNG
{
    void (*seed)(NiceRNG * src, guint32 seed);
    void (*generate_bytes)(NiceRNG * src, guint len, gchar * buf);
    guint(*generate_int)(NiceRNG * src, guint low, guint high);
    void (*free)(NiceRNG * src);
    void * priv;
};

NiceRNG * nice_rng_new(void);

void nice_rng_set_new_func(NiceRNG * (*func)(void));

void nice_rng_seed(NiceRNG * rng, uint32_t seed);

void nice_rng_generate_bytes(NiceRNG * rng, uint32_t len, char * buf);

void nice_rng_generate_bytes_print(NiceRNG * rng, uint32_t len, char * buf);

uint32_t n_rng_gen_int(NiceRNG * rng, uint32_t low, uint32_t high);

void nice_rng_free(NiceRNG * rng);

#endif // _RANDOM_H

