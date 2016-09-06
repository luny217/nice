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
Stream * stream_new(uint32_t n_components, NiceAgent * agent)
{
    Stream * stream;
    uint32_t n;
    Component * component;

    g_atomic_int_inc(&n_streams_created);
    nice_debug("Created NiceStream (%u created, %u destroyed)", n_streams_created, n_streams_destroyed);

    stream = g_slice_new0(Stream);
    for (n = 0; n < n_components; n++)
    {
        component = component_new(n + 1, agent, stream);
        stream->components = g_slist_append(stream->components, component);
    }

    stream->n_components = n_components;
    stream->initial_binding_request_received = FALSE;

    return stream;
}

void stream_close(Stream * stream)
{
    GSList * i;

    for (i = stream->components; i; i = i->next)
    {
        Component * component = i->data;
        component_close(component);
    }
}

void stream_free(Stream * stream)
{
    g_free(stream->name);
    g_slist_free_full(stream->components, (GDestroyNotify) component_free);
    g_slice_free(Stream, stream);

    g_atomic_int_inc(&n_streams_destroyed);
    nice_debug("Destroyed NiceStream (%u created, %u destroyed)", n_streams_created, n_streams_destroyed);
}

Component * stream_find_component_by_id(const Stream * stream, uint32_t id)
{
    GSList * i;

    for (i = stream->components; i; i = i->next)
    {
        Component * component = i->data;
        if (component && component->id == id)
            return component;
    }

    return NULL;
}

/*
 * Returns true if all components of the stream are either
 * 'CONNECTED' or 'READY' (connected plus nominated).
 */
gboolean stream_all_components_ready(const Stream * stream)
{
    GSList * i;

    for (i = stream->components; i; i = i->next)
    {
        Component * component = i->data;
        if (component &&
                !(component->state == COMPONENT_STATE_CONNECTED ||
                  component->state == COMPONENT_STATE_READY))
            return FALSE;
    }

    return TRUE;
}


/*
 * Initialized the local crendentials for the stream.
 */
void stream_initialize_credentials(Stream * stream, NiceRNG * rng)
{
    /* note: generate ufrag/pwd for the stream (see ICE 15.4.
     *       '"ice-ufrag" and "ice-pwd" Attributes', ID-19) */
    nice_rng_generate_bytes_print(rng, NICE_STREAM_DEF_UFRAG - 1, stream->local_ufrag);
    nice_rng_generate_bytes_print(rng, NICE_STREAM_DEF_PWD - 1, stream->local_password);
}

/*
 * Resets the stream state to that of a ICE restarted
 * session.
 */
void stream_restart(NiceAgent * agent, Stream * stream)
{
    GSList * i;

    /* step: clean up all connectivity checks */
    conn_check_prune_stream(agent, stream);

    stream->initial_binding_request_received = FALSE;

    stream_initialize_credentials(stream, agent->rng);

    for (i = stream->components; i; i = i->next)
    {
        Component * component = i->data;

        component_restart(component);
    }
}

