/* This file is part of the Nice GLib ICE library. */

/*
 * @file conncheck.c
 * @brief ICE connectivity checks
 */

#include <config.h>
#include <errno.h>
#include <string.h>
#include <glib.h>
#include "debug.h"
#include "agent.h"
#include "agent-priv.h"
#include "conncheck.h"
#include "discovery.h"
#include "stun/usages/ice.h"
#include "stun/usages/bind.h"
#include "stun/usages/turn.h"

static void priv_update_check_list_failed_components(n_agent_t * agent, Stream * stream);
static void priv_update_check_list_state_for_ready(n_agent_t * agent, Stream * stream, Component * component);
static uint32_t priv_prune_pending_checks(Stream * stream, uint32_t component_id);
static int priv_schedule_triggered_check(n_agent_t * agent, Stream * stream, Component * component, n_socket_t * local_socket, n_cand_t * remote_cand, int use_candidate);
static void priv_mark_pair_nominated(n_agent_t * agent, Stream * stream, Component * component, n_cand_t * remotecand);
static size_t priv_create_username(n_agent_t * agent, Stream * stream,
                                   uint32_t component_id, n_cand_t * remote, n_cand_t * local,
                                   uint8_t * dest, uint32_t dest_len, int inbound);
static size_t priv_get_password(n_agent_t * agent, Stream * stream,
                                n_cand_t * remote, uint8_t ** password);
static void conn_check_free_item(void * data);
static void priv_conn_check_add_for_candidate_pair_matched(n_agent_t * agent, uint32_t stream_id, Component * component, n_cand_t * local, n_cand_t * remote, NiceCheckState initial_state);

static void nice_print_candpair(n_agent_t * agent, CandidateCheckPair * pair);

static void nice_print_candpair(n_agent_t * agent, CandidateCheckPair * pair)
{
	if (nice_debug_is_enabled())
	{
		char tmpbuf1[INET6_ADDRSTRLEN];
		char tmpbuf2[INET6_ADDRSTRLEN];

		nice_address_to_string(&pair->local->addr, tmpbuf1);
		nice_address_to_string(&pair->remote->addr, tmpbuf2);
		nice_debug("[%s agent:0x%p]: local '%s:%u' -> remote '%s:%u'", G_STRFUNC, agent,
			tmpbuf1, nice_address_get_port(&pair->local->addr),
			tmpbuf2, nice_address_get_port(&pair->remote->addr));
	}
}

static int _timer_expired(n_timeval_t * timer, n_timeval_t * now)
{
    return (now->tv_sec == timer->tv_sec) ?
           now->tv_usec >= timer->tv_usec :
           now->tv_sec >= timer->tv_sec;
}

/*
 * Finds the next connectivity check in WAITING state.
 */
static CandidateCheckPair * priv_conn_check_find_next_waiting(n_slist_t * conn_check_list)
{
    n_slist_t * i;

    /* note: list is sorted in priority order to first waiting check has
     *       the highest priority */

    for (i = conn_check_list; i ; i = i->next)
    {
        CandidateCheckPair * p = i->data;
        if (p->state == NICE_CHECK_WAITING)
            return p;
    }

    return NULL;
}

/*
 * Initiates a new connectivity check for a ICE candidate pair.
 *
 * @return TRUE on success, FALSE on error
 */
static int priv_conn_check_initiate(n_agent_t * agent, CandidateCheckPair * pair)
{
    /* XXX: from ID-16 onwards, the checks should not be sent
     * immediately, but be put into the "triggered queue",
     * see  "7.2.1.4 Triggered Checks"
     */
    get_current_time(&pair->next_tick);
    time_val_add(&pair->next_tick, agent->timer_ta * 1000);
    pair->state = NICE_CHECK_IN_PROGRESS;
    nice_debug("[%s agent:0x%p]: pair %p state IN_PROGRESS", G_STRFUNC, agent, pair);
	nice_print_candpair(agent, pair);
    conn_check_send(agent, pair);
    return TRUE;
}

/*
 * Unfreezes the next connectivity check in the list. Follows the
 * algorithm (2.) defined in 5.7.4 (Computing States) of the ICE spec
 * (ID-19), with some exceptions (see comments in code).
 *
 * See also sect 7.1.2.2.3 (Updating Pair States), and
 * priv_conn_check_unfreeze_related().
 *
 * @return TRUE on success, and FALSE if no frozen candidates were found.
 */
static int priv_conn_check_unfreeze_next(n_agent_t * agent)
{
    CandidateCheckPair * pair = NULL;
    n_slist_t * i, *j;

    /* XXX: the unfreezing is implemented a bit differently than in the
     *      current ICE spec, but should still be interoperate:
     *   - checks are not grouped by foundation
     *   - one frozen check is unfrozen (lowest component-id, highest
     *     priority)
     */

    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * stream = i->data;
        guint64 max_frozen_priority = 0;


        for (j = stream->conncheck_list; j ; j = j->next)
        {
            CandidateCheckPair * p = j->data;

            /* XXX: the prio check could be removed as the pairs are sorted
             *       already */

            if (p->state == NICE_CHECK_FROZEN)
            {
                if (p->priority > max_frozen_priority)
                {
                    max_frozen_priority = p->priority;
                    pair = p;
                }
            }
        }

        if (pair)
            break;
    }

    if (pair)
    {
        nice_debug("[%s agent:0x%p]: Pair %p with s/c-id %u/%u (%s) unfrozen.", G_STRFUNC, agent, pair, pair->stream_id, pair->component_id, pair->foundation);
        pair->state = NICE_CHECK_WAITING;
        nice_debug("[%s agent:0x%p]: pair %p state NICE_CHECK_WAITING", G_STRFUNC, agent, pair);
		nice_print_candpair(agent, pair);
        return TRUE;
    }

    return FALSE;
}

/*
 * Unfreezes the next next connectivity check in the list after
 * check 'success_check' has successfully completed.
 *
 * See sect 7.1.2.2.3 (Updating Pair States) of ICE spec (ID-19).
 *
 * @param agent context
 * @param ok_check a connectivity check that has just completed
 *
 * @return TRUE on success, and FALSE if no frozen candidates were found.
 */
static void priv_conn_check_unfreeze_related(n_agent_t * agent, Stream * stream, CandidateCheckPair * ok_check)
{
    n_slist_t * i, *j;
    uint32_t unfrozen = 0;

    g_assert(ok_check);
    g_assert(ok_check->state == NICE_CHECK_SUCCEEDED);
    g_assert(stream);
    g_assert(stream->id == ok_check->stream_id);

    /* step: perform the step (1) of 'Updating Pair States' */
    for (i = stream->conncheck_list; i ; i = i->next)
    {
        CandidateCheckPair * p = i->data;

        if (p->stream_id == ok_check->stream_id)
        {
            if (p->state == NICE_CHECK_FROZEN &&
                    strcmp(p->foundation, ok_check->foundation) == 0)
            {
                nice_debug("[%s agent:0x%p]: Unfreezing check %p (after successful check %p).", G_STRFUNC, agent, p, ok_check);
                p->state = NICE_CHECK_WAITING;
                nice_debug("[%s agent:0x%p]: pair %p state NICE_CHECK_WAITING", G_STRFUNC, agent, p);
				nice_print_candpair(agent, p);
                ++unfrozen;
            }
        }
    }

    /* step: perform the step (2) of 'Updating Pair States' */
    stream = agent_find_stream(agent, ok_check->stream_id);
    if (stream_all_components_ready(stream))
    {
        /* step: unfreeze checks from other streams */
        for (i = agent->streams_list; i ; i = i->next)
        {
            Stream * s = i->data;
            for (j = stream->conncheck_list; j ; j = j->next)
            {
                CandidateCheckPair * p = j->data;

                if (p->stream_id == s->id &&
                        p->stream_id != ok_check->stream_id)
                {
                    if (p->state == NICE_CHECK_FROZEN &&
                            strcmp(p->foundation, ok_check->foundation) == 0)
                    {
                        nice_debug("[%s agent:0x%p]: Unfreezing check %p from stream %u (after successful check %p).", G_STRFUNC, agent, p, s->id, ok_check);
                        p->state = NICE_CHECK_WAITING;
                        nice_debug("[%s agent:0x%p]: pair %p state NICE_CHECK_WAITING", G_STRFUNC, agent, p);
						nice_print_candpair(agent, ok_check);
                        ++unfrozen;

                    }
                }
            }
            /* note: only unfreeze check from one stream at a time */
            if (unfrozen)
                break;
        }
    }

    if (unfrozen == 0)
        priv_conn_check_unfreeze_next(agent);
}

static void
candidate_check_pair_fail(Stream * stream, n_agent_t * agent, CandidateCheckPair * p)
{
    StunTransactionId id;
    Component * component;

    component = stream_find_comp_by_id(stream, p->component_id);

    p->state = NICE_CHECK_FAILED;
    nice_debug("[%s agent:0x%p]: pair %p state NICE_CHECK_FAILED", G_STRFUNC, agent, p);
	nice_print_candpair(agent, p);

    if (p->stun_message.buffer != NULL)
    {
        stun_message_id(&p->stun_message, id);
        stun_agent_forget_transaction(&component->stun_agent, id);
    }

    p->stun_message.buffer = NULL;
    p->stun_message.buffer_len = 0;
}

/*
 * Helper function for connectivity check timer callback that
 * runs through the stream specific part of the state machine.
 *
 * @param schedule if TRUE, schedule a new check
 *
 * @return will return FALSE when no more pending timers.
 */
static int priv_conn_check_tick_stream(Stream * stream, n_agent_t * agent, n_timeval_t * now)
{
    int keep_timer_going = FALSE;
	uint32_t s_inprogress = 0, s_succeeded = 0, s_discovered = 0;
	uint32_t s_nominated = 0, s_waiting_for_nomination = 0;
    uint32_t frozen = 0, waiting = 0;
    n_slist_t * i, * k;

    for (i = stream->conncheck_list; i ; i = i->next)
    {
        CandidateCheckPair * p = i->data;

        if (p->state == NICE_CHECK_IN_PROGRESS)
        {
            if (p->stun_message.buffer == NULL)
            {
                nice_debug("[%s agent:0x%p]: STUN connectivity check was cancelled, marking as done.", G_STRFUNC, agent);
                p->state = NICE_CHECK_FAILED;
                nice_debug("[%s agent:0x%p]: pair %p state NICE_CHECK_FAILED", G_STRFUNC, agent, p);
				nice_print_candpair(agent, p);
            }
            else if (_timer_expired(&p->next_tick, now))
            {
                switch (stun_timer_refresh(&p->timer))
                {
                    case STUN_TIMER_RET_TIMEOUT:
                    {
                        /* case: error, abort processing */
                        nice_debug("[%s agent:0x%p]: STUN Retransmissions failed, giving up on connectivity check %p", G_STRFUNC, agent, p);
                        candidate_check_pair_fail(stream, agent, p);
						nice_print_candpair(agent, p);

                        break;
                    }
                    case STUN_TIMER_RET_RETRANSMIT:
                    {
                        /* case: not ready, so schedule a new timeout */
                        unsigned int timeout = stun_timer_remainder(&p->timer);
                        nice_debug("[%s agent:0x%p]: STUN transaction retransmitted (timeout %dms)", G_STRFUNC, agent, timeout);
						nice_print_candpair(agent, p);

                        agent_socket_send(p->sockptr, &p->remote->addr,
                                          stun_message_length(&p->stun_message),
                                          (char *)p->stun_buffer);


                        /* note: convert from milli to microseconds for g_time_val_add() */
                        p->next_tick = *now;
                        time_val_add(&p->next_tick, timeout * 1000);

                        keep_timer_going = TRUE;
                        break;
                    }
                    case STUN_TIMER_RET_SUCCESS:
                    {
                        unsigned int timeout = stun_timer_remainder(&p->timer);

                        /* note: convert from milli to microseconds for g_time_val_add() */
                        p->next_tick = *now;
                        time_val_add(&p->next_tick, timeout * 1000);
						nice_debug("[%s agent:0x%p]: STUN success %p", G_STRFUNC, agent, p);
						nice_print_candpair(agent, p);
                        keep_timer_going = TRUE;
                        break;
                    }
                    default:
                        /* Nothing to do. */
                        break;
                }
            }
        }

        if (p->state == NICE_CHECK_FROZEN)
            ++frozen;
        else if (p->state == NICE_CHECK_IN_PROGRESS)
            ++s_inprogress;
        else if (p->state == NICE_CHECK_WAITING)
            ++waiting;
        else if (p->state == NICE_CHECK_SUCCEEDED)
            ++s_succeeded;
        else if (p->state == NICE_CHECK_DISCOVERED)
            ++s_discovered;

        if ((p->state == NICE_CHECK_SUCCEEDED || p->state == NICE_CHECK_DISCOVERED)
                && p->nominated)
            ++s_nominated;
        else if ((p->state == NICE_CHECK_SUCCEEDED ||
                  p->state == NICE_CHECK_DISCOVERED) && !p->nominated)
            ++s_waiting_for_nomination;
    }

    /* note: keep the timer going as long as there is work to be done */
    if (s_inprogress)
        keep_timer_going = TRUE;

    /* note: if some components have established connectivity,
     *       but yet no nominated pair, keep timer going */
    if (s_nominated < stream->n_components &&
            s_waiting_for_nomination)
    {
        keep_timer_going = TRUE;
        if (agent->controlling_mode)
        {
            n_slist_t * component_item;

            for (component_item = stream->components; component_item;
                    component_item = component_item->next)
            {
                Component * component = component_item->data;

                for (k = stream->conncheck_list; k ; k = k->next)
                {
                    CandidateCheckPair * p = k->data;
                    /* note: highest priority item selected (list always sorted) */
                    if (p->component_id == component->id &&
                            (p->state == NICE_CHECK_SUCCEEDED ||
                             p->state == NICE_CHECK_DISCOVERED))
                    {
                        nice_debug("[%s agent:0x%p]: restarting check %p as the nominated pair.", G_STRFUNC, agent, p);
						nice_print_candpair(agent, p);
                        p->nominated = TRUE;
                        priv_conn_check_initiate(agent, p);
                        break; /* move to the next component */
                    }
                }
            }
        }
    }
    {
        static int tick_counter = 0;
        if (tick_counter++ % 50 == 0 || keep_timer_going != TRUE)
            nice_debug("[%s agent:0x%p]: timer tick #%u: %u frozen, %u in-progress, "
                       "%u waiting, %u succeeded, %u discovered, %u nominated, "
                       "%u waiting-for-nomination", G_STRFUNC, agent,
                       tick_counter, frozen, s_inprogress, waiting, s_succeeded,
                       s_discovered, s_nominated, s_waiting_for_nomination);
    }

    return keep_timer_going;

}


/*
 * Timer callback that handles initiating and managing connectivity
 * checks (paced by the Ta timer).
 *
 * This function is designed for the g_timeout_add() interface.
 *
 * @return will return FALSE when no more pending timers.
 */
static int priv_conn_check_tick_unlocked(n_agent_t * agent)
{
    CandidateCheckPair * pair = NULL;
    int keep_timer_going = FALSE;
    n_slist_t * i, *j;
    n_timeval_t now;

    /* step: process ongoing STUN transactions */
    get_current_time(&now);

    /* step: find the highest priority waiting check and send it */
    for (i = agent->streams_list; i ; i = i->next)
    {
        Stream * stream = i->data;

        pair = priv_conn_check_find_next_waiting(stream->conncheck_list);
        if (pair)
            break;
    }

    if (pair)
    {
        priv_conn_check_initiate(agent, pair);
        keep_timer_going = TRUE;
    }
    else
    {
        keep_timer_going = priv_conn_check_unfreeze_next(agent);
    }

    for (j = agent->streams_list; j; j = j->next)
    {
        Stream * stream = j->data;
        int res =
            priv_conn_check_tick_stream(stream, agent, &now);
        if (res)
            keep_timer_going = res;
    }

    /* step: stop timer if no work left */
    if (keep_timer_going != TRUE)
    {
        nice_debug("[%s agent:0x%p]: %s: stopping conncheck timer", G_STRFUNC, agent, G_STRFUNC);
        for (i = agent->streams_list; i; i = i->next)
        {
            Stream * stream = i->data;
            priv_update_check_list_failed_components(agent, stream);
            for (j = stream->components; j; j = j->next)
            {
                Component * component = j->data;
                priv_update_check_list_state_for_ready(agent, stream, component);
            }
        }

        /* Stopping the timer so destroy the source.. this will allow
           the timer to be reset if we get a set_remote_candidates after this
           point */
        if (agent->conncheck_timer_source != NULL)
        {
            g_source_destroy(agent->conncheck_timer_source);
            g_source_unref(agent->conncheck_timer_source);
            agent->conncheck_timer_source = NULL;
        }

        /* XXX: what to signal, is all processing now really done? */
        nice_debug("[%s agent:0x%p]: changing conncheck state to COMPLETED.", G_STRFUNC, agent);
    }

    return keep_timer_going;
}

static int priv_conn_check_tick(void * pointer)
{
    int ret;
    n_agent_t * agent = pointer;

    agent_lock();
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. "
                   "Avoided race condition in priv_conn_check_tick");
        agent_unlock();
        return FALSE;
    }

    ret = priv_conn_check_tick_unlocked(agent);
    agent_unlock_and_emit(agent);

    return ret;
}

static int priv_conn_keepalive_retrans_tick(void * pointer)
{
    CandidatePair * pair = (CandidatePair *) pointer;

    agent_lock();

    /* A race condition might happen where the mutex above waits for the lock
     * and in the meantime another thread destroys the source.
     * In that case, we don't need to run our retransmission tick since it should
     * have been cancelled */
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. "
                   "Avoided race condition in priv_conn_keepalive_retrans_tick");
        agent_unlock();
        return FALSE;
    }

    g_source_destroy(pair->keepalive.tick_source);
    g_source_unref(pair->keepalive.tick_source);
    pair->keepalive.tick_source = NULL;

    switch (stun_timer_refresh(&pair->keepalive.timer))
    {
        case STUN_TIMER_RET_TIMEOUT:
        {
            /* Time out */
            StunTransactionId id;
            Component * component;

            if (!agent_find_comp(pair->keepalive.agent,
                                      pair->keepalive.stream_id, pair->keepalive.component_id,
                                      NULL, &component))
            {
                nice_debug("Could not find stream or component in"
                           " priv_conn_keepalive_retrans_tick");
                agent_unlock();
                return FALSE;
            }

            stun_message_id(&pair->keepalive.stun_message, id);
            stun_agent_forget_transaction(&component->stun_agent, id);

            if (pair->keepalive.agent->media_after_tick)
            {
                nice_debug("[%s agent:0x%p]: Keepalive conncheck timed out!! "
                           "but media was received. Suspecting keepalive lost because of "
                           "network bottleneck", G_STRFUNC, pair->keepalive.agent);

                pair->keepalive.stun_message.buffer = NULL;
            }
            else
            {
                nice_debug("[%s agent:0x%p]: Keepalive conncheck timed out!! "
                           "peer probably lost connection", G_STRFUNC, pair->keepalive.agent);
				nice_print_cand(pair->keepalive.agent, pair->local, pair->remote);
                n_sig_comp_state_change(pair->keepalive.agent,
                                                    pair->keepalive.stream_id, pair->keepalive.component_id,
                                                    COMP_STATE_FAILED);
            }
            break;
        }
        case STUN_TIMER_RET_RETRANSMIT:
            /* Retransmit */
            agent_socket_send(pair->local->sockptr, &pair->remote->addr,
                              stun_message_length(&pair->keepalive.stun_message),
                              (char *)pair->keepalive.stun_buffer);

            nice_debug("[%s agent:0x%p]: Retransmitting keepalive conncheck", G_STRFUNC, pair->keepalive.agent);
			nice_print_cand(pair->keepalive.agent, pair->local, pair->remote);
            agent_timeout_add(pair->keepalive.agent,
                                           &pair->keepalive.tick_source,
                                           "Pair keepalive", stun_timer_remainder(&pair->keepalive.timer),
                                           priv_conn_keepalive_retrans_tick, pair);
            break;
        case STUN_TIMER_RET_SUCCESS:
            agent_timeout_add(pair->keepalive.agent,
                                           &pair->keepalive.tick_source,
                                           "Pair keepalive", stun_timer_remainder(&pair->keepalive.timer),
                                           priv_conn_keepalive_retrans_tick, pair);
            break;
        default:
            /* Nothing to do. */
            break;
    }


    agent_unlock_and_emit(pair->keepalive.agent);
    return FALSE;
}

static uint32_t peer_reflexive_candidate_priority(n_agent_t * agent, n_cand_t * local_candidate)
{
    n_cand_t * candidate_priority = n_cand_new(CAND_TYPE_PEER);
    uint32_t priority;

    candidate_priority->transport = local_candidate->transport;
    candidate_priority->component_id = local_candidate->component_id;

    priority = n_cand_ice_priority(candidate_priority);
    n_cand_free(candidate_priority);

    return priority;
}



/*
 * Timer callback that handles initiating and managing connectivity
 * checks (paced by the Ta timer).
 *
 * This function is designed for the g_timeout_add() interface.
 *
 * @return will return FALSE when no more pending timers.
 */
static int priv_conn_keepalive_tick_unlocked(n_agent_t * agent)
{
    n_slist_t * i, *j, *k;
    int errors = 0;
    int ret = FALSE;
    size_t buf_len = 0;

    /* case 1: session established and media flowing
     *         (ref ICE sect 10 "Keepalives" ID-19)  */
    for (i = agent->streams_list; i; i = i->next)
    {

        Stream * stream = i->data;
        for (j = stream->components; j; j = j->next)
        {
            Component * component = j->data;
            if (component->selected_pair.local != NULL)
            {
                CandidatePair * p = &component->selected_pair;

                /* Disable keepalive checks on TCP candidates */
                if (p->local->transport != CANDIDATE_TRANSPORT_UDP)
                    continue;

                if (agent->keepalive_conncheck)
                {
                    uint32_t priority;
                    uint8_t uname[NICE_STREAM_MAX_UNAME];
                    size_t uname_len =
                        priv_create_username(agent, agent_find_stream(agent, stream->id),
                                             component->id, p->remote, p->local, uname, sizeof(uname),
                                             FALSE);
                    uint8_t * password = NULL;
                    size_t password_len = priv_get_password(agent, agent_find_stream(agent, stream->id), p->remote, &password);

                    priority = peer_reflexive_candidate_priority(agent, p->local);

                    if (nice_debug_is_enabled())
                    {
                        char tmpbuf[INET6_ADDRSTRLEN];
                        nice_address_to_string(&p->remote->addr, tmpbuf);
                        nice_debug("[%s agent:0x%p]: Keepalive STUN-CC REQ to '%s:%u', "
                                   "socket=%u (c-id:%u), username='%.*s' (%" G_GSIZE_FORMAT "), "
                                   "password='%.*s' (%" G_GSIZE_FORMAT "), priority=%u.", G_STRFUNC, agent,
                                   tmpbuf, nice_address_get_port(&p->remote->addr),
                                   g_socket_get_fd(((n_socket_t *)p->local->sockptr)->fileno),
                                   component->id, (int) uname_len, uname, uname_len,
                                   (int) password_len, password, password_len, priority);
                    }
                    if (uname_len > 0)
                    {
                        buf_len = stun_usage_ice_conncheck_create(&component->stun_agent,
                                  &p->keepalive.stun_message, p->keepalive.stun_buffer,
                                  sizeof(p->keepalive.stun_buffer),
                                  uname, uname_len, password, password_len,
                                  agent->controlling_mode, agent->controlling_mode, priority,
                                  agent->tie_breaker);

                        nice_debug("[%s agent:0x%p]: conncheck created %zd - %p",
								G_STRFUNC, agent, buf_len, p->keepalive.stun_message.buffer);

                        if (buf_len > 0)
                        {
                            stun_timer_start(&p->keepalive.timer, STUN_TIMER_TIMEOUT, STUN_TIMER_MAX_RETRANS);

                            agent->media_after_tick = FALSE;

                            /* send the conncheck */
                            agent_socket_send(p->local->sockptr, &p->remote->addr, buf_len, (char *)p->keepalive.stun_buffer);

                            p->keepalive.stream_id = stream->id;
                            p->keepalive.component_id = component->id;
                            p->keepalive.agent = agent;

                            agent_timeout_add(p->keepalive.agent,
                                                           &p->keepalive.tick_source, "Pair keepalive",
                                                           stun_timer_remainder(&p->keepalive.timer),
                                                           priv_conn_keepalive_retrans_tick, p);
                        }
                        else
                        {
                            ++errors;
                        }
                    }
                }
                else
                {
                    buf_len = stun_bind_keepalive(&component->stun_agent,
                                                        &p->keepalive.stun_message, p->keepalive.stun_buffer,
                                                        sizeof(p->keepalive.stun_buffer));

                    if (buf_len > 0)
                    {
                        agent_socket_send(p->local->sockptr, &p->remote->addr, buf_len,
                                          (char *)p->keepalive.stun_buffer);

                        nice_debug("[%s agent:0x%p]: stun_bind_keepalive for pair %p res %d.", G_STRFUNC, agent, p, (int) buf_len);
						nice_print_cand(agent, p->local, p->remote);
                    }
                    else
                    {
                        ++errors;
                    }
                }
            }
        }
    }

    /* case 2: connectivity establishment ongoing
     *         (ref ICE sect 4.1.1.4 "Keeping Candidates Alive" ID-19)  */
    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * stream = i->data;
        for (j = stream->components; j; j = j->next)
        {
            Component * component = j->data;
            if (component->state < COMP_STATE_READY && agent->stun_server_ip)
            {
                n_addr_t stun_server;
                if (nice_address_set_from_string(&stun_server, agent->stun_server_ip))
                {
                    StunAgent stun_agent;
                    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
                    StunMessage stun_message;
                    size_t buffer_len = 0;

                    nice_address_set_port(&stun_server, agent->stun_server_port);

                    /* FIXME: This will cause the stun response to arrive on the socket
                     * but the stun agent will not be able to parse it due to an invalid
                     * stun message since RFC3489 will not be compatible, and the response
                     * will be forwarded to the application as user data */
                    stun_agent_init(&stun_agent, 0);

                    buffer_len = stun_bind_create(&stun_agent, &stun_message, stun_buffer, sizeof(stun_buffer));

                    for (k = component->local_candidates; k; k = k->next)
                    {
                        n_cand_t * candidate = (n_cand_t *) k->data;
                        if (candidate->type == CAND_TYPE_HOST &&
                                candidate->transport == CANDIDATE_TRANSPORT_UDP)
                        {
                            /* send the conncheck */
                            nice_debug("[%s agent:0x%p]: resending STUN on %s to keep the " "candidate alive.", G_STRFUNC, agent, candidate->foundation);
                            agent_socket_send(candidate->sockptr, &stun_server, buffer_len, (char *)stun_buffer);
                        }
                    }
                }
            }
        }
    }

    if (errors)
    {
        nice_debug("[%s agent:0x%p]: %s: stopping keepalive timer", G_STRFUNC, agent, G_STRFUNC);
        goto done;
    }

    ret = TRUE;

done:
    return ret;
}

static int priv_conn_keepalive_tick(void * pointer)
{
    n_agent_t * agent = pointer;
    int ret;

    agent_lock();
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. "
                   "Avoided race condition in priv_conn_keepalive_tick");
        agent_unlock();
        return FALSE;
    }

    ret = priv_conn_keepalive_tick_unlocked(agent);
    if (ret == FALSE)
    {
        if (agent->keepalive_timer_source)
        {
            g_source_destroy(agent->keepalive_timer_source);
            g_source_unref(agent->keepalive_timer_source);
            agent->keepalive_timer_source = NULL;
        }
    }
    agent_unlock_and_emit(agent);
    return ret;
}

static int priv_turn_alloc_refresh_retrans_tick(void * pointer)
{
    CandidateRefresh * cand = (CandidateRefresh *) pointer;
    n_agent_t * agent = NULL;

    agent_lock();

    /* A race condition might happen where the mutex above waits for the lock
     * and in the meantime another thread destroys the source.
     * In that case, we don't need to run our retransmission tick since it should
     * have been cancelled */
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. "
                   "Avoided race condition in priv_turn_alloc_refresh_retrans_tick");
        agent_unlock();
        return FALSE;
    }

    g_source_destroy(cand->tick_source);
    g_source_unref(cand->tick_source);
    cand->tick_source = NULL;

    agent = g_object_ref(cand->agent);

    switch (stun_timer_refresh(&cand->timer))
    {
        case STUN_TIMER_RET_TIMEOUT:
        {
            /* Time out */
            StunTransactionId id;

            stun_message_id(&cand->stun_message, id);
            stun_agent_forget_transaction(&cand->stun_agent, id);

            refresh_cancel(cand);
            break;
        }
        case STUN_TIMER_RET_RETRANSMIT:
            /* Retransmit */
            agent_socket_send(cand->nicesock, &cand->server,
                              stun_message_length(&cand->stun_message), (char *)cand->stun_buffer);

            agent_timeout_add(agent, &cand->tick_source,
                                           "Candidate TURN refresh", stun_timer_remainder(&cand->timer),
                                           priv_turn_alloc_refresh_retrans_tick, cand);
            break;
        case STUN_TIMER_RET_SUCCESS:
            agent_timeout_add(agent, &cand->tick_source,
                                           "Candidate TURN refresh", stun_timer_remainder(&cand->timer),
                                           priv_turn_alloc_refresh_retrans_tick, cand);
            break;
        default:
            /* Nothing to do. */
            break;
    }


    agent_unlock_and_emit(agent);

    g_object_unref(agent);

    return FALSE;
}

static void priv_turn_alloc_refresh_tick_unlocked(CandidateRefresh * cand)
{
    uint8_t * username;
    uint32_t username_len;
    uint8_t * password;
    uint32_t password_len;
    size_t buffer_len = 0;
    StunUsageTurnCompatibility turn_compat = agent_to_turn_compatibility(cand->agent);

    username = (uint8_t *)cand->candidate->turn->username;
    username_len = (size_t) strlen(cand->candidate->turn->username);
    password = (uint8_t *)cand->candidate->turn->password;
    password_len = (size_t) strlen(cand->candidate->turn->password);

    buffer_len = turn_create_refresh(&cand->stun_agent,
                 &cand->stun_message,  cand->stun_buffer, sizeof(cand->stun_buffer),
                 cand->stun_resp_msg.buffer == NULL ? NULL : &cand->stun_resp_msg, -1,
                 username, username_len,
                 password, password_len,
                 turn_compat);

    nice_debug("[%s agent:0x%p]: Sending allocate Refresh %zd", cand->agent,  buffer_len);

    if (cand->tick_source != NULL)
    {
        g_source_destroy(cand->tick_source);
        g_source_unref(cand->tick_source);
        cand->tick_source = NULL;
    }

    if (buffer_len > 0)
    {
        stun_timer_start(&cand->timer, STUN_TIMER_RET_TIMEOUT, STUN_TIMER_MAX_RETRANS);
        /* send the refresh */
        agent_socket_send(cand->nicesock, &cand->server, buffer_len, (char *)cand->stun_buffer);
        agent_timeout_add(cand->agent, &cand->tick_source, "Candidate TURN refresh", stun_timer_remainder(&cand->timer),
                                       priv_turn_alloc_refresh_retrans_tick, cand);
    }
}

/*
 * Timer callback that handles refreshing TURN allocations
 *
 * This function is designed for the g_timeout_add() interface.
 *
 * @return will return FALSE when no more pending timers.
 */
static int priv_turn_allocate_refresh_tick(void * pointer)
{
    CandidateRefresh * cand = (CandidateRefresh *) pointer;

    agent_lock();
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. " "Avoided race condition in priv_turn_allocate_refresh_tick");
        agent_unlock();
        return FALSE;
    }

    priv_turn_alloc_refresh_tick_unlocked(cand);
    agent_unlock_and_emit(cand->agent);

    return FALSE;
}


/*
 * Initiates the next pending connectivity check.
 *
 * @return TRUE if a pending check was scheduled
 */
int conn_check_schedule_next(n_agent_t * agent)
{
    int res = priv_conn_check_unfreeze_next(agent);
    nice_debug("[%s agent:0x%p]: priv_conn_check_unfreeze_next returned %d", G_STRFUNC, agent, res);

    if (agent->disc_unsched_items > 0)
        nice_debug("[%s agent:0x%p]: WARN: starting conn checks before local candidate gathering is finished.", agent);

    /* step: call once imediately */
    res = priv_conn_check_tick_unlocked(agent);
    nice_debug("[%s agent:0x%p]: priv_conn_check_tick_unlocked returned %d", G_STRFUNC, agent, res);

    /* step: schedule timer if not running yet */
    if (res && agent->conncheck_timer_source == NULL)
    {
        agent_timeout_add(agent, &agent->conncheck_timer_source,
                                       "Connectivity check schedule", agent->timer_ta,
                                       priv_conn_check_tick, agent);
    }

    /* step: also start the keepalive timer */
    if (agent->keepalive_timer_source == NULL)
    {
        agent_timeout_add(agent, &agent->keepalive_timer_source,
                                       "Connectivity keepalive timeout", NICE_AGENT_TIMER_TR_DEFAULT,
                                       priv_conn_keepalive_tick, agent);
    }

    nice_debug("[%s agent:0x%p]: conn_check_schedule_next returning %d", G_STRFUNC, agent, res);
    return res;
}

/*
 * Compares two connectivity check items. Checkpairs are sorted
 * in descending priority order, with highest priority item at
 * the start of the list.
 */
int32_t conn_check_compare(const CandidateCheckPair * a, const CandidateCheckPair * b)
{
    if (a->priority > b->priority)
        return -1;
    else if (a->priority < b->priority)
        return 1;
    return 0;
}

/*
 * Preprocesses a new connectivity check by going through list
 * of a any stored early incoming connectivity checks from
 * the remote peer. If a matching incoming check has been already
 * received, update the state of the new outgoing check 'pair'.
 *
 * @param agent context pointer
 * @param stream which stream (of the agent)
 * @param component pointer to component object to which 'pair'has been added
 * @param pair newly added connectivity check
 */
static void priv_preprocess_conn_check_pending_data(n_agent_t * agent, Stream * stream, Component * component, CandidateCheckPair * pair)
{
    n_slist_t * i;
    for (i = component->incoming_checks; i; i = i->next)
    {
        IncomingCheck * icheck = i->data;
        if (nice_address_equal(&icheck->from, &pair->remote->addr) &&
                icheck->local_socket == pair->sockptr)
        {
            nice_debug("[%s agent:0x%p]: Updating check %p with stored early-icheck %p, %p/%u/%u (agent/stream/component).", G_STRFUNC, agent, pair, icheck, agent, stream->id, component->id);
            if (icheck->use_candidate)
                priv_mark_pair_nominated(agent, stream, component, pair->remote);
            priv_schedule_triggered_check(agent, stream, component, icheck->local_socket, pair->remote, icheck->use_candidate);
        }
    }
}


static n_slist_t * prune_cancelled_conn_check(n_slist_t * conncheck_list)
{
    n_slist_t * item = conncheck_list;

    while (item)
    {
        CandidateCheckPair * pair = item->data;
        n_slist_t * next = item->next;

        if (pair->state == NICE_CHECK_CANCELLED)
        {
            conn_check_free_item(pair);
            conncheck_list = n_slist_delete_link(conncheck_list, item);
        }

        item = next;
    }

    return conncheck_list;
}


/*
 * Handle any processing steps for connectivity checks after
 * remote candidates have been set. This function handles
 * the special case where answerer has sent us connectivity
 * checks before the answer (containing candidate information),
 * reaches us. The special case is documented in sect 7.2
 * if ICE spec (ID-19).
 */
void co_chk_remote_cands_set(n_agent_t * agent)
{
    n_slist_t * i, *j, *k, *l, *m, *n;

    for (i = agent->streams_list; i ; i = i->next)
    {
        Stream * stream = i->data;
        for (j = stream->conncheck_list; j ; j = j->next)
        {
            CandidateCheckPair * pair = j->data;
            Component * component = stream_find_comp_by_id(stream, pair->component_id);
            int match = FALSE;

            /* performn delayed processing of spec steps section 7.2.1.4,
            and section 7.2.1.5 */
            priv_preprocess_conn_check_pending_data(agent, stream, component, pair);

            for (k = component->incoming_checks; k; k = k->next)
            {
                IncomingCheck * icheck = k->data;
                /* sect 7.2.1.3., "Learning Peer Reflexive Candidates", has to
                 * be handled separately */
                for (l = component->remote_candidates; l; l = l->next)
                {
                    n_cand_t * cand = l->data;
                    if (nice_address_equal(&icheck->from, &cand->addr))
                    {
                        match = TRUE;
                        break;
                    }
                }
                if (match != TRUE)
                {
                    /* note: we have gotten an incoming connectivity check from
                     *       an address that is not a known remote candidate */

                    n_cand_t * local_candidate = NULL;
                    n_cand_t * remote_candidate = NULL;

                    if (agent->compatibility == NICE_COMPATIBILITY_GOOGLE ||
                            agent->compatibility == NICE_COMPATIBILITY_MSN ||
                            agent->compatibility == NICE_COMPATIBILITY_OC2007)
                    {
                        /* We need to find which local candidate was used */
                        uint8_t uname[NICE_STREAM_MAX_UNAME];
                        uint32_t uname_len;

                        nice_debug("[%s agent:0x%p]: We have a peer-reflexive candidate in a "
                                   "stored pending check", agent);

                        for (m = component->remote_candidates;
                                m != NULL && remote_candidate == NULL; m = m->next)
                        {
                            for (n = component->local_candidates; n; n = n->next)
                            {
                                n_cand_t * rcand = m->data;
                                n_cand_t * lcand = n->data;

                                uname_len = priv_create_username(agent, stream,
                                                                 component->id,  rcand, lcand,
                                                                 uname, sizeof(uname), TRUE);

                                stun_debug("pending check, comparing usernames of len %d and %d, equal=%d",
                                           icheck->username_len, uname_len,
                                           icheck->username && uname_len == icheck->username_len &&
                                           memcmp(uname, icheck->username, icheck->username_len) == 0);
                                stun_debug_bytes("  first username:  ",
                                                 icheck->username,
                                                 icheck->username ? icheck->username_len : 0);
                                stun_debug_bytes("  second username: ", uname, uname_len);

                                if (icheck->username &&
                                        uname_len == icheck->username_len &&
                                        memcmp(uname, icheck->username, icheck->username_len) == 0)
                                {
                                    local_candidate = lcand;
                                    remote_candidate = rcand;
                                    break;
                                }
                            }
                        }
                    }
                    else
                    {
                        for (l = component->local_candidates; l; l = l->next)
                        {
                            n_cand_t * cand = l->data;
                            if (nice_address_equal(&cand->addr, &icheck->local_socket->addr))
                            {
                                local_candidate = cand;
                                break;
                            }
                        }
                    }

                    if (agent->compatibility == NICE_COMPATIBILITY_GOOGLE &&
                            local_candidate == NULL)
                    {
                        /* if we couldn't match the username, then the matching remote
                         * candidate hasn't been received yet.. we must wait */
                        nice_debug("[%s agent:0x%p]: Username check failed. pending check has "
                                   "to wait to be processed", agent);
                    }
                    else
                    {
                        n_cand_t * candidate;

                        nice_debug("[%s agent:0x%p]: Discovered peer reflexive from early i-check",
                                   agent);
                        candidate =
                            disc_learn_remote_peer_cand(agent,
                                    stream,
                                    component,
                                    icheck->priority,
                                    &icheck->from,
                                    icheck->local_socket,
                                    local_candidate, remote_candidate);
                        if (candidate)
                        {                            
                            conn_check_add_for_candidate(agent, stream->id, component, candidate);
                            if (icheck->use_candidate)
                                priv_mark_pair_nominated(agent, stream, component, candidate);
                            priv_schedule_triggered_check(agent, stream, component, icheck->local_socket, candidate, icheck->use_candidate);
                        }
                    }
                }
            }

            /* Once we process the pending checks, we should free them to avoid
             * reprocessing them again if a dribble-mode set_remote_candidates
             * is called */
            n_slist_free_full(component->incoming_checks, (n_destroy_notify) incoming_check_free);
            component->incoming_checks = NULL;
        }

        stream->conncheck_list = prune_cancelled_conn_check(stream->conncheck_list);
    }
}

/*
 * Enforces the upper limit for connectivity checks as described
 * in ICE spec section 5.7.3 (ID-19). See also
 * conn_check_add_for_candidate().
 */
static void priv_limit_conn_check_list_size(n_slist_t * conncheck_list, uint32_t upper_limit)
{
    uint32_t valid = 0;
    uint32_t cancelled = 0;
    n_slist_t * item = conncheck_list;

    while (item)
    {
        CandidateCheckPair * pair = item->data;

        if (pair->state != NICE_CHECK_CANCELLED)
        {
            valid++;
            if (valid > upper_limit)
            {
                pair->state = NICE_CHECK_CANCELLED;
                cancelled++;
            }
        }

        item = item->next;
    }

    if (cancelled > 0)
        nice_debug("Agent : Pruned %d candidates. Conncheck list has %d elements"
                   " left. Maximum connchecks allowed : %d", cancelled, valid,
                   upper_limit);
}

/*
 * Changes the selected pair for the component if 'pair' is nominated
 * and has higher priority than the currently selected pair. See
 * ICE sect 11.1.1. "Procedures for Full Implementations" (ID-19).
 */
static int priv_update_selected_pair(n_agent_t * agent, Component * component, CandidateCheckPair * pair)
{
    CandidatePair cpair;

    g_assert(component);
    g_assert(pair);
    if (pair->priority > component->selected_pair.priority &&
            component_find_pair(component, agent, pair->local->foundation,
                                pair->remote->foundation, &cpair))
    {
        nice_debug("[%s agent:0x%p]: changing SELECTED PAIR for component %u: %s:%s "
                   "(prio:%" G_GUINT64_FORMAT ").", G_STRFUNC, agent, component->id,
                   pair->local->foundation, pair->remote->foundation, pair->priority);

        component_update_selected_pair(component, &cpair);

        priv_conn_keepalive_tick_unlocked(agent);

        agent_signal_new_selected_pair(agent, pair->stream_id, component->id,
                                       pair->local, pair->remote);

    }

    return TRUE;
}

/*
 * Updates the check list state.
 *
 * Implements parts of the algorithm described in
 * ICE sect 8.1.2. "Updating States" (ID-19): if for any
 * component, all checks have been completed and have
 * failed, mark that component's state to NICE_CHECK_FAILED.
 *
 * Sends a component state changesignal via 'agent'.
 */
static void priv_update_check_list_failed_components(n_agent_t * agent, Stream * stream)
{
    n_slist_t * i;
    /* note: emitting a signal might cause the client
     *       to remove the stream, thus the component count
     *       must be fetched before entering the loop*/
    uint32_t c, components = stream->n_components;

    for (i = agent->discovery_list; i; i = i->next)
    {
        n_cand_disc_t * d = i->data;

        /* There is still discovery ogoing for this stream,
         * so don't fail any of it's candidates.
         */
        if (d->stream == stream && !d->done)
            return;
    }
    if (agent->discovery_list != NULL)
        return;

    /* note: iterate the conncheck list for each component separately */
    for (c = 0; c < components; c++)
    {
        Component * comp = NULL;
        if (!agent_find_comp(agent, stream->id, c + 1, NULL, &comp))
            continue;

        for (i = stream->conncheck_list; i; i = i->next)
        {
            CandidateCheckPair * p = i->data;

            g_assert(p->agent == agent);
            g_assert(p->stream_id == stream->id);

            if (p->component_id == (c + 1))
            {
                if (p->state != NICE_CHECK_FAILED)
                    break;
            }
        }

        /* note: all checks have failed
         * Set the component to FAILED only if it actually had remote candidates
         * that failed.. */
        if (i == NULL && comp != NULL && comp->remote_candidates != NULL)
            n_sig_comp_state_change(agent, stream->id, (c + 1), COMP_STATE_FAILED);
    }
}

/*
 * Updates the check list state for a stream component.
 *
 * Implements the algorithm described in ICE sect 8.1.2
 * "Updating States" (ID-19) as it applies to checks of
 * a certain component. If there are any nominated pairs,
 * ICE processing may be concluded, and component state is
 * changed to READY.
 *
 * Sends a component state changesignal via 'agent'.
 */
static void priv_update_check_list_state_for_ready(n_agent_t * agent, Stream * stream, Component * component)
{
    n_slist_t * i;
    uint32_t succeeded = 0, nominated = 0;

    g_assert(component);

    /* step: search for at least one nominated pair */
    for (i = stream->conncheck_list; i; i = i->next)
    {
        CandidateCheckPair * p = i->data;
        if (p->component_id == component->id)
        {
            if (p->state == NICE_CHECK_SUCCEEDED ||
                    p->state == NICE_CHECK_DISCOVERED)
            {
                ++succeeded;
                if (p->nominated == TRUE)
                {
                    ++nominated;
                }
            }
        }
    }

    if (nominated > 0)
    {
        /* Only go to READY if no checks are left in progress. If there are
         * any that are kept, then this function will be called again when the
         * conncheck tick timer finishes them all */
        if (priv_prune_pending_checks(stream, component->id) == 0)
        {
            n_sig_comp_state_change(agent, stream->id,  component->id, COMP_STATE_READY);
        }
    }
    nice_debug("[%s agent:0x%p]: conn.check list status: %u nominated, %u succeeded, c-id %u.", G_STRFUNC, agent, nominated, succeeded, component->id);
}

/*
 * The remote party has signalled that the candidate pair
 * described by 'component' and 'remotecand' is nominated
 * for use.
 */
static void priv_mark_pair_nominated(n_agent_t * agent, Stream * stream, Component * component, n_cand_t * remotecand)
{
    n_slist_t * i;

    g_assert(component);

    /* step: search for at least one nominated pair */
    for (i = stream->conncheck_list; i; i = i->next)
    {
        CandidateCheckPair * pair = i->data;
        /* XXX: hmm, how to figure out to which local candidate the
         *      check was sent to? let's mark all matching pairs
         *      as nominated instead */
        if (pair->remote == remotecand)
        {
            nice_debug("[%s agent:0x%p]: marking pair %p (%s) as nominated", G_STRFUNC, agent, pair, pair->foundation);
            pair->nominated = TRUE;
            if (pair->state == NICE_CHECK_SUCCEEDED ||
                    pair->state == NICE_CHECK_DISCOVERED)
                priv_update_selected_pair(agent, component, pair);
            priv_update_check_list_state_for_ready(agent, stream, component);
        }
    }
}

/*
 * Creates a new connectivity check pair and adds it to
 * the agent's list of checks.
 */
static void priv_add_new_check_pair(n_agent_t * agent, uint32_t stream_id, Component * component, n_cand_t * local, 
		n_cand_t * remote, NiceCheckState initial_state, int use_candidate)
{
    Stream * stream;
    CandidateCheckPair * pair;

    g_assert(local != NULL);
    g_assert(remote != NULL);

    stream = agent_find_stream(agent, stream_id);
    pair = g_slice_new0(CandidateCheckPair);

    pair->agent = agent;
    pair->stream_id = stream_id;
    pair->component_id = component->id;;
    pair->local = local;
    pair->remote = remote;
    if (remote->type == CAND_TYPE_PEER)
        pair->sockptr = (n_socket_t *) remote->sockptr;
    else
        pair->sockptr = (n_socket_t *) local->sockptr;
    g_snprintf(pair->foundation, CANDIDATE_PAIR_MAX_FOUNDATION, "%s:%s", local->foundation, remote->foundation);

    pair->priority = agent_candidate_pair_priority(agent, local, remote);
    pair->state = initial_state;
    nice_debug("[%s agent:0x%p]: creating new pair %p state %d", G_STRFUNC, agent, pair, initial_state);
    pair->nominated = use_candidate;
    pair->controlling = agent->controlling_mode;

    stream->conncheck_list = n_slist_insert_sorted(stream->conncheck_list, pair,  (GCompareFunc)conn_check_compare);

    nice_debug("[%s agent:0x%p]: added a new conncheck %p with foundation of '%s' to list %u.", G_STRFUNC, agent, pair, pair->foundation, stream_id);

    /* implement the hard upper limit for number of checks (see sect 5.7.3 ICE ID-19): */    
    priv_limit_conn_check_list_size(stream->conncheck_list, agent->max_conn_checks);
}

NiceCandidateTransport conn_check_match_transport(NiceCandidateTransport transport)
{
    switch (transport)
    {
        case CANDIDATE_TRANSPORT_TCP_ACTIVE:
            return CANDIDATE_TRANSPORT_TCP_PASSIVE;
            break;
        case CANDIDATE_TRANSPORT_TCP_PASSIVE:
            return CANDIDATE_TRANSPORT_TCP_ACTIVE;
            break;
        case CANDIDATE_TRANSPORT_TCP_SO:
        case CANDIDATE_TRANSPORT_UDP:
        default:
            return transport;
            break;
    }
}

static void priv_conn_check_add_for_candidate_pair_matched(n_agent_t * agent,
        uint32_t stream_id, Component * component, n_cand_t * local,
        n_cand_t * remote, NiceCheckState initial_state)
{
    nice_debug("[%s agent:0x%p] Adding check pair between %s and %s", G_STRFUNC, agent, local->foundation, remote->foundation);

    priv_add_new_check_pair(agent, stream_id, component, local, remote, initial_state, FALSE);
    if (component->state == COMP_STATE_CONNECTED || component->state == COMP_STATE_READY)
    {
        n_sig_comp_state_change(agent, stream_id, component->id, COMP_STATE_CONNECTED);
    }
    else
    {
        n_sig_comp_state_change(agent, stream_id, component->id, COMP_STATE_CONNECTING);
    }
}

int conn_check_add_for_candidate_pair(n_agent_t * agent,
        uint32_t stream_id, Component * component, n_cand_t * local,
        n_cand_t * remote)
{
    int ret = FALSE;

    g_assert(local != NULL);
    g_assert(remote != NULL);

    /* note: do not create pairs where the local candidate is
     *       a srv-reflexive (ICE 5.7.3. "Pruning the pairs" ID-9) */
    if (local->type == CAND_TYPE_SERVER)
    {
        return FALSE;
    } 

    /* note: match pairs only if transport and address family are the same */
    if (local->addr.s.addr.sa_family == remote->addr.s.addr.sa_family)
    {
        priv_conn_check_add_for_candidate_pair_matched(agent, stream_id, component,
                local, remote, NICE_CHECK_FROZEN);
        ret = TRUE;
    }

    return ret;
}

/*
 * Forms new candidate pairs by matching the new remote candidate
 * 'remote_cand' with all existing local candidates of 'component'.
 * Implements the logic described in ICE sect 5.7.1. "Forming Candidate
 * Pairs" (ID-19).
 *
 * @param agent context
 * @param component pointer to the component
 * @param remote remote candidate to match with
 *
 * @return number of checks added, negative on fatal errors
 */
int conn_check_add_for_candidate(n_agent_t * agent, uint32_t stream_id, Component * component, n_cand_t * remote)
{
    n_slist_t * i;
    int added = 0;
    int ret = 0;

    g_assert(remote != NULL);

    for (i = component->local_candidates; i ; i = i->next)
    {

        n_cand_t * local = i->data;
        ret = conn_check_add_for_candidate_pair(agent, stream_id, component, local, remote);

        if (ret)
        {
            ++added;
        }
    }

    return added;
}

/*
 * Forms new candidate pairs by matching the new local candidate
 * 'local_cand' with all existing remote candidates of 'component'.
 *
 * @param agent context
 * @param component pointer to the component
 * @param local local candidate to match with
 *
 * @return number of checks added, negative on fatal errors
 */
int conn_check_add_for_local_candidate(n_agent_t * agent, uint32_t stream_id, Component * component, n_cand_t * local)
{
    n_slist_t * i;
    int added = 0;
    int ret = 0;

    g_assert(local != NULL);

    for (i = component->remote_candidates; i ; i = i->next)
    {

        n_cand_t * remote = i->data;
        ret = conn_check_add_for_candidate_pair(agent, stream_id, component, local, remote);

        if (ret)
        {
            ++added;
        }
    }

    return added;
}

/*
 * Frees the CandidateCheckPair structure pointer to
 * by 'user data'. Compatible with n_destroy_notify.
 */
static void conn_check_free_item(void * data)
{
    CandidateCheckPair * pair = data;

    pair->stun_message.buffer = NULL;
    pair->stun_message.buffer_len = 0;
    g_slice_free(CandidateCheckPair, pair);
}

static void conn_check_stop(n_agent_t * agent)
{
    if (agent->conncheck_timer_source == NULL)
        return;

    g_source_destroy(agent->conncheck_timer_source);
    g_source_unref(agent->conncheck_timer_source);
    agent->conncheck_timer_source = NULL;
}

/*
 * Frees all resources of all connectivity checks.
 */
void conn_check_free(n_agent_t * agent)
{
    n_slist_t * i;
    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * stream = i->data;

        if (stream->conncheck_list)
        {
            nice_debug("[%s agent:0x%p], freeing conncheck_list of stream %p", G_STRFUNC, agent, stream);
            n_slist_free_full(stream->conncheck_list, conn_check_free_item);
            stream->conncheck_list = NULL;
        }
    }

    conn_check_stop(agent);
}

/*
 * Prunes the list of connectivity checks for items related
 * to stream 'stream_id'.
 *
 * @return TRUE on success, FALSE on a fatal error
 */
void conn_check_prune_stream(n_agent_t * agent, Stream * stream)
{
    n_slist_t * i;
    int keep_going = FALSE;

    if (stream->conncheck_list)
    {
        nice_debug("[%s agent:0x%p], freeing conncheck_list of stream %p", G_STRFUNC, agent, stream);

        n_slist_free_full(stream->conncheck_list, conn_check_free_item);
        stream->conncheck_list = NULL;
    }

    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * s = i->data;
        if (s->conncheck_list)
        {
            keep_going = TRUE;
            break;
        }
    }

    if (!keep_going)
        conn_check_stop(agent);
}

/*
 * Fills 'dest' with a username string for use in an outbound connectivity
 * checks. No more than 'dest_len' characters (including terminating
 * NULL) is ever written to the 'dest'.
 */
static size_t priv_gen_username(n_agent_t * agent, uint32_t component_id,
                         char * remote, char * local, uint8_t * dest, uint32_t dest_len)
{
    uint32_t len = 0;
    uint32_t remote_len = strlen(remote);
    uint32_t local_len = strlen(local);

    if (remote_len > 0 && local_len > 0 && dest_len >= remote_len + local_len + 1)
    {
		memcpy(dest, remote, remote_len);
		len += remote_len;
		memcpy(dest + len, ":", 1);
		len++;
		memcpy(dest + len, local, local_len);
		len += local_len;
    }
    return len;
}

/*
 * Fills 'dest' with a username string for use in an outbound connectivity
 * checks. No more than 'dest_len' characters (including terminating
 * NULL) is ever written to the 'dest'.
 */
static size_t priv_create_username(n_agent_t * agent, Stream * stream,
                            uint32_t component_id, n_cand_t * remote, n_cand_t * local,
                            uint8_t * dest, uint32_t dest_len, int inbound)
{
    char * local_username = NULL;
    char * remote_username = NULL;


    if (remote && remote->username)
    {
        remote_username = remote->username;
    }

    if (local && local->username)
    {
        local_username = local->username;
    }

    if (stream)
    {
        if (remote_username == NULL)
        {
            remote_username = stream->remote_ufrag;
        }
        if (local_username == NULL)
        {
            local_username = stream->local_ufrag;
        }
    }

    if (local_username && remote_username)
    {
        if (inbound)
        {
            return priv_gen_username(agent, component_id, local_username, remote_username, dest, dest_len);
        }
        else
        {
            return priv_gen_username(agent, component_id, remote_username, local_username, dest, dest_len);
        }
    }

    return 0;
}

/*
 * Returns a password string for use in an outbound connectivity
 * check.
 */
static size_t priv_get_password(n_agent_t * agent, Stream * stream, n_cand_t * remote, uint8_t ** password)
{
    if (remote && remote->password)
    {
        *password = (uint8_t *)remote->password;
        return strlen(remote->password);
    }

    if (stream)
    {
        *password = (uint8_t *)stream->remote_password;
        return strlen(stream->remote_password);
    }

    return 0;
}

/* Implement the computation specific in RFC 5245 section 16 */

static unsigned int priv_compute_conncheck_timer(n_agent_t * agent, Stream * stream)
{
    n_slist_t * item;
    uint32_t waiting_and_in_progress = 0;
    unsigned int rto = 0;

    for (item = stream->conncheck_list; item; item = item->next)
    {
        CandidateCheckPair * pair = item->data;

        if (pair->state == NICE_CHECK_IN_PROGRESS || pair->state == NICE_CHECK_WAITING)
            waiting_and_in_progress++;
    }

    /* FIXME: This should also be multiple by "N", which I believe is the
     * number of Streams currently in the conncheck state. */
    rto = agent->timer_ta  * waiting_and_in_progress;

    /* We assume non-reliable streams are RTP, so we use 100 as the max */
    if (agent->reliable)
        return MAX(rto, 500);
    else
        return MAX(rto, 100);
}

/*
 * Sends a connectivity check over candidate pair 'pair'.
 *
 * @return zero on success, non-zero on error
 */
int conn_check_send(n_agent_t * agent, CandidateCheckPair * pair)
{

    /* note: following information is supplied:
     *  - username (for USERNAME attribute)
     *  - password (for MESSAGE-INTEGRITY)
     *  - priority (for PRIORITY)
     *  - ICE-CONTROLLED/ICE-CONTROLLING (for role conflicts)
     *  - USE-CANDIDATE (if sent by the controlling agent)
     */
    guint32 priority;

    uint8_t uname[NICE_STREAM_MAX_UNAME];
    Stream * stream;
    Component * component;
    uint32_t uname_len;
    uint8_t * password = NULL;
    uint32_t password_len;
    bool controlling = agent->controlling_mode;
    /* XXX: add API to support different nomination modes: */
    bool cand_use = controlling;
    size_t buffer_len;
    unsigned int timeout;

    if (!agent_find_comp(agent, pair->stream_id, pair->component_id, &stream, &component))
        return -1;

    uname_len = priv_create_username(agent, stream, pair->component_id,  pair->remote, pair->local, uname, sizeof(uname), FALSE);
    password_len = priv_get_password(agent, stream, pair->remote, &password);
    priority = peer_reflexive_candidate_priority(agent, pair->local);

    if (nice_debug_is_enabled())
    {
        char tmpbuf[INET6_ADDRSTRLEN];
        nice_address_to_string(&pair->remote->addr, tmpbuf);
        nice_debug("[%s agent:0x%p]: STUN-CC REQ to '%s:%u', socket=%u, "
                   "pair=%s (c-id:%u), tie=%llu, username='%.*s' (%" G_GSIZE_FORMAT "), "
                   "password='%.*s' (%" G_GSIZE_FORMAT "), priority=%u.", G_STRFUNC, agent,
                   tmpbuf,
                   nice_address_get_port(&pair->remote->addr),
                   pair->sockptr->fileno ? g_socket_get_fd(pair->sockptr->fileno) : -1,
                   pair->foundation, pair->component_id,
                   (unsigned long long)agent->tie_breaker,
                   (int) uname_len, uname, uname_len,
                   (int) password_len, password, password_len, priority);

    }

    if (cand_use)
        pair->nominated = controlling;

    if (uname_len > 0)
    {
        buffer_len = stun_usage_ice_conncheck_create(&component->stun_agent,
                     &pair->stun_message, pair->stun_buffer, sizeof(pair->stun_buffer),
                     uname, uname_len, password, password_len,
                     cand_use, controlling, priority,
                     agent->tie_breaker);

        nice_debug("[%s agent:0x%p]: conncheck created %zd - %p", G_STRFUNC, agent, buffer_len,  pair->stun_message.buffer);

        if (buffer_len > 0)
        {
            if (nice_socket_is_reliable(pair->sockptr))
            {
                stun_timer_start_reliable(&pair->timer, STUN_TIMER_RELIABLE_TIMEOUT);
            }
            else
            {
                stun_timer_start(&pair->timer, priv_compute_conncheck_timer(agent, stream), STUN_TIMER_MAX_RETRANS);
            }

            /* send the conncheck */
            agent_socket_send(pair->sockptr, &pair->remote->addr, buffer_len, (char *)pair->stun_buffer);

            timeout = stun_timer_remainder(&pair->timer);
            /* note: convert from milli to microseconds for g_time_val_add() */
            get_current_time(&pair->next_tick);
            time_val_add(&pair->next_tick, timeout * 1000);
        }
        else
        {
            nice_debug("[%s agent:0x%p]: buffer is empty, cancelling conncheck", agent);
            pair->stun_message.buffer = NULL;
            pair->stun_message.buffer_len = 0;
            return -1;
        }
    }
    else
    {
        nice_debug("[%s agent:0x%p]: no credentials found, cancelling conncheck", agent);
        pair->stun_message.buffer = NULL;
        pair->stun_message.buffer_len = 0;
        return -1;
    }

    return 0;
}

/*
 * Implemented the pruning steps described in ICE sect 8.1.2
 * "Updating States" (ID-19) after a pair has been nominated.
 *
 * @see priv_update_check_list_state_failed_components()
 */
static uint32_t priv_prune_pending_checks(Stream * stream, uint32_t component_id)
{
    n_slist_t * i;
    guint64 highest_nominated_priority = 0;
    uint32_t in_progress = 0;

    nice_debug("Agent XXX: Finding highest priority for component %d", component_id);

    for (i = stream->conncheck_list; i; i = i->next)
    {
        CandidateCheckPair * p = i->data;
        if (p->component_id == component_id &&
                (p->state == NICE_CHECK_SUCCEEDED ||
                 p->state == NICE_CHECK_DISCOVERED) &&
                p->nominated == TRUE)
        {
            if (p->priority > highest_nominated_priority)
            {
                highest_nominated_priority = p->priority;
            }
        }
    }

    nice_debug("Agent XXX: Pruning pending checks. Highest nominated priority "
               "is %" G_GUINT64_FORMAT, highest_nominated_priority);

    /* step: cancel all FROZEN and WAITING pairs for the component */
    for (i = stream->conncheck_list; i; i = i->next)
    {
        CandidateCheckPair * p = i->data;
        if (p->component_id == component_id)
        {
            if (p->state == NICE_CHECK_FROZEN || p->state == NICE_CHECK_WAITING)
            {
                p->state = NICE_CHECK_CANCELLED;
                nice_debug("Agent XXX : pair %p state CANCELED", p);
				nice_print_candpair(NULL, p);
            }

            /* note: a SHOULD level req. in ICE 8.1.2. "Updating States" (ID-19) */
            if (p->state == NICE_CHECK_IN_PROGRESS)
            {
                if (highest_nominated_priority != 0 &&
                        p->priority < highest_nominated_priority)
                {
                    p->stun_message.buffer = NULL;
                    p->stun_message.buffer_len = 0;
                    p->state = NICE_CHECK_CANCELLED;
                    nice_debug("Agent XXX : pair %p state CANCELED", p);
                }
                else
                {
                    /* We must keep the higher priority pairs running because if a udp
                     * packet was lost, we might end up using a bad candidate */
                    nice_debug("Agent XXX : pair %p kept IN_PROGRESS because priority %"
                               G_GUINT64_FORMAT " is higher than currently nominated pair %"
                               G_GUINT64_FORMAT, p, p->priority, highest_nominated_priority);
                    in_progress++;
                }
            }
        }
    }

    return in_progress;
}

/*
 * Schedules a triggered check after a successfully inbound
 * connectivity check. Implements ICE sect 7.2.1.4 "Triggered Checks" (ID-19).
 *
 * @param agent self pointer
 * @param component the check is related to
 * @param local_socket socket from which the inbound check was received
 * @param remote_cand remote candidate from which the inbound check was sent
 * @param use_candidate whether the original check had USE-CANDIDATE attribute set
 */
static int priv_schedule_triggered_check(n_agent_t * agent, Stream * stream, Component * component, n_socket_t * local_socket, n_cand_t * remote_cand, int use_candidate)
{
    n_slist_t * i;
    n_cand_t * local = NULL;

    g_assert(remote_cand != NULL);

    for (i = stream->conncheck_list; i ; i = i->next)
    {
        CandidateCheckPair * p = i->data;
        if (p->component_id == component->id &&
                p->remote == remote_cand &&
                ((p->local->transport == CANDIDATE_TRANSPORT_TCP_PASSIVE &&
                  p->sockptr == local_socket) ||
                 (p->local->transport != CANDIDATE_TRANSPORT_TCP_PASSIVE &&
                  p->local->sockptr == local_socket)))
        {
            /* We don't check for p->sockptr because in the case of
             * tcp-active we don't want to retrigger a check on a pair that
             * was FAILED when a peer-reflexive pair was created */

            nice_debug("[%s agent:0x%p]: Found a matching pair %p for triggered check.", G_STRFUNC, agent, p);

            if (p->state == NICE_CHECK_WAITING || p->state == NICE_CHECK_FROZEN)
                priv_conn_check_initiate(agent, p);
            else if (p->state == NICE_CHECK_IN_PROGRESS)
            {
                /* XXX: according to ICE 7.2.1.4 "Triggered Checks" (ID-19),
                 * we should cancel the existing one, instead we reset our timer, so
                 * we'll resend the exiting transactions faster if needed...? :P
                 */
                nice_debug("[%s agent:0x%p]: check already in progress, "
                           "restarting the timer again?: %s ..", G_STRFUNC, agent,
                           p->timer_restarted ? "no" : "yes");
                if (!nice_socket_is_reliable(p->sockptr) && !p->timer_restarted)
                {
                    stun_timer_start(&p->timer, priv_compute_conncheck_timer(agent, stream), STUN_TIMER_MAX_RETRANS);
                    p->timer_restarted = TRUE;
                }
            }
            else if (p->state == NICE_CHECK_SUCCEEDED || p->state == NICE_CHECK_DISCOVERED)
            {
                nice_debug("[%s agent:0x%p]: Skipping triggered check, already completed..", G_STRFUNC, agent);
                /* note: this is a bit unsure corner-case -- let's do the
                   same state update as for processing responses to our own checks */
                priv_update_check_list_state_for_ready(agent, stream, component);

                /* note: to take care of the controlling-controlling case in
                 *       aggressive nomination mode, send a new triggered
                 *       check to nominate the pair */
                if (agent->controlling_mode)
                    priv_conn_check_initiate(agent, p);
            }
            else if (p->state == NICE_CHECK_FAILED)
            {
                /* 7.2.1.4 Triggered Checks
                 * If the state of the pair is Failed, it is changed to Waiting
                   and the agent MUST create a new connectivity check for that
                   pair (representing a new STUN Binding request transaction), by
                   enqueueing the pair in the triggered check queue. */
                priv_conn_check_initiate(agent, p);
            }

            /* note: the spec says the we SHOULD retransmit in-progress
             *       checks immediately, but we won't do that now */

            return TRUE;
        }
    }

    for (i = component->local_candidates; i ; i = i->next)
    {
        local = i->data;
        if (local->sockptr == local_socket)
            break;
    }

    if (i)
    {
        nice_debug("[%s agent:0x%p]: Adding a triggered check to conn.check list (local=%p).", G_STRFUNC, agent, local);
        priv_add_new_check_pair(agent, stream->id, component, local, remote_cand, NICE_CHECK_WAITING, use_candidate);
        return TRUE;
    }
    else
    {
        nice_debug("[%s agent:0x%p]: Didn't find a matching pair for triggered check (remote-cand=%p).", G_STRFUNC, agent, remote_cand);
        return FALSE;
    }
}


/*
 * Sends a reply to an successfully received STUN connectivity
 * check request. Implements parts of the ICE spec section 7.2 (STUN
 * Server Procedures).
 *
 * @param agent context pointer
 * @param stream which stream (of the agent)
 * @param component which component (of the stream)
 * @param rcand remote candidate from which the request came, if NULL,
 *        the response is sent immediately but no other processing is done
 * @param toaddr address to which reply is sent
 * @param socket the socket over which the request came
 * @param rbuf_len length of STUN message to send
 * @param rbuf buffer containing the STUN message to send
 * @param use_candidate whether the request had USE_CANDIDATE attribute
 *
 * @pre (rcand == NULL || nice_address_equal(rcand->addr, toaddr) == TRUE)
 */
static void priv_reply_to_conn_check(n_agent_t * agent, Stream * stream, Component * component, n_cand_t * rcand, 
													const n_addr_t * toaddr, n_socket_t * sockptr, size_t  rbuf_len, uint8_t * rbuf, int use_candidate)
{
    g_assert(rcand == NULL || nice_address_equal(&rcand->addr, toaddr) == TRUE);

    if (nice_debug_is_enabled())
    {
        char tmpbuf[INET6_ADDRSTRLEN];
        nice_address_to_string(toaddr, tmpbuf);
        nice_debug("[%s agent:0x%p]: STUN-CC RESP to '%s:%u', socket=%u, len=%u, cand=%p (c-id:%u), use-cand=%d.", G_STRFUNC, agent,
                   tmpbuf,
                   nice_address_get_port(toaddr),
                   sockptr->fileno ? g_socket_get_fd(sockptr->fileno) : -1,
                   (unsigned)rbuf_len,
                   rcand, component->id,
                   (int)use_candidate);
    }

    agent_socket_send(sockptr, toaddr, rbuf_len, (const char *)rbuf);

    if (rcand)
    {
        /* note: upon successful check, make the reserve check immediately */
        priv_schedule_triggered_check(agent, stream, component, sockptr, rcand, use_candidate);

        if (use_candidate)
            priv_mark_pair_nominated(agent, stream, component, rcand);
    }
}

/*
 * Stores information of an incoming STUN connectivity check
 * for later use. This is only needed when a check is received
 * before we get information about the remote candidates (via
 * SDP or other signaling means).
 *
 * @return non-zero on error, zero on success
 */
static int priv_store_pending_check(n_agent_t * agent, Component * component,
                                    const n_addr_t * from, n_socket_t * sockptr, uint8_t * username,
                                    uint16_t username_len, uint32_t priority, int use_candidate)
{
    IncomingCheck * icheck;
    nice_debug("[%s agent:0x%p]: Storing pending check.", agent);

    if (component->incoming_checks && n_slist_length(component->incoming_checks) >= MAX_REMOTE_CANDIDATES)
    {
        nice_debug("[%s agent:0x%p]: WARN: unable to store information for early incoming check.", agent);
        return -1;
    }

    icheck = n_slice_new0(IncomingCheck);
    component->incoming_checks = n_slist_append(component->incoming_checks, icheck);
    icheck->from = *from;
    icheck->local_socket = sockptr;
    icheck->priority = priority;
    icheck->use_candidate = use_candidate;
    icheck->username_len = username_len;
    icheck->username = NULL;
    if (username_len > 0)
        icheck->username = g_memdup(username, username_len);

    return 0;
}

/*
 * Adds a new pair, discovered from an incoming STUN response, to
 * the connectivity check list.
 *
 * @return created pair, or NULL on fatal (memory allocation) errors
 */
static CandidateCheckPair * priv_add_peer_reflexive_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t * local_cand, CandidateCheckPair * parent_pair)
{
    CandidateCheckPair * pair = n_slice_new0(CandidateCheckPair);
    Stream * stream = agent_find_stream(agent, stream_id);

    pair->agent = agent;
    pair->stream_id = stream_id;
    pair->component_id = component_id;;
    pair->local = local_cand;
    pair->remote = parent_pair->remote;
    pair->sockptr = local_cand->sockptr;
    pair->state = NICE_CHECK_DISCOVERED;
    nice_debug("[%s agent:0x%p]: pair %p state DISCOVERED", G_STRFUNC, agent, pair);
	nice_print_candpair(agent, pair);
    g_snprintf(pair->foundation, CANDIDATE_PAIR_MAX_FOUNDATION, "%s:%s",
               local_cand->foundation, parent_pair->remote->foundation);
    if (agent->controlling_mode == TRUE)
        pair->priority = nice_candidate_pair_priority(pair->local->priority, pair->remote->priority);
    else
        pair->priority = nice_candidate_pair_priority(pair->remote->priority, pair->local->priority);
    pair->nominated = FALSE;
    pair->controlling = agent->controlling_mode;
    nice_debug("[%s agent:0x%p]: added a new peer-discovered pair with foundation of '%s'.",  agent, pair->foundation);

    stream->conncheck_list = n_slist_insert_sorted(stream->conncheck_list, pair, (GCompareFunc)conn_check_compare);

    return pair;
}

/*
 * Recalculates priorities of all candidate pairs. This
 * is required after a conflict in ICE roles.
 */
static void priv_recalculate_pair_priorities(n_agent_t * agent)
{
    n_slist_t * i, *j;

    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * stream = i->data;
        for (j = stream->conncheck_list; j; j = j->next)
        {
            CandidateCheckPair * p = j->data;
            p->priority = agent_candidate_pair_priority(agent, p->local, p->remote);
        }
    }
}

/*
 * Change the agent role if different from 'control'. Can be
 * initiated both by handling of incoming connectivity checks,
 * and by processing the responses to checks sent by us.
 */
static void priv_check_for_role_conflict(n_agent_t * agent, int control)
{
    /* role conflict, change mode; wait for a new conn. check */
    if (control != agent->controlling_mode)
    {
        nice_debug("[%s agent:0x%p]: Role conflict, changing agent role to %d.", G_STRFUNC, agent, control);
        agent->controlling_mode = control;
        /* the pair priorities depend on the roles, so recalculation
         * is needed */
        priv_recalculate_pair_priorities(agent);
    }
    else
        nice_debug("[%s agent:0x%p]: Role conflict, agent role already changed to %d.", G_STRFUNC, agent, control);
}

/*
 * Checks whether the mapped address in connectivity check response
 * matches any of the known local candidates. If not, apply the
 * mechanism for "Discovering Peer Reflexive Candidates" ICE ID-19)
 *
 * @param agent context pointer
 * @param stream which stream (of the agent)
 * @param component which component (of the stream)
 * @param p the connectivity check pair for which we got a response
 * @param socketptr socket used to send the reply
 * @param mapped_sockaddr mapped address in the response
 *
 * @return pointer to a new pair if one was created, otherwise NULL
 */
static CandidateCheckPair * priv_process_response_check_for_peer_reflexive(n_agent_t * agent, Stream * stream, Component * component, CandidateCheckPair * p,
        n_socket_t * sockptr, struct sockaddr * mapped_sockaddr, n_cand_t * local_candidate, n_cand_t * remote_candidate)
{
    CandidateCheckPair * new_pair = NULL;
    n_addr_t mapped;
    n_slist_t * i, *j;
    int local_cand_matches = FALSE;

    n_addr_set_from_sock(&mapped, mapped_sockaddr);

    for (j = component->local_candidates; j; j = j->next)
    {
        n_cand_t * cand = j->data;
        if (nice_address_equal(&mapped, &cand->addr))
        {
            local_cand_matches = TRUE;

            /* We always need to select the peer-reflexive Candidate Pair in the case
             * of a TCP-ACTIVE local candidate, so we find it even if an incoming
             * check matched an existing pair because it could be the original
             * ACTIVE-PASSIVE candidate pair which was retriggered */
            for (i = stream->conncheck_list; i; i = i->next)
            {
                CandidateCheckPair * pair = i->data;
                if (pair->local == cand && remote_candidate == pair->remote)
                {
                    new_pair = pair;
                    break;
                }
            }
            break;
        }
    }

    if (local_cand_matches == TRUE)
    {
        /* note: this is same as "adding to VALID LIST" in the spec
           text */
        p->state = NICE_CHECK_SUCCEEDED;
        nice_debug("[%s agent:0x%p]: conncheck %p SUCCEEDED.", G_STRFUNC, agent, p);
        priv_conn_check_unfreeze_related(agent, stream, p);
    }
    else
    {
        n_cand_t * cand =
            disc_add_peer_cand(agent,
                    stream->id,
                    component->id,
                    &mapped,
                    sockptr,
                    local_candidate,
                    remote_candidate);
        p->state = NICE_CHECK_FAILED;
        nice_debug("[%s agent:0x%p]: pair %p state FAILED", G_STRFUNC, agent, p);

        /* step: add a new discovered pair (see RFC 5245 7.1.3.2.2
               "Constructing a Valid Pair") */
        new_pair = priv_add_peer_reflexive_pair(agent, stream->id, component->id, cand, p);
        nice_debug("[%s agent:0x%p]: conncheck %p FAILED, %p DISCOVERED.", G_STRFUNC, agent, p, new_pair);
    }

    return new_pair;
}

/*
 * Tries to match STUN reply in 'buf' to an existing STUN connectivity
 * check transaction. If found, the reply is processed. Implements
 * section 7.1.2 "Processing the Response" of ICE spec (ID-19).
 *
 * @return TRUE if a matching transaction is found
 */
static int priv_map_reply_to_conn_check_request(n_agent_t * agent, Stream * stream, Component * component, n_socket_t * sockptr,
        const n_addr_t * from, n_cand_t * local_candidate, n_cand_t * remote_candidate, StunMessage * resp)
{
    union
    {
        struct sockaddr_storage storage;
        struct sockaddr addr;
    } sockaddr;
    socklen_t socklen = sizeof(sockaddr);
    n_slist_t * i;
    StunUsageIceReturn res;
    int trans_found = FALSE;
    StunTransactionId discovery_id;
    StunTransactionId response_id;
    stun_message_id(resp, response_id);

    for (i = stream->conncheck_list; i && trans_found != TRUE; i = i->next)
    {
        CandidateCheckPair * p = i->data;

        if (p->stun_message.buffer)
        {
            stun_message_id(&p->stun_message, discovery_id);

            if (memcmp(discovery_id, response_id, sizeof(StunTransactionId)) == 0)
            {
                res = stun_usage_ice_conncheck_process(resp, &sockaddr.storage, &socklen);
                nice_debug("[%s agent:0x%p]: stun_bind_process/conncheck for %p res %d "
                           "(controlling=%d).", G_STRFUNC, agent, p, (int)res, agent->controlling_mode);

                if (res == STUN_USAGE_ICE_RETURN_SUCCESS ||
                        res == STUN_USAGE_ICE_RETURN_NO_MAPPED_ADDRESS)
                {
                    /* case: found a matching connectivity check request */

                    CandidateCheckPair * ok_pair = NULL;

                    nice_debug("[%s agent:0x%p]: conncheck %p MATCHED.", G_STRFUNC, agent, p);
					nice_print_candpair(agent, p);
                    p->stun_message.buffer = NULL;
                    p->stun_message.buffer_len = 0;

                    /* step: verify that response came from the same IP address we
                     *       sent the original request to (see 7.1.2.1. "Failure
                     *       Cases") */
                    if (nice_address_equal(from, &p->remote->addr) != TRUE)
                    {
                        p->state = NICE_CHECK_FAILED;
                        if (nice_debug_is_enabled())
                        {
                            char tmpbuf[INET6_ADDRSTRLEN];
                            char tmpbuf2[INET6_ADDRSTRLEN];
                            nice_debug("[%s agent:0x%p]: conncheck %p FAILED"
                                       " (mismatch of source address).", G_STRFUNC, agent, p);
                            nice_address_to_string(&p->remote->addr, tmpbuf);
                            nice_address_to_string(from, tmpbuf2);
                            nice_debug("[%s agent:0x%p]: '%s:%u' != '%s:%u'", G_STRFUNC, agent,
                                       tmpbuf, nice_address_get_port(&p->remote->addr),
                                       tmpbuf2, nice_address_get_port(from));
                        }
                        trans_found = TRUE;
                        break;
                    }

                    /* note: CONNECTED but not yet READY, see docs */

                    /* step: handle the possible case of a peer-reflexive
                     *       candidate where the mapped-address in response does
                     *       not match any local candidate, see 7.1.2.2.1
                     *       "Discovering Peer Reflexive Candidates" ICE ID-19) */

                    if (res == STUN_USAGE_ICE_RETURN_NO_MAPPED_ADDRESS)
                    {
                        /* note: this is same as "adding to VALID LIST" in the spec
                           text */
                        p->state = NICE_CHECK_SUCCEEDED;
                        nice_debug("[%s agent:0x%p]: Mapped address not found." " conncheck %p SUCCEEDED.", G_STRFUNC, agent, p);
                        priv_conn_check_unfreeze_related(agent, stream, p);
                    }
                    else
                    {
                        ok_pair = priv_process_response_check_for_peer_reflexive(agent,
                                  stream, component, p, sockptr, &sockaddr.addr,
                                  local_candidate, remote_candidate);
                    }


                    if (!ok_pair)
                        ok_pair = p;

                    /* step: updating nominated flag (ICE 7.1.2.2.4 "Updating the
                       Nominated Flag" (ID-19) */
                    if (ok_pair->nominated == TRUE)
                    {
                        priv_update_selected_pair(agent, component, ok_pair);

                        /* Do not step down to CONNECTED if we're already at state READY*/
                        if (component->state != COMP_STATE_READY)
                        {
                            /* step: notify the client of a new component state (must be done
                             *       before the possible check list state update step */
                            n_sig_comp_state_change(agent, stream->id, component->id, COMP_STATE_CONNECTED);
                        }
                    }

                    /* step: update pair states (ICE 7.1.2.2.3 "Updating pair
                       states" and 8.1.2 "Updating States", ID-19) */
                    priv_update_check_list_state_for_ready(agent, stream, component);

                    trans_found = TRUE;
                }
                else if (res == STUN_USAGE_ICE_RETURN_ROLE_CONFLICT)
                {
                    /* case: role conflict error, need to restart with new role */
                    nice_debug("[%s agent:0x%p]: conncheck %p ROLE CONFLICT, restarting", G_STRFUNC, agent, p);
                    /* note: our role might already have changed due to an
                     * incoming request, but if not, change role now;
                     * follows ICE 7.1.2.1 "Failure Cases" (ID-19) */
                    priv_check_for_role_conflict(agent, !p->controlling);

                    p->stun_message.buffer = NULL;
                    p->stun_message.buffer_len = 0;
                    p->state = NICE_CHECK_WAITING;
                    nice_debug("[%s agent:0x%p]: pair %p state WAITING", G_STRFUNC, agent, p);
                    trans_found = TRUE;
                }
                else
                {
                    /* case: STUN error, the check STUN context was freed */
                    nice_debug("[%s agent:0x%p]: conncheck %p FAILED.", G_STRFUNC, agent, p);
                    p->stun_message.buffer = NULL;
                    p->stun_message.buffer_len = 0;
                    trans_found = TRUE;
                }
            }
        }
    }


    stream->conncheck_list =
        prune_cancelled_conn_check(stream->conncheck_list);

    return trans_found;
}

/*
 * Tries to match STUN reply in 'buf' to an existing STUN discovery
 * transaction. If found, a reply is sent.
 *
 * @return TRUE if a matching transaction is found
 */
static int priv_map_reply_disc_req(n_agent_t * agent, StunMessage * resp)
{
    union
    {
        struct sockaddr_storage storage;
        struct sockaddr addr;
    } sockaddr;
    socklen_t socklen = sizeof(sockaddr);

    union
    {
        struct sockaddr_storage storage;
        struct sockaddr addr;
    } alternate;
    socklen_t alternatelen = sizeof(sockaddr);

    n_slist_t * i;
    StunBind res;
    int trans_found = FALSE;
    StunTransactionId discovery_id;
    StunTransactionId response_id;
    stun_message_id(resp, response_id);

    for (i = agent->discovery_list; i && trans_found != TRUE; i = i->next)
    {
        n_cand_disc_t * d = i->data;

        if (d->type == CAND_TYPE_SERVER && d->stun_message.buffer)
        {
            stun_message_id(&d->stun_message, discovery_id);

            if (memcmp(discovery_id, response_id, sizeof(StunTransactionId)) == 0)
            {
                res = stun_bind_process(resp, &sockaddr.addr, &socklen, &alternate.addr, &alternatelen);
                nice_debug("[%s agent:0x%p]: stun_bind_process/disc for %p res %d.", G_STRFUNC, agent, d, (int)res);

                if (res == STUN_BIND_ALTERNATE_SERVER)
                {
                    /* handle alternate server */
                    n_addr_t niceaddr;
                    n_addr_set_from_sock(&niceaddr, &alternate.addr);
                    d->server = niceaddr;

                    d->pending = FALSE;
                }
                else if (res == STUN_BIND_SUCCESS)
                {
                    /* case: successful binding discovery, create a new local candidate */
                    n_addr_t niceaddr;
                    n_addr_set_from_sock(&niceaddr, &sockaddr.addr);
                    disc_add_server_cand(d->agent, d->stream->id, d->component->id, &niceaddr, d->nicesock);

                    d->stun_message.buffer = NULL;
                    d->stun_message.buffer_len = 0;
                    d->done = TRUE;
                    trans_found = TRUE;
                }
                else if (res == STUN_BIND_ERROR)
                {
                    /* case: STUN error, the check STUN context was freed */
                    d->stun_message.buffer = NULL;
                    d->stun_message.buffer_len = 0;
                    d->done = TRUE;
                    trans_found = TRUE;
                }
            }
        }
    }

    return trans_found;
}


static CandidateRefresh * priv_add_new_turn_refresh(n_cand_disc_t * cdisco, n_cand_t * relay_cand, uint32_t lifetime)
{
    CandidateRefresh * cand;
    n_agent_t * agent = cdisco->agent;

    cand = n_slice_new0(CandidateRefresh);
    agent->refresh_list = n_slist_append(agent->refresh_list, cand);

    cand->candidate = relay_cand;
    cand->nicesock = cdisco->nicesock;
    cand->server = cdisco->server;
    cand->stream = cdisco->stream;
    cand->component = cdisco->component;
    cand->agent = cdisco->agent;
    memcpy(&cand->stun_agent, &cdisco->stun_agent, sizeof(StunAgent));

    /* Use previous stun response for authentication credentials */
    if (cdisco->stun_resp_msg.buffer != NULL)
    {
        memcpy(cand->stun_resp_buffer, cdisco->stun_resp_buffer, sizeof(cand->stun_resp_buffer));
        memcpy(&cand->stun_resp_msg, &cdisco->stun_resp_msg, sizeof(StunMessage));
        cand->stun_resp_msg.buffer = cand->stun_resp_buffer;
        cand->stun_resp_msg.agent = NULL;
        cand->stun_resp_msg.key = NULL;
    }

    nice_debug("[%s agent:0x%p]: Adding new refresh candidate %p with timeout %d", G_STRFUNC, agent, cand, (lifetime - 60) * 1000);

    /* step: also start the refresh timer */
    /* refresh should be sent 1 minute before it expires */
    agent_timeout_add(agent, &cand->timer_source, "Candidate TURN refresh",
                                   (lifetime - 60) * 1000, priv_turn_allocate_refresh_tick, cand);

    nice_debug("timer source is : %p", cand->timer_source);

    return cand;
}

/*
 * Tries to match STUN reply in 'buf' to an existing STUN discovery
 * transaction. If found, a reply is sent.
 *
 * @return TRUE if a matching transaction is found
 */
static int priv_map_reply_to_relay_request(n_agent_t * agent, StunMessage * resp)
{
    union
    {
        struct sockaddr_storage storage;
        struct sockaddr addr;
    } sockaddr;
    socklen_t socklen = sizeof(sockaddr);

    union
    {
        struct sockaddr_storage storage;
        struct sockaddr addr;
    } alternate;
    socklen_t alternatelen = sizeof(alternate);

    union
    {
        struct sockaddr_storage storage;
        struct sockaddr addr;
    } relayaddr;
    socklen_t relayaddrlen = sizeof(relayaddr);

    uint32_t lifetime;
    uint32_t bandwidth;
    n_slist_t * i;
    StunUsageTurnReturn res;
    int trans_found = FALSE;
    StunTransactionId discovery_id;
    StunTransactionId response_id;
    stun_message_id(resp, response_id);

    for (i = agent->discovery_list; i && trans_found != TRUE; i = i->next)
    {
        n_cand_disc_t * d = i->data;

        if (d->type == CAND_TYPE_RELAYED && d->stun_message.buffer)
        {
            stun_message_id(&d->stun_message, discovery_id);

            if (memcmp(discovery_id, response_id, sizeof(StunTransactionId)) == 0)
            {
                res = stun_usage_turn_process(resp,
                                              &relayaddr.storage, &relayaddrlen,
                                              &sockaddr.storage, &socklen,
                                              &alternate.storage, &alternatelen,
                                              &bandwidth, &lifetime, agent_to_turn_compatibility(agent));
                nice_debug("[%s agent:0x%p]: stun_turn_process/disc for %p res %d.",
                           agent, d, (int)res);

                if (res == STUN_USAGE_TURN_RETURN_ALTERNATE_SERVER)
                {
                    /* handle alternate server */
                    n_addr_set_from_sock(&d->server, &alternate.addr);
                    n_addr_set_from_sock(&d->turn->server, &alternate.addr);

                    d->pending = FALSE;
                }
                else if (res == STUN_USAGE_TURN_RETURN_RELAY_SUCCESS ||
                         res == STUN_USAGE_TURN_RETURN_MAPPED_SUCCESS)
                {
                    /* case: successful allocate, create a new local candidate */
                    n_addr_t niceaddr;
                    n_cand_t * relay_cand;

                    if (res == STUN_USAGE_TURN_RETURN_MAPPED_SUCCESS)
                    {
                        /* We also received our mapped address */
                        n_addr_set_from_sock(&niceaddr, &sockaddr.addr);                        
                        disc_add_server_cand(d->agent, d->stream->id, d->component->id, &niceaddr, d->nicesock);
                    }

                    n_addr_set_from_sock(&niceaddr, &relayaddr.addr);
                    relay_cand = disc_add_relay_cand(d->agent, d->stream->id, d->component->id, &niceaddr, d->nicesock, d->turn);
                    if (relay_cand)
                    {
                           priv_add_new_turn_refresh(d, relay_cand, lifetime);
                    }

                    d->stun_message.buffer = NULL;
                    d->stun_message.buffer_len = 0;
                    d->done = TRUE;
                    trans_found = TRUE;
                }
                else if (res == STUN_USAGE_TURN_RETURN_ERROR)
                {
                    int code = -1;
                    uint8_t * sent_realm = NULL;
                    uint8_t * recv_realm = NULL;
                    uint16_t sent_realm_len = 0;
                    uint16_t recv_realm_len = 0;

                    sent_realm = (uint8_t *) stun_message_find(&d->stun_message, STUN_ATT_REALM, &sent_realm_len);
                    recv_realm = (uint8_t *) stun_message_find(resp, STUN_ATT_REALM, &recv_realm_len);

                    /* check for unauthorized error response */
                    if (stun_message_get_class(resp) == STUN_ERROR &&
                            stun_message_find_error(resp, &code) == STUN_MESSAGE_RETURN_SUCCESS &&
                            recv_realm != NULL && recv_realm_len > 0)
                    {
                        if (code == 438 || (code == 401 && !(recv_realm_len == sent_realm_len && sent_realm != NULL &&
                                                             memcmp(sent_realm, recv_realm, sent_realm_len) == 0)))
                        {
                            d->stun_resp_msg = *resp;
                            memcpy(d->stun_resp_buffer, resp->buffer, stun_message_length(resp));
                            d->stun_resp_msg.buffer = d->stun_resp_buffer;
                            d->stun_resp_msg.buffer_len = sizeof(d->stun_resp_buffer);
                            d->pending = FALSE;
                        }
                        else
                        {
                            /* case: a real unauthorized error */
                            d->stun_message.buffer = NULL;
                            d->stun_message.buffer_len = 0;
                            d->done = TRUE;
                        }
                    }
                    else
                    {
                        /* case: STUN error, the check STUN context was freed */
                        d->stun_message.buffer = NULL;
                        d->stun_message.buffer_len = 0;
                        d->done = TRUE;
                    }
                    trans_found = TRUE;
                }
            }
        }
    }
    return trans_found;
}


/*
 * Tries to match STUN reply in 'buf' to an existing STUN discovery
 * transaction. If found, a reply is sent.
 *
 * @return TRUE if a matching transaction is found
 */
static int priv_map_reply_to_relay_refresh(n_agent_t * agent, StunMessage * resp)
{
    uint32_t lifetime;
    n_slist_t * i;
    StunUsageTurnReturn res;
    int trans_found = FALSE;
    StunTransactionId refresh_id;
    StunTransactionId response_id;
    stun_message_id(resp, response_id);

    for (i = agent->refresh_list; i && trans_found != TRUE; i = i->next)
    {
        CandidateRefresh * cand = i->data;

        if (cand->stun_message.buffer)
        {
            stun_message_id(&cand->stun_message, refresh_id);

            if (memcmp(refresh_id, response_id, sizeof(StunTransactionId)) == 0)
            {
                res = stun_usage_turn_refresh_process(resp,
                                                      &lifetime, agent_to_turn_compatibility(cand->agent));
                nice_debug("[%s agent:0x%p]: stun_turn_refresh_process for %p res %d.",
                           agent, cand, (int)res);
                if (res == STUN_USAGE_TURN_RETURN_RELAY_SUCCESS)
                {
                    /* refresh should be sent 1 minute before it expires */
                    agent_timeout_add(cand->agent, &cand->timer_source,
                                                   "Candidate TURN refresh", (lifetime - 60) * 1000,
                                                   priv_turn_allocate_refresh_tick, cand);

                    g_source_destroy(cand->tick_source);
                    g_source_unref(cand->tick_source);
                    cand->tick_source = NULL;
                }
                else if (res == STUN_USAGE_TURN_RETURN_ERROR)
                {
                    int code = -1;
                    uint8_t * sent_realm = NULL;
                    uint8_t * recv_realm = NULL;
                    uint16_t sent_realm_len = 0;
                    uint16_t recv_realm_len = 0;

                    sent_realm = (uint8_t *) stun_message_find(&cand->stun_message,
                                 STUN_ATT_REALM, &sent_realm_len);
                    recv_realm = (uint8_t *) stun_message_find(resp,
                                 STUN_ATT_REALM, &recv_realm_len);

                    /* check for unauthorized error response */
                    if (cand->agent->compatibility == NICE_COMPATIBILITY_RFC5245 &&
                            stun_message_get_class(resp) == STUN_ERROR &&
                            stun_message_find_error(resp, &code) ==
                            STUN_MESSAGE_RETURN_SUCCESS &&
                            recv_realm != NULL && recv_realm_len > 0)
                    {

                        if (code == 438 ||
                                (code == 401 &&
                                 !(recv_realm_len == sent_realm_len &&
                                   sent_realm != NULL &&
                                   memcmp(sent_realm, recv_realm, sent_realm_len) == 0)))
                        {
                            cand->stun_resp_msg = *resp;
                            memcpy(cand->stun_resp_buffer, resp->buffer,
                                   stun_message_length(resp));
                            cand->stun_resp_msg.buffer = cand->stun_resp_buffer;
                            cand->stun_resp_msg.buffer_len = sizeof(cand->stun_resp_buffer);
                            priv_turn_alloc_refresh_tick_unlocked(cand);
                        }
                        else
                        {
                            /* case: a real unauthorized error */
                            refresh_cancel(cand);
                        }
                    }
                    else
                    {
                        /* case: STUN error, the check STUN context was freed */
                        refresh_cancel(cand);
                    }
                    trans_found = TRUE;
                }
            }
        }
    }

    return trans_found;
}


static int priv_map_reply_to_keepalive_conncheck(n_agent_t * agent, Component * component, StunMessage * resp)
{
    StunTransactionId conncheck_id;
    StunTransactionId response_id;
    stun_message_id(resp, response_id);

    if (component->selected_pair.keepalive.stun_message.buffer)
    {
        stun_message_id(&component->selected_pair.keepalive.stun_message,  conncheck_id);
        if (memcmp(conncheck_id, response_id, sizeof(StunTransactionId)) == 0)
        {
            nice_debug("[%s agent:0x%p]: Keepalive for selected pair received.", agent);
            if (component->selected_pair.keepalive.tick_source)
            {
                g_source_destroy(component->selected_pair.keepalive.tick_source);
                g_source_unref(component->selected_pair.keepalive.tick_source);
                component->selected_pair.keepalive.tick_source = NULL;
            }
            component->selected_pair.keepalive.stun_message.buffer = NULL;
            return TRUE;
        }
    }

    return FALSE;
}


typedef struct
{
    n_agent_t * agent;
    Stream * stream;
    Component * component;
    uint8_t * password;
} conncheck_validater_data;

static bool conncheck_stun_validater(StunAgent * agent,
                                     StunMessage * message, uint8_t * username, uint16_t username_len,
                                     uint8_t ** password, size_t * password_len, void * user_data)
{
    conncheck_validater_data * data = (conncheck_validater_data *) user_data;
    n_slist_t * i;
    char * ufrag = NULL;
    uint32_t ufrag_len;

    int msn_msoc_nice_compatibility =
        data->agent->compatibility == NICE_COMPATIBILITY_MSN ||
        data->agent->compatibility == NICE_COMPATIBILITY_OC2007;

    if (data->agent->compatibility == NICE_COMPATIBILITY_OC2007 &&
            stun_message_get_class(message) == STUN_RESPONSE)
        i = data->component->remote_candidates;
    else
        i = data->component->local_candidates;

    for (; i; i = i->next)
    {
        n_cand_t * cand = i->data;

        ufrag = NULL;
        if (cand->username)
            ufrag = cand->username;
        else if (data->stream)
            ufrag = data->stream->local_ufrag;
        ufrag_len = ufrag ? strlen(ufrag) : 0;

        if (ufrag && msn_msoc_nice_compatibility)
            ufrag = (char *)g_base64_decode(ufrag, &ufrag_len);

        if (ufrag == NULL)
            continue;

        stun_debug("Comparing username/ufrag of len %d and %zu, equal=%d",
                   username_len, ufrag_len, username_len >= ufrag_len ?
                   memcmp(username, ufrag, ufrag_len) : 0);
        stun_debug_bytes("  username: ", username, username_len);
        stun_debug_bytes("  ufrag:    ", ufrag, ufrag_len);
        if (ufrag_len > 0 && username_len >= ufrag_len &&
                memcmp(username, ufrag, ufrag_len) == 0)
        {
            char * pass = NULL;

            if (cand->password)
                pass = cand->password;
            else if (data->stream->local_password[0])
                pass = data->stream->local_password;

            if (pass)
            {
                *password = (uint8_t *) pass;
                *password_len = strlen(pass);

                if (msn_msoc_nice_compatibility)
                {
                    uint32_t pass_len;

                    data->password = g_base64_decode(pass, &pass_len);
                    *password = data->password;
                    *password_len = pass_len;
                }
            }

            if (msn_msoc_nice_compatibility)
                n_free(ufrag);

            stun_debug("Found valid username, returning password: '%s'", *password);
            return TRUE;
        }

        if (msn_msoc_nice_compatibility)
            n_free(ufrag);
    }

    return FALSE;
}


/*
 * Processing an incoming STUN message.
 *
 * @param agent self pointer
 * @param stream stream the packet is related to
 * @param component component the packet is related to
 * @param nicesock socket from which the packet was received
 * @param from address of the sender
 * @param buf message contents
 * @param buf message length
 *
 * @pre contents of 'buf' is a STUN message
 *
 * @return XXX (what FALSE means exactly?)
 */
int co_chk_handle_in_stun(n_agent_t * agent, Stream * stream,
                                        Component * component, n_socket_t * nicesock, const n_addr_t * from,
                                        char * buf, uint32_t len)
{
    union
    {
        struct sockaddr_storage storage;
        struct sockaddr addr;
    } sockaddr;
    uint8_t rbuf[MAX_STUN_DATAGRAM_PAYLOAD];
    ssize_t res;
    size_t rbuf_len = sizeof(rbuf);
    int control = agent->controlling_mode;
    //uint8_t uname[NICE_STREAM_MAX_UNAME];
    //uint32_t uname_len;
    uint8_t * username;
    uint16_t username_len;
    StunMessage req;
    StunMessage msg;
    StunValidationStatus valid;
    conncheck_validater_data validater_data = {agent, stream, component, NULL};
    n_slist_t * i;
    n_cand_t * remote_candidate = NULL;
    n_cand_t * remote_candidate2 = NULL;
    n_cand_t * local_candidate = NULL;
    int discovery_msg = FALSE;

    nice_address_copy_to_sockaddr(from, &sockaddr.addr);

    /* note: contents of 'buf' already validated, so it is
     *       a valid and fully received STUN message */

    if (nice_debug_is_enabled())
    {
        char tmpbuf[INET6_ADDRSTRLEN];
        nice_address_to_string(from, tmpbuf);
        nice_debug("[%s agent:0x%p]: inbound STUN packet for %u/%u (stream/component) from [%s]:%u (%u octets) :",
					G_STRFUNC, agent, stream->id, component->id, tmpbuf, nice_address_get_port(from), len);
    }

    /* note: ICE  7.2. "STUN Server Procedures" (ID-19) */

    valid = stun_agent_validate(&component->stun_agent, &req, (uint8_t *) buf, len);

    /* Check for discovery candidates stun agents */
    if (valid == STUN_VALIDATION_BAD_REQUEST || valid == STUN_VALIDATION_UNMATCHED_RESPONSE)
    {
        for (i = agent->discovery_list; i; i = i->next)
        {
            n_cand_disc_t * d = i->data;
            if (d->stream == stream && d->component == component && d->nicesock == nicesock)
            {
                valid = stun_agent_validate(&d->stun_agent, &req, (uint8_t *) buf, len);

                if (valid == STUN_VALIDATION_UNMATCHED_RESPONSE)
                    continue;

                discovery_msg = TRUE;
                break;
            }
        }
    }
    /* Check for relay refresh stun agents */
    if (valid == STUN_VALIDATION_BAD_REQUEST || valid == STUN_VALIDATION_UNMATCHED_RESPONSE)
    {
        for (i = agent->refresh_list; i; i = i->next)
        {
            CandidateRefresh * r = i->data;
            nice_debug("Comparing %p to %p, %p to %p and %p and %p to %p", r->stream,
                       stream, r->component, component, r->nicesock, r->candidate->sockptr,  nicesock);
            if (r->stream == stream && r->component == component && (r->nicesock == nicesock || r->candidate->sockptr == nicesock))
            {
                valid = stun_agent_validate(&r->stun_agent, &req, (uint8_t *) buf, len);
                nice_debug("Validating gave %d", valid);
                if (valid == STUN_VALIDATION_UNMATCHED_RESPONSE)
                    continue;
                discovery_msg = TRUE;
                break;
            }
        }
    }

    n_free(validater_data.password);

    if (valid == STUN_VALIDATION_NOT_STUN || valid == STUN_VALIDATION_INCOMPLETE_STUN || valid == STUN_VALIDATION_BAD_REQUEST)
    {
        nice_debug("[%s agent:0x%p]: Incorrectly multiplexed STUN message ignored.", agent);
        return FALSE;
    }

    if (valid == STUN_VALIDATION_UNKNOWN_REQUEST_ATTRIBUTE)
    {
        nice_debug("[%s agent:0x%p]: Unknown mandatory attributes in message.", agent);

        rbuf_len = stun_agent_build_unknown_attributes_error(&component->stun_agent, &msg, rbuf, rbuf_len, &req);
        if (rbuf_len != 0)
            agent_socket_send(nicesock, from, rbuf_len, (const char *)rbuf);

        return TRUE;
    }

    if (valid == STUN_VALIDATION_UNAUTHORIZED)
    {
        nice_debug("[%s agent:0x%p]: Integrity check failed.", agent);

        if (stun_agent_init_error(&component->stun_agent, &msg, rbuf, rbuf_len, &req, STUN_ERROR_UNAUTHORIZED))
        {
            rbuf_len = stun_agent_finish_message(&component->stun_agent, &msg, NULL, 0);
            if (rbuf_len > 0)
                agent_socket_send(nicesock, from, rbuf_len, (const char *)rbuf);
        }
        return TRUE;
    }
    if (valid == STUN_VALIDATION_UNAUTHORIZED_BAD_REQUEST)
    {
        nice_debug("[%s agent:0x%p]: Integrity check failed - bad request.", agent);
        if (stun_agent_init_error(&component->stun_agent, &msg, rbuf, rbuf_len, &req, STUN_ERROR_BAD_REQUEST))
        {
            rbuf_len = stun_agent_finish_message(&component->stun_agent, &msg, NULL, 0);
            if (rbuf_len > 0)
                agent_socket_send(nicesock, from, rbuf_len, (const char *)rbuf);
        }
        return TRUE;
    }

    username = (uint8_t *) stun_message_find(&req, STUN_ATT_USERNAME, &username_len);

    for (i = component->remote_candidates; i; i = i->next)
    {
        n_cand_t * cand = i->data;
        if (nice_address_equal(from, &cand->addr))
        {
            remote_candidate = cand;
            break;
        }
    }
    for (i = component->local_candidates; i; i = i->next)
    {
        n_cand_t * cand = i->data;
        if (nice_address_equal(&nicesock->addr, &cand->addr))
        {
            local_candidate = cand;
            break;
        }
    }

    if (valid != STUN_VALIDATION_SUCCESS)
    {
        nice_debug("[%s agent:0x%p]: STUN message is unsuccessfull %d, ignoring", G_STRFUNC, agent, valid);
        return FALSE;
    }

    if (stun_message_get_class(&req) == STUN_REQUEST)
    {
        rbuf_len = sizeof(rbuf);
        res = stun_usage_ice_conncheck_create_reply(&component->stun_agent, &req,
                &msg, rbuf, &rbuf_len, &sockaddr.storage, sizeof(sockaddr),  &control, agent->tie_breaker);

        if (res == STUN_USAGE_ICE_RETURN_ROLE_CONFLICT)
            priv_check_for_role_conflict(agent, control);

        if (res == STUN_USAGE_ICE_RETURN_SUCCESS ||
                res == STUN_USAGE_ICE_RETURN_ROLE_CONFLICT)
        {
            /* case 1: valid incoming request, send a reply/error */
            int use_candidate = stun_usage_ice_conncheck_use_candidate(&req);
            uint32_t priority = stun_usage_ice_conncheck_priority(&req);

            if (agent->controlling_mode)
                use_candidate = TRUE;

            if (stream->initial_binding_request_received != TRUE)
                agent_signal_initial_binding_request_received(agent, stream);

            if (component->remote_candidates && remote_candidate == NULL)
            {
                nice_debug("[%s agent:0x%p]: No matching remote candidate for incoming check ->" "peer-reflexive candidate.", agent);
                remote_candidate = disc_learn_remote_peer_cand(
                                       agent, stream, component, priority, from, nicesock,
                                       local_candidate,
                                       remote_candidate2 ? remote_candidate2 : remote_candidate);
                if (remote_candidate)
                {                    
					conn_check_add_for_candidate(agent, stream->id, component, remote_candidate);
                }
            }

            priv_reply_to_conn_check(agent, stream, component, remote_candidate,  from, nicesock, rbuf_len, rbuf, use_candidate);

            if (component->remote_candidates == NULL)
            {
                /* case: We've got a valid binding request to a local candidate
                 *       but we do not yet know remote credentials nor
                 *       candidates. As per sect 7.2 of ICE (ID-19), we send a reply
                 *       immediately but postpone all other processing until
                 *       we get information about the remote candidates */

                /* step: send a reply immediately but postpone other processing */
                priv_store_pending_check(agent, component, from, nicesock, username, username_len, priority, use_candidate);
            }
        }
        else
        {
            nice_debug("[%s agent:0x%p]: Invalid STUN packet, ignoring... %s",  agent, strerror(errno));
            return FALSE;
        }
    }
    else
    {
        /* case 2: not a new request, might be a reply...  */
        int trans_found = FALSE;

        /* note: ICE sect 7.1.2. "Processing the Response" (ID-19) */

        /* step: let's try to match the response to an existing check context */
        if (trans_found != TRUE)
            trans_found = priv_map_reply_to_conn_check_request(agent, stream,
                          component, nicesock, from, local_candidate, remote_candidate, &req);

        /* step: let's try to match the response to an existing discovery */
        if (trans_found != TRUE)
            trans_found = priv_map_reply_disc_req(agent, &req);

        /* step: let's try to match the response to an existing turn allocate */
        if (trans_found != TRUE)
            trans_found = priv_map_reply_to_relay_request(agent, &req);

        /* step: let's try to match the response to an existing turn refresh */
        if (trans_found != TRUE)
            trans_found = priv_map_reply_to_relay_refresh(agent, &req);

        /* step: let's try to match the response to an existing keepalive conncheck */
        if (trans_found != TRUE)
            trans_found = priv_map_reply_to_keepalive_conncheck(agent, component, &req);

        if (trans_found != TRUE)
            nice_debug("[%s agent:0x%p]: Unable to match to an existing transaction, " "probably a keepalive.", G_STRFUNC, agent);
    }

    return TRUE;
}

/* Remove all pointers to the given @sock from the connection checking process.
 * These are entirely NiceCandidates pointed to from various places. */
void conn_check_prune_socket(n_agent_t * agent, Stream * stream, Component * component, n_socket_t * sock)
{
    n_slist_t * l;

    if (component->selected_pair.local &&
            component->selected_pair.local->sockptr == sock &&
            component->state == COMP_STATE_READY)
    {
        nice_debug("[%s agent:0x%p]: Selected pair socket %p has been destroyed, " "declaring failed", G_STRFUNC, agent, sock);
        n_sig_comp_state_change(agent, stream->id, component->id, COMP_STATE_FAILED);
    }

    /* Prune from the candidate check pairs. */
    for (l = stream->conncheck_list; l != NULL; l = l->next)
    {
        CandidateCheckPair * p = l->data;

        if ((p->local != NULL && p->local->sockptr == sock) ||
                (p->remote != NULL && p->remote->sockptr == sock))
        {
            nice_debug("[%s agent:0x%p]: Retransmissions failed, giving up on " "connectivity check %p", G_STRFUNC, agent, p);
            candidate_check_pair_fail(stream, agent, p);
        }
    }
}
