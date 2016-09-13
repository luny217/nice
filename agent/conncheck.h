/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_CONNCHECK_H
#define _NICE_CONNCHECK_H

/* note: this is a private header to libnice */

#include "base.h"
#include "agent.h"
#include "stream.h"
#include "stun/stunagent.h"
#include "stun/usages/timer.h"

#define CAND_PAIR_MAX_FOUNDATION		CAND_MAX_FOUNDATION*2

/**
 * n_chk_state_e:
 * @NCHK_WAITING: Waiting to be scheduled.
 * @NCHK_IN_PROGRESS: Connection checks started.
 * @NCHK_SUCCEEDED: Connection successfully checked.
 * @NCHK_FAILED: No connectivity; retransmissions ceased.
 * @NCHK_FROZEN: Waiting to be scheduled to %NCHK_WAITING.
 * @NCHK_CANCELLED: Check cancelled.
 * @NCHK_DISCOVERED: A valid candidate pair not on the check list.
 *
 * States for checking a candidate pair.
 */
typedef enum
{
    NCHK_WAITING = 1,
    NCHK_IN_PROGRESS,
    NCHK_SUCCEEDED,
    NCHK_FAILED,
    NCHK_FROZEN,
    NCHK_CANCELLED,
    NCHK_DISCOVERED,
} n_chk_state_e; 

typedef struct _cand_chk_pair_st n_cand_chk_pair_t;

struct _cand_chk_pair_st
{
    n_agent_t * agent;        /* back pointer to owner */
    uint32_t stream_id;
	uint32_t component_id;
    n_cand_t * local;
    n_cand_t * remote;
    n_socket_t * sockptr;
    char foundation[CAND_PAIR_MAX_FOUNDATION];
    n_chk_state_e state;
    int nominated;
	int controlling;
	int timer_restarted;
    uint64_t priority;
	n_timeval_t next_tick;       /* next tick timestamp */
    StunTimer timer;
    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
    stun_msg_t stun_message;
};

int cocheck_add_cand(n_agent_t * agent, uint32_t stream_id, n_comp_t * component, n_cand_t * remote);
int cocheck_add_local_cand(n_agent_t * agent, uint32_t stream_id, n_comp_t * component, n_cand_t * local);
int cocheck_add_cand_pair(n_agent_t * agent, uint32_t stream_id, n_comp_t * component, n_cand_t * local, n_cand_t * remote);
void cocheck_free(n_agent_t * agent);
int cocheck_schedule_next(n_agent_t * agent);
int cocheck_send(n_agent_t * agent, n_cand_chk_pair_t * pair);
void cocheck_prune_stream(n_agent_t * agent, n_stream_t * stream);
int cocheck_handle_in_stun(n_agent_t * agent, n_stream_t * stream, n_comp_t * component, n_socket_t * udp_socket, const n_addr_t * from, char * buf, uint32_t len);
int32_t cocheck_compare(const n_cand_chk_pair_t * a, const n_cand_chk_pair_t * b);
void cocheck_remote_cands_set(n_agent_t * agent);
n_cand_trans_e cocheck_match_trans(n_cand_trans_e transport);
void cocheck_prune_socket(n_agent_t * agent, n_stream_t * stream, n_comp_t * component, n_socket_t * sock);

#endif /*_NICE_CONNCHECK_H */
