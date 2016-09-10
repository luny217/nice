/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_AGENT_PRIV_H
#define _NICE_AGENT_PRIV_H

/* note: this is a private header part of agent.h */

#include <config.h>
#include <glib.h>
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

void nice_input_message_iter_reset(NiceInputMessageIter * iter);
int nice_input_message_iter_is_at_end(NiceInputMessageIter * iter, n_input_msg_t * messages, uint32_t n_messages);
uint32_t nice_input_message_iter_get_n_valid_messages(NiceInputMessageIter * iter);

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


/* An upper limit to size of STUN packets handled (based on Ethernet
 * MTU and estimated typical sizes of ICE STUN packet */
#define MAX_STUN_DATAGRAM_PAYLOAD    1300

struct _agent_st
{
    GObject parent;                 /* gobject pointer */
    int32_t full_mode;             /* property: full-mode */
    n_timeval_t next_check_tv;         /* property: next conncheck timestamp */
    char * stun_server_ip;         /* property: STUN server IP */
    uint32_t stun_server_port;         /* property: STUN server port */
    int32_t controlling_mode;      /* property: controlling-mode */
    uint32_t timer_ta;                 /* property: timer Ta */
    uint32_t max_conn_checks;          /* property: max connectivity checks */
	n_slist_t * local_addresses;        /* list of NiceAddresses for local interfaces */
	n_slist_t * streams_list;               /* list of Stream objects */
    GMainContext * main_context;    /* main context pointer */
    uint32_t next_candidate_id;        /* id of next created candidate */
    uint32_t next_stream_id;           /* id of next created candidate */
    NiceRNG * rng;                  /* random number generator */
	n_slist_t * discovery_list;        /* list of n_cand_disc_t items */
    uint32_t disc_unsched_items;  /* number of discovery items unscheduled */
    GSource * disc_timer_source; /* source of discovery timer */
    GSource * conncheck_timer_source; /* source of conncheck timer */
    GSource * keepalive_timer_source; /* source of keepalive timer */
	n_slist_t * refresh_list;        /* list of CandidateRefresh items */
    uint64_t tie_breaker;            /* tie breaker (ICE sect 5.2 "Determining Role" ID-19) */
    NiceCompatibility compatibility; /* property: Compatibility mode */
    int32_t media_after_tick;       /* Received media after keepalive tick */
    char * software_attribute;      /* SOFTWARE attribute */
    int32_t reliable;               /* property: reliable */
    int32_t keepalive_conncheck;    /* property: keepalive_conncheck */
    n_queue_t pending_signals;
    int use_ice_udp;
    int use_ice_tcp;
    /* XXX: add pointer to internal data struct for ABI-safe extensions */
};

int32_t agent_find_comp(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, Stream ** stream, Component ** component);
Stream * agent_find_stream(n_agent_t * agent, uint32_t stream_id);
void agent_gathering_done(n_agent_t * agent);
void agent_signal_gathering_done(n_agent_t * agent);
void agent_lock(void);
void agent_unlock(void);
void agent_unlock_and_emit(n_agent_t * agent);

void agent_signal_new_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t * lcandidate, n_cand_t * rcandidate);
void n_sig_comp_state_change(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, NiceComponentState state);
void agent_sig_new_cand(n_agent_t * agent, n_cand_t * candidate);
void agent_sig_new_remote_cand(n_agent_t * agent, n_cand_t * candidate);
void agent_signal_initial_binding_request_received(n_agent_t * agent, Stream * stream);
uint64_t agent_candidate_pair_priority(n_agent_t * agent, n_cand_t * local, n_cand_t * remote);
void agent_timeout_add(n_agent_t * agent, GSource ** out, const char * name, uint32_t interval, GSourceFunc function, void * data);
StunUsageIceCompatibility agent_to_ice_compatibility(n_agent_t * agent);
StunUsageTurnCompatibility agent_to_turn_compatibility(n_agent_t * agent);
void agent_remove_local_candidate(n_agent_t * agent, n_cand_t * candidate);
void nice_agent_init_stun_agent(n_agent_t * agent, StunAgent * stun_agent);
void _priv_set_socket_tos(n_agent_t * agent, n_socket_t * sock, int32_t tos);
int32_t component_io_cb(GSocket * gsocket, GIOCondition condition, void * data);
uint32_t memcpy_buffer_to_input_message(n_input_msg_t * message, const uint8_t * buffer, uint32_t buffer_length);
uint8_t * compact_input_message(const n_input_msg_t * message, uint32_t * buffer_length);
uint8_t * compact_output_message(const n_output_msg_t * message, uint32_t * buffer_length);
uint32_t output_message_get_size(const n_output_msg_t * message);
int32_t agent_socket_send(n_socket_t * sock, const n_addr_t * addr, uint32_t len,  const gchar * buf);

uint32_t nice_candidate_ice_priority_full(uint32_t type_pref, uint32_t local_pref, uint32_t component_id);
uint32_t n_cand_ice_priority(const n_cand_t * candidate);
uint64_t nice_candidate_pair_priority(uint32_t o_prio, uint32_t a_prio);

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

#endif /*_NICE_AGENT_PRIV_H */
