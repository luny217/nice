/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_CONNCHECK_H
#define _NICE_CONNCHECK_H

/* note: this is a private header to libnice */

#include "agent.h"
#include "stream.h"
#include "stun/stunagent.h"
#include "stun/usages/timer.h"

#define NICE_CANDIDATE_PAIR_MAX_FOUNDATION        NICE_CANDIDATE_MAX_FOUNDATION*2

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
    guint stream_id;
    guint component_id;
    NiceCandidate * local;
    NiceCandidate * remote;
    NiceSocket * sockptr;
    gchar foundation[NICE_CANDIDATE_PAIR_MAX_FOUNDATION];
    NiceCheckState state;
    gboolean nominated;
    gboolean controlling;
    gboolean timer_restarted;
    guint64 priority;
    GTimeVal next_tick;       /* next tick timestamp */
    StunTimer timer;
    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
    StunMessage stun_message;
};

int conn_check_add_for_candidate(NiceAgent * agent, guint stream_id, Component * component, NiceCandidate * remote);
int conn_check_add_for_local_candidate(NiceAgent * agent, guint stream_id, Component * component, NiceCandidate * local);
gboolean conn_check_add_for_candidate_pair(NiceAgent * agent, guint stream_id, Component * component, NiceCandidate * local, NiceCandidate * remote);
void conn_check_free(NiceAgent * agent);
gboolean conn_check_schedule_next(NiceAgent * agent);
int conn_check_send(NiceAgent * agent, CandidateCheckPair * pair);
void conn_check_prune_stream(NiceAgent * agent, Stream * stream);
gboolean conn_check_handle_inbound_stun(NiceAgent * agent, Stream * stream, Component * component, NiceSocket * udp_socket, const NiceAddress * from, gchar * buf, guint len);
gint conn_check_compare(const CandidateCheckPair * a, const CandidateCheckPair * b);
void conn_check_remote_candidates_set(NiceAgent * agent);
NiceCandidateTransport conn_check_match_transport(NiceCandidateTransport transport);
void conn_check_prune_socket(NiceAgent * agent, Stream * stream, Component * component, NiceSocket * sock);

#endif /*_NICE_CONNCHECK_H */
