/* This file is part of the Nice GLib ICE library */

#ifndef _RANDOM_GLIB_H
#define _RANDOM_GLIB_H

//#include <glib.h>

#include "random.h"

n_rng_t * nice_rng_glib_new (void);

n_rng_t * nice_rng_glib_new_predictable (void);



#endif /* _RANDOM_GLIB_H */

