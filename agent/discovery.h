/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_DISCOVERY_H
#define _NICE_DISCOVERY_H

/* note: this is a private header to libnice */

#include "base.h"
#include "stream.h"
#include "agent.h"

typedef struct
{
    n_agent_t * agent;        /* back pointer to owner */
    n_cand_type_e type;   /* candidate type STUN or TURN */
    n_socket_t * nicesock; /* XXX: should be taken from local cand: existing socket to use */
    n_addr_t server;       /* STUN/TURN server address */
	n_timeval_t next_tick;       /* next tick timestamp */
    int pending;         /* is discovery in progress? */
    int done;            /* is discovery complete? */
    Stream * stream;
    Component * component;
    TurnServer * turn;
    StunAgent stun_agent;
    StunTimer timer;
    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
    StunMessage stun_message;
    uint8_t stun_resp_buffer[STUN_MAX_MESSAGE_SIZE];
    StunMessage stun_resp_msg;
} n_cand_disc_t; 

typedef struct
{
    n_agent_t * agent;        /* back pointer to owner */
    n_socket_t * nicesock;    /* existing socket to use */
    n_addr_t server;       /* STUN/TURN server address */
    n_cand_t * candidate; /* candidate to refresh */
    Stream * stream;
    Component * component;
    StunAgent stun_agent;
    GSource * timer_source;
    GSource * tick_source;
    StunTimer timer;
    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
    StunMessage stun_message;
    uint8_t stun_resp_buffer[STUN_MAX_MESSAGE_SIZE];
    StunMessage stun_resp_msg;
} CandidateRefresh;

void refresh_free(n_agent_t * agent);
void refresh_prune_stream(n_agent_t * agent, uint32_t stream_id);
void refresh_prune_candidate(n_agent_t * agent, n_cand_t * candidate);
void refresh_prune_socket(n_agent_t * agent, n_socket_t * sock);
void refresh_cancel(CandidateRefresh * refresh);


void disc_free(n_agent_t * agent);
void disc_prune_stream(n_agent_t * agent, uint32_t stream_id);
void disc_prune_socket(n_agent_t * agent, n_socket_t * sock);
void disc_schedule(n_agent_t * agent);

typedef enum
{
    CANDIDATE_SUCCESS,
    CANDIDATE_FAILED,
    CANDIDATE_CANT_CREATE_SOCKET,
    CANDIDATE_REDUNDANT
} HostCandidateResult;  //cand_ret_e

HostCandidateResult
disc_add_local_host_cand(
    n_agent_t * agent,
    uint32_t stream_id,
    uint32_t component_id,
    n_addr_t * address,
    n_cand_t ** candidate);

n_cand_t *
disc_add_relay_cand(
    n_agent_t * agent,
    uint32_t stream_id,
    uint32_t component_id,
    n_addr_t * address,
    n_socket_t * base_socket,
    TurnServer * turn);

n_cand_t *
disc_add_server_cand(
    n_agent_t * agent,
    uint32_t stream_id,
    uint32_t component_id,
    n_addr_t * address,
    n_socket_t * base_socket);

n_cand_t *
disc_add_peer_cand(
    n_agent_t * agent,
    uint32_t stream_id,
    uint32_t component_id,
    n_addr_t * address,
    n_socket_t * base_socket,
    n_cand_t * local,
    n_cand_t * remote);

n_cand_t *
disc_learn_remote_peer_cand(
    n_agent_t * agent,
    Stream * stream,
    Component * component,
	uint32_t priority,
    const n_addr_t * remote_address,
    n_socket_t * udp_socket,
    n_cand_t * local,
    n_cand_t * remote);

#endif /*_NICE_CONNCHECK_H */
