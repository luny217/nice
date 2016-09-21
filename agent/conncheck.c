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
#include "timer.h"

static void _update_chk_list_failed_comps(n_agent_t * agent, n_stream_t * stream);
static void _update_chk_list_state_for_ready(n_agent_t * agent, n_stream_t * stream, n_comp_t * component);
static uint32_t _prune_pending_checks(n_stream_t * stream, uint32_t component_id);
static int _schedule_triggered_check(n_agent_t * agent, n_stream_t * stream, n_comp_t * component, n_socket_t * local_socket, n_cand_t * remote_cand, int use_candidate);
static void _mark_pair_nominated(n_agent_t * agent, n_stream_t * stream, n_comp_t * component, n_cand_t * remotecand);
static size_t _create_username(n_agent_t * agent, n_stream_t * stream, uint32_t component_id, 
											n_cand_t * remote, n_cand_t * local, uint8_t * dest, uint32_t dest_len, int inbound);
static size_t _get_password(n_agent_t * agent, n_stream_t * stream, n_cand_t * remote, uint8_t ** password);
static void cocheck_free_item(void * data);
static void _cocheck_add_cand_pair_matched(n_agent_t * agent, uint32_t stream_id, n_comp_t * component, n_cand_t * local, n_cand_t * remote, n_chk_state_e initial_state);

static void nice_print_candpair(n_agent_t * agent, n_cand_chk_pair_t * pair);

static void nice_print_candpair(n_agent_t * agent, n_cand_chk_pair_t * pair)
{
	if (nice_debug_is_enabled())
	{
		char tmpbuf1[INET6_ADDRSTRLEN];
		char tmpbuf2[INET6_ADDRSTRLEN];

		nice_address_to_string(&pair->local->addr, tmpbuf1);
		nice_address_to_string(&pair->remote->addr, tmpbuf2);
		nice_debug("[%s]: local '%s:%u' -> remote '%s:%u'", G_STRFUNC,
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
static n_cand_chk_pair_t * _cochk_find_next_waiting(n_slist_t * conn_check_list)
{
    n_slist_t * i;

    /* note: list is sorted in priority order to first waiting check has
     *       the highest priority */

    for (i = conn_check_list; i ; i = i->next)
    {
        n_cand_chk_pair_t * p = i->data;
        if (p->state == NCHK_WAITING)
            return p;
    }

    return NULL;
}

/*
 * Initiates a new connectivity check for a ICE candidate pair.
 *
 * @return TRUE on success, FALSE on error
 */
static int _cocheck_initiate(n_agent_t * agent, n_cand_chk_pair_t * pair)
{
    /* XXX: from ID-16 onwards, the checks should not be sent
     * immediately, but be put into the "triggered queue",
     * see  "7.2.1.4 Triggered Checks"
     */
    get_current_time(&pair->next_tick);
    time_val_add(&pair->next_tick, agent->timer_ta * 1000);
    pair->state = NCHK_IN_PROGRESS;
    nice_debug("[%s]: pair %p state IN_PROGRESS", G_STRFUNC, pair);
	nice_print_candpair(agent, pair);
    cocheck_send(agent, pair);
    return TRUE;
}

/*
 * Unfreezes the next connectivity check in the list. Follows the
 * algorithm (2.) defined in 5.7.4 (Computing States) of the ICE spec
 * (ID-19), with some exceptions (see comments in code).
 *
 * See also sect 7.1.2.2.3 (Updating Pair States), and
 * _cocheck_unfreeze_related().
 *
 * @return TRUE on success, and FALSE if no frozen candidates were found.
 */
static int _cocheck_unfreeze_next(n_agent_t * agent)
{
    n_cand_chk_pair_t * pair = NULL;
    n_slist_t * i, *j;

    /* XXX: the unfreezing is implemented a bit differently than in the
     *      current ICE spec, but should still be interoperate:
     *   - checks are not grouped by foundation
     *   - one frozen check is unfrozen (lowest component-id, highest
     *     priority)
     */

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * stream = i->data;
        guint64 max_frozen_priority = 0;


        for (j = stream->conncheck_list; j ; j = j->next)
        {
            n_cand_chk_pair_t * p = j->data;

            /* XXX: the prio check could be removed as the pairs are sorted
             *       already */

            if (p->state == NCHK_FROZEN)
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
        nice_debug("[%s]: Pair %p with s/c-id %u/%u (%s) unfrozen.", G_STRFUNC, pair, pair->stream_id, pair->component_id, pair->foundation);
        pair->state = NCHK_WAITING;
        nice_debug("[%s]: pair %p state NCHK_WAITING", G_STRFUNC, pair);
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
static void _cocheck_unfreeze_related(n_agent_t * agent, n_stream_t * stream, n_cand_chk_pair_t * ok_check)
{
    n_slist_t * i, *j;
    uint32_t unfrozen = 0;

    g_assert(ok_check);
    g_assert(ok_check->state == NCHK_SUCCEEDED);
    g_assert(stream);
    g_assert(stream->id == ok_check->stream_id);

    /* step: perform the step (1) of 'Updating Pair States' */
    for (i = stream->conncheck_list; i ; i = i->next)
    {
        n_cand_chk_pair_t * p = i->data;

        if (p->stream_id == ok_check->stream_id)
        {
            if (p->state == NCHK_FROZEN && strcmp(p->foundation, ok_check->foundation) == 0)
            {
                nice_debug("[%s]: Unfreezing check %p (after successful check %p).", G_STRFUNC, p, ok_check);
                p->state = NCHK_WAITING;
                nice_debug("[%s]: pair %p state NCHK_WAITING", G_STRFUNC, p);
				nice_print_candpair(agent, p);
                unfrozen++;
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
            n_stream_t * s = i->data;
            for (j = stream->conncheck_list; j ; j = j->next)
            {
                n_cand_chk_pair_t * p = j->data;

                if (p->stream_id == s->id &&
                        p->stream_id != ok_check->stream_id)
                {
                    if (p->state == NCHK_FROZEN && strcmp(p->foundation, ok_check->foundation) == 0)
                    {
                        nice_debug("[%s]: Unfreezing check %p from stream %u (after successful check %p).", G_STRFUNC, p, s->id, ok_check);
                        p->state = NCHK_WAITING;
                        nice_debug("[%s]: pair %p state NCHK_WAITING", G_STRFUNC, p);
						nice_print_candpair(agent, ok_check);
                        unfrozen++;
                    }
                }
            }
            /* note: only unfreeze check from one stream at a time */
            if (unfrozen)
                break;
        }
    }

    if (unfrozen == 0)
        _cocheck_unfreeze_next(agent);
}

static void cand_chk_pair_fail(n_stream_t * stream, n_agent_t * agent, n_cand_chk_pair_t * p)
{
    stun_trans_id id;
    n_comp_t * comp;

	comp = stream_find_comp_by_id(stream, p->component_id);

    p->state = NCHK_FAILED;
    nice_debug("[%s]: pair %p state NCHK_FAILED", G_STRFUNC, p);
	nice_print_candpair(agent, p);

    if (p->stun_message.buffer != NULL)
    {
        stun_msg_id(&p->stun_message, id);
        stun_agent_forget_trans(&comp->stun_agent, id);
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
static int _cocheck_tick_stream(n_stream_t * stream, n_agent_t * agent, n_timeval_t * now)
{
    int keep_timer_going = FALSE;
	uint32_t s_inprogress = 0, s_succeeded = 0, s_discovered = 0;
	uint32_t s_nominated = 0, s_waiting_for_nomination = 0;
    uint32_t frozen = 0, waiting = 0;
    n_slist_t * i, * k;

    for (i = stream->conncheck_list; i ; i = i->next)
    {
        n_cand_chk_pair_t * p = i->data;

        if (p->state == NCHK_IN_PROGRESS)
        {
            if (p->stun_message.buffer == NULL)
            {
                nice_debug("[%s]: STUN connectivity check was cancelled, marking as done.", G_STRFUNC);
                p->state = NCHK_FAILED;
                nice_debug("[%s]: pair %p state NCHK_FAILED", G_STRFUNC, p);
				nice_print_candpair(agent, p);
            }
            else if (_timer_expired(&p->next_tick, now))
            {
                switch (stun_timer_refresh(&p->timer))
                {
                    case STUN_TIMER_RET_TIMEOUT:
                    {
                        /* case: error, abort processing */
                        nice_debug("[%s]: STUN Retransmissions failed, giving up on connectivity check %p", G_STRFUNC, p);
                        cand_chk_pair_fail(stream, agent, p);
						nice_print_candpair(agent, p);

                        break;
                    }
                    case STUN_TIMER_RET_RETRANSMIT:
                    {
                        /* case: not ready, so schedule a new timeout */
                        unsigned int timeout = stun_timer_remainder(&p->timer);
                        nice_debug("[%s]: STUN transaction retransmitted (timeout %dms)", G_STRFUNC, timeout);
						nice_print_candpair(agent, p);

                        agent_socket_send(p->sockptr, &p->remote->addr, stun_msg_len(&p->stun_message), (char *)p->stun_buffer);

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
						nice_debug("[%s]: STUN success %p", G_STRFUNC, p);
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

        if (p->state == NCHK_FROZEN)
            frozen++;
        else if (p->state == NCHK_IN_PROGRESS)
            s_inprogress++;
        else if (p->state == NCHK_WAITING)
            waiting++;
        else if (p->state == NCHK_SUCCEEDED)
            s_succeeded++;
        else if (p->state == NCHK_DISCOVERED)
            s_discovered++;

        if ((p->state == NCHK_SUCCEEDED || p->state == NCHK_DISCOVERED) && p->nominated)
            ++s_nominated;
        else if ((p->state == NCHK_SUCCEEDED ||
                  p->state == NCHK_DISCOVERED) && !p->nominated)
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
                n_comp_t * component = component_item->data;

                for (k = stream->conncheck_list; k ; k = k->next)
                {
                    n_cand_chk_pair_t * p = k->data;
                    /* note: highest priority item selected (list always sorted) */
                    if (p->component_id == component->id &&
                            (p->state == NCHK_SUCCEEDED ||
                             p->state == NCHK_DISCOVERED))
                    {
                        nice_debug("[%s]: restarting check %p as the nominated pair.", G_STRFUNC, p);
						nice_print_candpair(agent, p);
                        p->nominated = TRUE;
                        _cocheck_initiate(agent, p);
                        break; /* move to the next component */
                    }
                }
            }
        }
    }
    {
        static int tick_counter = 0;
        if (tick_counter++ % 50 == 0 || keep_timer_going != TRUE)
            nice_debug("[%s]: timer tick #%u: %u frozen, %u in-progress, "
                       "%u waiting, %u succeeded, %u discovered, %u nominated, "
                       "%u waiting-for-nomination", G_STRFUNC,
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
static int _cocheck_tick_unlocked(n_agent_t * agent)
{
    n_cand_chk_pair_t * pair = NULL;
    int keep_timer_going = FALSE;
    n_slist_t * i, *j;
    n_timeval_t now;

    /* step: process ongoing STUN transactions */
    get_current_time(&now);

    /* step: find the highest priority waiting check and send it */
    for (i = agent->streams_list; i ; i = i->next)
    {
        n_stream_t * stream = i->data;

        pair = _cochk_find_next_waiting(stream->conncheck_list);
        if (pair)
            break;
    }

    if (pair)
    {
        _cocheck_initiate(agent, pair);
        keep_timer_going = TRUE;
    }
    else
    {
        keep_timer_going = _cocheck_unfreeze_next(agent);
    }

    for (j = agent->streams_list; j; j = j->next)
    {
        n_stream_t * stream = j->data;
        int res =
            _cocheck_tick_stream(stream, agent, &now);
        if (res)
            keep_timer_going = res;
    }

    /* step: stop timer if no work left */
    if (keep_timer_going != TRUE)
    {
        nice_debug("[%s]: stopping conncheck timer", G_STRFUNC);
        for (i = agent->streams_list; i; i = i->next)
        {
            n_stream_t * stream = i->data;
            _update_chk_list_failed_comps(agent, stream);
            for (j = stream->components; j; j = j->next)
            {
                n_comp_t * component = j->data;
                _update_chk_list_state_for_ready(agent, stream, component);
            }
        }

        /* Stopping the timer so destroy the source.. this will allow
           the timer to be reset if we get a set_remote_candidates after this
           point */
        /*if (agent->conncheck_timer_source != NULL)
        {
            g_source_destroy(agent->conncheck_timer_source);
            g_source_unref(agent->conncheck_timer_source);
            agent->conncheck_timer_source = NULL;
        }*/

		if (agent->cocheck_timer != 0)
		{
			timer_stop(agent->cocheck_timer);
			timer_destroy(agent->cocheck_timer);
			agent->cocheck_timer = 0;
		}

        /* XXX: what to signal, is all processing now really done? */
        nice_debug("[%s]: changing conncheck state to COMPLETED.", G_STRFUNC);
    }

    return keep_timer_going;
}

static int _cocheck_tick(void * pointer)
{
    int ret;
    n_agent_t * agent = pointer;

	//nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    agent_lock();
/*
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. " "Avoided race condition in _cocheck_tick");
        agent_unlock();
        return FALSE;
    }*/

    ret = _cocheck_tick_unlocked(agent);
    agent_unlock_and_emit(agent);

    return ret;
}

static int _conn_keepalive_retrans_tick(void * pointer)
{
    n_cand_pair_t * pair = (n_cand_pair_t *) pointer;

	//nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    agent_lock();

    /* A race condition might happen where the mutex above waits for the lock
     * and in the meantime another thread destroys the source.
     * In that case, we don't need to run our retransmission tick since it should
     * have been cancelled */
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. " "Avoided race condition in _conn_keepalive_retrans_tick");
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
            stun_trans_id id;
            n_comp_t * component;

            if (!agent_find_comp(pair->keepalive.agent,
                                      pair->keepalive.stream_id, pair->keepalive.component_id,
                                      NULL, &component))
            {
                nice_debug("Could not find stream or component in" " _conn_keepalive_retrans_tick");
                agent_unlock();
                return FALSE;
            }

            stun_msg_id(&pair->keepalive.stun_message, id);
            stun_agent_forget_trans(&component->stun_agent, id);

            if (pair->keepalive.agent->media_after_tick)
            {
                nice_debug("[%s]: Keepalive conncheck timed out!! "
                           "but media was received. Suspecting keepalive lost because of "
                           "network bottleneck", G_STRFUNC);

                pair->keepalive.stun_message.buffer = NULL;
            }
            else
            {
                nice_debug("[%s]: Keepalive conncheck timed out!! "
                           "peer probably lost connection", G_STRFUNC);
				nice_print_cand(pair->keepalive.agent, pair->local, pair->remote);
                agent_sig_comp_state_change(pair->keepalive.agent,
                                                    pair->keepalive.stream_id, pair->keepalive.component_id,
                                                    COMP_STATE_FAILED);
            }
            break;
        }
        case STUN_TIMER_RET_RETRANSMIT:
            /* Retransmit */
            agent_socket_send(pair->local->sockptr, &pair->remote->addr,
                              stun_msg_len(&pair->keepalive.stun_message),
                              (char *)pair->keepalive.stun_buffer);

            nice_debug("[%s]: Retransmitting keepalive conncheck", G_STRFUNC);
			nice_print_cand(pair->keepalive.agent, pair->local, pair->remote);
            agent_timeout_add(pair->keepalive.agent,
                                           &pair->keepalive.tick_source,
                                           "Pair keepalive", stun_timer_remainder(&pair->keepalive.timer),
                                           _conn_keepalive_retrans_tick, pair);
            break;
        case STUN_TIMER_RET_SUCCESS:
            agent_timeout_add(pair->keepalive.agent,
                                           &pair->keepalive.tick_source,
                                           "Pair keepalive", stun_timer_remainder(&pair->keepalive.timer),
                                           _conn_keepalive_retrans_tick, pair);
            break;
        default:
            /* Nothing to do. */
            break;
    }


    agent_unlock_and_emit(pair->keepalive.agent);
    return FALSE;
}

static uint32_t peer_reflexive_cand_priority(n_agent_t * agent, n_cand_t * local_candidate)
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
static int _conn_keepalive_tick_unlocked(n_agent_t * agent)
{
    n_slist_t * i, *j, *k;
    int errors = 0;
    int ret = FALSE;
    size_t buf_len = 0;

    /* case 1: session established and media flowing
     *         (ref ICE sect 10 "Keepalives" ID-19)  */
    for (i = agent->streams_list; i; i = i->next)
    {

        n_stream_t * stream = i->data;
        for (j = stream->components; j; j = j->next)
        {
            n_comp_t * component = j->data;
            if (component->selected_pair.local != NULL)
            {
                n_cand_pair_t * p = &component->selected_pair;

                /* Disable keepalive checks on TCP candidates */
                if (p->local->transport != CAND_TRANS_UDP)
                    continue;

                if (agent->keepalive_conncheck)
                {
                    uint32_t priority;
                    uint8_t uname[N_STREAM_MAX_UNAME];
                    size_t uname_len =  _create_username(agent, agent_find_stream(agent, stream->id), component->id, 
																				p->remote, p->local, uname, sizeof(uname), FALSE);
                    uint8_t * password = NULL;
                    size_t password_len = _get_password(agent, agent_find_stream(agent, stream->id), p->remote, &password);

                    priority = peer_reflexive_cand_priority(agent, p->local);

                    if (nice_debug_is_enabled())
                    {
                        char tmpbuf[INET6_ADDRSTRLEN];
                        nice_address_to_string(&p->remote->addr, tmpbuf);
                        nice_debug("[%s]: Keepalive STUN-CC REQ to '%s:%u', "
                                   "socket=%u (c-id:%u), username='%.*s' (%" G_GSIZE_FORMAT "), "
                                   "password='%.*s' (%" G_GSIZE_FORMAT "), priority=%u.", G_STRFUNC,
                                   tmpbuf, nice_address_get_port(&p->remote->addr),
                                   g_socket_get_fd(((n_socket_t *)p->local->sockptr)->fileno),
                                   component->id, (int) uname_len, uname, uname_len,
                                   (int) password_len, password, password_len, priority);
                    }
                    if (uname_len > 0)
                    {
                        buf_len = stun_ice_cocheck_create(&component->stun_agent,
                                  &p->keepalive.stun_message, p->keepalive.stun_buffer,
                                  sizeof(p->keepalive.stun_buffer),
                                  uname, uname_len, password, password_len,
                                  agent->controlling_mode, agent->controlling_mode, priority,
                                  agent->tie_breaker);

                        nice_debug("[%s]: conncheck created %zd - %p",
								G_STRFUNC, buf_len, p->keepalive.stun_message.buffer);

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
                                                           _conn_keepalive_retrans_tick, p);
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

                        nice_debug("[%s]: stun_bind_keepalive for pair %p res %d.", G_STRFUNC, p, (int) buf_len);
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
        n_stream_t * stream = i->data;
        for (j = stream->components; j; j = j->next)
        {
            n_comp_t * component = j->data;
            if (component->state < COMP_STATE_READY && agent->stun_server_ip)
            {
                n_addr_t stun_server;
                if (nice_address_set_from_string(&stun_server, agent->stun_server_ip))
                {
                    stun_agent_t stun_agent;
                    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
                    stun_msg_t stun_message;
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
                                candidate->transport == CAND_TRANS_UDP)
                        {
                            /* send the conncheck */
                            nice_debug("[%s]: resending STUN on %s to keep the " "candidate alive.", G_STRFUNC, candidate->foundation);
                            agent_socket_send(candidate->sockptr, &stun_server, buffer_len, (char *)stun_buffer);
                        }
                    }
                }
            }
        }
    }

    if (errors)
    {
        nice_debug("[%s]: %s: stopping keepalive timer", G_STRFUNC, G_STRFUNC);
        goto done;
    }

    ret = TRUE;

done:
    return ret;
}

static int _conn_keepalive_tick(void * pointer)
{
    n_agent_t * agent = pointer;
    int ret;

	//nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    agent_lock();
/*
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. "
                   "Avoided race condition in _conn_keepalive_tick");
        agent_unlock();
        return FALSE;
    }*/

    ret = _conn_keepalive_tick_unlocked(agent);
    if (ret == FALSE)
    {
        /*if (agent->keepalive_timer_source)
        {
            g_source_destroy(agent->keepalive_timer_source);
            g_source_unref(agent->keepalive_timer_source);
            agent->keepalive_timer_source = NULL;
        }*/

		if (agent->keepalive_timer != 0)
		{
			timer_stop(agent->keepalive_timer);
			timer_destroy(agent->keepalive_timer);
			agent->keepalive_timer = 0;
		}
    }
    agent_unlock_and_emit(agent);
    return ret;
}

static int _turn_alloc_refresh_retrans_tick(void * pointer)
{
    n_cand_refresh_t * cand = (n_cand_refresh_t *) pointer;
    n_agent_t * agent = NULL;

	//nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    agent_lock();

    /* A race condition might happen where the mutex above waits for the lock
     * and in the meantime another thread destroys the source.
     * In that case, we don't need to run our retransmission tick since it should
     * have been cancelled */
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. "
                   "Avoided race condition in _turn_alloc_refresh_retrans_tick");
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
            stun_trans_id id;

            stun_msg_id(&cand->stun_message, id);
            stun_agent_forget_trans(&cand->stun_agent, id);

            refresh_cancel(cand);
            break;
        }
        case STUN_TIMER_RET_RETRANSMIT:
            /* Retransmit */
            agent_socket_send(cand->nicesock, &cand->server,
                              stun_msg_len(&cand->stun_message), (char *)cand->stun_buffer);

            agent_timeout_add(agent, &cand->tick_source,
                                           "Candidate TURN refresh", stun_timer_remainder(&cand->timer),
                                           _turn_alloc_refresh_retrans_tick, cand);
            break;
        case STUN_TIMER_RET_SUCCESS:
            agent_timeout_add(agent, &cand->tick_source,
                                           "Candidate TURN refresh", stun_timer_remainder(&cand->timer),
                                           _turn_alloc_refresh_retrans_tick, cand);
            break;
        default:
            /* Nothing to do. */
            break;
    }


    agent_unlock_and_emit(agent);

    g_object_unref(agent);

    return FALSE;
}

static void _turn_alloc_refresh_tick_unlocked(n_cand_refresh_t * cand)
{
    uint8_t * username;
    uint32_t username_len;
    uint8_t * password;
    uint32_t password_len;
    size_t buffer_len = 0;

    username = (uint8_t *)cand->candidate->turn->username;
    username_len = (size_t) strlen(cand->candidate->turn->username);
    password = (uint8_t *)cand->candidate->turn->password;
    password_len = (size_t) strlen(cand->candidate->turn->password);

    buffer_len = turn_create_refresh(&cand->stun_agent,
                 &cand->stun_message,  cand->stun_buffer, sizeof(cand->stun_buffer),
                 cand->stun_resp_msg.buffer == NULL ? NULL : &cand->stun_resp_msg, -1,
                 username, username_len, password, password_len);

    nice_debug("[%s]: sending allocate refresh %zd", G_STRFUNC, buffer_len);

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
        agent_timeout_add(cand->agent, &cand->tick_source, "candidate turn refresh", stun_timer_remainder(&cand->timer),
                                       _turn_alloc_refresh_retrans_tick, cand);
    }
}

/*
 * Timer callback that handles refreshing TURN allocations
 *
 * This function is designed for the g_timeout_add() interface.
 *
 * @return will return FALSE when no more pending timers.
 */
static int _turn_allocate_refresh_tick(void * pointer)
{
    n_cand_refresh_t * cand = (n_cand_refresh_t *) pointer;

	//nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
	agent_lock();
    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. " "Avoided race condition in _turn_allocate_refresh_tick");
        agent_unlock();
        return FALSE;
    }

    _turn_alloc_refresh_tick_unlocked(cand);
    agent_unlock_and_emit(cand->agent);

    return FALSE;
}

/*
 * Initiates the next pending connectivity check.
 *
 * @return TRUE if a pending check was scheduled
 */
int cocheck_schedule_next(n_agent_t * agent)
{
    int res = _cocheck_unfreeze_next(agent);
    nice_debug("[%s]: _cocheck_unfreeze_next returned %d", G_STRFUNC, res);

    if (agent->disc_unsched_items > 0)
        nice_debug("[%s]: WARN: starting conn checks before local candidate gathering is finished.", G_STRFUNC);

    /* step: call once imediately */
    res = _cocheck_tick_unlocked(agent);
    nice_debug("[%s]: _cocheck_tick_unlocked returned %d", G_STRFUNC, res);

    /* step: schedule timer if not running yet */
    if (res && agent->cocheck_timer == 0)
    {
        /*agent_timeout_add(agent, &agent->conncheck_timer_source, "cocheck schedule", agent->timer_ta, _cocheck_tick, agent);*/
		agent->cocheck_timer = timer_create();
		timer_init(agent->cocheck_timer, 0, agent->timer_ta, (notifycallback)_cocheck_tick, (void *)agent, "cocheck schedule");
		timer_start(agent->cocheck_timer);
    }

    /* step: also start the keepalive timer */
    if (agent->keepalive_timer == 0)
    {
        /*agent_timeout_add(agent, &agent->keepalive_timer_source,
                                       "Connectivity keepalive timeout", NICE_AGENT_TIMER_TR_DEFAULT,
                                       _conn_keepalive_tick, agent);*/
		agent->keepalive_timer = timer_create();
		timer_init(agent->keepalive_timer, 0, NICE_AGENT_TIMER_TR_DEFAULT, (notifycallback)_conn_keepalive_tick, (void *)agent, "cocheck keepalive");
		timer_start(agent->keepalive_timer);
    }

    nice_debug("[%s]: cocheck_schedule_next returning %d", G_STRFUNC, res);
    return res;
}

/*
 * Compares two connectivity check items. Checkpairs are sorted
 * in descending priority order, with highest priority item at
 * the start of the list.
 */
int32_t cocheck_compare(const n_cand_chk_pair_t * a, const n_cand_chk_pair_t * b)
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
static void _preprocess_cocheck_pending_data(n_agent_t * agent, n_stream_t * stream, n_comp_t * component, n_cand_chk_pair_t * pair)
{
    n_slist_t * i;
    for (i = component->incoming_checks; i; i = i->next)
    {
        n_inchk_t * icheck = i->data;
        if (nice_address_equal(&icheck->from, &pair->remote->addr) &&
                icheck->local_socket == pair->sockptr)
        {
            nice_debug("[%s]: updating check %p with stored early-icheck %p, %p/%u/%u (agent/stream/component)", G_STRFUNC, pair, icheck, agent, stream->id, component->id);
            if (icheck->use_candidate)
                _mark_pair_nominated(agent, stream, component, pair->remote);
            _schedule_triggered_check(agent, stream, component, icheck->local_socket, pair->remote, icheck->use_candidate);
        }
    }
}

static n_slist_t * prune_cancelled_cocheck(n_slist_t * conncheck_list)
{
    n_slist_t * item = conncheck_list;

    while (item)
    {
        n_cand_chk_pair_t * pair = item->data;
        n_slist_t * next = item->next;

        if (pair->state == NCHK_CANCELLED)
        {
            cocheck_free_item(pair);
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
void cocheck_remote_cands_set(n_agent_t * agent)
{
    n_slist_t * i, * j, * k, * l;

    for (i = agent->streams_list; i ; i = i->next)
    {
        n_stream_t * stream = i->data;
        for (j = stream->conncheck_list; j ; j = j->next)
        {
            n_cand_chk_pair_t * pair = j->data;
            n_comp_t * component = stream_find_comp_by_id(stream, pair->component_id);
            int match = FALSE;

            /* performn delayed processing of spec steps section 7.2.1.4, and section 7.2.1.5 */
            _preprocess_cocheck_pending_data(agent, stream, component, pair);

            for (k = component->incoming_checks; k; k = k->next)
            {
                n_inchk_t * icheck = k->data;
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

                    n_cand_t * l_candidate = NULL;
                    n_cand_t * r_candidate = NULL;
					n_cand_t * candidate;
                    
					for (l = component->local_candidates; l; l = l->next)
					{
						n_cand_t * cand = l->data;
						if (nice_address_equal(&cand->addr, &icheck->local_socket->addr))
						{
							l_candidate = cand;
							break;
						}
					}                       

					nice_debug("[%s]: Discovered peer reflexive from early i-check", G_STRFUNC);
					candidate = disc_learn_remote_peer_cand(agent, stream, component, icheck->priority,
						&icheck->from, icheck->local_socket, l_candidate, r_candidate);
					if (candidate)
					{
						cocheck_add_cand(agent, stream->id, component, candidate);
						if (icheck->use_candidate)
							_mark_pair_nominated(agent, stream, component, candidate);
						_schedule_triggered_check(agent, stream, component, icheck->local_socket, candidate, icheck->use_candidate);
					}
                }
            }

            /* Once we process the pending checks, we should free them to avoid
             * reprocessing them again if a dribble-mode set_remote_candidates
             * is called */
            n_slist_free_full(component->incoming_checks, (n_destroy_notify) incoming_check_free);
            component->incoming_checks = NULL;
        }

        stream->conncheck_list = prune_cancelled_cocheck(stream->conncheck_list);
    }
}

/*
 * Enforces the upper limit for connectivity checks as described
 * in ICE spec section 5.7.3 (ID-19). See also
 * cocheck_add_cand().
 */
static void _limit_cochk_list_size(n_slist_t * cocheck_list, uint32_t upper_limit)
{
    uint32_t valid = 0;
    uint32_t cancelled = 0;
    n_slist_t * item = cocheck_list;

    while (item)
    {
        n_cand_chk_pair_t * pair = item->data;

        if (pair->state != NCHK_CANCELLED)
        {
            valid++;
            if (valid > upper_limit)
            {
                pair->state = NCHK_CANCELLED;
                cancelled++;
            }
        }

        item = item->next;
    }

    if (cancelled > 0)
        nice_debug("[%s]: pruned %d candidates. conncheck list has %d elements"
                   " left. maximum connchecks allowed : %d", G_STRFUNC, cancelled, valid, upper_limit);
}

/*
 * Changes the selected pair for the component if 'pair' is nominated
 * and has higher priority than the currently selected pair. See
 * ICE sect 11.1.1. "Procedures for Full Implementations" (ID-19).
 */
static int _update_selected_pair(n_agent_t * agent, n_comp_t * component, n_cand_chk_pair_t * pair)
{
    n_cand_pair_t cpair;

    g_assert(component);
    g_assert(pair);
    if (pair->priority > component->selected_pair.priority &&
            comp_find_pair(component, agent, pair->local->foundation,
                                pair->remote->foundation, &cpair))
    {
        nice_debug("[%s]: changing SELECTED PAIR for component %u: %s:%s "
                   "(prio:%" G_GUINT64_FORMAT ").", G_STRFUNC, component->id,
                   pair->local->foundation, pair->remote->foundation, pair->priority);

        comp_update_selected_pair(component, &cpair);

        _conn_keepalive_tick_unlocked(agent);

        agent_sig_new_selected_pair(agent, pair->stream_id, component->id,
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
 * failed, mark that component's state to NCHK_FAILED.
 *
 * Sends a component state changesignal via 'agent'.
 */
static void _update_chk_list_failed_comps(n_agent_t * agent, n_stream_t * stream)
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
        n_comp_t * comp = NULL;
        if (!agent_find_comp(agent, stream->id, c + 1, NULL, &comp))
            continue;

        for (i = stream->conncheck_list; i; i = i->next)
        {
            n_cand_chk_pair_t * p = i->data;

            g_assert(p->agent == agent);
            g_assert(p->stream_id == stream->id);

            if (p->component_id == (c + 1))
            {
                if (p->state != NCHK_FAILED)
                    break;
            }
        }

        /* note: all checks have failed
         * Set the component to FAILED only if it actually had remote candidates
         * that failed.. */
        if (i == NULL && comp != NULL && comp->remote_candidates != NULL)
            agent_sig_comp_state_change(agent, stream->id, (c + 1), COMP_STATE_FAILED);
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
static void _update_chk_list_state_for_ready(n_agent_t * agent, n_stream_t * stream, n_comp_t * component)
{
    n_slist_t * i;
    uint32_t succeeded = 0, nominated = 0;

    g_assert(component);

    /* step: search for at least one nominated pair */
    for (i = stream->conncheck_list; i; i = i->next)
    {
        n_cand_chk_pair_t * p = i->data;
        if (p->component_id == component->id)
        {
            if (p->state == NCHK_SUCCEEDED ||
                    p->state == NCHK_DISCOVERED)
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
        if (_prune_pending_checks(stream, component->id) == 0)
        {
            agent_sig_comp_state_change(agent, stream->id,  component->id, COMP_STATE_READY);
        }
    }
    nice_debug("[%s]: conn.check list status: %u nominated, %u succeeded, c-id %u.", G_STRFUNC, nominated, succeeded, component->id);
}

/*
 * The remote party has signalled that the candidate pair
 * described by 'component' and 'remotecand' is nominated
 * for use.
 */
static void _mark_pair_nominated(n_agent_t * agent, n_stream_t * stream, n_comp_t * component, n_cand_t * remotecand)
{
    n_slist_t * i;

    g_assert(component);

    /* step: search for at least one nominated pair */
    for (i = stream->conncheck_list; i; i = i->next)
    {
        n_cand_chk_pair_t * pair = i->data;
        /* XXX: hmm, how to figure out to which local candidate the
         *      check was sent to? let's mark all matching pairs
         *      as nominated instead */
        if (pair->remote == remotecand)
        {
            nice_debug("[%s]: marking pair %p (%s) as nominated", G_STRFUNC, pair, pair->foundation);
            pair->nominated = TRUE;
            if (pair->state == NCHK_SUCCEEDED ||
                    pair->state == NCHK_DISCOVERED)
                _update_selected_pair(agent, component, pair);
            _update_chk_list_state_for_ready(agent, stream, component);
        }
    }
}

/*
 * Creates a new connectivity check pair and adds it to
 * the agent's list of checks.
 */
static void _add_new_chk_pair(n_agent_t * agent, uint32_t stream_id, n_comp_t * component, n_cand_t * local, 
		n_cand_t * remote, n_chk_state_e initial_state, int use_candidate)
{
    n_stream_t * stream;
    n_cand_chk_pair_t * pair;

    g_assert(local != NULL);
    g_assert(remote != NULL);

    stream = agent_find_stream(agent, stream_id);
    pair = g_slice_new0(n_cand_chk_pair_t);

    pair->agent = agent;
    pair->stream_id = stream_id;
    pair->component_id = component->id;;
    pair->local = local;
    pair->remote = remote;
    if (remote->type == CAND_TYPE_PEER)
        pair->sockptr = (n_socket_t *) remote->sockptr;
    else
        pair->sockptr = (n_socket_t *) local->sockptr;
    g_snprintf(pair->foundation, CAND_PAIR_MAX_FOUNDATION, "%s:%s", local->foundation, remote->foundation);

    pair->priority = agent_candidate_pair_priority(agent, local, remote);
    pair->state = initial_state;
    nice_debug("[%s]: creating new pair %p state %d", G_STRFUNC, pair, initial_state);
    pair->nominated = use_candidate;
    pair->controlling = agent->controlling_mode;

    stream->conncheck_list = n_slist_insert_sorted(stream->conncheck_list, pair,  (GCompareFunc)cocheck_compare);

    nice_debug("[%s]: added a new conncheck %p with foundation of '%s' to list %u.", G_STRFUNC, pair, pair->foundation, stream_id);

    /* implement the hard upper limit for number of checks (see sect 5.7.3 ICE ID-19): */    
    _limit_cochk_list_size(stream->conncheck_list, agent->max_conn_checks);
}

n_cand_trans_e cocheck_match_trans(n_cand_trans_e transport)
{
    switch (transport)
    {
        case CAND_TRANS_TCP_ACTIVE:
            return CAND_TRANS_TCP_PASSIVE;
            break;
        case CAND_TRANS_TCP_PASSIVE:
            return CAND_TRANS_TCP_ACTIVE;
            break;
        case CAND_TRANS_TCP_SO:
        case CAND_TRANS_UDP:
        default:
            return transport;
            break;
    }
}

static void _cocheck_add_cand_pair_matched(n_agent_t * agent,
        uint32_t stream_id, n_comp_t * component, n_cand_t * local,
        n_cand_t * remote, n_chk_state_e initial_state)
{
    nice_debug("[%s] Adding check pair between %s and %s", G_STRFUNC, local->foundation, remote->foundation);

    _add_new_chk_pair(agent, stream_id, component, local, remote, initial_state, FALSE);
    if (component->state == COMP_STATE_CONNECTED || component->state == COMP_STATE_READY)
    {
        agent_sig_comp_state_change(agent, stream_id, component->id, COMP_STATE_CONNECTED);
    }
    else
    {
        agent_sig_comp_state_change(agent, stream_id, component->id, COMP_STATE_CONNECTING);
    }
}

int cocheck_add_cand_pair(n_agent_t * agent, uint32_t stream_id, n_comp_t * comp, n_cand_t * local, n_cand_t * remote)
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
        _cocheck_add_cand_pair_matched(agent, stream_id, comp, local, remote, NCHK_FROZEN);
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
int cocheck_add_cand(n_agent_t * agent, uint32_t stream_id, n_comp_t * component, n_cand_t * remote)
{
    n_slist_t * i;
    int added = 0;
    int ret = 0;

    g_assert(remote != NULL);

    for (i = component->local_candidates; i ; i = i->next)
    {

        n_cand_t * local = i->data;
        ret = cocheck_add_cand_pair(agent, stream_id, component, local, remote);

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
int cocheck_add_local_cand(n_agent_t * agent, uint32_t stream_id, n_comp_t * comp, n_cand_t * local)
{
    n_slist_t * i;
    int added = 0;
    int ret = 0;

    g_assert(local != NULL);

    for (i = comp->remote_candidates; i ; i = i->next)
    {

        n_cand_t * remote = i->data;
        ret = cocheck_add_cand_pair(agent, stream_id, comp, local, remote);

        if (ret)
        {
            ++added;
        }
    }

    return added;
}

/*
 * Frees the n_cand_chk_pair_t structure pointer to
 * by 'user data'. Compatible with n_destroy_notify.
 */
static void cocheck_free_item(void * data)
{
    n_cand_chk_pair_t * pair = data;

    pair->stun_message.buffer = NULL;
    pair->stun_message.buffer_len = 0;
    n_slice_free(n_cand_chk_pair_t, pair);
}

static void cocheck_stop(n_agent_t * agent)
{
/*
    if (agent->conncheck_timer_source == NULL)
        return;

    g_source_destroy(agent->conncheck_timer_source);
    g_source_unref(agent->conncheck_timer_source);
    agent->conncheck_timer_source = NULL;*/
	if (agent->cocheck_timer == 0)
		return;
	timer_stop(agent->cocheck_timer);
	timer_destroy(agent->cocheck_timer);
	agent->cocheck_timer = 0;
}

/*
 * Frees all resources of all connectivity checks.
 */
void cocheck_free(n_agent_t * agent)
{
    n_slist_t * i;
    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * stream = i->data;

        if (stream->conncheck_list)
        {
            nice_debug("[%s], freeing conncheck_list of stream %p", G_STRFUNC, stream);
            n_slist_free_full(stream->conncheck_list, cocheck_free_item);
            stream->conncheck_list = NULL;
        }
    }

    cocheck_stop(agent);
}

/*
 * Prunes the list of connectivity checks for items related
 * to stream 'stream_id'.
 *
 * @return TRUE on success, FALSE on a fatal error
 */
void cocheck_prune_stream(n_agent_t * agent, n_stream_t * stream)
{
    n_slist_t * i;
    int keep_going = FALSE;

    if (stream->conncheck_list)
    {
        nice_debug("[%s], freeing conncheck_list of stream %p", G_STRFUNC, stream);

        n_slist_free_full(stream->conncheck_list, cocheck_free_item);
        stream->conncheck_list = NULL;
    }

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * s = i->data;
        if (s->conncheck_list)
        {
            keep_going = TRUE;
            break;
        }
    }

    if (!keep_going)
        cocheck_stop(agent);
}

/*
 * Fills 'dest' with a username string for use in an outbound connectivity
 * checks. No more than 'dest_len' characters (including terminating
 * NULL) is ever written to the 'dest'.
 */
static size_t _gen_username(n_agent_t * agent, uint32_t component_id,
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
static size_t _create_username(n_agent_t * agent, n_stream_t * stream,
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
            return _gen_username(agent, component_id, local_username, remote_username, dest, dest_len);
        }
        else
        {
            return _gen_username(agent, component_id, remote_username, local_username, dest, dest_len);
        }
    }

    return 0;
}

/*
 * Returns a password string for use in an outbound connectivity
 * check.
 */
static size_t _get_password(n_agent_t * agent, n_stream_t * stream, n_cand_t * remote, uint8_t ** password)
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

static unsigned int _compute_cocheck_timer(n_agent_t * agent, n_stream_t * stream)
{
    n_slist_t * item;
    uint32_t waiting_and_in_progress = 0;
    unsigned int rto = 0;

    for (item = stream->conncheck_list; item; item = item->next)
    {
        n_cand_chk_pair_t * pair = item->data;

        if (pair->state == NCHK_IN_PROGRESS || pair->state == NCHK_WAITING)
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
int cocheck_send(n_agent_t * agent, n_cand_chk_pair_t * pair)
{

    /* note: following information is supplied:
     *  - username (for USERNAME attribute)
     *  - password (for MESSAGE-INTEGRITY)
     *  - priority (for PRIORITY)
     *  - ICE-CONTROLLED/ICE-CONTROLLING (for role conflicts)
     *  - USE-CANDIDATE (if sent by the controlling agent)
     */
    guint32 priority;

    uint8_t uname[N_STREAM_MAX_UNAME];
    n_stream_t * stream;
    n_comp_t * component;
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

    uname_len = _create_username(agent, stream, pair->component_id,  pair->remote, pair->local, uname, sizeof(uname), FALSE);
    password_len = _get_password(agent, stream, pair->remote, &password);
    priority = peer_reflexive_cand_priority(agent, pair->local);

    if (nice_debug_is_enabled())
    {
        char tmpbuf[INET6_ADDRSTRLEN];
        nice_address_to_string(&pair->remote->addr, tmpbuf);
        nice_debug("[%s]: STUN-CC REQ to '%s:%u', socket=%u, "
                   "pair=%s (c-id:%u), tie=%llu, username='%.*s' (%" G_GSIZE_FORMAT "), "
                   "password='%.*s' (%" G_GSIZE_FORMAT "), priority=%u.", G_STRFUNC,
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
        buffer_len = stun_ice_cocheck_create(&component->stun_agent,
                     &pair->stun_message, pair->stun_buffer, sizeof(pair->stun_buffer),
                     uname, uname_len, password, password_len,
                     cand_use, controlling, priority,
                     agent->tie_breaker);

        nice_debug("[%s]: conncheck created %zd - %p", G_STRFUNC, buffer_len,  pair->stun_message.buffer);

        if (buffer_len > 0)
        {
            if (nice_socket_is_reliable(pair->sockptr))
            {
                stun_timer_start_reliable(&pair->timer, STUN_TIMER_RELIABLE_TIMEOUT);
            }
            else
            {
                stun_timer_start(&pair->timer, _compute_cocheck_timer(agent, stream), STUN_TIMER_MAX_RETRANS);
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
            nice_debug("[%s]: buffer is empty, cancelling conncheck", G_STRFUNC);
            pair->stun_message.buffer = NULL;
            pair->stun_message.buffer_len = 0;
            return -1;
        }
    }
    else
    {
        nice_debug("[%s]: no credentials found, cancelling conncheck", G_STRFUNC);
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
static uint32_t _prune_pending_checks(n_stream_t * stream, uint32_t component_id)
{
    n_slist_t * i;
    guint64 highest_nominated_priority = 0;
    uint32_t in_progress = 0;

    nice_debug("Agent XXX: Finding highest priority for component %d", component_id);

    for (i = stream->conncheck_list; i; i = i->next)
    {
        n_cand_chk_pair_t * p = i->data;
        if (p->component_id == component_id &&
                (p->state == NCHK_SUCCEEDED ||
                 p->state == NCHK_DISCOVERED) &&
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
        n_cand_chk_pair_t * p = i->data;
        if (p->component_id == component_id)
        {
            if (p->state == NCHK_FROZEN || p->state == NCHK_WAITING)
            {
                p->state = NCHK_CANCELLED;
                nice_debug("Agent XXX : pair %p state CANCELED", p);
				nice_print_candpair(NULL, p);
            }

            /* note: a SHOULD level req. in ICE 8.1.2. "Updating States" (ID-19) */
            if (p->state == NCHK_IN_PROGRESS)
            {
                if (highest_nominated_priority != 0 &&
                        p->priority < highest_nominated_priority)
                {
                    p->stun_message.buffer = NULL;
                    p->stun_message.buffer_len = 0;
                    p->state = NCHK_CANCELLED;
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
static int _schedule_triggered_check(n_agent_t * agent, n_stream_t * stream, n_comp_t * comp, n_socket_t * local_socket, n_cand_t * remote_cand, int use_candidate)
{
    n_slist_t * i;
    n_cand_t * local = NULL;

    g_assert(remote_cand != NULL);

    for (i = stream->conncheck_list; i ; i = i->next)
    {
        n_cand_chk_pair_t * p = i->data;
        if (p->component_id == comp->id && p->remote == remote_cand && p->local->sockptr == local_socket)
        {
            /* We don't check for p->sockptr because in the case of
             * tcp-active we don't want to retrigger a check on a pair that
             * was FAILED when a peer-reflexive pair was created */

            nice_debug("[%s]: Found a matching pair %p for triggered check.", G_STRFUNC, p);

            if (p->state == NCHK_WAITING || p->state == NCHK_FROZEN)
                _cocheck_initiate(agent, p);
            else if (p->state == NCHK_IN_PROGRESS)
            {
                /* XXX: according to ICE 7.2.1.4 "Triggered Checks" (ID-19),
                 * we should cancel the existing one, instead we reset our timer, so
                 * we'll resend the exiting transactions faster if needed...? :P
                 */
                nice_debug("[%s]: check already in progress, "
                           "restarting the timer again?: %s ..", G_STRFUNC,
                           p->timer_restarted ? "no" : "yes");
                if (!nice_socket_is_reliable(p->sockptr) && !p->timer_restarted)
                {
                    stun_timer_start(&p->timer, _compute_cocheck_timer(agent, stream), STUN_TIMER_MAX_RETRANS);
                    p->timer_restarted = TRUE;
                }
            }
            else if (p->state == NCHK_SUCCEEDED || p->state == NCHK_DISCOVERED)
            {
                nice_debug("[%s]: Skipping triggered check, already completed..", G_STRFUNC);
                /* note: this is a bit unsure corner-case -- let's do the
                   same state update as for processing responses to our own checks */
                _update_chk_list_state_for_ready(agent, stream, comp);

                /* note: to take care of the controlling-controlling case in
                 *       aggressive nomination mode, send a new triggered
                 *       check to nominate the pair */
                if (agent->controlling_mode)
                    _cocheck_initiate(agent, p);
            }
            else if (p->state == NCHK_FAILED)
            {
                /* 7.2.1.4 Triggered Checks
                 * If the state of the pair is Failed, it is changed to Waiting
                   and the agent MUST create a new connectivity check for that
                   pair (representing a new STUN Binding request transaction), by
                   enqueueing the pair in the triggered check queue. */
                _cocheck_initiate(agent, p);
            }

            /* note: the spec says the we SHOULD retransmit in-progress
             *       checks immediately, but we won't do that now */

            return TRUE;
        }
    }

    for (i = comp->local_candidates; i ; i = i->next)
    {
        local = i->data;
        if (local->sockptr == local_socket)
            break;
    }

    if (i)
    {
        nice_debug("[%s]: Adding a triggered check to conn.check list (local=%p).", G_STRFUNC, local);
        _add_new_chk_pair(agent, stream->id, comp, local, remote_cand, NCHK_WAITING, use_candidate);
        return TRUE;
    }
    else
    {
        nice_debug("[%s]: Didn't find a matching pair for triggered check (remote-cand=%p).", G_STRFUNC, remote_cand);
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
static void _reply_to_cocheck(n_agent_t * agent, n_stream_t * stream, n_comp_t * component, n_cand_t * rcand, 
													const n_addr_t * toaddr, n_socket_t * sockptr, size_t  rbuf_len, uint8_t * rbuf, int use_candidate)
{
    g_assert(rcand == NULL || nice_address_equal(&rcand->addr, toaddr) == TRUE);

    if (nice_debug_is_enabled())
    {
        char tmpbuf[INET6_ADDRSTRLEN];
        nice_address_to_string(toaddr, tmpbuf);
        nice_debug("[%s]: stun-cc resp to '%s:%u', socket=%u, len=%u, cand=%p (c-id:%u), use-cand=%d.", G_STRFUNC,
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
        _schedule_triggered_check(agent, stream, component, sockptr, rcand, use_candidate);

        if (use_candidate)
            _mark_pair_nominated(agent, stream, component, rcand);
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
static int _store_pending_chk(n_agent_t * agent, n_comp_t * component,
                                    const n_addr_t * from, n_socket_t * sockptr, uint8_t * username,
                                    uint16_t username_len, uint32_t priority, int use_candidate)
{
    n_inchk_t * icheck;
    nice_debug("[%s]: storing pending check", G_STRFUNC);

    if (component->incoming_checks && n_slist_length(component->incoming_checks) >= MAX_REMOTE_CANDIDATES)
    {
        nice_debug("[%s]: warn: unable to store information for early incoming check", G_STRFUNC);
        return -1;
    }

    icheck = n_slice_new0(n_inchk_t);
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
static n_cand_chk_pair_t * _add_peer_reflexive_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t * l_cand, n_cand_chk_pair_t * parent_pair)
{
    n_cand_chk_pair_t * pair = n_slice_new0(n_cand_chk_pair_t);
    n_stream_t * stream = agent_find_stream(agent, stream_id);

    pair->agent = agent;
    pair->stream_id = stream_id;
    pair->component_id = component_id;;
    pair->local = l_cand;
    pair->remote = parent_pair->remote;
    pair->sockptr = l_cand->sockptr;
    pair->state = NCHK_DISCOVERED;
    nice_debug("[%s]: pair %p state DISCOVERED", G_STRFUNC, pair);
	nice_print_candpair(agent, pair);
    g_snprintf(pair->foundation, CAND_PAIR_MAX_FOUNDATION, "%s:%s", l_cand->foundation, parent_pair->remote->foundation);
    if (agent->controlling_mode == TRUE)
        pair->priority = n_cand_pair_priority(pair->local->priority, pair->remote->priority);
    else
        pair->priority = n_cand_pair_priority(pair->remote->priority, pair->local->priority);
    pair->nominated = FALSE;
    pair->controlling = agent->controlling_mode;
    nice_debug("[%s]: added a new peer-discovered pair with foundation of '%s'",  agent, pair->foundation);

    stream->conncheck_list = n_slist_insert_sorted(stream->conncheck_list, pair, (GCompareFunc)cocheck_compare);

    return pair;
}

/*
 * Recalculates priorities of all candidate pairs. This
 * is required after a conflict in ICE roles.
 */
static void _recalc_pair_priorities(n_agent_t * agent)
{
    n_slist_t * i, *j;

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * stream = i->data;
        for (j = stream->conncheck_list; j; j = j->next)
        {
            n_cand_chk_pair_t * p = j->data;
            p->priority = agent_candidate_pair_priority(agent, p->local, p->remote);
        }
    }
}

/*
 * Change the agent role if different from 'control'. Can be
 * initiated both by handling of incoming connectivity checks,
 * and by processing the responses to checks sent by us.
 */
static void _check_for_role_conflict(n_agent_t * agent, int control)
{
    /* role conflict, change mode; wait for a new conn. check */
    if (control != agent->controlling_mode)
    {
        nice_debug("[%s]: role conflict, changing agent role to %d", G_STRFUNC, control);
        agent->controlling_mode = control;
        /* the pair priorities depend on the roles, so recalculation
         * is needed */
        _recalc_pair_priorities(agent);
    }
    else
        nice_debug("[%s]: role conflict, agent role already changed to %d", G_STRFUNC, control);
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
static n_cand_chk_pair_t * _process_resp_chk_peer_reflexive(n_agent_t * agent, n_stream_t * stream, n_comp_t * component, n_cand_chk_pair_t * p,
        n_socket_t * sockptr, struct sockaddr * mapped_sockaddr, n_cand_t * local_candidate, n_cand_t * remote_candidate)
{
    n_cand_chk_pair_t * new_pair = NULL;
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
                n_cand_chk_pair_t * pair = i->data;
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
        p->state = NCHK_SUCCEEDED;
        nice_debug("[%s]: conncheck %p succeeded", G_STRFUNC, p);
        _cocheck_unfreeze_related(agent, stream, p);
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
        p->state = NCHK_FAILED;
        nice_debug("[%s]: pair %p state failed", G_STRFUNC, p);

        /* step: add a new discovered pair (see RFC 5245 7.1.3.2.2
               "Constructing a Valid Pair") */
        new_pair = _add_peer_reflexive_pair(agent, stream->id, component->id, cand, p);
        nice_debug("[%s]: conncheck %p failed, %p discovered", G_STRFUNC, p, new_pair);
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
static int _map_reply_to_cocheck_request(n_agent_t * agent, n_stream_t * stream, n_comp_t * component, n_socket_t * sockptr,
        const n_addr_t * from, n_cand_t * local_candidate, n_cand_t * remote_candidate, stun_msg_t * resp)
{
    union
    {
        struct sockaddr_storage storage;
        struct sockaddr addr;
    } sockaddr;
    socklen_t socklen = sizeof(sockaddr);
    n_slist_t * i;
    stun_ice_ret_e res;
    int trans_found = FALSE;
    stun_trans_id discovery_id;
    stun_trans_id response_id;
    stun_msg_id(resp, response_id);

    for (i = stream->conncheck_list; i && trans_found != TRUE; i = i->next)
    {
        n_cand_chk_pair_t * p = i->data;

        if (p->stun_message.buffer)
        {
            stun_msg_id(&p->stun_message, discovery_id);

            if (memcmp(discovery_id, response_id, sizeof(stun_trans_id)) == 0)
            {
                res = stun_ice_cocheck_process(resp, &sockaddr.storage, &socklen);
                nice_debug("[%s]: stun_bind_process/conncheck for %p res %d "
                           "(controlling=%d).", G_STRFUNC, p, (int)res, agent->controlling_mode);

                if (res == STUN_ICE_RET_SUCCESS ||
                        res == STUN_ICE_RET_NO_MAPPED_ADDRESS)
                {
                    /* case: found a matching connectivity check request */

                    n_cand_chk_pair_t * ok_pair = NULL;

                    nice_debug("[%s]: conncheck %p matched.", G_STRFUNC, p);
					nice_print_candpair(agent, p);
                    p->stun_message.buffer = NULL;
                    p->stun_message.buffer_len = 0;

                    /* step: verify that response came from the same IP address we
                     *       sent the original request to (see 7.1.2.1. "Failure
                     *       Cases") */
                    if (nice_address_equal(from, &p->remote->addr) != TRUE)
                    {
                        p->state = NCHK_FAILED;
                        if (nice_debug_is_enabled())
                        {
                            char tmpbuf[INET6_ADDRSTRLEN];
                            char tmpbuf2[INET6_ADDRSTRLEN];
                            nice_debug("[%s]: conncheck %p failed" " (mismatch of source address).", G_STRFUNC, p);
                            nice_address_to_string(&p->remote->addr, tmpbuf);
                            nice_address_to_string(from, tmpbuf2);
                            nice_debug("[%s]: '%s:%u' != '%s:%u'", G_STRFUNC,
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

                    if (res == STUN_ICE_RET_NO_MAPPED_ADDRESS)
                    {
                        /* note: this is same as "adding to VALID LIST" in the spec
                           text */
                        p->state = NCHK_SUCCEEDED;
                        nice_debug("[%s]: mapped address not found." " conncheck %p succeeded.", G_STRFUNC, p);
                        _cocheck_unfreeze_related(agent, stream, p);
                    }
                    else
                    {
                        ok_pair = _process_resp_chk_peer_reflexive(agent,
                                  stream, component, p, sockptr, &sockaddr.addr,
                                  local_candidate, remote_candidate);
                    }


                    if (!ok_pair)
                        ok_pair = p;

                    /* step: updating nominated flag (ICE 7.1.2.2.4 "Updating the
                       Nominated Flag" (ID-19) */
                    if (ok_pair->nominated == TRUE)
                    {
                        _update_selected_pair(agent, component, ok_pair);

                        /* Do not step down to CONNECTED if we're already at state READY*/
                        if (component->state != COMP_STATE_READY)
                        {
                            /* step: notify the client of a new component state (must be done
                             *       before the possible check list state update step */
                            agent_sig_comp_state_change(agent, stream->id, component->id, COMP_STATE_CONNECTED);
                        }
                    }

                    /* step: update pair states (ICE 7.1.2.2.3 "Updating pair
                       states" and 8.1.2 "Updating States", ID-19) */
                    _update_chk_list_state_for_ready(agent, stream, component);

                    trans_found = TRUE;
                }
                else if (res == STUN_ICE_RET_ROLE_CONFLICT)
                {
                    /* case: role conflict error, need to restart with new role */
                    nice_debug("[%s]: conncheck %p role conflict, restarting", G_STRFUNC, p);
                    /* note: our role might already have changed due to an
                     * incoming request, but if not, change role now;
                     * follows ICE 7.1.2.1 "Failure Cases" (ID-19) */
                    _check_for_role_conflict(agent, !p->controlling);

                    p->stun_message.buffer = NULL;
                    p->stun_message.buffer_len = 0;
                    p->state = NCHK_WAITING;
                    nice_debug("[%s]: pair %p state WAITING", G_STRFUNC, p);
                    trans_found = TRUE;
                }
                else
                {
                    /* case: STUN error, the check STUN context was freed */
                    nice_debug("[%s]: conncheck %p failed", G_STRFUNC, p);
                    p->stun_message.buffer = NULL;
                    p->stun_message.buffer_len = 0;
                    trans_found = TRUE;
                }
            }
        }
    }


    stream->conncheck_list =
        prune_cancelled_cocheck(stream->conncheck_list);

    return trans_found;
}

/*
 * Tries to match STUN reply in 'buf' to an existing STUN discovery
 * transaction. If found, a reply is sent.
 *
 * @return TRUE if a matching transaction is found
 */
static int _map_reply_disc_req(n_agent_t * agent, stun_msg_t * resp)
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
    stun_trans_id discovery_id;
    stun_trans_id response_id;
    stun_msg_id(resp, response_id);

    for (i = agent->discovery_list; i && trans_found != TRUE; i = i->next)
    {
        n_cand_disc_t * d = i->data;

        if (d->type == CAND_TYPE_SERVER && d->stun_message.buffer)
        {
            stun_msg_id(&d->stun_message, discovery_id);

            if (memcmp(discovery_id, response_id, sizeof(stun_trans_id)) == 0)
            {
                res = stun_bind_process(resp, &sockaddr.addr, &socklen, &alternate.addr, &alternatelen);
                nice_debug("[%s]: stun_bind_process/disc for %p res %d.", G_STRFUNC, d, (int)res);

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


static n_cand_refresh_t * _add_new_turn_refresh(n_cand_disc_t * cdisco, n_cand_t * relay_cand, uint32_t lifetime)
{
    n_cand_refresh_t * cand;
    n_agent_t * agent = cdisco->agent;

    cand = n_slice_new0(n_cand_refresh_t);
    agent->refresh_list = n_slist_append(agent->refresh_list, cand);

    cand->candidate = relay_cand;
    cand->nicesock = cdisco->nicesock;
    cand->server = cdisco->server;
    cand->stream = cdisco->stream;
    cand->component = cdisco->component;
    cand->agent = cdisco->agent;
    memcpy(&cand->stun_agent, &cdisco->stun_agent, sizeof(stun_agent_t));

    /* Use previous stun response for authentication credentials */
    if (cdisco->stun_resp_msg.buffer != NULL)
    {
        memcpy(cand->stun_resp_buffer, cdisco->stun_resp_buffer, sizeof(cand->stun_resp_buffer));
        memcpy(&cand->stun_resp_msg, &cdisco->stun_resp_msg, sizeof(stun_msg_t));
        cand->stun_resp_msg.buffer = cand->stun_resp_buffer;
        cand->stun_resp_msg.agent = NULL;
        cand->stun_resp_msg.key = NULL;
    }

    nice_debug("[%s]: Adding new refresh candidate %p with timeout %d", G_STRFUNC, cand, (lifetime - 60) * 1000);

    /* step: also start the refresh timer */
    /* refresh should be sent 1 minute before it expires */
    agent_timeout_add(agent, &cand->timer_source, "Candidate TURN refresh",
                                   (lifetime - 60) * 1000, _turn_allocate_refresh_tick, cand);

    nice_debug("timer source is : %p", cand->timer_source);

    return cand;
}

/*
 * Tries to match STUN reply in 'buf' to an existing STUN discovery
 * transaction. If found, a reply is sent.
 *
 * @return TRUE if a matching transaction is found
 */
static int _map_reply_to_relay_request(n_agent_t * agent, stun_msg_t * resp)
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
    stun_trans_id discovery_id;
    stun_trans_id response_id;
    stun_msg_id(resp, response_id);

    for (i = agent->discovery_list; i && trans_found != TRUE; i = i->next)
    {
        n_cand_disc_t * d = i->data;

        if (d->type == CAND_TYPE_RELAYED && d->stun_message.buffer)
        {
            stun_msg_id(&d->stun_message, discovery_id);

            if (memcmp(discovery_id, response_id, sizeof(stun_trans_id)) == 0)
            {
                res = stun_usage_turn_process(resp,
                                              &relayaddr.storage, &relayaddrlen,
                                              &sockaddr.storage, &socklen,
                                              &alternate.storage, &alternatelen,
                                              &bandwidth, &lifetime, agent_to_turn_compatibility(agent));
                nice_debug("[%s]: stun_turn_process/disc for %p res %d", G_STRFUNC, d, (int)res);

                if (res == STUN_TURN_RET_ALTERNATE_SERVER)
                {
                    /* handle alternate server */
                    n_addr_set_from_sock(&d->server, &alternate.addr);
                    n_addr_set_from_sock(&d->turn->server, &alternate.addr);

                    d->pending = FALSE;
                }
                else if (res == STUN_TURN_RET_RELAY_SUCCESS ||
                         res == STUN_TURN_RET_MAPPED_SUCCESS)
                {
                    /* case: successful allocate, create a new local candidate */
                    n_addr_t niceaddr;
                    n_cand_t * relay_cand;

                    if (res == STUN_TURN_RET_MAPPED_SUCCESS)
                    {
                        /* We also received our mapped address */
                        n_addr_set_from_sock(&niceaddr, &sockaddr.addr);                        
                        disc_add_server_cand(d->agent, d->stream->id, d->component->id, &niceaddr, d->nicesock);
                    }

                    n_addr_set_from_sock(&niceaddr, &relayaddr.addr);
                    relay_cand = disc_add_relay_cand(d->agent, d->stream->id, d->component->id, &niceaddr, d->nicesock, d->turn);
                    if (relay_cand)
                    {
                           _add_new_turn_refresh(d, relay_cand, lifetime);
                    }

                    d->stun_message.buffer = NULL;
                    d->stun_message.buffer_len = 0;
                    d->done = TRUE;
                    trans_found = TRUE;
                }
                else if (res == STUN_TURN_RET_ERROR)
                {
                    int code = -1;
                    uint8_t * sent_realm = NULL;
                    uint8_t * recv_realm = NULL;
                    uint16_t sent_realm_len = 0;
                    uint16_t recv_realm_len = 0;

                    sent_realm = (uint8_t *) stun_msg_find(&d->stun_message, STUN_ATT_REALM, &sent_realm_len);
                    recv_realm = (uint8_t *) stun_msg_find(resp, STUN_ATT_REALM, &recv_realm_len);

                    /* check for unauthorized error response */
                    if (stun_msg_get_class(resp) == STUN_ERROR &&
                            stun_msg_find_error(resp, &code) == STUN_MSG_RET_SUCCESS &&
                            recv_realm != NULL && recv_realm_len > 0)
                    {
                        if (code == 438 || (code == 401 && !(recv_realm_len == sent_realm_len && sent_realm != NULL &&
                                                             memcmp(sent_realm, recv_realm, sent_realm_len) == 0)))
                        {
                            d->stun_resp_msg = *resp;
                            memcpy(d->stun_resp_buffer, resp->buffer, stun_msg_len(resp));
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
static int _map_reply_to_relay_refresh(n_agent_t * agent, stun_msg_t * resp)
{
    uint32_t lifetime;
    n_slist_t * i;
    StunUsageTurnReturn res;
    int trans_found = FALSE;
    stun_trans_id refresh_id;
    stun_trans_id response_id;
    stun_msg_id(resp, response_id);

    for (i = agent->refresh_list; i && trans_found != TRUE; i = i->next)
    {
        n_cand_refresh_t * cand = i->data;

        if (cand->stun_message.buffer)
        {
            stun_msg_id(&cand->stun_message, refresh_id);

            if (memcmp(refresh_id, response_id, sizeof(stun_trans_id)) == 0)
            {
                res = stun_usage_turn_refresh_process(resp, &lifetime, agent_to_turn_compatibility(cand->agent));
                nice_debug("[%s]: stun_turn_refresh_process for %p res %d", G_STRFUNC, cand, (int)res);
                if (res == STUN_TURN_RET_RELAY_SUCCESS)
                {
                    /* refresh should be sent 1 minute before it expires */
                    agent_timeout_add(cand->agent, &cand->timer_source,
                                                   "Candidate TURN refresh", (lifetime - 60) * 1000,
                                                   _turn_allocate_refresh_tick, cand);

                    g_source_destroy(cand->tick_source);
                    g_source_unref(cand->tick_source);
                    cand->tick_source = NULL;
                }
                else if (res == STUN_TURN_RET_ERROR)
                {
                    int code = -1;
                    uint8_t * sent_realm = NULL;
                    uint8_t * recv_realm = NULL;
                    uint16_t sent_realm_len = 0;
                    uint16_t recv_realm_len = 0;

                    sent_realm = (uint8_t *) stun_msg_find(&cand->stun_message, STUN_ATT_REALM, &sent_realm_len);
                    recv_realm = (uint8_t *) stun_msg_find(resp, STUN_ATT_REALM, &recv_realm_len);

                    /* check for unauthorized error response */
                    if (stun_msg_get_class(resp) == STUN_ERROR &&
                            stun_msg_find_error(resp, &code) == STUN_MSG_RET_SUCCESS &&
                            recv_realm != NULL && recv_realm_len > 0)
                    {

                        if (code == 438 || (code == 401 && !(recv_realm_len == sent_realm_len && sent_realm != NULL &&
                                   memcmp(sent_realm, recv_realm, sent_realm_len) == 0)))
                        {
                            cand->stun_resp_msg = *resp;
                            memcpy(cand->stun_resp_buffer, resp->buffer, stun_msg_len(resp));
                            cand->stun_resp_msg.buffer = cand->stun_resp_buffer;
                            cand->stun_resp_msg.buffer_len = sizeof(cand->stun_resp_buffer);
                            _turn_alloc_refresh_tick_unlocked(cand);
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


static int _map_reply_to_keepalive_cocheck(n_agent_t * agent, n_comp_t * component, stun_msg_t * resp)
{
    stun_trans_id conncheck_id;
    stun_trans_id response_id;
    stun_msg_id(resp, response_id);

    if (component->selected_pair.keepalive.stun_message.buffer)
    {
        stun_msg_id(&component->selected_pair.keepalive.stun_message,  conncheck_id);
        if (memcmp(conncheck_id, response_id, sizeof(stun_trans_id)) == 0)
        {
            nice_debug("[%s]: keepalive for selected pair received", G_STRFUNC);
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
    n_stream_t * stream;
    n_comp_t * component;
    uint8_t * password;
} cocheck_validater_data_t;

static bool cocheck_stun_validater(stun_agent_t * agent,
                                     stun_msg_t * message, uint8_t * username, uint16_t username_len,
                                     uint8_t ** password, size_t * password_len, void * user_data)
{
    cocheck_validater_data_t * data = (cocheck_validater_data_t *) user_data;
    n_slist_t * i;
    char * ufrag = NULL;
    uint32_t ufrag_len;

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

        if (ufrag == NULL)
            continue;

        stun_debug("comparing username/ufrag of len %d and %zu, equal=%d",
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
			}

            stun_debug("Found valid username, returning password: '%s'", *password);
            return TRUE;
        }
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
int cocheck_handle_in_stun(n_agent_t * agent, n_stream_t * stream,
                                        n_comp_t * comp, n_socket_t * nicesock, const n_addr_t * from,
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
    //uint8_t uname[N_STREAM_MAX_UNAME];
    //uint32_t uname_len;
    uint8_t * username;
    uint16_t username_len;
    stun_msg_t req;
    stun_msg_t msg;
    stun_valid_status_e valid;
    cocheck_validater_data_t validater_data = {agent, stream, comp, NULL};
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
        nice_debug("[%s]: inbound stun packet for %u/%u (stream/component) from [%s]:%u (%u octets) :",
					G_STRFUNC, stream->id, comp->id, tmpbuf, nice_address_get_port(from), len);
    }

    /* note: ICE  7.2. "STUN Server Procedures" (ID-19) */

    valid = stun_agent_validate(&comp->stun_agent, &req, (uint8_t *) buf, len);

    /* Check for discovery candidates stun agents */
    if (valid == STUN_VALIDATION_BAD_REQUEST || valid == STUN_VALIDATION_UNMATCHED_RESPONSE)
    {
        for (i = agent->discovery_list; i; i = i->next)
        {
            n_cand_disc_t * d = i->data;
            if (d->stream == stream && d->component == comp && d->nicesock == nicesock)
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
            n_cand_refresh_t * r = i->data;
            nice_debug("comparing %p to %p, %p to %p and %p and %p to %p", r->stream,
                       stream, r->component, comp, r->nicesock, r->candidate->sockptr,  nicesock);
            if (r->stream == stream && r->component == comp && (r->nicesock == nicesock || r->candidate->sockptr == nicesock))
            {
                valid = stun_agent_validate(&r->stun_agent, &req, (uint8_t *) buf, len);
                nice_debug("validating gave %d", valid);
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
        nice_debug("[%s]: incorrectly multiplexed STUN message ignored", G_STRFUNC);
        return FALSE;
    }

    if (valid == STUN_VALIDATION_UNKNOWN_REQUEST_ATTRIBUTE)
    {
        nice_debug("[%s]: unknown mandatory attributes in message", G_STRFUNC);

        rbuf_len = stun_agent_build_unknown_attributes_error(&comp->stun_agent, &msg, rbuf, rbuf_len, &req);
        if (rbuf_len != 0)
            agent_socket_send(nicesock, from, rbuf_len, (const char *)rbuf);

        return TRUE;
    }

    if (valid == STUN_VALIDATION_UNAUTHORIZED)
    {
        nice_debug("[%s]: integrity check failed", G_STRFUNC);

        if (stun_agent_init_error(&comp->stun_agent, &msg, rbuf, rbuf_len, &req, STUN_ERROR_UNAUTHORIZED))
        {
            rbuf_len = stun_agent_finish_message(&comp->stun_agent, &msg, NULL, 0);
            if (rbuf_len > 0)
                agent_socket_send(nicesock, from, rbuf_len, (const char *)rbuf);
        }
        return TRUE;
    }
    if (valid == STUN_VALIDATION_UNAUTHORIZED_BAD_REQUEST)
    {
        nice_debug("[%s]: integrity check failed - bad request", G_STRFUNC);
        if (stun_agent_init_error(&comp->stun_agent, &msg, rbuf, rbuf_len, &req, STUN_ERROR_BAD_REQUEST))
        {
            rbuf_len = stun_agent_finish_message(&comp->stun_agent, &msg, NULL, 0);
            if (rbuf_len > 0)
                agent_socket_send(nicesock, from, rbuf_len, (const char *)rbuf);
        }
        return TRUE;
    }

    username = (uint8_t *) stun_msg_find(&req, STUN_ATT_USERNAME, &username_len);

    for (i = comp->remote_candidates; i; i = i->next)
    {
        n_cand_t * cand = i->data;
        if (nice_address_equal(from, &cand->addr))
        {
            remote_candidate = cand;
            break;
        }
    }
    for (i = comp->local_candidates; i; i = i->next)
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
        nice_debug("[%s]: STUN message is unsuccessfull %d, ignoring", G_STRFUNC, valid);
        return FALSE;
    }

    if (stun_msg_get_class(&req) == STUN_REQUEST)
    {
        rbuf_len = sizeof(rbuf);
        res = stun_ice_cocheck_create_reply(&comp->stun_agent, &req,
                &msg, rbuf, &rbuf_len, &sockaddr.storage, sizeof(sockaddr),  &control, agent->tie_breaker);

        if (res == STUN_ICE_RET_ROLE_CONFLICT)
            _check_for_role_conflict(agent, control);

        if (res == STUN_ICE_RET_SUCCESS ||
                res == STUN_ICE_RET_ROLE_CONFLICT)
        {
            /* case 1: valid incoming request, send a reply/error */
            int use_candidate = stun_ice_cocheck_use_cand(&req);
            uint32_t priority = stun_ice_cocheck_priority(&req);

            if (agent->controlling_mode)
                use_candidate = TRUE;

            if (stream->initial_binding_request_received != TRUE)
                agent_sig_initial_binding_request_received(agent, stream);

            if (comp->remote_candidates && remote_candidate == NULL)
            {
                nice_debug("[%s]: No matching remote candidate for incoming check ->" "peer-reflexive candidate.", agent);
                remote_candidate = disc_learn_remote_peer_cand(
                                       agent, stream, comp, priority, from, nicesock,
                                       local_candidate,
                                       remote_candidate2 ? remote_candidate2 : remote_candidate);
                if (remote_candidate)
                {                    
					cocheck_add_cand(agent, stream->id, comp, remote_candidate);
                }
            }

            _reply_to_cocheck(agent, stream, comp, remote_candidate,  from, nicesock, rbuf_len, rbuf, use_candidate);

            if (comp->remote_candidates == NULL)
            {
                /* case: We've got a valid binding request to a local candidate
                 *       but we do not yet know remote credentials nor
                 *       candidates. As per sect 7.2 of ICE (ID-19), we send a reply
                 *       immediately but postpone all other processing until
                 *       we get information about the remote candidates */

                /* step: send a reply immediately but postpone other processing */
                _store_pending_chk(agent, comp, from, nicesock, username, username_len, priority, use_candidate);
            }
        }
        else
        {
            nice_debug("[%s]: invalid stun packet, ignoring... %s", G_STRFUNC, strerror(errno));
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
            trans_found = _map_reply_to_cocheck_request(agent, stream, comp, nicesock, from, local_candidate, remote_candidate, &req);

        /* step: let's try to match the response to an existing discovery */
        if (trans_found != TRUE)
            trans_found = _map_reply_disc_req(agent, &req);

        /* step: let's try to match the response to an existing turn allocate */
        if (trans_found != TRUE)
            trans_found = _map_reply_to_relay_request(agent, &req);

        /* step: let's try to match the response to an existing turn refresh */
        if (trans_found != TRUE)
            trans_found = _map_reply_to_relay_refresh(agent, &req);

        /* step: let's try to match the response to an existing keepalive conncheck */
        if (trans_found != TRUE)
            trans_found = _map_reply_to_keepalive_cocheck(agent, comp, &req);

        if (trans_found != TRUE)
            nice_debug("[%s]: unable to match to an existing transaction, " "probably a keepalive", G_STRFUNC);
    }

    return TRUE;
}

/* Remove all pointers to the given @sock from the connection checking process.
 * These are entirely NiceCandidates pointed to from various places. */
void cocheck_prune_socket(n_agent_t * agent, n_stream_t * stream, n_comp_t * comp, n_socket_t * sock)
{
    n_slist_t * l;

    if (comp->selected_pair.local && comp->selected_pair.local->sockptr == sock && comp->state == COMP_STATE_READY)
    {
        nice_debug("[%s]: Selected pair socket %p has been destroyed, " "declaring failed", G_STRFUNC, sock);
        agent_sig_comp_state_change(agent, stream->id, comp->id, COMP_STATE_FAILED);
    }

    /* Prune from the candidate check pairs. */
    for (l = stream->conncheck_list; l != NULL; l = l->next)
    {
        n_cand_chk_pair_t * p = l->data;

        if ((p->local != NULL && p->local->sockptr == sock) ||
                (p->remote != NULL && p->remote->sockptr == sock))
        {
            nice_debug("[%s]: Retransmissions failed, giving up on " "connectivity check %p", G_STRFUNC, p);
            cand_chk_pair_fail(stream, agent, p);
        }
    }
}
