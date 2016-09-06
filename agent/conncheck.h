/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_CONNCHECK_H
#define _NICE_CONNCHECK_H

/* note: this is a private header to libnice */

#include "base.h"
#include "agent.h"
#include "stream.h"
#include "stun/stunagent.h"
#include "stun/usages/timer.h"

#define CANDIDATE_PAIR_MAX_FOUNDATION		CANDIDATE_MAX_FOUNDATION*2

/**
 * NiceCheckState:
 * @NICE_CHECK_WAITING: Waiting to be scheduled.
 * @NICE_CHECK_IN_PROGRESS: Connection checks started.
 * @NICE_CHECK_SUCCEEDED: Connection successfully checked.
 * @NICE_CHECK_FAILED: No connectivity; retransmissions ceased.
 * @NICE_CHECK_FROZEN: Waiting to be scheduled to %NICE_CHECK_WAITING.
 * @NICE_CHECK_CANCELLED: Check cancelled.
 * @NICE_CHECK_DISCOVERED: A valid candidate pair not on the check list.
 *
 * States for checking a candidate pair.
 */
typedef enum
{
    NICE_CHECK_WAITING = 1,
    NICE_CHECK_IN_PROGRESS,
    NICE_CHECK_SUCCEEDED,
    NICE_CHECK_FAILED,
    NICE_CHECK_FROZEN,
    NICE_CHECK_CANCELLED,
    NICE_CHECK_DISCOVERED,
} NiceCheckState;

typedef struct _CandidateCheckPair CandidateCheckPair;

struct _CandidateCheckPair
{
    NiceAgent * agent;        /* back pointer to owner */
    uint32_t stream_id;
	uint32_t component_id;
    NiceCandidate * local;
    NiceCandidate * remote;
    NiceSocket * sockptr;
    char foundation[CANDIDATE_PAIR_MAX_FOUNDATION];
    NiceCheckState state;
    int nominated;
	int controlling;
	int timer_restarted;
    uint64_t priority;
	g_time_val next_tick;       /* next tick timestamp */
    StunTimer timer;
    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
    StunMessage stun_message;
};

int conn_check_add_for_candidate(NiceAgent * agent, uint32_t stream_id, Component * component, NiceCandidate * remote);
int conn_check_add_for_local_candidate(NiceAgent * agent, uint32_t stream_id, Component * component, NiceCandidate * local);
int conn_check_add_for_candidate_pair(NiceAgent * agent, uint32_t stream_id, Component * component, NiceCandidate * local, NiceCandidate * remote);
void conn_check_free(NiceAgent * agent);
int conn_check_schedule_next(NiceAgent * agent);
int conn_check_send(NiceAgent * agent, CandidateCheckPair * pair);
void conn_check_prune_stream(NiceAgent * agent, Stream * stream);
int conn_check_handle_inbound_stun(NiceAgent * agent, Stream * stream, Component * component, NiceSocket * udp_socket, const NiceAddress * from, char * buf, uint32_t len);
int32_t conn_check_compare(const CandidateCheckPair * a, const CandidateCheckPair * b);
void conn_check_remote_candidates_set(NiceAgent * agent);
NiceCandidateTransport conn_check_match_transport(NiceCandidateTransport transport);
void conn_check_prune_socket(NiceAgent * agent, Stream * stream, Component * component, NiceSocket * sock);

#endif /*_NICE_CONNCHECK_H */
