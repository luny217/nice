/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_STREAM_H
#define _NICE_STREAM_H

#include <stdint.h>
#include <glib.h>

typedef struct _Stream Stream;

#include "component.h"
#include "random.h"
#include "nlist.h"

/* Maximum and default sizes for ICE attributes,
 * last updated from ICE ID-19
 * (the below sizes include the terminating NULL): */

#define NICE_STREAM_MAX_UFRAG   256 + 1  /* ufrag + NULL */
#define NICE_STREAM_MAX_UNAME   256 * 2 + 1 + 1 /* 2*ufrag + colon + NULL */
#define NICE_STREAM_MAX_PWD     256 + 1  /* pwd + NULL */
#define NICE_STREAM_DEF_UFRAG   4 + 1    /* ufrag + NULL */
#define NICE_STREAM_DEF_PWD     22 + 1   /* pwd + NULL */

struct _Stream
{
    char * name;
    uint32_t id;
    uint32_t n_components;
    int initial_binding_request_received;
	n_slist_t * components; /* list of 'Component' structs */
	n_slist_t * conncheck_list;        /* list of CandidateCheckPair items */
    char local_ufrag[NICE_STREAM_MAX_UFRAG];
    char local_password[NICE_STREAM_MAX_PWD];
    char remote_ufrag[NICE_STREAM_MAX_UFRAG];
    char remote_password[NICE_STREAM_MAX_PWD];
    int gathering;
    int gathering_started;
    int tos;
};

Stream * stream_new(uint32_t n_components, NiceAgent * agent);
void stream_close(Stream * stream);
void stream_free(Stream * stream);
int stream_all_components_ready(const Stream * stream);
Component * stream_find_component_by_id(const Stream * stream, uint32_t id);
void stream_initialize_credentials(Stream * stream, NiceRNG * rng);
void stream_restart(NiceAgent * agent, Stream * stream);

#endif /* _NICE_STREAM_H */

