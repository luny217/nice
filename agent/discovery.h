/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_DISCOVERY_H
#define _NICE_DISCOVERY_H

/* note: this is a private header to libnice */

#include "base.h"
#include "stream.h"
#include "agent.h"

typedef struct
{
    NiceAgent * agent;        /* back pointer to owner */
    NiceCandidateType type;   /* candidate type STUN or TURN */
    NiceSocket * nicesock; /* XXX: should be taken from local cand: existing socket to use */
    NiceAddress server;       /* STUN/TURN server address */
	g_time_val next_tick;       /* next tick timestamp */
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
} CandidateDiscovery;

typedef struct
{
    NiceAgent * agent;        /* back pointer to owner */
    NiceSocket * nicesock;    /* existing socket to use */
    NiceAddress server;       /* STUN/TURN server address */
    NiceCandidate * candidate; /* candidate to refresh */
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

void refresh_free(NiceAgent * agent);
void refresh_prune_stream(NiceAgent * agent, uint32_t stream_id);
void refresh_prune_candidate(NiceAgent * agent, NiceCandidate * candidate);
void refresh_prune_socket(NiceAgent * agent, NiceSocket * sock);
void refresh_cancel(CandidateRefresh * refresh);


void discovery_free(NiceAgent * agent);
void discovery_prune_stream(NiceAgent * agent, uint32_t stream_id);
void discovery_prune_socket(NiceAgent * agent, NiceSocket * sock);
void discovery_schedule(NiceAgent * agent);

typedef enum
{
    HOST_CANDIDATE_SUCCESS,
    HOST_CANDIDATE_FAILED,
    HOST_CANDIDATE_CANT_CREATE_SOCKET,
    HOST_CANDIDATE_REDUNDANT
} HostCandidateResult;

HostCandidateResult
discovery_add_local_host_candidate(
    NiceAgent * agent,
    uint32_t stream_id,
    uint32_t component_id,
    NiceAddress * address,
    NiceCandidate ** candidate);

NiceCandidate *
discovery_add_relay_candidate(
    NiceAgent * agent,
    uint32_t stream_id,
    uint32_t component_id,
    NiceAddress * address,
    NiceSocket * base_socket,
    TurnServer * turn);

NiceCandidate *
discovery_add_server_reflexive_candidate(
    NiceAgent * agent,
    uint32_t stream_id,
    uint32_t component_id,
    NiceAddress * address,
    NiceSocket * base_socket);

NiceCandidate *
discovery_add_peer_reflexive_candidate(
    NiceAgent * agent,
    uint32_t stream_id,
    uint32_t component_id,
    NiceAddress * address,
    NiceSocket * base_socket,
    NiceCandidate * local,
    NiceCandidate * remote);

NiceCandidate *
discovery_learn_remote_peer_reflexive_candidate(
    NiceAgent * agent,
    Stream * stream,
    Component * component,
	uint32_t priority,
    const NiceAddress * remote_address,
    NiceSocket * udp_socket,
    NiceCandidate * local,
    NiceCandidate * remote);

#endif /*_NICE_CONNCHECK_H */
