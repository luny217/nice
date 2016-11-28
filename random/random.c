/* This file is part of the Nice GLib ICE library. */

#include <config.h>
#include <stdint.h>
#include <string.h>
#include "random.h"
#include "random-glib.h"

static n_rng_t * (*nice_rng_new_func)(void) = NULL;

/*
 * Creates a new random number generator instance.
 */
n_rng_t * nice_rng_new(void)
{
    if (nice_rng_new_func == NULL)
        return nice_rng_glib_new();
    else
        return nice_rng_new_func();
}

/*
 * Sets a new generator function.
 */
void nice_rng_set_new_func(n_rng_t * (*func)(void))
{
    nice_rng_new_func = func;
}

/*
 * Frees the random number generator instance.
 *
 * @param rng context
 */
void nice_rng_free(n_rng_t * rng)
{
    rng->free(rng);
}

/*
 * Generates random octets.
 *
 * @param rng context
 * @param len number of octets to product
 * @param buf buffer to store the results
 */
void nice_rng_generate_bytes(n_rng_t * rng, uint32_t len, char * buf)
{
    rng->generate_bytes(rng, len, buf);
}

/*
 * Generates a random unsigned integer.
 *
 * @param rng context
 * @param low closed lower bound
 * @param high open upper bound
 */
uint32_t n_rng_gen_int(n_rng_t * rng, uint32_t low, uint32_t high)
{
    return rng->generate_int(rng, low, high);
}

/*
 * Generates a stream of octets containing only characters
 * with ASCII codecs of 0x41-5A (A-Z), 0x61-7A (a-z),
 * 0x30-39 (0-9), 0x2b (+) and 0x2f (/). This matches
 * the definition of 'ice-char' in ICE Ispecification,
 * section 15.1 (ID-16).
 *
 * @param rng context
 * @param len number of octets to product
 * @param buf buffer to store the results
 */
void nice_rng_generate_bytes_print(n_rng_t * rng, uint32_t len, char * buf)
{
	uint32_t i;
    const char * chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        "+/";

    for (i = 0; i < len; i++)
        buf[i] = chars[n_rng_gen_int(rng, 0, strlen(chars))];
}

