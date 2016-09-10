/* This file is part of the Nice GLib ICE library. */

#ifndef _NICE_COMPONENT_H
#define _NICE_COMPONENT_H

#include <stdint.h>
#include <glib.h>

typedef struct _Component Component;

#include "agent.h"
#include "agent-priv.h"
#include "candidate.h"
#include "stun/stunagent.h"
#include "stun/usages/timer.h"
#include "pseudotcp.h"
#include "stream.h"
#include "socket.h"
#include "nlist.h"
#include "nqueue.h"

/* (ICE 4.1.1.1, ID-19)
 * ""For RTP-based media streams, the RTP itself has a component
 * ID of 1, and RTCP a component ID of 2.  If an agent is using RTCP it MUST
 * obtain a candidate for it.  If an agent is using both RTP and RTCP, it
 * would end up with 2*K host candidates if an agent has K interfaces.""
 */

typedef struct _CandidatePair CandidatePair;
typedef struct _CandidatePairKeepalive CandidatePairKeepalive;
typedef struct _IncomingCheck IncomingCheck;

struct _CandidatePairKeepalive
{
    n_agent_t * agent;
    GSource * tick_source;
    uint32_t stream_id;
    uint32_t component_id;
    StunTimer timer;
    uint8_t stun_buffer[STUN_MAX_MESSAGE_SIZE_IPV6];
    StunMessage stun_message;
};

struct _CandidatePair
{
    n_cand_t * local;
    n_cand_t * remote;
    uint32_t priority;           /* candidate pair priority */
    CandidatePairKeepalive keepalive;
};

struct _IncomingCheck
{
    n_addr_t from;
    n_socket_t * local_socket;
    uint32_t priority;
    int use_candidate;
    uint8_t * username;
    uint16_t username_len;
};

void incoming_check_free(IncomingCheck * icheck);

/* A pair of a socket and the GSource which polls it from the main loop. All
 * GSources in a Component must be attached to the same main context:
 * component->ctx.
 *
 * Socket must be non-NULL, but source may be NULL if it has been detached.
 *
 * The Component is stored so this may be used as the user data for a GSource
 * callback. */
typedef struct
{
    n_socket_t * socket;
    GSource * source;
    Component * component;
} SocketSource;


/* A message which has been received and processed (so is guaranteed not
 * to be a STUN packet, or to contain pseudo-TCP header bytes, for example), but
 * which hasnt yet been sent to the client in an I/O callback. This could be
 * due to the main context not being run, or due to the I/O callback being
 * detached.
 *
 * The @offset member gives the byte offset into @buf which has already been
 * sent to the client. #IOCallbackData buffers remain in the
 * #Component::pending_io_messages queue until all of their bytes have been sent
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

struct _Component
{
    NiceComponentType type;
    uint32_t id;                    /* component id */
    NiceComponentState state;
	n_slist_t * local_candidates;   /* list of n_cand_t objs */
	n_slist_t * remote_candidates;  /* list of n_cand_t objs */
	n_slist_t * socket_sources;     /* list of SocketSource objs; must only grow monotonically */
    uint32_t socket_sources_age;    /* incremented when socket_sources changes */
	n_slist_t * incoming_checks;    /* list of IncomingCheck objs */
	n_dlist_t * turn_servers;            /* List of TurnServer objs */
    CandidatePair selected_pair; /* independent from checklists, see ICE 11.1. "Sending Media" (ID-19) */
    n_cand_t * restart_candidate; /* for storing active remote candidate during a restart */
    n_cand_t * turn_candidate; /* for storing active turn candidate if turn servers have been cleared */
    /* I/O handling. The main context must always be non-NULL, and is used for all
     * socket recv() operations. All io_callback emissions are invoked in this
     * context too.
     *
     * recv_messages and io_callback are mutually exclusive, but it is allowed for
     * both to be NULL if the Component is not currently ready to receive data. */
    GMutex io_mutex;                  /* protects io_callback, io_user_data,
                                         pending_io_messages and io_callback_id.
                                         immutable: can be accessed without
                                         holding the agent lock; if the agent
                                         lock is to be taken, it must always be
                                         taken before this one */
    NiceAgentRecvFunc io_callback;    /* function called on io cb */
    void * io_user_data;            /* data passed to the io function */
    n_queue_t pending_io_messages;       /* queue of messages which have been
                                         received but not passed to the client
                                         in an I/O callback or recv() call yet.
                                         each element is an owned
                                         IOCallbackData */
    uint32_t io_callback_id;             /* GSource ID of the I/O callback */

    GMainContext * own_ctx;            /* own context for GSources for this
                                       component */
    GMainContext * ctx;                /* context for GSources for this
                                       component (possibly set from the app) */
    n_input_msg_t * recv_messages; /* unowned messages for receiving into */
    uint32_t n_recv_messages;            /* length of recv_messages */
    NiceInputMessageIter recv_messages_iter; /* current write position in
                                                recv_messages */
    GError ** recv_buf_error;         /* error information about failed reads */

    n_agent_t * agent;  /* unowned, immutable: can be accessed without holding the
                      * agent lock */
    Stream * stream;  /* unowned, immutable: can be accessed without holding the
                    * agent lock */

    StunAgent stun_agent; /* This stun agent is used to validate all stun requests */


    GCancellable * stop_cancellable;
    GSource * stop_cancellable_source; /* owned */

    pst_socket_t * tcp;
    GSource * tcp_clock;
    uint64_t last_clock_timeout;
    int tcp_readable;
    GCancellable * tcp_writable_cancellable;

    //GIOStream * iostream;

    uint32_t min_port;
    uint32_t max_port;

    /* Queue of messages received before a selected socket was available to send
     * ACKs on. The messages are dequeued to the pseudo-TCP socket once a selected
     * UDP socket is available. This is only used for reliable Components. */
    n_queue_t queued_tcp_packets;
};

Component * component_new(uint32_t component_id, n_agent_t * agent, Stream * stream);
void component_close(Component * cmp);
void component_free(Component * cmp);
int component_find_pair(Component * cmp, n_agent_t * agent, const gchar * lfoundation, const gchar * rfoundation, CandidatePair * pair);
void component_restart(Component * cmp);
void component_update_selected_pair(Component * component, const CandidatePair * pair);
n_cand_t * comp_find_remote_cand(const Component * component, const n_addr_t * addr);
n_cand_t * comp_set_selected_remote_cand(n_agent_t * agent, Component * component, n_cand_t * candidate);
void component_attach_socket(Component * component, n_socket_t * nsocket);
void component_detach_socket(Component * component, n_socket_t * nsocket);
void component_detach_all_sockets(Component * component);
void component_free_socket_sources(Component * component);

GSource * component_input_source_new(n_agent_t * agent, uint32_t stream_id,
                                     uint32_t component_id, GPollableInputStream * pollable_istream, GCancellable * cancellable);

GMainContext * component_dup_io_context(Component * component);
void component_set_io_context(Component * component, GMainContext * context);
void component_set_io_callback(Component * component,  NiceAgentRecvFunc func, void * user_data,
                               n_input_msg_t * recv_messages, uint32_t n_recv_messages, GError ** error);
void component_emit_io_callback(Component * component, const uint8_t * buf, uint32_t buf_len);
int component_has_io_callback(Component * component);
void component_clean_turn_servers(Component * component);
TurnServer * turn_server_new(const gchar * server_ip, uint32_t server_port, const char * username, const char * password, NiceRelayType type);
TurnServer * turn_server_ref(TurnServer * turn);
void turn_server_unref(TurnServer * turn);

#endif /* _NICE_COMPONENT_H */

