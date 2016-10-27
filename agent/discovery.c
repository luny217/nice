/* This file is part of the Nice GLib ICE library. */

/*
 * @file discovery.c
 * @brief ICE candidate discovery functions
 */

#include <config.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "base.h"
#include "debug.h"
#include "agent.h"
#include "agent-priv.h"
#include "component.h"
#include "discovery.h"
#include "stun/usages/bind.h"
#include "stun/usages/turn.h"
#include "socket.h"
#include "timer.h"

static inline int _timer_expired(n_timeval_t * timer, n_timeval_t * now)
{
    return (now->tv_sec == timer->tv_sec) ?
           now->tv_usec >= timer->tv_usec :
           now->tv_sec >= timer->tv_sec;
}

/*
 * Frees the n_cand_disc_t structure pointed to
 * by 'user data'. Compatible with g_slist_free_full().
 */
static void disc_free_item(n_cand_disc_t * cand)
{
    if (cand->turn)
        turn_server_unref(cand->turn);

    g_slice_free(n_cand_disc_t, cand);
}

/*
 * Frees all discovery related resources for the agent.
 */
void disc_free(n_agent_t * agent)
{
    n_slist_free_full(agent->discovery_list, (n_destroy_notify) disc_free_item);
    agent->discovery_list = NULL;
    agent->disc_unsched_items = 0;

   /* if (agent->disc_timer_source != NULL)
    {
        g_source_destroy(agent->disc_timer_source);
        g_source_unref(agent->disc_timer_source);
        agent->disc_timer_source = NULL;
    }*/

	timer_stop(agent->disc_timer);
	timer_destroy(agent->disc_timer);
	agent->disc_timer = 0;
}

/*
 * Prunes the list of discovery processes for items related
 * to stream 'stream_id'.
 */
void disc_prune_stream(n_agent_t * agent, uint32_t stream_id)
{
    n_slist_t  * i;

    for (i = agent->discovery_list; i ;)
    {
        n_cand_disc_t * cand = i->data;
        n_slist_t  * next = i->next;

        if (cand->stream->id == stream_id)
        {
            agent->discovery_list = n_slist_remove(agent->discovery_list, cand);
            disc_free_item(cand);
        }
        i = next;
    }

    if (agent->discovery_list == NULL)
    {
        /* noone using the timer anymore, clean it up */
        disc_free(agent);
    }
}

/*
 * Prunes the list of discovery processes for items related
 * to socket @sock.
 */
void disc_prune_socket(n_agent_t * agent, n_socket_t * sock)
{
    n_slist_t  * i;

    for (i = agent->discovery_list; i ;)
    {
        n_cand_disc_t * discovery = i->data;
        n_slist_t  * next = i->next;

        if (discovery->nicesock == sock)
        {
            agent->discovery_list = n_slist_remove(agent->discovery_list, discovery);
            disc_free_item(discovery);
        }
        i = next;
    }

    if (agent->discovery_list == NULL)
    {
        /* noone using the timer anymore, clean it up */
        disc_free(agent);
    }
}


/*
 * Frees the n_cand_disc_t structure pointed to
 * by 'user data'. Compatible with n_slist_free_full().
 */
static void refresh_free_item(n_cand_refresh_t * cand)
{
    n_agent_t * agent = cand->agent;
    uint8_t * username;
    uint32_t username_len;
    uint8_t * password;
    uint32_t password_len;
    size_t buffer_len = 0;

    if (cand->timer_clock != 0)
    {
        /*g_source_destroy(cand->timer_source);
        g_source_unref(cand->timer_source);
        cand->timer_source = NULL;*/

		timer_stop(cand->timer_clock);
		timer_destroy(cand->timer_clock);
		cand->timer_clock = 0;
    }
    if (cand->tick_clock != 0)
    {
        /*g_source_destroy(cand->tick_source);
        g_source_unref(cand->tick_source);
        cand->tick_source = NULL;*/
		timer_stop(cand->tick_clock);
		timer_destroy(cand->tick_clock);
		cand->tick_clock = 0;
    }

    username = (uint8_t *)cand->candidate->turn->username;
    username_len = (size_t) strlen(cand->candidate->turn->username);
    password = (uint8_t *)cand->candidate->turn->password;
    password_len = (size_t) strlen(cand->candidate->turn->password);

    buffer_len = turn_create_refresh(&cand->stun_agent,
                 &cand->stun_message,  cand->stun_buffer, sizeof(cand->stun_buffer),
                 cand->stun_resp_msg.buffer == NULL ? NULL : &cand->stun_resp_msg, 0,
                 username, username_len, password, password_len);

    if (buffer_len > 0)
    {
        stun_trans_id id;

        /* forget the transaction since we don't care about the result and
         * we don't implement retransmissions/timeout */
        stun_msg_id(&cand->stun_message, id);
        stun_agent_forget_trans(&cand->stun_agent, id);

        /* send the refresh twice since we won't do retransmissions */
        agent_socket_send(cand->nicesock, &cand->server, buffer_len, (gchar *)cand->stun_buffer);
        if (!nice_socket_is_reliable(cand->nicesock))
        {
            agent_socket_send(cand->nicesock, &cand->server, buffer_len, (gchar *)cand->stun_buffer);
        }

    }

    n_slice_free(n_cand_refresh_t, cand);
}

/*
 * Frees all discovery related resources for the agent.
 */
void refresh_free(n_agent_t * agent)
{
    n_slist_free_full(agent->refresh_list, (n_destroy_notify) refresh_free_item);
    agent->refresh_list = NULL;
}

/*
 * Prunes the list of discovery processes for items related
 * to stream 'stream_id'.
 *
 * @return TRUE on success, FALSE on a fatal error
 */
void refresh_prune_stream(n_agent_t * agent, uint32_t stream_id)
{
    n_slist_t  * i;

    for (i = agent->refresh_list; i ;)
    {
        n_cand_refresh_t * cand = i->data;
        n_slist_t  * next = i->next;

        /* Don't free the candidate refresh to the currently selected local candidate
         * unless the whole pair is being destroyed.
         */
        if (cand->stream->id == stream_id)
        {
            agent->refresh_list = n_slist_delete_link(agent->refresh_list, i);
            refresh_free_item(cand);
        }

        i = next;
    }
}

void refresh_prune_candidate(n_agent_t * agent, n_cand_t * candidate)
{
    n_slist_t  * i;

    for (i = agent->refresh_list; i;)
    {
        n_slist_t  * next = i->next;
        n_cand_refresh_t * refresh = i->data;

        if (refresh->candidate == candidate)
        {
            agent->refresh_list = n_slist_delete_link(agent->refresh_list, i);
            refresh_free_item(refresh);
        }

        i = next;
    }
}

void refresh_prune_socket(n_agent_t * agent, n_socket_t * sock)
{
    n_slist_t  * i;

    for (i = agent->refresh_list; i;)
    {
        n_slist_t  * next = i->next;
        n_cand_refresh_t * refresh = i->data;

        if (refresh->nicesock == sock)
        {
            agent->refresh_list = n_slist_delete_link(agent->refresh_list, i);
            refresh_free_item(refresh);
        }

        i = next;
    }
}

void refresh_cancel(n_cand_refresh_t * refresh)
{
    refresh->agent->refresh_list = n_slist_remove(refresh->agent->refresh_list, refresh);
    refresh_free_item(refresh);
}


/*
 * Adds a new local candidate. Implements the candidate pruning
 * defined in ICE spec section 4.1.3 "Eliminating Redundant
 * Candidates" (ID-19).
 */
static int _add_local_cand_pruned(n_agent_t * agent, uint32_t stream_id, n_comp_t * component, n_cand_t * candidate)
{
    n_slist_t  * i;

    g_assert(candidate != NULL);

    for (i = component->local_candidates; i ; i = i->next)
    {
        n_cand_t * c = i->data;

        if (nice_address_equal(&c->base_addr, &candidate->base_addr) &&
                nice_address_equal(&c->addr, &candidate->addr) &&
                c->transport == candidate->transport)
        {
            nice_debug("[%s]: Candidate %p (component-id %u) redundant, ignoring.", G_STRFUNC, candidate, component->id);
			nice_print_cand(agent, candidate, candidate);
            return FALSE;
        }
    }

    component->local_candidates = n_slist_append(component->local_candidates, candidate);
    cocheck_add_local_cand(agent, stream_id, component, candidate);

    return TRUE;
}

static uint32_t _highest_remote_foundation(n_comp_t * component)
{
    n_slist_t  * i;
    uint32_t highest = 1;
    char foundation[CAND_MAX_FOUNDATION];

    for (highest = 1;; highest++)
    {
        int taken = FALSE;

        g_snprintf(foundation, CAND_MAX_FOUNDATION, "remote-%u",  highest);
        for (i = component->remote_candidates; i; i = i->next)
        {
            n_cand_t * cand = i->data;
            if (strncmp(foundation, cand->foundation, CAND_MAX_FOUNDATION) == 0)
            {
                taken = TRUE;
                break;
            }
        }
        if (!taken)
            return highest;
    }

    g_return_val_if_reached(highest);
}

/* From RFC 5245 section 4.1.3:
 *
 *   for reflexive and relayed candidates, the STUN or TURN servers
 *   used to obtain them have the same IP address.
 */
static int _compare_turn_servers(TurnServer * turn1, TurnServer * turn2)
{
    if (turn1 == turn2)
        return TRUE;
    if (turn1 == NULL || turn2 == NULL)
        return FALSE;

    return nice_address_equal_no_port(&turn1->server, &turn2->server);
}

/*
 * Assings a foundation to the candidate.
 *
 * Implements the mechanism described in ICE sect
 * 4.1.1.3 "Computing Foundations" (ID-19).
 */
static void _assign_foundation(n_agent_t * agent, n_cand_t * cand)
{
    n_slist_t  * i, *j, *k;

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * stream = i->data;
        for (j = stream->components; j; j = j->next)
        {
            n_comp_t * comp= j->data;
            for (k = comp->local_candidates; k; k = k->next)
            {
                n_cand_t * n = k->data;

                /* note: candidate must not on the local candidate list */
                //g_assert(cand != n);

                if (cand->type == n->type && cand->transport == n->transport &&
					cand->stream_id == n->stream_id && nice_address_equal_no_port(&cand->base_addr, &n->base_addr) &&
                   (cand->type != CAND_TYPE_RELAYED || _compare_turn_servers(cand->turn, n->turn)))
                {
                    /* note: currently only one STUN server per stream at a
                     *       time is supported, so there is no need to check
                     *       for candidates that would otherwise share the
                     *       foundation, but have different STUN servers */
                    strncpy(cand->foundation, n->foundation, CAND_MAX_FOUNDATION);
                    if (n->username)
                    {
                        n_free(cand->username);
						cand->username = n_strdup(n->username);
                    }
                    if (n->password)
                    {
                        n_free(cand->password);
						cand->password = n_strdup(n->password);
                    }
                    return;
                }
            }
        }
    }

    snprintf(cand->foundation, CAND_MAX_FOUNDATION, "%u", agent->next_candidate_id++);
}

static void _assign_remote_foundation(n_agent_t * agent, n_cand_t * candidate)
{
    n_slist_t  * i, *j, *k;
    uint32_t next_remote_id;
    n_comp_t * component = NULL;

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * stream = i->data;
        for (j = stream->components; j; j = j->next)
        {
            n_comp_t * c = j->data;

            if (c->id == candidate->component_id)
                component = c;

            for (k = c->remote_candidates; k; k = k->next)
            {
                n_cand_t * n = k->data;

                /* note: candidate must not on the remote candidate list */
                g_assert(candidate != n);

                if (candidate->type == n->type &&
                        candidate->transport == n->transport &&
                        candidate->stream_id == n->stream_id &&
                        nice_address_equal_no_port(&candidate->addr, &n->addr))
                {
                    /* note: No need to check for STUN/TURN servers, as these candidate
                         * will always be peer reflexive, never relayed or serve reflexive.
                         */
                    g_strlcpy(candidate->foundation, n->foundation, CAND_MAX_FOUNDATION);
                    if (n->username)
                    {
                        n_free(candidate->username);
                        candidate->username = g_strdup(n->username);
                    }
                    if (n->password)
                    {
                        n_free(candidate->password);
                        candidate->password = g_strdup(n->password);
                    }
                    return;
                }
            }
        }
    }

    if (component)
    {
        next_remote_id = _highest_remote_foundation(component);
        g_snprintf(candidate->foundation, CAND_MAX_FOUNDATION, "remote-%u", next_remote_id);
    }
}

 /* 为 stream_id 的 component_id 创建一个本地主机候选地址
 *  成功输出候选指针, 失败为NULL*/

HostCandidateResult disc_add_local_host_cand(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, n_addr_t * address, n_cand_t ** outcandidate)
{
    n_cand_t * candidate;
    n_comp_t * comp;
    n_stream_t * stream;
    n_socket_t * nicesock = NULL;
    HostCandidateResult res = CANDIDATE_FAILED;

    if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
        return res;

    candidate = n_cand_new(CAND_TYPE_HOST);
    candidate->transport = CAND_TRANS_UDP;
    candidate->stream_id = stream_id;
    candidate->component_id = comp_id;
    candidate->addr = *address;
    candidate->base_addr = *address;
    candidate->priority = n_cand_ice_priority(candidate);

    //_generate_cand_cred(agent, candidate);
    _assign_foundation(agent, candidate);

    nicesock = n_udp_socket_new(address);
    if (!nicesock)
    {
        res = CANDIDATE_CANT_CREATE_SOCKET;
        goto errors;
    }

    candidate->sockptr = nicesock;
    candidate->addr = nicesock->addr;
    candidate->base_addr = nicesock->addr;

    if (!_add_local_cand_pruned(agent, stream_id, comp, candidate))
    {
        res = CANDIDATE_REDUNDANT;
        goto errors;
    }

    _set_socket_tos(agent, nicesock, stream->tos);
    comp_attach_socket(comp, nicesock);

    *outcandidate = candidate;

    return CANDIDATE_SUCCESS;

errors:
    n_cand_free(candidate);
    if (nicesock)
        nice_socket_free(nicesock);
    return res;
}

/*
 * Creates a server reflexive candidate for 'component_id' of stream
 * 'stream_id'.
 *
 * @return pointer to the created candidate, or NULL on error
 */
n_cand_t * disc_add_server_cand(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, n_addr_t * address, n_socket_t * base_socket)
{
    n_cand_t * candidate;
    n_comp_t * comp;
    n_stream_t * stream;
    int result = FALSE;

    if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
        return NULL;

    candidate = n_cand_new(CAND_TYPE_SERVER);
	candidate->transport = CAND_TRANS_UDP;
    candidate->stream_id = stream_id;
    candidate->component_id = comp_id;
    candidate->addr = *address;
    candidate->priority =  n_cand_ice_priority(candidate);

    /* step: link to the base candidate+socket */
    candidate->sockptr = base_socket;
    candidate->base_addr = base_socket->addr;

    //_generate_cand_cred(agent, candidate);
    _assign_foundation(agent, candidate);

    result = _add_local_cand_pruned(agent, stream_id, comp, candidate);
    if (result)
    {
        //agent_sig_new_cand(agent, candidate);
    }
    else
    {
        /* error: duplicate candidate */
        n_cand_free(candidate), candidate = NULL;
    }

    return candidate;
}

/*
 * Creates a server reflexive candidate for 'component_id' of stream
 * 'stream_id'.
 *
 * @return pointer to the created candidate, or NULL on error
 */
n_cand_t * disc_add_relay_cand(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, n_addr_t * address, n_socket_t * base_socket, TurnServer * turn)
{
    n_cand_t * candidate;
    n_comp_t * comp;
    n_stream_t * stream;
    n_socket_t * relay_socket = NULL;

    if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
        return NULL;

    candidate = n_cand_new(CAND_TYPE_RELAYED);
    candidate->transport = CAND_TRANS_UDP;
    candidate->stream_id = stream_id;
    candidate->component_id = comp_id;
    candidate->addr = *address;
    candidate->turn = turn_server_ref(turn);
    candidate->priority =  n_cand_ice_priority(candidate);

    /* step: link to the base candidate+socket */
    relay_socket = n_udp_turn_new(NULL, address, base_socket, &turn->server, turn->username, turn->password);
    if (!relay_socket)
        goto errors;

    candidate->sockptr = relay_socket;
    candidate->base_addr = base_socket->addr;

    //_generate_cand_cred(agent, candidate);
    _assign_foundation(agent, candidate);

    if (!_add_local_cand_pruned(agent, stream_id, comp, candidate))
        goto errors;

    comp_attach_socket(comp, relay_socket);
    agent_sig_new_cand(agent, candidate);

    return candidate;

errors:
    n_cand_free(candidate);
    if (relay_socket)
        nice_socket_free(relay_socket);
    return NULL;
}

/*
 * Creates a peer reflexive candidate for 'component_id' of stream
 * 'stream_id'.
 *
 * @return pointer to the created candidate, or NULL on error
 */
n_cand_t * disc_add_peer_cand(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, 
												n_addr_t * address, n_socket_t * base_socket,  n_cand_t * local, n_cand_t * remote)
{
    n_cand_t * candidate;
    n_comp_t * comp;
    n_stream_t * stream;
    int result;

    if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
        return NULL;

    candidate = n_cand_new(CAND_TYPE_PEER);

	candidate->transport = CAND_TRANS_UDP;
    candidate->stream_id = stream_id;
    candidate->component_id = comp_id;
    candidate->addr = *address;
    candidate->sockptr = base_socket;
    candidate->base_addr = base_socket->addr;
    candidate->priority = n_cand_ice_priority(candidate);

    _assign_foundation(agent, candidate);

	if (local)
    {
        n_free(candidate->username);
        n_free(candidate->password);

        candidate->username = g_strdup(local->username);
        candidate->password = g_strdup(local->password);
    }

    result = _add_local_cand_pruned(agent, stream_id, comp, candidate);
    if (result != TRUE)
    {
        /* error: memory allocation, or duplicate candidate */
        n_cand_free(candidate), candidate = NULL;
    }

    return candidate;
}


/*
 * Adds a new peer reflexive candidate to the list of known
 * remote candidates. The candidate is however not paired with
 * existing local candidates.
 *
 * See ICE sect 7.2.1.3 "Learning Peer Reflexive Candidates" (ID-19).
 *
 * @return pointer to the created candidate, or NULL on error
 */
n_cand_t * disc_learn_remote_peer_cand(
    n_agent_t * agent,
    n_stream_t * stream,
    n_comp_t * component,
    uint32_t priority,
    const n_addr_t * remote_address,
    n_socket_t * nicesock,
    n_cand_t * local,
    n_cand_t * remote)
{
    n_cand_t * candidate;

    candidate = n_cand_new(CAND_TYPE_PEER);

    candidate->addr = *remote_address;
    candidate->base_addr = *remote_address;
    if (remote)
        candidate->transport = remote->transport;
    else if (local)
        candidate->transport = cocheck_match_trans(local->transport);
    else
    {       
		candidate->transport = CAND_TRANS_UDP;        
    }
    candidate->sockptr = nicesock;
    candidate->stream_id = stream->id;
    candidate->component_id = component->id;

    /* if the check didn't contain the PRIORITY attribute, then the priority will
     * be 0, which is invalid... */
    if (priority != 0)
    {
        candidate->priority = priority;
    }
	else
	{
        candidate->priority = n_cand_ice_priority(candidate);
    }

    _assign_remote_foundation(agent, candidate);

    if (remote)
    {
        n_free(candidate->username);
        n_free(candidate->password);
        candidate->username = g_strdup(remote->username);
        candidate->password = g_strdup(remote->password);
    }

    /* note: candidate username and password are left NULL as stream
       level ufrag/password are used */

    component->remote_candidates = n_slist_append(component->remote_candidates, candidate);
    agent_sig_new_remote_cand(agent, candidate);

    return candidate;
}

/*
 * Timer callback that handles scheduling new candidate discovery
 * processes (paced by the Ta timer), and handles running of the
 * existing discovery processes.
 *
 * This function is designed for the g_timeout_add() interface.
 *
 * @return will return FALSE when no more pending timers.
 */
static int _disc_tick_unlocked(void * pointer)
{
    n_cand_disc_t * candidate;
    n_agent_t * agent = pointer;
    n_slist_t  * i;
    int not_done = 0; /* note: track whether to continue timer */
    size_t buffer_len = 0;

    {
        static int tick_counter = 0;
        if (tick_counter++ % 50 == 0)
            nice_debug("[%s]: discovery tick #%d with list %p (1)", G_STRFUNC, tick_counter, agent->discovery_list);
    }

    for (i = agent->discovery_list; i ; i = i->next)
    {
		candidate = i->data;

        if (candidate->pending != TRUE)
        {
			candidate->pending = TRUE;

            if (agent->disc_unsched_items)
                agent->disc_unsched_items--;

            if (nice_debug_is_enabled())
            {
                char tmpbuf[INET6_ADDRSTRLEN];
                nice_address_to_string(&candidate->server, tmpbuf);
                nice_debug("[%s]: discovery - scheduling candidate type %u addr %s", G_STRFUNC, candidate->type, tmpbuf);
            }

            if (n_addr_is_valid(&candidate->server) && (candidate->type == CAND_TYPE_SERVER || candidate->type == CAND_TYPE_RELAYED))
            {
                agent_sig_comp_state_change(agent, candidate->stream->id, candidate->component->id,  COMP_STATE_GATHERING);

                if (candidate->type == CAND_TYPE_SERVER)
                {
                    buffer_len = stun_bind_create(&candidate->stun_agent, &candidate->stun_message, candidate->stun_buffer, sizeof(candidate->stun_buffer));
                }
                else if (candidate->type == CAND_TYPE_RELAYED)
                {
                    uint8_t * username = (uint8_t *)candidate->turn->username;
                    uint32_t username_len = strlen(candidate->turn->username);
                    uint8_t * password = (uint8_t *)candidate->turn->password;
                    uint32_t password_len = strlen(candidate->turn->password);

                    buffer_len = turn_create(&candidate->stun_agent, &candidate->stun_message, candidate->stun_buffer, sizeof(candidate->stun_buffer),
														candidate->stun_resp_msg.buffer == NULL ? NULL : &candidate->stun_resp_msg,
                                                       TURN_REQUEST_PORT_NORMAL, -1, -1, username, username_len, password, password_len);                    
                }

                if (buffer_len > 0)
                {
                    stun_timer_start(&candidate->timer, 200, STUN_TIMER_MAX_RETRANS);

                    /* send the conn check */
                    agent_socket_send(candidate->nicesock, &candidate->server,  buffer_len, (char *)candidate->stun_buffer);

                    /* case: success, start waiting for the result */
                    get_current_time(&candidate->next_tick);
                }
                else
                {
                    /* case: error in starting discovery, start the next discovery */
					candidate->done = TRUE;
					candidate->stun_message.buffer = NULL;
					candidate->stun_message.buffer_len = 0;
                    continue;
                }
            }
            else
                /* allocate relayed candidates */
                g_assert_not_reached();

            not_done++; /* note: new discovery scheduled */
        }

        if (candidate->done != TRUE)
        {
            n_timeval_t now;

            get_current_time(&now);

            if (candidate->stun_message.buffer == NULL)
            {
                nice_debug("[%s]: stun discovery was cancelled, marking discovery done.", G_STRFUNC);
				//nice_print_cand(agent, candidate, candidate);
				candidate->done = TRUE;
            }
            else if (_timer_expired(&candidate->next_tick, &now))
            {
                switch (stun_timer_refresh(&candidate->timer))
                {
                    case STUN_TIMER_RET_TIMEOUT:
                    {
                        /* Time out */
                        /* case: error, abort processing */
                        stun_trans_id id;

                        stun_msg_id(&candidate->stun_message, id);
                        stun_agent_forget_trans(&candidate->stun_agent, id);

						candidate->done = TRUE;
						candidate->stun_message.buffer = NULL;
						candidate->stun_message.buffer_len = 0;
                        nice_debug("[%s]: bind discovery timed out, aborting discovery item", G_STRFUNC);
                        break;
                    }
                    case STUN_TIMER_RET_RETRANSMIT:
                    {
                        /* case: not ready complete, so schedule next timeout */
                        unsigned int timeout = stun_timer_remainder(&candidate->timer);

						nice_debug("[%s]: stun transaction retransmitted (timeout %d ms)", G_STRFUNC, timeout);

                        /* retransmit */
                        agent_socket_send(candidate->nicesock, &candidate->server,
                                          stun_msg_len(&candidate->stun_message),
                                          (char *)candidate->stun_buffer);

                        /* note: convert from milli to microseconds for g_time_val_add() */
						candidate->next_tick = now;
                        time_val_add(&candidate->next_tick, timeout * 1000);

                        not_done++; /* note: retry later */
                        break;
                    }
                    case STUN_TIMER_RET_SUCCESS:
                    {
                        unsigned int timeout = stun_timer_remainder(&candidate->timer);

						candidate->next_tick = now;
                        time_val_add(&candidate->next_tick, timeout * 1000);

						nice_debug("[%s]: stun transaction success", G_STRFUNC);
						//nice_print_cand(agent, candidate, candidate);

                        not_done++; /* note: retry later */
                        break;
                    }
                    default:
                        /* Nothing to do. */
                        break;
                }
            }
            else
            {
                not_done++; /* note: discovery not expired yet */
            }
        }
    }

    if (not_done == 0)
    {
        nice_debug("[%s]: candidate gathering finished, stopping discovery timer", G_STRFUNC);
		//nice_print_cand(agent, candidate, candidate);
        disc_free(agent);
        agent_gathering_done(agent);

        /* note: no pending timers, return FALSE to stop timer */
        return FALSE;
    }

    return TRUE;
}

static int _disc_tick(void * pointer)
{
    n_agent_t * agent = pointer;
    int ret;

	//nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
	agent_lock();
    /*if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. " "Avoided race condition in _disc_tick");
        agent_unlock();
        return FALSE;
    }*/

    ret = _disc_tick_unlocked(pointer);
    if (ret == FALSE)
    {
        /*if (agent->disc_timer_source != NULL)
        {
            g_source_destroy(agent->disc_timer_source);
            g_source_unref(agent->disc_timer_source);
            agent->disc_timer_source = NULL;
        }*/
		timer_stop(agent->disc_timer);
		agent->disc_timer = 0;
    }
	//nice_debug("[%s]: agent_unlock+++++++++++", G_STRFUNC);
    agent_unlock_and_emit(agent);

    return ret;
}

/*
 * Initiates the candidate discovery process by starting
 * the necessary timers.
 *
 * @pre agent->discovery_list != NULL  // unsched discovery items available
 */
void disc_schedule(n_agent_t * agent)
{
    g_assert(agent->discovery_list != NULL);

    if (agent->disc_unsched_items > 0)
    {
        if (agent->disc_timer == 0)
        {
            /* step: run first iteration immediately */
            int res = _disc_tick_unlocked(agent);
            if (res == TRUE)
            {
                //agent_timeout_add(agent, &agent->disc_timer_source, "Candidate discovery tick", agent->timer_ta, _disc_tick, agent);
				agent->disc_timer = timer_create();
				timer_init(agent->disc_timer, 0, agent->timer_ta, (notifycallback)_disc_tick, (void *)agent,  "Candidate discovery tick");
				timer_start(agent->disc_timer);
            }
        }
    }
}
