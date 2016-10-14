/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_AGENT_PRIV_H
#define _NICE_AGENT_PRIV_H

/* note: this is a private header part of agent.h */

//#include <config.h>
//#include <glib.h>
#include <stdint.h>

#include "agent.h"
#include "nlist.h"
#include "nqueue.h"

/**
 * NiceInputMessageIter:
 * @message: index of the message currently being written into
 * @buffer: index of the buffer currently being written into
 * @offset: byte offset into the buffer
 *
 * Iterator for sequentially writing into an array of #NiceInputMessages,
 * tracking the current write position (i.e. the index of the next byte to be
 * written).
 *
 * If @message is equal to the number of messages in the associated
 * #n_input_msg_t array, and @buffer and @offset are zero, the iterator is at
 * the end of the messages array, and the array is (presumably) full.
 *
 * Since: 0.1.5
 */
typedef struct
{
    uint32_t message;
    uint32_t buffer;
    uint32_t offset;
} NiceInputMessageIter;

void n_input_msg_iter_reset(NiceInputMessageIter * iter);
int n_input_msg_iter_is_at_end(NiceInputMessageIter * iter, n_input_msg_t * messages, uint32_t n_messages);
uint32_t n_input_msg_iter_get_n_valid_msgs(NiceInputMessageIter * iter);

#include "socket.h"
#include "candidate.h"
#include "stream.h"
#include "conncheck.h"
#include "component.h"
#include "random.h"
#include "stun/stunagent.h"
#include "stun/usages/turn.h"
#include "stun/usages/ice.h"

/* XXX: starting from ICE ID-18, Ta SHOULD now be set according
 *      to session bandwidth -> this is not yet implemented in NICE */

#define _AGENT_TIMER_TA_DEFAULT 20      /* timer Ta, msecs (impl. defined) */
#define NICE_AGENT_TIMER_TR_DEFAULT 25000   /* timer Tr, msecs (impl. defined) */
#define NICE_AGENT_TIMER_TR_MIN     15000   /* timer Tr, msecs (ICE ID-19) */
#define _AGENT_MAX_CONNECTIVITY_CHECKS 100 /* see spec 5.7.3 (ID-19) */

#define N_EVENT_COMP_STATE_CHANGED        (1<<31)
#define N_EVENT_CAND_GATHERING_DONE         (1<<28)
#define N_EVENT_NEW_SELECTED_PAIR       (1<<27)
#define N_EVENT_CAND        (1<<26)
#define N_EVENT_REMOTE_CAND       (1<<25)
#define N_EVENT_INITIAL_BINDING_REQUEST_RECEIVED        (1<<24)
#define N_EVENT_RELIABLE_TRANSPORT_WRITABLE         (1<<23)
#define N_EVENT_STREAMS_REMOVED       (1<<22)
#define N_EVENT_NEW_SELECTED_PAIR_FULL        (1<<21)
#define N_EVENT_NEW_CAND_FULL       (1<<20)
#define N_EVENT_NEW_CAND       (1<<19)
#define N_EVENT_NEW_REMOTE_CAND_FULL       (1<<18)
#define N_EVENT_NEW_REMOTE_CAND      (1<<17)

/* An upper limit to size of STUN packets handled (based on Ethernet
 * MTU and estimated typical sizes of ICE STUN packet */
#define MAX_STUN_DATAGRAM_PAYLOAD    1300

struct _agent_st
{
    int32_t full_mode;             /* property: full-mode */
    n_timeval_t next_check_tv;         /* property: next conncheck timestamp */
    char * stun_server_ip;         /* property: STUN server IP */
    uint32_t stun_server_port;         /* property: STUN server port */
    int32_t controlling_mode;      /* property: controlling-mode */
    uint32_t timer_ta;                 /* property: timer Ta */
    uint32_t max_conn_checks;          /* property: max connectivity checks */
	n_slist_t * local_addresses;        /* list of NiceAddresses for local interfaces */
	n_slist_t * streams_list;               /* list of n_stream_t objects */
    uint32_t next_candidate_id;        /* id of next created candidate */
    uint32_t next_stream_id;           /* id of next created candidate */
    NiceRNG * rng;                  /* random number generator */
	n_slist_t * discovery_list;        /* list of n_cand_disc_t items */
    uint32_t disc_unsched_items;  /* number of discovery items unscheduled */
	int32_t disc_timer;
	int32_t cocheck_timer;
	int32_t keepalive_timer;
	n_slist_t * refresh_list;        /* list of n_cand_refresh_t items */
    uint64_t tie_breaker;            /* tie breaker (ICE sect 5.2 "Determining Role" ID-19) */
    int32_t media_after_tick;       /* Received media after keepalive tick */
    int32_t reliable;               /* property: reliable */
    int32_t keepalive_conncheck;    /* property: keepalive_conncheck */
    n_queue_t pending_signals;
    int use_ice_udp;
    int use_ice_tcp;
	int32_t n_event;
    /* XXX: add pointer to internal data struct for ABI-safe extensions */
};

int32_t agent_find_comp(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_stream_t ** stream, n_comp_t ** component);
n_stream_t * agent_find_stream(n_agent_t * agent, uint32_t stream_id);
void agent_gathering_done(n_agent_t * agent);
void agent_sig_gathering_done(n_agent_t * agent);
void agent_lock(void);
void agent_unlock(void);
void agent_unlock_and_emit(n_agent_t * agent);

void agent_sig_new_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t * lcandidate, n_cand_t * rcandidate);
void agent_sig_comp_state_change(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_comp_state_e state);
void agent_sig_new_cand(n_agent_t * agent, n_cand_t * candidate);
void agent_sig_new_remote_cand(n_agent_t * agent, n_cand_t * candidate);
void agent_sig_initial_binding_request_received(n_agent_t * agent, n_stream_t * stream);
uint64_t agent_candidate_pair_priority(n_agent_t * agent, n_cand_t * local, n_cand_t * remote);
void agent_remove_local_candidate(n_agent_t * agent, n_cand_t * candidate);
void n_agent_init_stun_agent(n_agent_t * agent, stun_agent_t * stun_agent);
void _set_socket_tos(n_agent_t * agent, n_socket_t * sock, int32_t tos);
int32_t comp_io_cb(GSocket * gsocket, GIOCondition condition, void * data);
uint32_t memcpy_buffer_to_input_message(n_input_msg_t * message, const uint8_t * buffer, uint32_t buffer_length);
uint8_t * compact_input_message(const n_input_msg_t * message, uint32_t * buffer_length);
uint8_t * compact_output_message(const n_output_msg_t * message, uint32_t * buffer_length);
uint32_t output_message_get_size(const n_output_msg_t * message);
int32_t agent_socket_send(n_socket_t * sock, const n_addr_t * addr, uint32_t len,  const gchar * buf);

uint32_t nice_candidate_ice_priority_full(uint32_t type_pref, uint32_t local_pref, uint32_t component_id);
uint32_t n_cand_ice_priority(const n_cand_t * candidate);
uint64_t n_cand_pair_priority(uint32_t o_prio, uint32_t a_prio);

/*
 * nice_debug_init:
 *
 * Initialize the debugging system. Uses the NICE_DEBUG environment variable
 * to set the appropriate debugging flags
 */
void nice_debug_init(void);


#ifdef NDEBUG
static inline int32_t nice_debug_is_enabled(void)
{
    return FALSE;
}
static inline void nice_debug(const char * fmt, ...) { }
#else
int32_t nice_debug_is_enabled(void);
void nice_debug(const char * fmt, ...) G_GNUC_PRINTF(1, 2);
#endif

typedef struct
{
	uint32_t stream_id;
	uint32_t component_id;
	char lfoundation[CAND_MAX_FOUNDATION];
	char rfoundation[CAND_MAX_FOUNDATION];
} ev_new_pair_t;

typedef struct
{
	uint32_t stream_id;
	uint32_t component_id;
	n_cand_t * lcandidate;
	n_cand_t * rcandidate;
} ev_new_pair_full_t;

typedef struct
{
	uint32_t stream_id;
	uint32_t comp_id;
	n_comp_state_e state;
} ev_state_changed_t;

typedef struct
{
	uint32_t stream_id;
	uint32_t comp_id;
} ev_trans_writable_t;

typedef struct
{
	uint32_t stream_id;
	uint32_t comp_id;
	char foundation[CAND_MAX_FOUNDATION];
} ev_new_cand_t;

#endif /*_NICE_AGENT_PRIV_H */
