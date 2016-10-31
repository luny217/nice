/* */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "base.h"
#include "nlist.h"
#include "random-glib.h"

static void rng_seed(NiceRNG * rng, uint32_t seed)
{
    (void)rng;
    g_random_set_seed(seed);
}

static void rng_generate_bytes(NiceRNG * rng, uint32_t len, char * buf)
{
    uint32_t i;

    (void)rng;

    for (i = 0; i < len; i++)
        buf[i] = g_random_int_range(0, 256);
}

static uint32_t rng_generate_int(NiceRNG * rng, uint32_t low, uint32_t high)
{
    (void)rng;
    return g_random_int_range(low, high);
}

static void rng_free(NiceRNG * rng)
{
    n_slice_free(NiceRNG, rng);
}

NiceRNG * nice_rng_glib_new(void)
{
    NiceRNG * ret;

    ret = n_slice_new0(NiceRNG);
    ret->seed = rng_seed;
    ret->generate_bytes = rng_generate_bytes;
    ret->generate_int = rng_generate_int;
    ret->free = rng_free;
    return ret;
}

NiceRNG * nice_rng_glib_new_predictable(void)
{
    NiceRNG * rng;

    rng = nice_rng_glib_new();
    rng->seed(rng, 0);
    return rng;
}

