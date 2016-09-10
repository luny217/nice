/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_CONNCHECK_H
#define _NICE_CONNCHECK_H

/* note: this is a private header to libnice */

#include "base.h"
#include "agent.h"
#include "stream.h"
#include "stun/stunagent.h"
#include "stun/usages/timer.h"

#define CANDIDATE_PAIR_MAX_FOUNDATION		CAND_MAX_FOUNDATION*2

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
    n_agent_t * agent;        /* back pointer to owner */
    uint32_t stream_id;
	uint32_t component_id;
    n_cand_t * local;
    n_cand_t * remote;
    n_socket_t * sockptr;
    char foundation[CANDIDATE_PAIR_MAX_FOUNDATION];
    NiceCheckState state;
    int nominated;
	int controlling;
	int timer_restarted;
    uint64_t priority;
	n_timeval_t next_tick;       /* next tick timestamp */
    StunTimer timer;
    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
    StunMessage stun_message;
};

int conn_check_add_for_candidate(n_agent_t * agent, uint32_t stream_id, Component * component, n_cand_t * remote);
int conn_check_add_for_local_candidate(n_agent_t * agent, uint32_t stream_id, Component * component, n_cand_t * local);
int conn_check_add_for_candidate_pair(n_agent_t * agent, uint32_t stream_id, Component * component, n_cand_t * local, n_cand_t * remote);
void conn_check_free(n_agent_t * agent);
int conn_check_schedule_next(n_agent_t * agent);
int conn_check_send(n_agent_t * agent, CandidateCheckPair * pair);
void conn_check_prune_stream(n_agent_t * agent, Stream * stream);
int co_chk_handle_in_stun(n_agent_t * agent, Stream * stream, Component * component, n_socket_t * udp_socket, const n_addr_t * from, char * buf, uint32_t len);
int32_t conn_check_compare(const CandidateCheckPair * a, const CandidateCheckPair * b);
void co_chk_remote_cands_set(n_agent_t * agent);
NiceCandidateTransport conn_check_match_transport(NiceCandidateTransport transport);
void conn_check_prune_socket(n_agent_t * agent, Stream * stream, Component * component, n_socket_t * sock);

#endif /*_NICE_CONNCHECK_H */
