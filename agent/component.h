/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_COMPONENT_H
#define _NICE_COMPONENT_H

#include <stdint.h>
#include <glib.h>

typedef struct _comp_st n_comp_t;

#include "agent.h"
#include "agent-priv.h"
#include "candidate.h"
#include "stun/stunagent.h"
#include "stun/usages/stun_timer.h"
#include "pseudotcp.h"
#include "stream.h"
#include "socket.h"
#include "nlist.h"
#include "nqueue.h"
#include "pthread.h"
#include "uv.h"

/* (ICE 4.1.1.1, ID-19)
 * ""For RTP-based media streams, the RTP itself has a component
 * ID of 1, and RTCP a component ID of 2.  If an agent is using RTCP it MUST
 * obtain a candidate for it.  If an agent is using both RTP and RTCP, it
 * would end up with 2*K host candidates if an agent has K interfaces.""
 */

typedef struct _cand_pair_st n_cand_pair_t;
typedef struct _cand_pair_alive_st n_cand_pair_alive_t;
typedef struct _inchk_st n_inchk_t;

struct _cand_pair_alive_st
{
    n_agent_t * agent;
    //GSource * tick_source;
    int32_t  tick_clock;
    uint32_t stream_id;
    uint32_t component_id;
    StunTimer timer;
    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
    stun_msg_t stun_message;
};

struct _cand_pair_st
{
    n_cand_t * local;
    n_cand_t * remote;
    uint32_t priority;           /* candidate pair priority */
    n_cand_pair_alive_t keepalive;
};

struct _inchk_st
{
    n_addr_t from;
    n_socket_t * local_socket;
    uint32_t priority;
    int use_candidate;
    uint8_t * username;
    uint16_t username_len;
};

void incoming_check_free(n_inchk_t * icheck);

/* A pair of a socket and the GSource which polls it from the main loop. All
 * GSources in a n_comp_t must be attached to the same main context:
 * component->ctx.
 *
 * Socket must be non-NULL, but source may be NULL if it has been detached.
 *
 * The n_comp_t is stored so this may be used as the user data for a GSource
 * callback. */
typedef struct
{
    n_socket_t * socket;
    GSource * source;
    n_comp_t * component;
} SocketSource;


/* A message which has been received and processed (so is guaranteed not
 * to be a STUN packet, or to contain pseudo-TCP header bytes, for example), but
 * which hasnt yet been sent to the client in an I/O callback. This could be
 * due to the main context not being run, or due to the I/O callback being
 * detached.
 *
 * The @offset member gives the byte offset into @buf which has already been
 * sent to the client. #IOCallbackData buffers remain in the
 * #n_comp_t::pend_io_msgs queue until all of their bytes have been sent
 * to the client.
 *
 * @offset is guaranteed to be smaller than @buf_len. */
typedef struct
{
    uint8_t * buf; /* owned */
    uint32_t buf_len;
    uint32_t offset;
} IOCallbackData;

IOCallbackData * io_callback_data_new(const uint8_t * buf, uint32_t buf_len);
void io_callback_data_free(IOCallbackData * data);

struct _comp_st
{
    n_comp_type_e type;
    uint32_t id;                    /* component id */
    n_comp_state_e state;
    n_slist_t * local_candidates;   /* list of n_cand_t objs */
    n_slist_t * remote_candidates;  /* list of n_cand_t objs */
    n_slist_t * socket_sources;     /* list of SocketSource objs; must only grow monotonically */
    uint32_t socket_sources_age;    /* incremented when socket_sources changes */
    n_slist_t * incoming_checks;    /* list of n_inchk_t objs */
    n_dlist_t * turn_servers;            /* List of TurnServer objs */
    n_cand_pair_t selected_pair; /* independent from checklists, see ICE 11.1. "Sending Media" (ID-19) */
    n_cand_t * restart_candidate; /* for storing active remote candidate during a restart */
    n_cand_t * turn_candidate; /* for storing active turn candidate if turn servers have been cleared */
    /* I/O handling. The main context must always be non-NULL, and is used for all
     * socket recv() operations. All io_callback emissions are invoked in this
     * context too.
     *
     * recv_messages and io_callback are mutually exclusive, but it is allowed for
     * both to be NULL if the n_comp_t is not currently ready to receive data. */
    pthread_mutex_t io_mutex;            /* protects io_callback, io_user_data,
                                         pend_io_msgs and io_callback_id.
                                         immutable: can be accessed without
                                         holding the agent lock; if the agent
                                         lock is to be taken, it must always be
                                         taken before this one */
                                     
    n_agent_recv_func io_callback;    /* function called on io cb */
    void * io_user_data;            /* data passed to the io function */
    n_queue_t pend_io_msgs;       /* queue of messages which have been
                                         received but not passed to the client
                                         in an I/O callback or recv() call yet.
                                         each element is an owned
                                         IOCallbackData */
    uint32_t io_callback_id;             /* GSource ID of the I/O callback */
    uv_idle_t io_cb_handle;

    GMainContext * own_ctx;            /* own context for GSources for this component */
    GMainContext * ctx;                /* context for GSources for this component (possibly set from the app) */
    n_input_msg_t * recv_messages; /* unowned messages for receiving into */
    uint32_t n_recv_messages;            /* length of recv_messages */
    n_input_msg_iter_t recv_messages_iter; /* current write position in
                                                recv_messages */
    GError ** recv_buf_error;         /* error information about failed reads */

    n_agent_t * agent;  /* unowned, immutable: can be accessed without holding the agent lock */
    n_stream_t * stream;  /* unowned, immutable: can be accessed without holding the agent lock */
    stun_agent_t stun_agent; /* This stun agent is used to validate all stun requests */   

    GCancellable * stop_cancellable;
    GSource * stop_cancellable_source; /* owned */

    pst_socket_t * tcp;
    int32_t tcp_clock;
    uint64_t last_clock_timeout;
    int tcp_readable;
    GCancellable * tcp_writable_cancellable;

    uint32_t min_port;
    uint32_t max_port;

    /* Queue of messages received before a selected socket was available to send
     * ACKs on. The messages are dequeued to the pseudo-TCP socket once a selected
     * UDP socket is available. This is only used for reliable Components. */
    n_queue_t queued_tcp_packets;
};

n_comp_t * component_new(uint32_t component_id, n_agent_t * agent, n_stream_t * stream);
void component_close(n_comp_t * cmp);
void component_free(n_comp_t * cmp);
int comp_find_pair(n_comp_t * cmp, n_agent_t * agent, const gchar * lfoundation, const gchar * rfoundation, n_cand_pair_t * pair);
void component_restart(n_comp_t * cmp);
void comp_update_selected_pair(n_comp_t * component, const n_cand_pair_t * pair);
n_cand_t * comp_find_remote_cand(const n_comp_t * component, const n_addr_t * addr);
n_cand_t * comp_set_selected_remote_cand(n_agent_t * agent, n_comp_t * component, n_cand_t * candidate);
void component_attach_socket(n_comp_t * component, n_socket_t * nsocket);
void component_detach_socket(n_comp_t * component, n_socket_t * nsocket);
void component_detach_all_sockets(n_comp_t * component);
void component_free_socket_sources(n_comp_t * component);

void comp_set_io_context(n_comp_t * component, GMainContext * context);
void comp_set_io_callback(n_comp_t * component,  n_agent_recv_func func, void * user_data);
void comp_emit_io_cb(n_comp_t * component, const uint8_t * buf, uint32_t buf_len);
int component_has_io_callback(n_comp_t * component);
void component_clean_turn_servers(n_comp_t * component);
TurnServer * turn_server_new(const char * server_ip, uint32_t server_port, const char * username, const char * password, n_relay_type_e type);
TurnServer * turn_server_ref(TurnServer * turn);
void turn_server_unref(TurnServer * turn);

#endif /* _NICE_COMPONENT_H */

