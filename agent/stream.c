/* This file is part of the Nice GLib ICE library. */

#include <config.h>
#include <string.h>
#include "stream.h"

/* Simple tracking for the number of alive streams. These must be accessed
 * atomically. */
static volatile unsigned int n_streams_created = 0;
static volatile unsigned int n_streams_destroyed = 0;

/*
 * @file stream.c
 * @brief ICE stream functionality
 */
n_stream_t * stream_new(n_agent_t * agent, uint32_t n_comps)
{
    n_stream_t * stream;
    uint32_t n;
    n_comp_t * comp;

    stream = n_slice_new0(n_stream_t);
    for (n = 0; n < n_comps; n++)
    {
        comp = comp_new(n + 1, agent, stream);
        stream->components = n_slist_append(stream->components, comp);
    }

    stream->n_components = n_comps;
    stream->initial_binding_request_received = FALSE;

    return stream;
}

void stream_close(n_stream_t * stream)
{
	n_slist_t * i;

    for (i = stream->components; i; i = i->next)
    {
        n_comp_t * component = i->data;
        component_close(component);
    }
}

void stream_free(n_stream_t * stream)
{
    free(stream->name);
    n_slist_free_full(stream->components, (n_destroy_notify) component_free);
    n_slice_free(n_stream_t, stream);  
}

n_comp_t * stream_find_comp_by_id(const n_stream_t * stream, uint32_t id)
{
    n_slist_t * l;
    n_comp_t * comp = NULL;

    for (l = stream->components; l; l = l->next)
    {
        comp = l->data;
        if (comp && comp->id == id)
            return comp;
    }

    return NULL;
}

/*
 * Returns true if all components of the stream are either
 * 'CONNECTED' or 'READY' (connected plus nominated).
 */
int stream_all_components_ready(const n_stream_t * stream)
{
	n_slist_t * l;
	n_comp_t * comp = NULL;

    for (l = stream->components; l; l = l->next)
    {
        n_comp_t * comp = l->data;
        if (comp && !(comp->state == COMP_STATE_CONNECTED || comp->state == COMP_STATE_READY))
            return FALSE;
    }

    return TRUE;
}


/*
 * Initialized the local crendentials for the stream.
 */
void stream_initialize_credentials(n_stream_t * stream, n_rng_t * rng)
{
    /* note: generate ufrag/pwd for the stream (see ICE 15.4.
     *       '"ice-ufrag" and "ice-pwd" Attributes', ID-19) */
    nice_rng_generate_bytes_print(rng, N_STREAM_DEF_UFRAG - 1, stream->local_ufrag);
    nice_rng_generate_bytes_print(rng, N_STREAM_DEF_PWD - 1, stream->local_password);
}

/*
 * Resets the stream state to that of a ICE restarted
 * session.
 */
void stream_restart(n_agent_t * agent, n_stream_t * stream)
{
	n_slist_t * i;

    /* step: clean up all connectivity checks */
    cocheck_prune_stream(agent, stream);

    stream->initial_binding_request_received = FALSE;

    stream_initialize_credentials(stream, agent->rng);

    for (i = stream->components; i; i = i->next)
    {
        n_comp_t * component = i->data;

        component_restart(component);
    }
}

