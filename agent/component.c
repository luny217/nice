/* This file is part of the Nice GLib ICE library. */

/* Simple tracking for the number of alive components. These must be accessed
 * atomically. */
static volatile unsigned int n_components_created = 0;
static volatile unsigned int n_components_destroyed = 0;

#include <config.h>
#include <string.h>
#include "debug.h"
#include "component.h"
#include "discovery.h"
#include "agent-priv.h"
#include "timer.h"
#include "uv.h"

static void comp_sched_io_cb(n_comp_t * component);
static void comp_desched_io_cb(n_comp_t * component);

void incoming_check_free(n_inchk_t * icheck)
{
    n_free(icheck->username);
    n_slice_free(n_inchk_t, icheck);
}

static void socket_source_free(n_socket_source_t * source)
{
    //socket_source_detach(source);
    nice_socket_free(source->socket);

    n_slice_free(n_socket_source_t, source);
}

n_comp_t * comp_new(uint32_t id, n_agent_t * agent, n_stream_t * stream)
{
    n_comp_t * comp;

    atomic_int_inc(&n_components_created);
    nice_debug("[%s]: created component (%u created, %u destroyed)", G_STRFUNC, n_components_created, n_components_destroyed);

    comp = n_slice_new0(n_comp_t);
    comp->id = id;
    comp->state = COMP_STATE_DISCONNECTED;
    comp->restart_candidate = NULL;
    comp->tcp = NULL;
    comp->agent = agent;
    comp->stream = stream;

    n_agent_init_stun_agent(agent, &comp->stun_agent);

    pthread_mutex_init(&comp->io_mutex, NULL);
    n_queue_init(&comp->pend_io_msgs);
    comp->io_callback_id = 0;

    /* Start off with a fresh main context and all I/O paused. This
     * will be updated when n_agent_attach_recv() or nice_agent_recv_messages()
     * are called. */
    /*comp_set_io_context(comp, NULL);
    comp_set_io_callback(comp, NULL, NULL);*/

    n_queue_init(&comp->queued_tcp_packets);

    return comp;
}

void component_clean_turn_servers(n_comp_t * comp)
{
    n_slist_t * i;

    n_dlist_free_full(comp->turn_servers, (n_destroy_notify) turn_server_unref);
    comp->turn_servers = NULL;

    for (i = comp->local_candidates; i;)
    {
        n_cand_t * candidate = i->data;
        n_slist_t  * next = i->next;

        if (candidate->type != CAND_TYPE_RELAYED)
        {
            i = next;
            continue;
        }

        /* note: do not remove the remote candidate that is
         *       currently part of the 'selected pair', see ICE
         *       9.1.1.1. "ICE Restarts" (ID-19)
         *
         * So what we do instead is that we put the selected candidate
         * in a special location and keep it "alive" that way. This is
         * especially important for TURN, because refresh requests to the
         * server need to keep happening.
         */
        if (candidate == comp->selected_pair.local)
        {
            if (comp->turn_candidate)
            {
                refresh_prune_candidate(comp->agent, comp->turn_candidate);
                disc_prune_socket(comp->agent, comp->turn_candidate->sockptr);
                cocheck_prune_socket(comp->agent, comp->stream, comp, comp->turn_candidate->sockptr);
                component_detach_socket(comp, comp->turn_candidate->sockptr);
                n_cand_free(comp->turn_candidate);
            }
            /* Bring the priority down to 0, so that it will be replaced
             * on the new run.
             */
            comp->selected_pair.priority = 0;
            comp->turn_candidate = candidate;
        }
        else
        {
            refresh_prune_candidate(comp->agent, candidate);
            disc_prune_socket(comp->agent, candidate->sockptr);
            cocheck_prune_socket(comp->agent, comp->stream, comp, candidate->sockptr);
            component_detach_socket(comp, candidate->sockptr);
            agent_remove_local_candidate(comp->agent, candidate);
            n_cand_free(candidate);
        }
        comp->local_candidates = n_slist_delete_link(comp->local_candidates, i);
        i = next;
    }
}

static void comp_clear_selected_pair(n_comp_t * comp)
{
    if (comp->selected_pair.keepalive.tick_clock != 0)
    {
        /*g_source_destroy(component->selected_pair.keepalive.tick_source);
        g_source_unref(component->selected_pair.keepalive.tick_source);
        component->selected_pair.keepalive.tick_source = NULL;*/

        timer_stop(comp->selected_pair.keepalive.tick_clock);
        timer_destroy(comp->selected_pair.keepalive.tick_clock);
        comp->selected_pair.keepalive.tick_clock = 0;
    }

    memset(&comp->selected_pair, 0, sizeof(n_cand_pair_t));
}

/* Must be called with the agent lock held as it touches internal n_comp_t
 * state. */
void component_close(n_comp_t * comp)
{
    IOCallbackData * data;
    n_outvector_t * vec;

    /* Start closing the pseudo-TCP socket first. FIXME: There is a very big and
     * reliably triggerable race here. pst_close() does not block
     * on the socket closing ? it only sends the first packet of the FIN
     * handshake. component_close() will immediately afterwards close the
     * underlying component sockets, aborting the handshake.
     *
     * On the principle that starting the FIN handshake is better than not
     * starting it, even if it?s later truncated, call pst_close().
     * A long-term fix is needed in the form of making component_close() (and all
     * its callers) async, so we can properly block on closure. */
    if (comp->tcp)
    {
        pst_close(comp->tcp, TRUE);
    }

    if (comp->restart_candidate)
        n_cand_free(comp->restart_candidate), comp->restart_candidate = NULL;

    if (comp->turn_candidate)
        n_cand_free(comp->turn_candidate), comp->turn_candidate = NULL;

    while (comp->local_candidates)
    {
        agent_remove_local_candidate(comp->agent, comp->local_candidates->data);
        n_cand_free(comp->local_candidates->data);
        comp->local_candidates = n_slist_delete_link(comp->local_candidates, comp->local_candidates);
    }

    n_slist_free_full(comp->remote_candidates, (n_destroy_notify) n_cand_free);
    comp->remote_candidates = NULL;
    component_free_socket_sources(comp);
    n_slist_free_full(comp->incoming_checks, (n_destroy_notify) incoming_check_free);
    comp->incoming_checks = NULL;

    component_clean_turn_servers(comp);

    if (comp->tcp_clock)
    {
        /*g_source_destroy(cmp->tcp_clock);
        g_source_unref(cmp->tcp_clock);
        cmp->tcp_clock = NULL;*/

        timer_stop(comp->tcp_clock);
        timer_destroy(comp->tcp_clock);
        comp->tcp_clock = 0;
    }
    /*if (comp->tcp_writable_cancellable)
    {
        g_cancellable_cancel(comp->tcp_writable_cancellable);
        g_clear_object(&comp->tcp_writable_cancellable);
    }*/

    while ((data = n_queue_pop_head(&comp->pend_io_msgs)) != NULL)
        io_callback_data_free(data);

    comp_desched_io_cb(comp);

    //g_cancellable_cancel(comp->stop_cancellable);

    while ((vec = n_queue_pop_head(&comp->queued_tcp_packets)) != NULL)
    {
        n_free((void *) vec->buffer);
        n_slice_free(n_outvector_t, vec);
    }
}

/* Must be called with the agent lock released as it could dispose of
 * NiceIOStreams. */
void component_free(n_comp_t * cmp)
{
    /* n_comp_t should have been closed already. */
    //g_warn_if_fail(cmp->local_candidates == NULL);
    //g_warn_if_fail(cmp->remote_candidates == NULL);
    //g_warn_if_fail(cmp->incoming_checks == NULL);

    //g_clear_object(&cmp->tcp);
    //g_clear_object(&cmp->stop_cancellable);
    //g_clear_object(&cmp->iostream);
    pthread_mutex_destroy(&cmp->io_mutex);

/*
    if (cmp->stop_cancellable_source != NULL)
    {
        g_source_destroy(cmp->stop_cancellable_source);
        g_source_unref(cmp->stop_cancellable_source);
    }*/

/*
    if (cmp->ctx != NULL)
    {
        //g_main_context_unref(cmp->ctx);
        cmp->ctx = NULL;
    }*/

    //g_main_context_unref(cmp->own_ctx);

    n_slice_free(n_comp_t, cmp);

    //atomic_int_inc(&n_components_destroyed);
    //nice_debug("Destroyed NiceComponent (%u created, %u destroyed)", n_components_created, n_components_destroyed);
}

/*
 * Finds a candidate pair that has matching foundation ids.
 *
 * @return TRUE if pair found, pointer to pair stored at 'pair'
 */
int comp_find_pair(n_comp_t * cmp, n_agent_t * agent, const char * lfoundation, const char * rfoundation, n_cand_pair_t * pair)
{
    n_slist_t  * i;
    n_cand_pair_t result = { 0, };

    for (i = cmp->local_candidates; i; i = i->next)
    {
        n_cand_t * candidate = i->data;
        if (strncmp(candidate->foundation, lfoundation, CAND_MAX_FOUNDATION) == 0)
        {
            result.local = candidate;
            break;
        }
    }

    for (i = cmp->remote_candidates; i; i = i->next)
    {
        n_cand_t * candidate = i->data;
        if (strncmp(candidate->foundation, rfoundation, CAND_MAX_FOUNDATION) == 0)
        {
            result.remote = candidate;
            break;
        }
    }

    if (result.local && result.remote)
    {
        result.priority = (uint32_t)agent_candidate_pair_priority(agent, result.local, result.remote);
        if (pair)
            *pair = result;
        return TRUE;
    }

    return FALSE;
}

/*
 * Resets the component state to that of a ICE restarted session.
 */
void component_restart(n_comp_t * cmp)
{
    n_slist_t  * i;

    for (i = cmp->remote_candidates; i; i = i->next)
    {
        n_cand_t * candidate = i->data;

        /* note: do not remove the local candidate that is
         *       currently part of the 'selected pair', see ICE
         *       9.1.1.1. "ICE Restarts" (ID-19) */
        if (candidate == cmp->selected_pair.remote)
        {
            if (cmp->restart_candidate)
                n_cand_free(cmp->restart_candidate);
            cmp->restart_candidate = candidate;
        }
        else
            n_cand_free(candidate);
    }
    n_slist_free(cmp->remote_candidates), cmp->remote_candidates = NULL;

    n_slist_free_full(cmp->incoming_checks, (n_destroy_notify) incoming_check_free);
    cmp->incoming_checks = NULL;

    /* Reset the priority to 0 to make sure we get a new pair */
    cmp->selected_pair.priority = 0;

    /* note: component state managed by agent */
}

/*
 * Changes the selected pair for the component to 'pair'. Does not
 * emit the "selected-pair-changed" signal.
 */
void comp_update_selected_pair(n_comp_t * comp, const n_cand_pair_t * pair)
{
    //g_assert(comp);
    //g_assert(pair);
    nice_debug("[%s]: setting SELECTED PAIR for component %u: %s:%s (prio:%I64u)", G_STRFUNC, comp->id, pair->local->foundation,
               pair->remote->foundation, pair->priority);

    if (comp->selected_pair.local && comp->selected_pair.local == comp->turn_candidate)
    {
        refresh_prune_candidate(comp->agent, comp->turn_candidate);
        disc_prune_socket(comp->agent, comp->turn_candidate->sockptr);
        cocheck_prune_socket(comp->agent, comp->stream, comp, comp->turn_candidate->sockptr);
        component_detach_socket(comp, comp->turn_candidate->sockptr);
        n_cand_free(comp->turn_candidate);
        comp->turn_candidate = NULL;
    }

    comp_clear_selected_pair(comp);

    comp->selected_pair.local = pair->local;
    comp->selected_pair.remote = pair->remote;
    comp->selected_pair.priority = pair->priority;

}

/*
 * Finds a remote candidate with matching address and
 * transport.
 *
 * @return pointer to candidate or NULL if not found
 */
n_cand_t * comp_find_remote_cand(const n_comp_t * comp, const n_addr_t * addr)
{
    n_slist_t  * i;

    for (i = comp->remote_candidates; i; i = i->next)
    {
        n_cand_t * candidate = i->data;

        if (nice_address_equal(&candidate->addr, addr))
            return candidate;
    }
    return NULL;
}

/*
 * Sets the desired remote candidate as the selected pair
 *
 * It will start sending on the highest priority pair available with
 * this candidate.
 */

n_cand_t * comp_set_selected_remote_cand(n_agent_t * agent, n_comp_t * component, n_cand_t * candidate)
{
    n_cand_t * local = NULL;
    n_cand_t * remote = NULL;
    uint64_t priority = 0;
    n_slist_t  * item = NULL;

    //g_assert(candidate != NULL);

    for (item = component->local_candidates; item; item = n_slist_next(item))
    {
        n_cand_t * tmp = item->data;
        uint64_t tmp_prio = 0;

        if (tmp->transport != candidate->transport ||
                tmp->addr.s.addr.sa_family != candidate->addr.s.addr.sa_family ||
                tmp->type != CAND_TYPE_HOST)
            continue;

        tmp_prio = agent_candidate_pair_priority(agent, tmp, candidate);

        if (tmp_prio > priority)
        {
            priority = tmp_prio;
            local = tmp;
        }
    }

    if (local == NULL)
        return NULL;

    remote = comp_find_remote_cand(component, &candidate->addr);

    if (!remote)
    {
        remote = nice_candidate_copy(candidate);
        component->remote_candidates = n_slist_append(component->remote_candidates, remote);
        agent_sig_new_remote_cand(agent, remote);
    }

    comp_clear_selected_pair(component);

    component->selected_pair.local = local;
    component->selected_pair.remote = remote;
    component->selected_pair.priority = (uint32_t)priority;

    return local;
}

static int32_t _find_socket_source(const void * a, const void * b)
{
    const n_socket_source_t * source_a = a;
    const n_socket_t * socket_b = b;

    return (source_a->socket == socket_b) ? 0 : 1;
}

/* This takes ownership of the socket.
 * It creates and attaches a source to the components context. */
void comp_attach_socket(n_comp_t * comp, n_socket_t * nicesock)
{
    n_slist_t * l;
    n_socket_source_t * socket_source;

    /* 在链表里查找nicesock, 没有的话创建一个新的，并且添加到链表尾部 */
    l = n_slist_find_custom(comp->socket_srcs_slist, nicesock, _find_socket_source);
    if (l != NULL)
    {
        socket_source = l->data;
    }
    else
    {
        socket_source = n_slice_new0(n_socket_source_t);
        socket_source->socket = nicesock;
        socket_source->component = comp;
        comp->socket_srcs_slist = n_slist_prepend(comp->socket_srcs_slist, socket_source);
        comp->socket_sources_age++;
    }
    
    nice_debug("[%s]: n_comp_t %p: attach source (fd %d)", G_STRFUNC, comp, nicesock->sock_fd);
    
    //socket_source_attach(socket_source, comp->ctx);
}

/* Reattaches socket handles of @component to the main context.
 *
 * Must *not* take the agent lock, since it?s called from within
 * comp_set_io_context(), which holds the n_comp_t?s I/O lock. */
static void component_reattach_all_sockets(n_comp_t * component)
{
    n_slist_t  * i;

    for (i = component->socket_srcs_slist; i != NULL; i = i->next)
    {
        n_socket_source_t * socket_source = i->data;
        //nice_debug("Reattach source %p.", socket_source->source);
        //socket_source_detach(socket_source);
        //socket_source_attach(socket_source, component->ctx);
    }
}

/**
 * component_detach_socket:
 * @component: a #n_comp_t
 * @socket: the socket to detach the source for
 *
 * Detach the #GSource for the single specified @socket. It also closes it
 * and frees it!
 *
 * If the @socket doesn?t exist in this @component, do nothing.
 */
void component_detach_socket(n_comp_t * component, n_socket_t * nicesock)
{
    n_slist_t  * l;
    n_socket_source_t * socket_source;

    nice_debug("Detach socket %p.", nicesock);

    /* Remove the socket from various lists. */
    for (l = component->incoming_checks; l != NULL;)
    {
        n_inchk_t * icheck = l->data;
        n_slist_t  * next = l->next;

        if (icheck->local_socket == nicesock)
        {
            component->incoming_checks =
                n_slist_delete_link(component->incoming_checks, l);
            incoming_check_free(icheck);
        }

        l = next;
    }

    /* Find the n_socket_source_t for the socket. */
    l = n_slist_find_custom(component->socket_srcs_slist, nicesock,  _find_socket_source);
    if (l == NULL)
        return;

    /* Detach the source. */
    socket_source = l->data;
    component->socket_srcs_slist = n_slist_delete_link(component->socket_srcs_slist, l);
    component->socket_sources_age++;

    //socket_source_detach(socket_source);
    socket_source_free(socket_source);
}

/*
 * Detaches socket handles of @component from the main context. Leaves the
 * sockets themselves untouched.
 *
 * Must *not* take the agent lock, since it?s called from within
 * comp_set_io_context(), which holds the n_comp_t?s I/O lock.
 */
void component_detach_all_sockets(n_comp_t * component)
{
    n_slist_t  * i;

    for (i = component->socket_srcs_slist; i != NULL; i = i->next)
    {
        n_socket_source_t * socket_source = i->data;
        //nice_debug("Detach source %p, socket %p.", socket_source->source,  socket_source->socket);
        //socket_source_detach(socket_source);
    }
}

void component_free_socket_sources(n_comp_t * component)
{
    nice_debug("Free socket sources for component %p.", component);

    n_slist_free_full(component->socket_srcs_slist, (n_destroy_notify) socket_source_free);
    component->socket_srcs_slist = NULL;
    component->socket_sources_age++;

    comp_clear_selected_pair(component);
}

/* (func, user_data) and (recv_messages, n_recv_messages) are mutually
 * exclusive. At most one of the two must be specified; if both are NULL, the
 * n_comp_t will not receive any data (i.e. reception is paused).
 *
 * Apart from during setup, this must always be called with the agent lock held,
 * and the I/O lock released (because it takes the I/O lock itself). Requiring
 * the agent lock to be held means it can?t be called between a packet being
 * dequeued from the kernel buffers in agent.c, and an I/O callback being
 * emitted for it (which could cause data loss if the I/O callback function was
 * unset in that time). */
void comp_set_io_callback(n_comp_t * comp, n_agent_recv_func func, void * user_data)
{
    pthread_mutex_lock(&comp->io_mutex);

    if (func != NULL)
    {
        comp->io_callback = func;
        comp->io_user_data = user_data;
        comp->recv_messages = NULL;
        comp->n_recv_messages = 0;

        //comp_sched_io_cb(comp);
    }
    else
    {
        comp->io_callback = NULL;
        comp->io_user_data = NULL;
        comp->recv_messages = NULL;
        comp->n_recv_messages = 0;

        comp_desched_io_cb(comp);
    }

    n_input_msg_iter_reset(&comp->recv_messages_iter);

    pthread_mutex_unlock(&comp->io_mutex);
}

int component_has_io_callback(n_comp_t * component)
{
    int has_io_callback;

    pthread_mutex_lock(&component->io_mutex);
    has_io_callback = (component->io_callback != NULL);
    pthread_mutex_unlock(&component->io_mutex);

    return has_io_callback;
}

IOCallbackData * io_callback_data_new(const uint8_t * buf, uint32_t buf_len)
{
    IOCallbackData * data;

    data = n_slice_new0(IOCallbackData);
    data->buf = n_memdup(buf, buf_len);
    data->buf_len = buf_len;
    data->offset = 0;

    return data;
}

void io_callback_data_free(IOCallbackData * data)
{
    n_free(data->buf);
    n_slice_free(IOCallbackData, data);
}

/* 回收用，不一定会调用 */
static int emit_io_callback_cb(void * user_data)
{
    n_comp_t * comp = user_data;
    IOCallbackData * data;
    n_agent_recv_func io_callback;
    void * io_user_data;
    uint32_t stream_id, comp_id;
    n_agent_t * agent;

    agent = comp->agent;

    //g_object_ref(agent);

    stream_id = comp->stream->id;
    comp_id = comp->id;

    pthread_mutex_lock(&comp->io_mutex);

    while (TRUE)
    {
        io_callback = comp->io_callback;
        io_user_data = comp->io_user_data;
        data = n_queue_peek_head(&comp->pend_io_msgs);

        if (data == NULL || io_callback == NULL)
            break;

        pthread_mutex_unlock(&comp->io_mutex);

        io_callback(agent, stream_id, comp_id, data->buf_len - data->offset, (char *) data->buf + data->offset, io_user_data);

        /* Check for the user destroying things underneath our feet. */
        if (!agent_find_comp(agent, stream_id, comp_id, NULL, &comp))
        {
            nice_debug("%s: Agent or component destroyed.", G_STRFUNC);
            goto done;
        }

        n_queue_pop_head(&comp->pend_io_msgs);
        io_callback_data_free(data);

        pthread_mutex_lock(&comp->io_mutex);
    }

    comp->io_callback_id = 0;
    pthread_mutex_unlock(&comp->io_mutex);

done:
    //g_object_unref(agent);

    return FALSE;
}

/* This must be called with the agent lock *held*. */
void comp_emit_io_cb(n_comp_t * comp, const uint8_t * buf, uint32_t buf_len)
{
    n_agent_t * agent;
    uint32_t stream_id, comp_id;
    n_agent_recv_func io_callback;
    void * io_user_data;

    //g_assert(component != NULL);
    //g_assert(buf != NULL);
    //g_assert(buf_len > 0);

    agent = comp->agent;
    stream_id = comp->stream->id;
    comp_id = comp->id;

    pthread_mutex_lock(&comp->io_mutex);
    io_callback = comp->io_callback;
    io_user_data = comp->io_user_data;
    pthread_mutex_unlock(&comp->io_mutex);

    /* Allow this to be called with a NULL io_callback, since the caller can?t
     * lock io_mutex to check beforehand. */
    if (io_callback == NULL)
        return;

    //g_assert(NICE_IS_AGENT(agent));
    //g_assert(stream_id > 0);
    //g_assert(component_id > 0);
    //g_assert(io_callback != NULL);

    /* Only allocate a closure if the callback is being deferred to an idle
     * handler. */
    //if (g_main_context_is_owner(comp->ctx))
    {
        /* Thread owns the main context, so invoke the callback directly. */
        agent_unlock();
        io_callback(agent, stream_id, comp_id, buf_len, (char *) buf, io_user_data);
        //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
        agent_lock();
    }
    /*else
    {
        IOCallbackData * data;

        pthread_mutex_lock(&comp->io_mutex);

        / * Slow path: Current thread doesn?t own the n_comp_t?s context at the
         * moment, so schedule the callback in an idle handler. * /
        data = io_callback_data_new(buf, buf_len);
        n_queue_push_tail(&comp->pend_io_msgs, data);  / * transfer ownership * /

        nice_debug("[%s]:: **WARNING: SLOW PATH**", G_STRFUNC);
        comp_sched_io_cb(comp);
        pthread_mutex_unlock(&comp->io_mutex);
    }*/
}


#if 0
/* Note: Must be called with the io_mutex held. */
static void comp_sched_io_cb(n_comp_t * component)
{
    GSource * source;

    /* Already scheduled or nothing to schedule? */
    if (component->io_callback_id != 0 || n_queue_is_empty(&component->pend_io_msgs))
        return;

    /* Add the idle callback. If n_agent_attach_recv() is called with a
     * NULL callback before this source is dispatched, the source will be
     * destroyed, but any pending data will remain in
     * component->pend_io_msgs, ready to be picked up when a callback
     * is re-attached, or if nice_agent_recv() is called. */
    source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(source, emit_io_callback_cb, component, NULL);
    component->io_callback_id = g_source_attach(source, component->ctx);
    g_source_unref(source);
}
#endif

/* Note: Must be called with the io_mutex held. */
static void comp_desched_io_cb(n_comp_t * component)
{
    /* Already descheduled? */
    if (component->io_callback_id == 0)
        return;

    //g_source_remove(component->io_callback_id);
    component->io_callback_id = 0;
}


turn_server_t * turn_server_new(const char * server_ip, uint32_t server_port, const char * username, const char * password)
{
    turn_server_t * turn = n_slice_new(turn_server_t);

    nice_address_init(&turn->server);

    turn->ref_count = 1;
    if (nice_address_set_from_string(&turn->server, server_ip))
    {
        nice_address_set_port(&turn->server, server_port);
    }
    else
    {
        n_slice_free(turn_server_t, turn);
        return NULL;
    }
    turn->username = n_strdup(username);
    turn->password = n_strdup(password);

    return turn;
}

turn_server_t * turn_server_ref(turn_server_t * turn)
{
    turn->ref_count++;
    return turn;
}

void turn_server_unref(turn_server_t * turn)
{
    turn->ref_count--;

    if (turn->ref_count == 0)
    {
        n_free(turn->username);
        n_free(turn->password);
        n_slice_free(turn_server_t, turn);
    }
}
