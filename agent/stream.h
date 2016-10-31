/* This file is part of the Nice GLib ICE library. */

#ifndef _N_STREAM_H
#define _N_STREAM_H

#include <stdint.h>
#include <glib.h>

typedef struct _stream_st n_stream_t;

#include "component.h"
#include "random.h"
#include "nlist.h"

/* Maximum and default sizes for ICE attributes,
 * last updated from ICE ID-19
 * (the below sizes include the terminating NULL): */

#define N_STREAM_MAX_UFRAG   256 + 1  /* ufrag + NULL */
#define N_STREAM_MAX_UNAME   256 * 2 + 1 + 1 /* 2*ufrag + colon + NULL */
#define N_STREAM_MAX_PWD     256 + 1  /* pwd + NULL */
#define N_STREAM_DEF_UFRAG   4 + 1    /* ufrag + NULL */
#define N_STREAM_DEF_PWD     22 + 1   /* pwd + NULL */

struct _stream_st
{
    char * name;
    uint32_t id;
    uint32_t n_components;
    int initial_binding_request_received;
    n_slist_t * components; /* list of 'n_comp_t' structs */
    n_slist_t * conncheck_list;        /* list of n_cand_chk_pair_t items */
    char local_ufrag[N_STREAM_MAX_UFRAG];
    char local_password[N_STREAM_MAX_PWD];
    char remote_ufrag[N_STREAM_MAX_UFRAG];
    char remote_password[N_STREAM_MAX_PWD];
    int gathering;
    int gathering_started;
    int tos;
};

n_stream_t * stream_new(n_agent_t * agent, uint32_t n_comps);
void stream_close(n_stream_t * stream);
void stream_free(n_stream_t * stream);
int stream_all_components_ready(const n_stream_t * stream);
n_comp_t * stream_find_comp_by_id(const n_stream_t * stream, uint32_t id);
void stream_initialize_credentials(n_stream_t * stream, NiceRNG * rng);
void stream_restart(n_agent_t * agent, n_stream_t * stream);

#endif /* _N_STREAM_H */

