/* This file is part of the Nice GLib ICE library. */

#include <config.h>
#include <glib.h>
#include <gobject/gvaluecollector.h>

#include <string.h>
#include <errno.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "debug.h"
#include "socket.h"
#include "stun/usages/turn.h"
#include "candidate.h"
#include "component.h"
#include "conncheck.h"
#include "discovery.h"
#include "agent.h"
#include "agent-priv.h"
//#include "iostream.h"
#include "stream.h"
#include "interfaces.h"
#include "pseudotcp.h"

#include "base.h"
#include "nlist.h"
#include "nqueue.h"
#include "event.h"

/* Maximum size of a UDP packet's payload, as the packet's length field is 16b
 * wide. */
#define MAX_BUFFER_SIZE ((1 << 16) - 1)  /* 65535 */

#define DEFAULT_STUN_PORT  3478
#define DEFAULT_UPNP_TIMEOUT 200  /* milliseconds */

#define MAX_TCP_MTU 1400 /* Use 1400 because of VPNs and we assume IEE 802.3 */
#define TCP_HEADER_SIZE 24 /* bytes */

static void n_debug_input_msg(const n_input_msg_t * messages,  uint32_t n_messages);

//G_DEFINE_TYPE(n_agent_t, nice_agent, G_TYPE_OBJECT);

static void n_agent_init(n_agent_t * self);
static void nice_agent_class_init(NiceAgentClass * klass);
static gpointer nice_agent_parent_class = ((void *)0);
static int32_t NiceAgent_private_offset;
static void nice_agent_class_intern_init(gpointer klass)
{
    nice_agent_parent_class = g_type_class_peek_parent(klass);
    if (NiceAgent_private_offset != 0) g_type_class_adjust_private_offset(klass, &NiceAgent_private_offset);
    nice_agent_class_init((NiceAgentClass *) klass);
}
static __inline gpointer nice_agent_get_instance_private(n_agent_t * self)
{
    return (((gpointer)((guint8 *)(self) + (glong)(NiceAgent_private_offset))));
}
GType nice_agent_get_type(void)
{
    static volatile gsize g_define_type_id__volatile = 0;
    if ((g_once_init_enter((&g_define_type_id__volatile))))
    {
        GType g_define_type_id = g_type_register_static_simple(((GType)((20) << (2))), g_intern_static_string("n_agent_t"), sizeof(NiceAgentClass), (GClassInitFunc) nice_agent_class_intern_init, sizeof(n_agent_t), (GInstanceInitFunc) n_agent_init, (GTypeFlags) 0);
        {
            {
                {
                };
            }
        }(g_once_init_leave((&g_define_type_id__volatile), (gsize)(g_define_type_id)));
    }
    return g_define_type_id__volatile;
};


enum
{
    PROP_COMPATIBILITY = 1,
    PROP_MAIN_CONTEXT,
    PROP_STUN_SERVER,
    PROP_STUN_SERVER_PORT,
    PROP_CONTROLLING_MODE,
    PROP_FULL_MODE,
    PROP_STUN_PACING_TIMER,
    PROP_MAX_CONNECTIVITY_CHECKS,
    PROP_PROXY_TYPE,
    PROP_PROXY_IP,
    PROP_PROXY_PORT,
    PROP_PROXY_USERNAME,
    PROP_PROXY_PASSWORD,
    PROP_UPNP,
    PROP_UPNP_TIMEOUT,
    PROP_RELIABLE,
    PROP_ICE_UDP,
    PROP_ICE_TCP,
    PROP_BYTESTREAM_TCP,
    PROP_KEEPALIVE_CONNCHECK
};

enum
{
    SIGNAL_COMP_STATE_CHANGED,
    SIGNAL_CANDIDATE_GATHERING_DONE,
    SIGNAL_NEW_SELECTED_PAIR,
    SIGNAL_NEW_CANDIDATE,
    SIGNAL_NEW_REMOTE_CANDIDATE,
    SIGNAL_INITIAL_BINDING_REQUEST_RECEIVED,
    SIGNAL_RELIABLE_TRANSPORT_WRITABLE,
    SIGNAL_STREAMS_REMOVED,
    SIGNAL_NEW_SELECTED_PAIR_FULL,
    SIGNAL_NEW_CANDIDATE_FULL,
    SIGNAL_NEW_REMOTE_CANDIDATE_FULL,

    N_SIGNALS,
};

static uint32_t signals[N_SIGNALS];

static GMutex agent_mutex;    /* Mutex used for thread-safe lib */

static void pst_opened(pst_socket_t * sock, void * user_data);
static void pst_readable(pst_socket_t * sock, void * user_data);
static void pst_writable(pst_socket_t * sock, void * user_data);
static void pst_closed(pst_socket_t * sock, uint32_t err, void * user_data);
static pst_wret_e pst_write_packet(pst_socket_t * sock, const char * buffer, uint32_t len, void * user_data);
static void adjust_tcp_clock(n_agent_t * agent, n_stream_t * stream, n_comp_t * component);
static void n_agent_dispose(GObject * object);
static void n_agent_get_property(GObject * object, uint32_t property_id, GValue * value, GParamSpec * pspec);
static void nice_agent_set_property(GObject * object, uint32_t property_id, const GValue * value, GParamSpec * pspec);

void agent_lock(void)
{
    g_mutex_lock(&agent_mutex);
}

void agent_unlock(void)
{
    g_mutex_unlock(&agent_mutex);
}

static GType _nice_agent_stream_ids_get_type(void);

G_DEFINE_POINTER_TYPE(_NiceAgentStreamIds, _nice_agent_stream_ids);

#define NICE_TYPE_AGENT_STREAM_IDS _nice_agent_stream_ids_get_type ()

typedef struct
{
    uint32_t signal_id;
    GSignalQuery query;
    GValue * params;
} QueuedSignal;

static void free_queued_signal(QueuedSignal * sig)
{
    uint32_t i;

    g_value_unset(&sig->params[0]);

    for (i = 0; i < sig->query.n_params; i++)
    {
        if (G_VALUE_HOLDS(&sig->params[i + 1], NICE_TYPE_AGENT_STREAM_IDS))
            free(g_value_get_pointer(&sig->params[i + 1]));
        g_value_unset(&sig->params[i + 1]);
    }

    n_slice_free1(sizeof(GValue) * (sig->query.n_params + 1), sig->params);
    n_slice_free(QueuedSignal, sig);
}

void agent_unlock_and_emit(n_agent_t * agent)
{
    n_queue_t queue = G_QUEUE_INIT;
    QueuedSignal * sig;

    queue = agent->pending_signals;
    n_queue_init(&agent->pending_signals);

    agent_unlock();

    while ((sig = n_queue_pop_head(&queue)))
    {
        g_signal_emitv(sig->params, sig->signal_id, 0, NULL);

        free_queued_signal(sig);
    }
}

static void agent_queue_signal(n_agent_t * agent, uint32_t signal_id, ...)
{
    QueuedSignal * sig;
    uint32_t i;
    char * error = NULL;
    va_list var_args;

    sig = n_slice_new(QueuedSignal);
    g_signal_query(signal_id, &sig->query);

    sig->signal_id = signal_id;
    sig->params = n_slice_alloc0(sizeof(GValue) * (sig->query.n_params + 1));

    g_value_init(&sig->params[0], G_TYPE_OBJECT);
    g_value_set_object(&sig->params[0], agent);

    va_start(var_args, signal_id);
    for (i = 0; i < sig->query.n_params; i++)
    {
        G_VALUE_COLLECT_INIT(&sig->params[i + 1], sig->query.param_types[i], var_args, 0, &error);
        if (error)
            break;
    }
    va_end(var_args);

    if (error)
    {
        free_queued_signal(sig);
        g_critical("Error collecting values for signal: %s", error);
        n_free(error);
        return;
    }

    n_queue_push_tail(&agent->pending_signals, sig);
}

StunUsageTurnCompatibility agent_to_turn_compatibility(n_agent_t * agent)
{
    return STUN_USAGE_TURN_COMPATIBILITY_RFC5766;
}

n_stream_t * agent_find_stream(n_agent_t * agent, uint32_t stream_id)
{
	n_slist_t * i;

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * s = i->data;

        if (s->id == stream_id)
            return s;
    }

    return NULL;
}

int32_t agent_find_comp(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_stream_t ** stream, n_comp_t ** component)
{
    n_stream_t * s;
    n_comp_t * c;

    s = agent_find_stream(agent, stream_id);

    if (s == NULL)
        return FALSE;

    c = stream_find_comp_by_id(s, component_id);

    if (c == NULL)
        return FALSE;

    if (stream)
        *stream = s;

    if (component)
        *component = c;

    return TRUE;
}

static void nice_agent_class_init(NiceAgentClass * klass)
{
    GObjectClass * gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->get_property = n_agent_get_property;
    gobject_class->set_property = nice_agent_set_property;
    gobject_class->dispose = n_agent_dispose;

    /* install properties */
    /**
     * n_agent_t:main-context:
     *
     * A GLib main context is needed for all timeouts used by libnice.
     * This is a property being set by the n_agent_new() call.
     */
    g_object_class_install_property(gobject_class, PROP_MAIN_CONTEXT,
                                    g_param_spec_pointer(
                                        "main-context",
                                        "The GMainContext to use for timeouts",
                                        "The GMainContext to use for timeouts",
                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  
    /* install signals */

    /**
     * n_agent_t::component-state-changed
     * @agent: The #n_agent_t object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @state: The #n_comp_state_e of the component
     *
     * This signal is fired whenever a component's state changes
     */
    signals[SIGNAL_COMP_STATE_CHANGED] =
        g_signal_new(
            "component-state-changed",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            3,
            G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
            G_TYPE_INVALID);

    /**
     * n_agent_t::candidate-gathering-done:
     * @agent: The #n_agent_t object
     * @stream_id: The ID of the stream
     *
     * This signal is fired whenever a stream has finished gathering its
     * candidates after a call to n_agent_gather_cands()
     */
    signals[SIGNAL_CANDIDATE_GATHERING_DONE] =
        g_signal_new(
            "candidate-gathering-done",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            1,
            G_TYPE_UINT, G_TYPE_INVALID);

    /**
     * n_agent_t::new-selected-pair
     * @agent: The #n_agent_t object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @lfoundation: The local foundation of the selected candidate pair
     * @rfoundation: The remote foundation of the selected candidate pair
     *
     * This signal is fired once a candidate pair is selected for data
     * transfer for a stream's component This is emitted along with
     * #n_agent_t::new-selected-pair-full which has the whole candidate,
     * the Foundation of a Candidate is not a unique identifier.
     *
     * See also: #n_agent_t::new-selected-pair-full
     * Deprecated: 0.1.8: Use #n_agent_t::new-selected-pair-full
     */
    signals[SIGNAL_NEW_SELECTED_PAIR] =
        g_signal_new(
            "new-selected-pair",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            4,
            G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING,
            G_TYPE_INVALID);

    /**
     * n_agent_t::new-candidate
     * @agent: The #n_agent_t object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @foundation: The foundation of the new candidate
     *
     * This signal is fired when the agent discovers a new local candidate.
     * When this signal is emitted, a matching #n_agent_t::new-candidate-full is
     * also emitted with the candidate.
     *
     * See also: #n_agent_t::candidate-gathering-done,
     * #n_agent_t::new-candidate-full
     * Deprecated: 0.1.8: Use #n_agent_t::new-candidate-full
     */
    signals[SIGNAL_NEW_CANDIDATE] =
        g_signal_new(
            "new-candidate",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            3,
            G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING,
            G_TYPE_INVALID);

    /**
     * n_agent_t::new-remote-candidate
     * @agent: The #n_agent_t object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @foundation: The foundation of the new candidate
     *
     * This signal is fired when the agent discovers a new remote
     * candidate.  This can happen with peer reflexive candidates.  When
     * this signal is emitted, a matching
     * #n_agent_t::new-remote-candidate-full is also emitted with the
     * candidate.
     *
     * See also: #n_agent_t::new-remote-candidate-full
     * Deprecated: 0.1.8: Use #n_agent_t::new-remote-candidate-full
     */
    signals[SIGNAL_NEW_REMOTE_CANDIDATE] =
        g_signal_new(
            "new-remote-candidate",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            3,
            G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING,
            G_TYPE_INVALID);

    /**
     * n_agent_t::initial-binding-request-received
     * @agent: The #n_agent_t object
     * @stream_id: The ID of the stream
     *
     * This signal is fired when we received our first binding request from
     * the peer.
     */
    signals[SIGNAL_INITIAL_BINDING_REQUEST_RECEIVED] =
        g_signal_new(
            "initial-binding-request-received",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            1,
            G_TYPE_UINT,
            G_TYPE_INVALID);

    /**
     * n_agent_t::reliable-transport-writable
     * @agent: The #n_agent_t object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     *
     * This signal is fired on the reliable #n_agent_t when the underlying reliable
     * transport becomes writable.
     * This signal is only emitted when the n_agent_send() function returns less
     * bytes than requested to send (or -1) and once when the connection
     * is established.
     *
     * Since: 0.0.11
     */
    signals[SIGNAL_RELIABLE_TRANSPORT_WRITABLE] =
        g_signal_new(
            "reliable-transport-writable",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            2,
            G_TYPE_UINT, G_TYPE_UINT,
            G_TYPE_INVALID);

    /**
     * n_agent_t::streams-removed
     * @agent: The #n_agent_t object
     * @stream_ids: (array zero-terminated=1) (element-type uint): An array of
     * unsigned integer stream IDs, ending with a 0 ID
     *
     * This signal is fired whenever one or more streams are removed from the
     * @agent.
     *
     * Since: 0.1.5
     */
    signals[SIGNAL_STREAMS_REMOVED] =
        g_signal_new(
            "streams-removed",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__POINTER,
            G_TYPE_NONE,
            1,
            NICE_TYPE_AGENT_STREAM_IDS,
            G_TYPE_INVALID);


    /**
     * n_agent_t::new-selected-pair-full
     * @agent: The #n_agent_t object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @lcandidate: The local #n_cand_t of the selected candidate pair
     * @rcandidate: The remote #n_cand_t of the selected candidate pair
     *
     * This signal is fired once a candidate pair is selected for data
     * transfer for a stream's component. This is emitted along with
     * #n_agent_t::new-selected-pair.
     *
     * See also: #n_agent_t::new-selected-pair
     * Since: 0.1.8
     */
    signals[SIGNAL_NEW_SELECTED_PAIR_FULL] =
        g_signal_new(
            "new-selected-pair-full",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            4, G_TYPE_UINT, G_TYPE_UINT, NICE_TYPE_CANDIDATE, NICE_TYPE_CANDIDATE,
            G_TYPE_INVALID);

    /**
     * n_agent_t::new-candidate-full
     * @agent: The #n_agent_t object
     * @candidate: The new #n_cand_t
     *
     * This signal is fired when the agent discovers a new local candidate.
     * When this signal is emitted, a matching #n_agent_t::new-candidate is
     * also emitted with the candidate's foundation.
     *
     * See also: #n_agent_t::candidate-gathering-done,
     * #n_agent_t::new-candidate
     * Since: 0.1.8
     */
    signals[SIGNAL_NEW_CANDIDATE_FULL] =
        g_signal_new(
            "new-candidate-full",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            1,
            NICE_TYPE_CANDIDATE,
            G_TYPE_INVALID);

    /**
     * n_agent_t::new-remote-candidate-full
     * @agent: The #n_agent_t object
     * @candidate: The new #n_cand_t
     *
     * This signal is fired when the agent discovers a new remote candidate.
     * This can happen with peer reflexive candidates.
     * When this signal is emitted, a matching #n_agent_t::new-remote-candidate is
     * also emitted with the candidate's foundation.
     *
     * See also: #n_agent_t::new-remote-candidate
     * Since: 0.1.8
     */
    signals[SIGNAL_NEW_REMOTE_CANDIDATE_FULL] =
        g_signal_new(
            "new-remote-candidate-full",
            G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            NULL,
            G_TYPE_NONE,
            1,
            NICE_TYPE_CANDIDATE,
            G_TYPE_INVALID);

    /* Init debug options depending on env variables */
    nice_debug_init();
}

static void _generate_tie_breaker(n_agent_t * agent)
{
    nice_rng_generate_bytes(agent->rng, 8, (char *)&agent->tie_breaker);
}

static void n_agent_init(n_agent_t * agent)
{
	agent->main_context = NULL;
    agent->next_candidate_id = 1;
    agent->next_stream_id = 1;

    /* set defaults; not construct params, so set here */
    agent->stun_server_port = DEFAULT_STUN_PORT;
    agent->controlling_mode = TRUE;
    agent->max_conn_checks = _AGENT_MAX_CONNECTIVITY_CHECKS;

    agent->timer_ta = _AGENT_TIMER_TA_DEFAULT;

    agent->discovery_list = NULL;
    agent->disc_unsched_items = 0;
    agent->disc_timer_source = NULL;
    agent->conncheck_timer_source = NULL;
    agent->keepalive_timer_source = NULL;
    agent->refresh_list = NULL;
    agent->media_after_tick = FALSE;

    agent->reliable = TRUE;
    agent->use_ice_udp = TRUE;
    agent->use_ice_tcp = FALSE;
	agent->full_mode = TRUE;

    agent->rng = nice_rng_new();
    _generate_tie_breaker(agent);

    n_queue_init(&agent->pending_signals);
}

n_agent_t * n_agent_new()
{
    n_agent_t * agent = g_object_new(NICE_TYPE_AGENT, NULL);
    return agent;
}

static void n_agent_get_property(GObject * object, uint32_t property_id, GValue * value, GParamSpec * pspec)
{
    n_agent_t * agent = NICE_AGENT(object);

    agent_lock();

    switch (property_id)
    {
        case PROP_MAIN_CONTEXT:
            g_value_set_pointer(value, agent->main_context);
            break;

        case PROP_STUN_SERVER:
            g_value_set_string(value, agent->stun_server_ip);
            break;

        case PROP_STUN_SERVER_PORT:
            g_value_set_uint(value, agent->stun_server_port);
            break;

        case PROP_CONTROLLING_MODE:
            g_value_set_boolean(value, agent->controlling_mode);
            break;

        case PROP_FULL_MODE:
            g_value_set_boolean(value, agent->full_mode);
            break;

        case PROP_STUN_PACING_TIMER:
            g_value_set_uint(value, agent->timer_ta);
            break;

        case PROP_MAX_CONNECTIVITY_CHECKS:
            g_value_set_uint(value, agent->max_conn_checks);
            /* XXX: should we prune the list of already existing checks? */
            break;

        case PROP_RELIABLE:
            g_value_set_boolean(value, agent->reliable);
            break;

        case PROP_ICE_UDP:
            g_value_set_boolean(value, agent->use_ice_udp);
            break;

        case PROP_ICE_TCP:
            g_value_set_boolean(value, agent->use_ice_tcp);
            break;

        case PROP_BYTESTREAM_TCP:
            if (agent->reliable)
            {                
				g_value_set_boolean(value, FALSE);
            }
            else
            {
                g_value_set_boolean(value, FALSE);
            }
            break;

        case PROP_KEEPALIVE_CONNCHECK:
			g_value_set_boolean(value, agent->keepalive_conncheck);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }

    agent_unlock_and_emit(agent);
}

void n_agent_init_stun_agent(n_agent_t * agent, stun_agent_t * stun_agent)
{
    stun_agent_init(stun_agent, 0);
}

static void n_agent_reset_all_stun_agents(n_agent_t * agent, int32_t only_software)
{
    n_slist_t * stream_item, *comp_item;

    for (stream_item = agent->streams_list; stream_item; stream_item = stream_item->next)
    {
        n_stream_t * stream = stream_item->data;

        for (comp_item = stream->components; comp_item; comp_item = comp_item->next)
        {
            n_comp_t * comp = comp_item->data;
            n_agent_init_stun_agent(agent, &comp->stun_agent);
        }
    }
}

static void nice_agent_set_property(GObject * object, uint32_t property_id, const GValue * value, GParamSpec * pspec)
{
    
}

static void agent_signal_socket_writable(n_agent_t * agent, n_comp_t * component)
{
    g_cancellable_cancel(component->tcp_writable_cancellable);
    agent_queue_signal(agent, signals[SIGNAL_RELIABLE_TRANSPORT_WRITABLE], component->stream->id, component->id);
}

static void pst_create(n_agent_t * agent, n_stream_t * stream, n_comp_t * component)
{
    pst_callback_t tcp_callbacks = 
	{
		component,
		pst_opened,
		pst_readable,
		pst_writable,
		pst_closed,
		pst_write_packet
    };
    component->tcp = pst_new(0, &tcp_callbacks);
    component->tcp_writable_cancellable = g_cancellable_new();
    nice_debug("[%s]: Create Pseudo Tcp Socket for component %d", G_STRFUNC, component->id);
}

static void _pseudo_tcp_error(n_agent_t * agent, n_stream_t * stream, n_comp_t * comp)
{
    if (comp->tcp_writable_cancellable)
    {
        g_cancellable_cancel(comp->tcp_writable_cancellable);
        g_clear_object(&comp->tcp_writable_cancellable);
    }

    if (comp->tcp)
    {
        agent_sig_comp_state_change(agent, stream->id, comp->id, COMP_STATE_FAILED);
        component_detach_all_sockets(comp);
        pst_close(comp->tcp, TRUE);
    }

    if (comp->tcp_clock)
    {
        g_source_destroy(comp->tcp_clock);
        g_source_unref(comp->tcp_clock);
		comp->tcp_clock = NULL;
    }
}

static void pst_opened(pst_socket_t * sock, void * user_data)
{
    n_comp_t * comp = user_data;
    n_agent_t * agent = comp->agent;
    n_stream_t * stream = comp->stream;

    nice_debug("[%s]: s%d:%d pseudo Tcp socket Opened", G_STRFUNC,  stream->id, comp->id);

    agent_signal_socket_writable(agent, comp);
}

/* Will attempt to queue all @n_messages into the pseudo-TCP transmission
 * buffer. This is always used in reliable mode, so essentially treats @messages
 * as a massive flat array of buffers.
 *
 * Returns the number of messages successfully sent on success (which may be
 * zero if sending the first buffer of the message would have blocked), or
 * a negative number on error. If "allow_partial" is TRUE, then it returns
 * the number of bytes sent
 */
static int32_t pst_send_messages(pst_socket_t * self, const n_output_msg_t * messages,
        uint32_t n_messages, int32_t allow_partial, GError ** error)
{
    uint32_t i;
    int32_t bytes_sent = 0;

    for (i = 0; i < n_messages; i++)
    {
        const n_output_msg_t * message = &messages[i];
        uint32_t j;

        /* If allow_partial is FALSE and there??s not enough space for the
         * entire message, bail now before queuing anything. This doesn??t
         * gel with the fact this function is only used in reliable mode,
         * and there is no concept of a ??message??, but is necessary
         * because the calling API has no way of returning to the client
         * and indicating that a message was partially sent. */
        if (!allow_partial &&
                output_message_get_size(message) >
                pst_get_available_send_space(self))
        {
            return i;
        }

        for (j = 0;
                (message->n_buffers >= 0 && j < (uint32_t) message->n_buffers) ||
                (message->n_buffers < 0 && message->buffers[j].buffer != NULL);
                j++)
        {
            const n_outvector_t * buffer = &message->buffers[j];
            int32_t ret;

            /* Send on the pseudo-TCP socket. */
            ret = pst_send(self, buffer->buffer, buffer->size);

            /* In case of -1, the error is either EWOULDBLOCK or ENOTCONN, which both
             * need the user to wait for the reliable-transport-writable signal */
            if (ret < 0)
            {
                if (pst_get_error(self) == EWOULDBLOCK)
                    goto out;

                if (pst_get_error(self) == ENOTCONN ||  pst_get_error(self) == EPIPE)
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK, "TCP connection is not yet established.");
                else
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Error writing data to pseudo-TCP socket.");
                return -1;
            }
            else
            {
                bytes_sent += ret;
            }
        }
    }

out:

    return allow_partial ? bytes_sent : (int32_t) i;
}

/* Will fill up @messages from the first free byte onwards (as determined using
 * @iter). This is always used in reliable mode, so it essentially treats
 * @messages as a massive flat array of buffers.
 *
 * Updates @iter in place. @iter and @messages are left in invalid states if
 * an error is returned.
 *
 * Returns the number of valid messages in @messages on success (which may be
 * zero if no data is pending and the peer has disconnected), or a negative
 * number on error (including if the request would have blocked returning no
 * messages). */
static int32_t
pst_recv_messages(pst_socket_t * self, n_input_msg_t * messages, uint32_t n_messages, NiceInputMessageIter * iter, GError ** error)
{
    for (; iter->message < n_messages; iter->message++)
    {
        n_input_msg_t * message = &messages[iter->message];

        if (iter->buffer == 0 && iter->offset == 0)
        {
            message->length = 0;
        }

        for (;
                (message->n_buffers >= 0 && iter->buffer < (uint32_t) message->n_buffers) ||
                (message->n_buffers < 0 && message->buffers[iter->buffer].buffer != NULL);
                iter->buffer++)
        {
            n_invector_t * buffer = &message->buffers[iter->buffer];

            do
            {
                int32_t len;

                len = pst_recv(self, (char *) buffer->buffer + iter->offset, buffer->size - iter->offset);

                nice_debug("%s: Received %" G_GSSIZE_FORMAT " bytes into "
                           "buffer %p (offset %" G_GSIZE_FORMAT ", length %" G_GSIZE_FORMAT
                           ").", G_STRFUNC, len, buffer->buffer, iter->offset, buffer->size);

                if (len == 0)
                {
                    /* Reached EOS. */
                    len = 0;
                    goto done;
                }
                else if (len < 0 && pst_get_error(self) == EWOULDBLOCK)
                {
                    /* EWOULDBLOCK. If we??ve already received something, return that;
                     * otherwise, error. */
                    if (n_input_msg_iter_get_n_valid_msgs(iter) > 0)
                    {
                        goto done;
                    }
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK,
                                "Error reading data from pseudo-TCP socket: would block.");
                    return len;
                }
                else if (len < 0 && pst_get_error(self) == ENOTCONN)
                {
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK,
                                "Error reading data from pseudo-TCP socket: not connected.");
                    return len;
                }
                else if (len < 0)
                {
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Error reading data from pseudo-TCP socket.");
                    return len;
                }
                else
                {
                    /* Got some data! */
                    message->length += len;
                    iter->offset += len;
                }
            }
            while (iter->offset < buffer->size);

            iter->offset = 0;
        }

        iter->buffer = 0;
    }

done:
    return n_input_msg_iter_get_n_valid_msgs(iter);
}

/* This is called with the agent lock held. */
static void pst_readable(pst_socket_t * sock, void * user_data)
{
    n_comp_t * component = user_data;
    n_agent_t * agent = component->agent;
    n_stream_t * stream = component->stream;
    int32_t has_io_callback;
    uint32_t stream_id = stream->id;
    uint32_t component_id = component->id;

    g_object_ref(agent);

    nice_debug("[%s]: s%d:%d pseudo Tcp socket readable", G_STRFUNC, stream->id, component->id);

    component->tcp_readable = TRUE;

    has_io_callback = component_has_io_callback(component);

    /* Only dequeue pseudo-TCP data if we can reliably inform the client. The
     * agent lock is held here, so has_io_callback can only change during
     * component_emit_io_callback(), after which it??s re-queried. This ensures
     * no data loss of packets already received and dequeued. */
    if (has_io_callback)
    {
        do
        {
            uint8_t buf[MAX_BUFFER_SIZE];
            int32_t len;

            /* FIXME: Why copy into a temporary buffer here? Why can't the I/O
             * callbacks be emitted directly from the pseudo-TCP receive buffer? */
            len = pst_recv(sock, (char *) buf, sizeof(buf));

            nice_debug("%s: I/O callback case: Received %" G_GSSIZE_FORMAT " bytes", G_STRFUNC, len);

            if (len == 0)
            {
                /* Reached EOS. */
                component->tcp_readable = FALSE;
                pst_close(component->tcp, FALSE);
                break;
            }
            else if (len < 0)
            {
                /* Handle errors. */
                if (pst_get_error(sock) != EWOULDBLOCK)
                {
                    nice_debug("%s: calling _pseudo_tcp_error()", G_STRFUNC);
                    _pseudo_tcp_error(agent, stream, component);
                }

                if (component->recv_buf_error != NULL)
                {
                    GIOErrorEnum error_code;

                    if (pst_get_error(sock) == ENOTCONN)
                        error_code = G_IO_ERROR_BROKEN_PIPE;
                    else if (pst_get_error(sock) == EWOULDBLOCK)
                        error_code = G_IO_ERROR_WOULD_BLOCK;
                    else
                        error_code = G_IO_ERROR_FAILED;

                    g_set_error(component->recv_buf_error, G_IO_ERROR, error_code, "Error reading data from pseudo-TCP socket.");
                }
                break;
            }

            component_emit_io_callback(component, buf, len);

            if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
            {
                nice_debug("n_stream_t or n_comp_t disappeared during the callback");
                goto out;
            }
            if (pst_is_closed(component->tcp))
            {
                nice_debug("PseudoTCP socket got destroyed in readable callback!");
                goto out;
            }

            has_io_callback = component_has_io_callback(component);
        }
        while (has_io_callback);
    }
    else if (component->recv_messages != NULL)
    {
        int32_t n_valid_messages;
        GError * child_error = NULL;

        /* Fill up every buffer in every message until the connection closes or an
         * error occurs. Copy the data directly into the client's receive message
         * array without making any callbacks. Update component->recv_messages_iter
         * as we go. */
        n_valid_messages = pst_recv_messages(sock, component->recv_messages, component->n_recv_messages,
                           &component->recv_messages_iter, &child_error);

        nice_debug("%s: Client buffers case: Received %d valid messages:", G_STRFUNC, n_valid_messages);
        n_debug_input_msg(component->recv_messages, component->n_recv_messages);

        if (n_valid_messages < 0)
        {
            g_propagate_error(component->recv_buf_error, child_error);
        }
        else
        {
            g_clear_error(&child_error);
        }

        if (n_valid_messages < 0 && g_error_matches(child_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
        {
            component->tcp_readable = FALSE;
        }
        else if (n_valid_messages < 0)
        {
            nice_debug("%s: calling _pseudo_tcp_error()", G_STRFUNC);
            _pseudo_tcp_error(agent, stream, component);
        }
        else if (n_valid_messages == 0)
        {
            /* Reached EOS. */
            component->tcp_readable = FALSE;
            pst_close(component->tcp, FALSE);
        }
    }
    else
    {
        nice_debug("%s: no data read", G_STRFUNC);
    }

    if (stream && component)
        adjust_tcp_clock(agent, stream, component);

out:
    g_object_unref(agent);
}

static void pst_writable(pst_socket_t * sock, void * user_data)
{
    n_comp_t * component = user_data;
    n_agent_t * agent = component->agent;
    n_stream_t * stream = component->stream;

    nice_debug("[%s]: s%d:%d pseudo Tcp socket writable", G_STRFUNC, stream->id, component->id);

    agent_signal_socket_writable(agent, component);
}

static void pst_closed(pst_socket_t * sock, uint32_t err, void * user_data)
{
    n_comp_t * comp = user_data;
    n_agent_t * agent = comp->agent;
    n_stream_t * stream = comp->stream;

    nice_debug("[%s]: s%d:%d pseudo Tcp socket closed, Calling _pseudo_tcp_error()", G_STRFUNC,  stream->id, comp->id);
    _pseudo_tcp_error(agent, stream, comp);
}


static pst_wret_e pst_write_packet(pst_socket_t * psocket, const char * buffer, uint32_t len, void * user_data)
{
    n_comp_t * component = user_data;

    if (component->selected_pair.local != NULL)
    {
        n_socket_t * sock;
        n_addr_t * addr;

        sock = component->selected_pair.local->sockptr;
        addr = &component->selected_pair.remote->addr;

        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN];
            nice_address_to_string(addr, tmpbuf);

            nice_debug("[%s]: s%d:%d: sending %d bytes on socket %p (FD %d) to [%s]:%d",
				G_STRFUNC, component->stream->id, component->id, len,
                sock->fileno, g_socket_get_fd(sock->fileno), tmpbuf,
                nice_address_get_port(addr));
        }

        /* Send the segment. nice_socket_send() returns 0 on EWOULDBLOCK; in that
         * case the segment is not sent on the wire, but we return WR_SUCCESS
         * anyway. This effectively drops the segment. The pseudo-TCP state machine
         * will eventually pick up this loss and go into recovery mode, reducing
         * its transmission rate and, hopefully, the usage of system resources
         * which caused the EWOULDBLOCK in the first place. */
        if (nice_socket_send(sock, addr, len, buffer) >= 0)
        {
            return WR_SUCCESS;
        }
    }
    else
    {
        nice_debug("%s: WARNING: Failed to send pseudo-TCP packet from agent %p "
                   "as no pair has been selected yet.", G_STRFUNC, component->agent);
    }

    return WR_FAIL;
}


static int notify_pst_clock(void * user_data)
{
    n_comp_t * component = user_data;
    n_stream_t * stream;
    n_agent_t * agent;

    agent_lock();

    stream = component->stream;
    agent = component->agent;

    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. " "Avoided race condition in notify_pst_clock");
        agent_unlock();
        return FALSE;
    }

    pst_notify_clock(component->tcp);
    adjust_tcp_clock(agent, stream, component);

    agent_unlock_and_emit(agent);

    return G_SOURCE_CONTINUE;
}

static void adjust_tcp_clock(n_agent_t * agent, n_stream_t * stream, n_comp_t * component)
{
    if (!pst_is_closed(component->tcp))
    {
        guint64 timeout = component->last_clock_timeout;

        if (pst_get_next_clock(component->tcp, &timeout))
        {
            if (timeout != component->last_clock_timeout)
            {
                component->last_clock_timeout = timeout;
                if (component->tcp_clock)
                {
                    g_source_set_ready_time(component->tcp_clock, timeout * 1000);
                }
                if (!component->tcp_clock)
                {
                    int32_t interval = (int32_t)timeout - (uint32_t)(g_get_monotonic_time() / 1000);

                    /* Prevent integer overflows */
                    if (interval < 0 || interval > INT_MAX)
                        interval = INT_MAX;
                    agent_timeout_add(agent, &component->tcp_clock, "Pseudo-TCP clock", interval, notify_pst_clock, component);
                }
            }
        }
        else
        {
            nice_debug("[%s]: component %d pseudo-TCP socket should be "
                       "destroyed. Calling _pseudo_tcp_error().", G_STRFUNC, component->id);
            _pseudo_tcp_error(agent, stream, component);
        }
    }
}

static void _tcp_sock_is_writable(n_socket_t * sock, void * user_data)
{
    n_comp_t * component = user_data;
    n_agent_t * agent = component->agent;
    n_stream_t * stream = component->stream;

    agent_lock();

    /* Don't signal writable if the socket that has become writable is not
     * the selected pair */
    if (component->selected_pair.local == NULL || component->selected_pair.local->sockptr != sock)
    {
        agent_unlock();
        return;
    }

    nice_debug("[%s]: s%d:%d Tcp socket writable", G_STRFUNC, stream->id, component->id);
    agent_signal_socket_writable(agent, component);

    agent_unlock_and_emit(agent);
}

static const char * _transport_to_string(n_cand_trans_e type)
{
    switch (type)
    {
        case CAND_TRANS_UDP:
            return "UDP";
        case CAND_TRANS_TCP_ACTIVE:
            return "TCP-ACT";
        case CAND_TRANS_TCP_PASSIVE:
            return "TCP-PASS";
        case CAND_TRANS_TCP_SO:
            return "TCP-SO";
        default:
            return "???";
    }
}

void agent_gathering_done(n_agent_t * agent)
{
	n_slist_t * i, *j, *k, *l, *m;

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * stream = i->data;
        for (j = stream->components; j; j = j->next)
        {
            n_comp_t * component = j->data;

            for (k = component->local_candidates; k; k = k->next)
            {
                n_cand_t * local_candidate = k->data;
                if (nice_debug_is_enabled())
                {
                    char tmpbuf[INET6_ADDRSTRLEN];
                    nice_address_to_string(&local_candidate->addr, tmpbuf);
                    nice_debug("[%s]: gathered %s local candidate : [%s]:%u"
                               " for s%d/c%d", G_STRFUNC,
                               _transport_to_string(local_candidate->transport),
                               tmpbuf, nice_address_get_port(&local_candidate->addr),
                               local_candidate->stream_id, local_candidate->component_id);
                }
                for (l = component->remote_candidates; l; l = l->next)
                {
                    n_cand_t * remote_candidate = l->data;

                    for (m = stream->conncheck_list; m; m = m->next)
                    {
                        n_cand_chk_pair_t * p = m->data;

                        if (p->local == local_candidate && p->remote == remote_candidate)
                            break;
                    }
                    if (m == NULL)
                    {
                        cocheck_add_cand_pair(agent, stream->id, component,
                                                          local_candidate, remote_candidate);
                    }
                }
            }
        }
    }

    if (agent->disc_timer_source == NULL)
        agent_sig_gathering_done(agent);
}

void agent_sig_gathering_done(n_agent_t * agent)
{
	n_slist_t * i;

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * stream = i->data;
        if (stream->gathering)
        {
            stream->gathering = FALSE;
            agent_queue_signal(agent, signals[SIGNAL_CANDIDATE_GATHERING_DONE], stream->id);
			if (agent->n_event)
			{
				event_post(agent->n_event, N_EVENT_CAND_GATHERING_DONE);
			}
        }
    }
}

void agent_sig_initial_binding_request_received(n_agent_t * agent, n_stream_t * stream)
{
    if (stream->initial_binding_request_received != TRUE)
    {
        stream->initial_binding_request_received = TRUE;
        agent_queue_signal(agent, signals[SIGNAL_INITIAL_BINDING_REQUEST_RECEIVED], stream->id);
    }
}

/* If the n_comp_t now has a selected_pair, and has pending TCP packets which
 * it couldn??t receive before due to not being able to send out ACKs (or
 * SYNACKs, for the initial SYN packet), handle them now.
 *
 * Must be called with the agent lock held. */
static void process_queued_tcp_packets(n_agent_t * agent, n_stream_t * stream, n_comp_t * component)
{
    n_outvector_t * vec;
    uint32_t stream_id = stream->id;
    uint32_t component_id = component->id;

    g_assert(agent->reliable);

    if (component->selected_pair.local == NULL ||
            pst_is_closed(component->tcp) ||
            nice_socket_is_reliable(component->selected_pair.local->sockptr))
    {
        return;
    }

    nice_debug("%s: Sending outstanding packets for agent %p.", G_STRFUNC);

    while ((vec = n_queue_peek_head(&component->queued_tcp_packets)) != NULL)
    {
        int32_t retval;

        nice_debug("%s: Sending %" G_GSIZE_FORMAT " bytes.", G_STRFUNC, vec->size);
        retval =  pst_notify_packet(component->tcp, vec->buffer, vec->size);

        if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
        {
            nice_debug("n_stream_t or n_comp_t disappeared during " "pst_notify_packet()");
            return;
        }
        if (pst_is_closed(component->tcp))
        {
            nice_debug("PseudoTCP socket got destroyed in" " pst_notify_packet()!");
            return;
        }

        adjust_tcp_clock(agent, stream, component);

        if (!retval)
        {
            /* Failed to send; try again later. */
            break;
        }

        n_queue_pop_head(&component->queued_tcp_packets);
        n_free((void *) vec->buffer);
        n_slice_free(n_outvector_t, vec);
    }
}

void agent_sig_new_selected_pair(n_agent_t * agent, uint32_t stream_id,
                                    uint32_t component_id, n_cand_t * lcandidate, n_cand_t * rcandidate)
{
    n_comp_t * component;
    n_stream_t * stream;

    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
        return;

    if (((n_socket_t *)lcandidate->sockptr)->type == NICE_SOCKET_TYPE_UDP_TURN)
    {
        nice_udp_turn_socket_set_peer(lcandidate->sockptr, &rcandidate->addr);
    }

    if (agent->reliable && !nice_socket_is_reliable(lcandidate->sockptr))
    {
        if (!component->tcp)
            pst_create(agent, stream, component);
        process_queued_tcp_packets(agent, stream, component);

        pst_connect(component->tcp);
        pst_notify_mtu(component->tcp, MAX_TCP_MTU);
        adjust_tcp_clock(agent, stream, component);
    }

    if (nice_debug_is_enabled())
    {
        char ip[100];
        uint32_t port;

        port = nice_address_get_port(&lcandidate->addr);
        nice_address_to_string(&lcandidate->addr, ip);

        nice_debug("[%s]: Local selected pair: %d:%d %s %s %s:%d %s", G_STRFUNC,
                   stream_id, component_id, lcandidate->foundation,
                   lcandidate->transport == CAND_TRANS_UDP ? "UDP" : "???",
                   ip, port, lcandidate->type == CAND_TYPE_HOST ? "HOST" :
                   lcandidate->type == CAND_TYPE_SERVER ?
                   "SRV-RFLX" :
                   lcandidate->type == CAND_TYPE_RELAYED ?
                   "RELAYED" :
                   lcandidate->type == CAND_TYPE_PEER ?
                   "PEER-RFLX" : "???");

        port = nice_address_get_port(&rcandidate->addr);
        nice_address_to_string(&rcandidate->addr, ip);

        nice_debug("[%s]: Remote selected pair: %d:%d %s %s %s:%d %s", G_STRFUNC,
                   stream_id, component_id, rcandidate->foundation,
                   rcandidate->transport == CAND_TRANS_UDP ? "UDP" : "???",
                   ip, port, rcandidate->type == CAND_TYPE_HOST ? "HOST" :
                   rcandidate->type == CAND_TYPE_SERVER ?
                   "SRV-RFLX" :
                   rcandidate->type == CAND_TYPE_RELAYED ?
                   "RELAYED" :
                   rcandidate->type == CAND_TYPE_PEER ?
                   "PEER-RFLX" : "???");
		nice_print_cand(agent, lcandidate, rcandidate);
    }

    agent_queue_signal(agent, signals[SIGNAL_NEW_SELECTED_PAIR_FULL],
                       stream_id, component_id, lcandidate, rcandidate);
    agent_queue_signal(agent, signals[SIGNAL_NEW_SELECTED_PAIR],
                       stream_id, component_id, lcandidate->foundation, rcandidate->foundation);

    if (agent->reliable && nice_socket_is_reliable(lcandidate->sockptr))
    {
        agent_signal_socket_writable(agent, component);
    }
}

void agent_sig_new_cand(n_agent_t * agent, n_cand_t * candidate)
{
    agent_queue_signal(agent, signals[SIGNAL_NEW_CANDIDATE_FULL],  candidate);
    agent_queue_signal(agent, signals[SIGNAL_NEW_CANDIDATE],
                       candidate->stream_id, candidate->component_id, candidate->foundation);
}

void agent_sig_new_remote_cand(n_agent_t * agent, n_cand_t * candidate)
{
    agent_queue_signal(agent, signals[SIGNAL_NEW_REMOTE_CANDIDATE_FULL], candidate);
    agent_queue_signal(agent, signals[SIGNAL_NEW_REMOTE_CANDIDATE],
                       candidate->stream_id, candidate->component_id, candidate->foundation);
}

const char * n_comp_state_to_str(n_comp_state_e state)
{
    switch (state)
    {
        case COMP_STATE_DISCONNECTED:
            return "disconnected";
        case COMP_STATE_GATHERING:
            return "gathering";
        case COMP_STATE_CONNECTING:
            return "connecting";
        case COMP_STATE_CONNECTED:
            return "connected";
        case COMP_STATE_READY:
            return "ready";
        case COMP_STATE_FAILED:
            return "failed";
        case COMP_STATE_LAST:
        default:
            return "invalid";
    }
}

void agent_sig_comp_state_change(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_comp_state_e state)
{
    n_comp_t * component;
    n_stream_t * stream;

    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
        return;

    if (component->state != state && state < COMP_STATE_LAST)
    {
        nice_debug("[%s]: stream %u component %u STATE-CHANGE (%s -> %s)", G_STRFUNC,
                   stream_id, component_id, n_comp_state_to_str(component->state),
                   n_comp_state_to_str(state));

        component->state = state;

        if (agent->reliable)
            process_queued_tcp_packets(agent, stream, component);

        agent_queue_signal(agent, signals[SIGNAL_COMP_STATE_CHANGED], stream_id, component_id, state);
    }
}

uint64_t agent_candidate_pair_priority(n_agent_t * agent, n_cand_t * local, n_cand_t * remote)
{
    if (agent->controlling_mode)
        return n_cand_pair_priority(local->priority, remote->priority);
    else
        return n_cand_pair_priority(remote->priority, local->priority);
}

static void _add_new_cdisc_stun(n_agent_t * agent, n_socket_t * nicesock, n_addr_t server, n_stream_t * stream, uint32_t component_id)
{
    n_cand_disc_t * cdisco;

    /* note: no need to check for redundant candidates, as this is
     *       done later on in the process */

    cdisco = n_slice_new0(n_cand_disc_t);

    cdisco->type = CAND_TYPE_SERVER;
    cdisco->nicesock = nicesock;
    cdisco->server = server;
    cdisco->stream = stream;
    cdisco->component = stream_find_comp_by_id(stream, component_id);
    cdisco->agent = agent;
    stun_agent_init(&cdisco->stun_agent, 0);

    nice_debug("[%s]: Adding new srv-rflx candidate discovery %p", G_STRFUNC, cdisco);
    agent->discovery_list = n_slist_append(agent->discovery_list, cdisco);
    ++agent->disc_unsched_items;
}

static void _add_new_cdisc_turn(n_agent_t * agent, n_socket_t * nicesock, TurnServer * turn, n_stream_t * stream, uint32_t comp_id)
{
    n_cand_disc_t * cdisco;
    n_comp_t * comp = stream_find_comp_by_id(stream, comp_id);
    //n_addr_t local_address;

    /* note: no need to check for redundant candidates, as this is
     *       done later on in the process */

    cdisco = n_slice_new0(n_cand_disc_t);
    cdisco->type = CAND_TYPE_RELAYED;
    cdisco->nicesock = nicesock;
    cdisco->turn = turn_server_ref(turn);
    cdisco->server = turn->server;

    cdisco->stream = stream;
    cdisco->component = stream_find_comp_by_id(stream, comp_id);
    cdisco->agent = agent;

    stun_agent_init(&cdisco->stun_agent, STUN_AGENT_LONG_TERM_CREDENTIALS);

    nice_debug("[%s]: Adding new relay-rflx candidate discovery %p", G_STRFUNC, cdisco);
    agent->discovery_list = n_slist_append(agent->discovery_list, cdisco);
    ++agent->disc_unsched_items;
}

uint32_t n_agent_add_stream(n_agent_t * agent, uint32_t n_components)
{
    n_stream_t * stream;
    uint32_t ret = 0;
    uint32_t i;

    g_return_val_if_fail(NICE_IS_AGENT(agent), 0);
    g_return_val_if_fail(n_components >= 1, 0);

    agent_lock();
    stream = stream_new(n_components, agent);

    agent->streams_list = n_slist_append(agent->streams_list, stream);
    stream->id = agent->next_stream_id++;
    nice_debug("[%s]: allocating stream id %u (%p)", G_STRFUNC, stream->id, stream);
    if (agent->reliable)
    {
        nice_debug("[%s]: reliable stream", G_STRFUNC);
        for (i = 0; i < n_components; i++)
        {
            n_comp_t * component = stream_find_comp_by_id(stream, i + 1);
            if (component)
            {
                pst_create(agent, stream, component);
            }
            else
            {
                nice_debug("[%s]: couldn't find component %d", G_STRFUNC, i + 1);
            }
        }
    }

    stream_initialize_credentials(stream, agent->rng);

    ret = stream->id;

    agent_unlock_and_emit(agent);
    return ret;
}


int n_agent_set_relay_info(n_agent_t * agent, uint32_t stream_id, uint32_t component_id,  const char * server_ip,
                              uint32_t server_port, const char * username, const char * password, n_relay_type_e type)
{
    n_comp_t * component = NULL;
    n_stream_t * stream = NULL;
    int32_t ret = TRUE;
    TurnServer * turn;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(component_id >= 1, FALSE);
    g_return_val_if_fail(server_ip, FALSE);
    g_return_val_if_fail(server_port, FALSE);
    g_return_val_if_fail(username, FALSE);
    g_return_val_if_fail(password, FALSE);
    g_return_val_if_fail(type <= RELAY_TYPE_TURN_TLS, FALSE);

    agent_lock();

    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
    {
        ret = FALSE;
        goto done;
    }

    turn = turn_server_new(server_ip, server_port, username, password, type);

    if (!turn)
    {
        ret = FALSE;
        goto done;
    }

    nice_debug("[%s]: added relay server [%s]:%d of type %d to s/c %d/%d "
               "with user/pass : %s -- %s", G_STRFUNC, server_ip, server_port, type,
               stream_id, component_id, username, password);

    component->turn_servers = n_dlist_append(component->turn_servers, turn);

    if (stream->gathering_started)
    {
		n_slist_t * i;

        stream->gathering = TRUE;

        for (i = component->local_candidates; i; i = i->next)
        {
            n_cand_t * candidate = i->data;

            if (candidate->type == CAND_TYPE_HOST)
                _add_new_cdisc_turn(agent, candidate->sockptr, turn, stream, component_id);
        }

        if (agent->disc_unsched_items)
            disc_schedule(agent);
    }

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int n_agent_gather_cands(n_agent_t * agent, uint32_t stream_id)
{
	int32_t ret = TRUE;
    uint32_t cid;
	n_slist_t * i, * local_addresses = NULL;
    n_stream_t * stream;    
	//HostCandidateResult res = CANDIDATE_CANT_CREATE_SOCKET;

	agent_lock();

    stream = agent_find_stream(agent, stream_id);
    if (stream == NULL)
    {
        agent_unlock_and_emit(agent);
        return FALSE;
    }

    if (stream->gathering_started)
    {
        /* n_stream_t is already gathering, ignore this call */
        agent_unlock_and_emit(agent);
        return TRUE;
    }

    /* 获取本地所有网络接口的IP地址 */
    if (agent->local_addresses == NULL)
    {
		n_dlist_t * addresses = n_get_local_ips(FALSE);
		n_dlist_t * item;

        for (item = addresses; item; item = n_dlist_next(item))
        {
            const char * addr_string = item->data;
            n_addr_t * addr = nice_address_new();

            if (nice_address_set_from_string(addr, addr_string))
            {
                local_addresses = n_slist_append(local_addresses, addr);
            }
            else
            {
                nice_debug("Error: Failed to parse local address %s", addr_string);
                nice_address_free(addr);
            }
        }

        n_dlist_foreach(addresses, (n_func)n_free, NULL);
        n_dlist_free(addresses);
    }
    else
    {
        for (i = agent->local_addresses; i; i = i->next)
        {
            n_addr_t * addr = i->data;
            n_addr_t * dupaddr = nice_address_dup(addr);

            local_addresses = n_slist_append(local_addresses, dupaddr);
        }
    }

    /* 为所有本地地址生成候选地址 */
    for (i = local_addresses; i; i = i->next)
    {
        n_addr_t * addr = i->data;
        n_cand_t * host_candidate;
		uint32_t  current_port, start_port;
		HostCandidateResult res = CANDIDATE_CANT_CREATE_SOCKET;

        for (cid = 1; cid <= stream->n_components; cid++)
        {
            n_comp_t * component = stream_find_comp_by_id(stream, cid);

            if (component == NULL)
                continue;

            start_port = component->min_port;
            if (component->min_port != 0)
            {
                start_port = nice_rng_generate_int(agent->rng, component->min_port, component->max_port + 1);
            }
            current_port = start_port;

            host_candidate = NULL;
            while (res == CANDIDATE_CANT_CREATE_SOCKET)
            {
                nice_debug("[%s]: Trying to create host candidate on port %d", G_STRFUNC, current_port);
                nice_address_set_port(addr, current_port);
				/*添加本地候选*/
                res =  disc_add_local_host_cand(agent, stream->id, cid, addr, &host_candidate);
				nice_print_cand(agent, host_candidate, host_candidate);
                if (current_port > 0)
                    current_port++;
                if (current_port > component->max_port) current_port = component->min_port;
                if (current_port == 0 || current_port == start_port)
                    break;
            }

            if (res == CANDIDATE_REDUNDANT)
            {
                nice_debug("[%s]: Ignoring local candidate, it's redundant", G_STRFUNC);
                continue;
            }
            else if (res == CANDIDATE_FAILED)
            {
                nice_debug("[%s]: Could ot retrieive component %d/%d", G_STRFUNC, stream->id, cid);
                ret = FALSE;
                goto error;
            }
            else if (res == CANDIDATE_CANT_CREATE_SOCKET)
            {
                if (nice_debug_is_enabled())
                {
                    char ip[NICE_ADDRESS_STRING_LEN];
                    nice_address_to_string(addr, ip);
                    nice_debug("[%s]: Unable to add local host candidate %s for"
                                " s%d:%d. Invalid interface?", G_STRFUNC, ip, stream->id, component->id);
                }
                ret = FALSE;
                goto error;
            }

            /* TODO: Add server-reflexive support for TCP candidates */
            if (agent->stun_server_ip)
            {
                n_addr_t stun_server;
                if (nice_address_set_from_string(&stun_server, agent->stun_server_ip))
                {
                    nice_address_set_port(&stun_server, agent->stun_server_port);
					/*添加stun外网映射候选*/
                    _add_new_cdisc_stun(agent, host_candidate->sockptr, stun_server, stream, cid);
                }
            }

            if (component->turn_servers)
            {
                n_dlist_t * item;
                for (item = component->turn_servers; item; item = item->next)
                {
                    TurnServer * turn = item->data;
					/*添加turn转发候选*/
                    _add_new_cdisc_turn(agent, host_candidate->sockptr, turn, stream, cid);
                }
            }
        }
    }

    stream->gathering = TRUE;
    stream->gathering_started = TRUE;

    /* Only signal the new candidates after we're sure that the gathering was
     * successful. But before sending gathering-done */
    for (cid = 1; cid <= stream->n_components; cid++)
    {
        n_comp_t * component = stream_find_comp_by_id(stream, cid);
        for (i = component->local_candidates; i; i = i->next)
        {
            n_cand_t * candidate = i->data;
            agent_sig_new_cand(agent, candidate);
        }
    }

    /* note: no async discoveries pending, signal that we are ready */
    if (agent->disc_unsched_items == 0)
    {
        nice_debug("[%s]: Candidate gathering FINISHED, no scheduled items.", G_STRFUNC);
        agent_gathering_done(agent);
    }
    else if (agent->disc_unsched_items)
    {
        disc_schedule(agent);
    }

error:
	for (i = local_addresses; i; i = i->next)
	{
		nice_address_free(i->data);
	}    
    n_slist_free(local_addresses);

    if (ret == FALSE)
    {
        for (cid = 1; cid <= stream->n_components; cid++)
        {
            n_comp_t * component = stream_find_comp_by_id(stream, cid);

            component_free_socket_sources(component);

            for (i = component->local_candidates; i; i = i->next)
            {
                n_cand_t * candidate = i->data;

                agent_remove_local_candidate(agent, candidate);

                n_cand_free(candidate);
            }
            n_slist_free(component->local_candidates);
            component->local_candidates = NULL;
        }
        disc_prune_stream(agent, stream_id);
    }

    agent_unlock_and_emit(agent);

    return ret;
}

void agent_remove_local_candidate(n_agent_t * agent, n_cand_t * candidate)
{
}

static void _remove_keepalive_timer(n_agent_t * agent)
{
    if (agent->keepalive_timer_source != NULL)
    {
        g_source_destroy(agent->keepalive_timer_source);
        g_source_unref(agent->keepalive_timer_source);
        agent->keepalive_timer_source = NULL;
    }
}

void n_agent_remove_stream(n_agent_t * agent, uint32_t stream_id)
{
    uint32_t stream_ids[] = { stream_id, 0 };

    /* note that streams/candidates can be in use by other threads */

    n_stream_t * stream;

    g_return_if_fail(NICE_IS_AGENT(agent));
    g_return_if_fail(stream_id >= 1);

    agent_lock();
    stream = agent_find_stream(agent, stream_id);

    if (!stream)
    {
        agent_unlock_and_emit(agent);
        return;
    }

    /* note: remove items with matching stream_ids from both lists */
    cocheck_prune_stream(agent, stream);
    disc_prune_stream(agent, stream_id);
    refresh_prune_stream(agent, stream_id);

    /* Remove the stream and signal its removal. */
    agent->streams_list = n_slist_remove(agent->streams_list, stream);
    stream_close(stream);

    if (!agent->streams_list)
        _remove_keepalive_timer(agent);

    agent_queue_signal(agent, signals[SIGNAL_STREAMS_REMOVED], g_memdup(stream_ids, sizeof(stream_ids)));

    agent_unlock_and_emit(agent);

    /* Actually free the stream. This should be done with the lock released, as
     * it could end up disposing of a NiceIOStream, which tries to take the
     * agent lock itself. */
    stream_free(stream);

    return;
}

void n_agent_set_port_range(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, uint32_t min_port, uint32_t max_port)
{
    n_stream_t * stream;
    n_comp_t * component;

    g_return_if_fail(NICE_IS_AGENT(agent));
    g_return_if_fail(stream_id >= 1);
    g_return_if_fail(component_id >= 1);

    agent_lock();

    if (agent_find_comp(agent, stream_id, component_id, &stream, &component))
    {
        if (stream->gathering_started)
        {
            g_critical("n_agent_gather_cands (stream_id=%u) already called for this stream", stream_id);
        }
        else
        {
            component->min_port = min_port;
            component->max_port = max_port;
        }
    }

    agent_unlock_and_emit(agent);
}

int32_t n_agent_add_local_addr(n_agent_t * agent, n_addr_t * addr)
{
    n_addr_t * dupaddr;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(addr != NULL, FALSE);

    agent_lock();

    dupaddr = nice_address_dup(addr);
    nice_address_set_port(dupaddr, 0);
    agent->local_addresses = n_slist_append(agent->local_addresses, dupaddr);

    agent_unlock_and_emit(agent);
    return TRUE;
}

static int32_t _add_remote_cand(
    n_agent_t * agent,
    uint32_t stream_id,
    uint32_t comp_id,
    n_cand_type_e type,
    const n_addr_t * addr,
    const n_addr_t * base_addr,
    n_cand_trans_e transport,
	uint32_t priority,
    const char * username,
    const char * password,
    const char * foundation)
{
    n_comp_t * comp;
    n_cand_t * candidate;

    if (!agent_find_comp(agent, stream_id, comp_id, NULL, &comp))
        return FALSE;

    /* step: check whether the candidate already exists */
    candidate = comp_find_remote_cand(comp, addr);
    if (candidate)
    {
        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN];
            nice_address_to_string(addr, tmpbuf);
            nice_debug("[%s]: Updating existing remote candidate with addr [%s]:%u"
                       " for s%d/c%d. U/P '%s'/'%s' prio: %u", G_STRFUNC, tmpbuf,
                       nice_address_get_port(addr), stream_id, comp_id,
                       username, password, priority);
        }
        /* case 1: an existing candidate, update the attributes */
        candidate->type = type;
        if (base_addr)
            candidate->base_addr = *base_addr;
        candidate->priority = priority;
        if (foundation)
            g_strlcpy(candidate->foundation, foundation, CAND_MAX_FOUNDATION);
        /* note: username and password must remain the same during
         *       a session; see sect 9.1.2 in ICE ID-19 */

        /* note: however, the user/pass in ID-19 is global, if the user/pass
         * are set in the candidate here, it means they need to be updated...
         * this is essential to overcome a race condition where we might receive
         * a valid binding request from a valid candidate that wasn't yet added to
         * our list of candidates.. this 'update' will make the peer-rflx a
         * server-rflx/host candidate again and restore that user/pass it needed
         * to have in the first place */
        if (username)
        {
            n_free(candidate->username);
            candidate->username = g_strdup(username);
        }
        if (password)
        {
            n_free(candidate->password);
            candidate->password = g_strdup(password);
        }
    }
    else
    {
        /* case 2: add a new candidate */

        if (type == CAND_TYPE_PEER)
        {
            nice_debug("[%s]: warning: ignoring externally set peer-reflexive candidate", G_STRFUNC);
            return FALSE;
        }
        candidate = n_cand_new(type);
		comp->remote_candidates = n_slist_append(comp->remote_candidates, candidate);

        candidate->stream_id = stream_id;
        candidate->component_id = comp_id;

        candidate->type = type;
        if (addr)
            candidate->addr = *addr;

        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN] = {0};
            if (addr)
                nice_address_to_string(addr, tmpbuf);
            nice_debug("[%s]: Adding %s remote candidate with addr [%s]:%u"
                       " for s%d/c%d. U/P '%s'/'%s' prio: %u", G_STRFUNC,
                       _transport_to_string(transport), tmpbuf,
                       addr ? nice_address_get_port(addr) : 0, stream_id, comp_id,
                       username, password, priority);
        }

        if (base_addr)
            candidate->base_addr = *base_addr;

        candidate->transport = transport;
        candidate->priority = priority;
        candidate->username = g_strdup(username);
        candidate->password = g_strdup(password);

        if (foundation)
            g_strlcpy(candidate->foundation, foundation, CAND_MAX_FOUNDATION);
    }

    if (cocheck_add_cand(agent, stream_id, comp, candidate) < 0)
    {
        goto errors;
    }

    return TRUE;

errors:
    n_cand_free(candidate);
    return FALSE;
}

int32_t n_agent_set_remote_credentials(n_agent_t * agent, uint32_t stream_id, const char * ufrag, const char * pwd)
{
    n_stream_t * stream;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);

    agent_lock();

    stream = agent_find_stream(agent, stream_id);
    /* note: oddly enough, ufrag and pwd can be empty strings */
    if (stream && ufrag && pwd)
    {

        g_strlcpy(stream->remote_ufrag, ufrag, N_STREAM_MAX_UFRAG);
        g_strlcpy(stream->remote_password, pwd, N_STREAM_MAX_PWD);

        ret = TRUE;
        goto done;
    }

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t n_agent_set_local_credentials(n_agent_t * agent, uint32_t stream_id, const char * ufrag, const char * pwd)
{
    n_stream_t * stream;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);

    agent_lock();

    stream = agent_find_stream(agent, stream_id);

    /* note: oddly enough, ufrag and pwd can be empty strings */
    if (stream && ufrag && pwd)
    {
        g_strlcpy(stream->local_ufrag, ufrag, N_STREAM_MAX_UFRAG);
        g_strlcpy(stream->local_password, pwd, N_STREAM_MAX_PWD);

        ret = TRUE;
        goto done;
    }

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t n_agent_get_local_credentials(n_agent_t * agent, uint32_t stream_id, char ** ufrag, char ** pwd)
{
    n_stream_t * stream;
    int32_t ret = TRUE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);

    agent_lock();

    stream = agent_find_stream(agent, stream_id);
    if (stream == NULL)
    {
        goto done;
    }

    if (!ufrag || !pwd)
    {
        goto done;
    }

    *ufrag = g_strdup(stream->local_ufrag);
    *pwd = g_strdup(stream->local_password);
    ret = TRUE;

done:
    agent_unlock_and_emit(agent);
    return ret;
}

static int32_t _set_remote_cands_locked(n_agent_t * agent, n_stream_t * stream, n_comp_t * comp, const n_slist_t * candidates)
{
    const n_slist_t * i;
    int32_t added = 0;

    for (i = candidates; i && added >= 0; i = i->next)
    {
        n_cand_t * d = (n_cand_t *) i->data;

        if (n_addr_is_valid(&d->addr) == TRUE)
        {
            int32_t res = _add_remote_cand(agent, stream->id, comp->id, d->type, &d->addr, &d->base_addr, d->transport, d->priority,
															d->username, d->password, d->foundation);
            if (res)
                ++added;
        }
    }

    cocheck_remote_cands_set(agent);

    if (added > 0)
    {
        int32_t res = cocheck_schedule_next(agent);
        if (res != TRUE)
            nice_debug("[%s]: Warning: unable to schedule any conn checks!", G_STRFUNC);
    }

    return added;
}


int32_t n_agent_set_remote_cands(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, const n_slist_t * candidates)
{
    int32_t added = 0;
    n_stream_t * stream;
    n_comp_t * component;

    g_return_val_if_fail(NICE_IS_AGENT(agent), 0);
    g_return_val_if_fail(stream_id >= 1, 0);
    g_return_val_if_fail(component_id >= 1, 0);

    nice_debug("[%s]: set_remote_candidates %d %d", G_STRFUNC, stream_id, component_id);

    agent_lock();

    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
    {
		nice_debug("Could not find component %u in stream %u", component_id, stream_id);
        added = -1;
        goto done;
    }

    added = _set_remote_cands_locked(agent, stream, component, candidates);

done:
    agent_unlock_and_emit(agent);

    return added;
}

/* Return values for agent_recv_msg_unlocked(). Needed purely because it
 * must differentiate between RECV_OOB and RECV_SUCCESS. */
typedef enum
{
    RECV_ERROR = -2,
    RECV_WOULD_BLOCK = -1,
    RECV_OOB = 0,
    RECV_SUCCESS = 1,
} n_recv_status_t;

/*
 * agent_recv_msg_unlocked:
 * @agent: a #n_agent_t
 * @stream: the stream to receive from
 * @component: the component to receive from
 * @socket: the socket to receive on
 * @message: the message to write into (must have at least 65536 bytes of buffer
 * space)
 *
 * Receive a single message of data from the given @stream, @component and
 * @socket tuple, in a non-blocking fashion. The caller must ensure that
 * @message contains enough buffers to provide at least 65536 bytes of buffer
 * space, but the buffers may be split as the caller sees fit.
 *
 * This must be called with the agent's lock held.
 *
 * Returns: number of valid messages received on success (i.e. %RECV_SUCCESS or
 * 1), %RECV_OOB if data was successfully received but was handled out-of-band
 * (e.g. due to being a STUN control packet), %RECV_WOULD_BLOCK if no data is
 * available and the call would block, or %RECV_ERROR on error
 */
static n_recv_status_t agent_recv_msg_unlocked(n_agent_t * agent, n_stream_t * stream, n_comp_t * comp, n_socket_t * nicesock, n_input_msg_t * message)
{
    n_addr_t from;
    n_dlist_t * item;
    int32_t retval;

    /* We need an address for packet parsing, below. */
    if (message->from == NULL)
    {
        message->from = &from;
    }

    retval = n_socket_recv_msgs(nicesock, message, 1);

    //nice_debug("%s: Received %d valid messages of length %" G_GSIZE_FORMAT  " from base socket %p.", G_STRFUNC, retval, message->length, nicesock);

    if (retval == 0)
    {
        retval = RECV_WOULD_BLOCK;  /* EWOULDBLOCK */
        goto done;
    }
    else if (retval < 0)
    {
        nice_debug("[%s]: %s returned %d, errno (%d) : %s", G_STRFUNC, G_STRFUNC, retval, errno, g_strerror(errno));
        retval = RECV_ERROR;
        goto done;
    }

    if (retval == RECV_OOB || message->length == 0)
    {
        retval = RECV_OOB;
        goto done;
    }

    if (nice_debug_is_enabled())
    {
        char tmpbuf[INET6_ADDRSTRLEN];
        nice_address_to_string(message->from, tmpbuf);
        nice_debug("[%s]: Packet received on local socket %d from [%s]:%u (%" G_GSSIZE_FORMAT " octets).", G_STRFUNC,
                   g_socket_get_fd(nicesock->fileno), tmpbuf,  nice_address_get_port(message->from), message->length);
    }

    for (item = comp->turn_servers; item; item = n_dlist_next(item))
    {
        TurnServer * turn = item->data;
		n_slist_t * i = NULL;

        if (!nice_address_equal(message->from, &turn->server))
            continue;

        nice_debug("[%s]: Packet received from TURN server candidate", G_STRFUNC);

        for (i = comp->local_candidates; i; i = i->next)
        {
            n_cand_t * cand = i->data;

            if (cand->type == CAND_TYPE_RELAYED && cand->stream_id == stream->id && cand->component_id == comp->id)
            {
                retval = n_udp_turn_socket_parse_recv_msg(cand->sockptr, &nicesock, message);
                break;
            }
        }
        break;
    }

    if (retval == RECV_OOB)
        goto done;

    agent->media_after_tick = TRUE;

    /* If the messages stated length is equal to its actual length, its probably
     * a STUN message; otherwise its probably data. */
    if (stun_msg_valid_buflen_fast((StunInputVector *) message->buffers, message->n_buffers, message->length, 1) == (ssize_t) message->length)
    {
        /* Slow path: If this message isnt obviously *not* a STUN packet, compact
         * its buffers  into a single monolithic one and parse the packet properly. */
        uint8_t * big_buf;
        uint32_t big_buf_len;
        int32_t validated_len;

        big_buf = compact_input_message(message, &big_buf_len);

        validated_len = stun_msg_valid_buflen(big_buf, big_buf_len, 1);

        if (validated_len == (int32_t) big_buf_len)
        {
            int32_t handled;

            handled = cocheck_handle_in_stun(agent, stream, comp, nicesock, message->from, (char *) big_buf, big_buf_len);

            if (handled)
            {
                /* Handled STUN message. */
                nice_debug("[%s]: Valid STUN packet received.", G_STRFUNC);
                retval = RECV_OOB;
                n_free(big_buf);
                goto done;
            }
        }

        nice_debug("[%s]: Packet passed fast STUN validation but failed " "slow validation.", G_STRFUNC);

        free(big_buf);
    }

    /* Unhandled STUN; try handling TCP data, then pass to the client. */
    if (message->length > 0  && agent->reliable)
    {
        if (!nice_socket_is_reliable(nicesock) && !pst_is_closed(comp->tcp))
        {
            /* If we don??t yet have an underlying selected socket, queue up the
             * incoming data to handle later. This is because we can??t send ACKs (or,
             * more importantly for the first few packets, SYNACKs) without an
             * underlying socket. We??d rather wait a little longer for a pair to be
             * selected, then process the incoming packets and send out ACKs, than try
             * to process them now, fail to send the ACKs, and incur a timeout in our
             * pseudo-TCP state machine. */
            if (comp->selected_pair.local == NULL)
            {
                n_outvector_t * vec = n_slice_new(n_outvector_t);
                vec->buffer = compact_input_message(message, &vec->size);
                n_queue_push_tail(&comp->queued_tcp_packets, vec);
                nice_debug("%s: Queued %" G_GSSIZE_FORMAT " bytes for agent %p.", G_STRFUNC, vec->size, agent);

                return RECV_OOB;
            }
            else
            {
                process_queued_tcp_packets(agent, stream, comp);
            }

            /* Received data on a reliable connection. */

            nice_debug("%s: notifying pseudo-TCP of packet, length %" G_GSIZE_FORMAT, G_STRFUNC, message->length);
            pst_notify_message(comp->tcp, message);

            adjust_tcp_clock(agent, stream, comp);

            /* Success! Handled out-of-band. */
            retval = RECV_OOB;
            goto done;
        }
        else if (pst_is_closed(comp->tcp))
        {
            nice_debug("[%s]: Received data on a pseudo tcp FAILED component. Ignoring.", G_STRFUNC);

            retval = RECV_OOB;
            goto done;
        }
    }

done:
    /* Clear local modifications. */
    if (message->from == &from)
    {
        message->from = NULL;
    }
    return retval;
}

/* Print the composition of an array of messages. No-op if debugging is
 * disabled. */
static void n_debug_input_msg(const n_input_msg_t * messages, uint32_t n_messages)
{
    uint32_t i;

    if (!nice_debug_is_enabled())
        return;

    for (i = 0; i < n_messages; i++)
    {
        const n_input_msg_t * message = &messages[i];
        uint32_t j;

        nice_debug("[%s]: Message %p (from: %p, length: %" G_GSIZE_FORMAT ")", G_STRFUNC, message, message->from, message->length);
        for (j = 0;
                (message->n_buffers >= 0 && j < (uint32_t) message->n_buffers) ||
                (message->n_buffers < 0 && message->buffers[j].buffer != NULL);
                j++)
        {
            n_invector_t * buffer = &message->buffers[j];

            nice_debug("[%s]: Buffer %p (length: %" G_GSIZE_FORMAT ")", G_STRFUNC, buffer->buffer, buffer->size);
        }
    }
}

static uint8_t * compact_message(const n_output_msg_t * message, uint32_t buffer_length)
{
    uint8_t * buffer;
    uint32_t offset = 0;
    uint32_t i;

    buffer = g_malloc(buffer_length);

    for (i = 0;
            (message->n_buffers >= 0 && i < (uint32_t) message->n_buffers) ||
            (message->n_buffers < 0 && message->buffers[i].buffer != NULL);
            i++)
    {
        uint32_t len = MIN(buffer_length - offset, message->buffers[i].size);
        memcpy(buffer + offset, message->buffers[i].buffer, len);
        offset += len;
    }

    return buffer;
}

/* Concatenate all the buffers in the given @recv_message into a single, newly
 * allocated, monolithic buffer which is returned. The length of the new buffer
 * is returned in @buffer_length, and should be equal to the length field of
 * @recv_message.
 *
 * The return value must be freed with n_free(). */
uint8_t * compact_input_message(const n_input_msg_t * message, uint32_t * buffer_length)
{
    //nice_debug("%s: **WARNING: SLOW PATH**", G_STRFUNC);
    n_debug_input_msg(message, 1);

    /* This works as long as n_input_msg_t is a subset of eNiceOutputMessage */

    *buffer_length = message->length;

    return compact_message((n_output_msg_t *) message, *buffer_length);
}

/* Returns the number of bytes copied. Silently drops any data from @buffer
 * which doesn??t fit in @message. */
uint32_t memcpy_buffer_to_input_message(n_input_msg_t * message, const uint8_t * buffer, uint32_t buffer_length)
{
    uint32_t i;

    //nice_debug("%s: **WARNING: SLOW PATH**", G_STRFUNC);

    message->length = 0;

    for (i = 0;
            buffer_length > 0 &&
            ((message->n_buffers >= 0 && i < (uint32_t) message->n_buffers) ||
             (message->n_buffers < 0 && message->buffers[i].buffer != NULL));
            i++)
    {
        uint32_t len;

        len = MIN(message->buffers[i].size, buffer_length);
        memcpy(message->buffers[i].buffer, buffer, len);

        buffer += len;
        buffer_length -= len;

        message->length += len;
    }

    n_debug_input_msg(message, 1);

    if (buffer_length > 0)
    {
		nice_debug("Dropped %" G_GSIZE_FORMAT " bytes of data from the end of "
                  "buffer %p (length: %" G_GSIZE_FORMAT ") due to not fitting in "
                  "message %p", buffer_length, buffer - message->length,
                  message->length + buffer_length, message);
    }

    return message->length;
}

/* Concatenate all the buffers in the given @message into a single, newly
 * allocated, monolithic buffer which is returned. The length of the new buffer
 * is returned in @buffer_length, and should be equal to the length field of
 * @recv_message.
 *
 * The return value must be freed with n_free(). */
uint8_t * compact_output_message(const n_output_msg_t * message, uint32_t * buffer_length)
{
    //nice_debug("%s: **WARNING: SLOW PATH**", G_STRFUNC);

    *buffer_length = output_message_get_size(message);

    return compact_message(message, *buffer_length);
}

uint32_t output_message_get_size(const n_output_msg_t * message)
{
    uint32_t i;
    uint32_t message_len = 0;

    /* Find the total size of the message */
    for (i = 0;
            (message->n_buffers >= 0 && i < (uint32_t) message->n_buffers) ||
            (message->n_buffers < 0 && message->buffers[i].buffer != NULL);
            i++)
        message_len += message->buffers[i].size;

    return message_len;
}

static uint32_t input_message_get_size(const n_input_msg_t * message)
{
    uint32_t i;
    uint32_t message_len = 0;

    /* Find the total size of the message */
    for (i = 0;
            (message->n_buffers >= 0 && i < (uint32_t) message->n_buffers) ||
            (message->n_buffers < 0 && message->buffers[i].buffer != NULL);
            i++)
        message_len += message->buffers[i].size;

    return message_len;
}


/*
 * n_input_msg_iter_reset:
 * @iter: a #NiceInputMessageIter
 *
 * Reset the given @iter to point to the beginning of the array of messages.
 * This may be used both to initialise it and to reset it after use.
 *
 * Since: 0.1.5
 */
void n_input_msg_iter_reset(NiceInputMessageIter * iter)
{
    iter->message = 0;
    iter->buffer = 0;
    iter->offset = 0;
}

/*
 * n_input_msg_iter_is_at_end:
 * @iter: a #NiceInputMessageIter
 * @messages: (array length=n_messages): an array of #NiceInputMessages
 * @n_messages: number of entries in @messages
 *
 * Determine whether @iter points to the end of the given @messages array. If it
 * does, the array is full: every buffer in every message is full of valid
 * bytes.
 *
 * Returns: %TRUE if the messages?? buffers are full, %FALSE otherwise
 *
 * Since: 0.1.5
 */
int32_t n_input_msg_iter_is_at_end(NiceInputMessageIter * iter, n_input_msg_t * messages, uint32_t n_messages)
{
    return (iter->message == n_messages && iter->buffer == 0 && iter->offset == 0);
}

/*
 * n_input_msg_iter_get_n_valid_msgs:
 * @iter: a #NiceInputMessageIter
 *
 * Calculate the number of valid messages in the messages array. A valid message
 * is one which contains at least one valid byte of data in its buffers.
 *
 * Returns: number of valid messages (may be zero)
 *
 * Since: 0.1.5
 */
uint32_t n_input_msg_iter_get_n_valid_msgs(NiceInputMessageIter * iter)
{
    if (iter->buffer == 0 && iter->offset == 0)
        return iter->message;
    else
        return iter->message + 1;
}

/* n_send_msgs_nonblock_internal:
 *
 * Returns: number of bytes sent if allow_partial is %TRUE, the number
 * of messages otherwise.
 */

static int32_t n_send_msgs_nonblock_internal(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, const n_output_msg_t * messages,
																	uint32_t n_messages, int32_t allow_partial, GError ** error)
{
    n_stream_t * stream;
    n_comp_t * component;
    int32_t n_sent = -1; /* is in bytes if allow_partial is TRUE, otherwise in messages */
    GError * child_error = NULL;

    g_assert(n_messages == 1 || !allow_partial);

    agent_lock();

    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
    {
        g_set_error(&child_error, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE,  "Invalid stream/component.");
        goto done;
    }

    /* FIXME: Cancellation isnt yet supported, but it doesnt matter because
     * we only deal with non-blocking writes. */
    if (component->selected_pair.local != NULL)
    {
        //if (nice_debug_is_enabled())
        //{
        //    char tmpbuf[INET6_ADDRSTRLEN];
        //    nice_address_to_string(&component->selected_pair.remote->addr, tmpbuf);

        //    nice_debug("[%s]: s%d:%d: sending %u messages to "
        //               "[%s]:%d", G_STRFUNC, stream_id, component_id, n_messages, tmpbuf,
        //               nice_address_get_port(&component->selected_pair.remote->addr));
        //}

        if (!nice_socket_is_reliable(component->selected_pair.local->sockptr))
        {
            if (!pst_is_closed(component->tcp))
            {
                /* Send on the pseudo-TCP socket. */
                n_sent = pst_send_messages(component->tcp, messages, n_messages, allow_partial, &child_error);
                adjust_tcp_clock(agent, stream, component);

                if (!pst_can_send(component->tcp))
                    g_cancellable_reset(component->tcp_writable_cancellable);
                if (n_sent < 0 && !g_error_matches(child_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
                {
                    /* Signal errors */
                    _pseudo_tcp_error(agent, stream, component);
                }
            }
            else
            {
                g_set_error(&child_error, G_IO_ERROR, G_IO_ERROR_FAILED, "Pseudo-TCP socket not connected.");
            }
        }
    }
    else
    {
        /* Socket isn't properly open yet. */
        n_sent = 0;  /* EWOULDBLOCK */
    }

    /* Handle errors and cancellations. */
    if (n_sent == 0)
    {
        g_set_error_literal(&child_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK, g_strerror(EAGAIN));
        n_sent = -1;
    }

    //nice_debug("[%s]: n_sent: %d, n_messages: %u", G_STRFUNC,  n_sent, n_messages);

done:
    g_assert((child_error != NULL) == (n_sent == -1));
    g_assert(n_sent != 0);
    g_assert(n_sent < 0 ||
             (!allow_partial && (uint32_t) n_sent <= n_messages) ||
             (allow_partial && n_messages == 1 &&
              (uint32_t) n_sent <= output_message_get_size(&messages[0])));

    if (child_error != NULL)
        g_propagate_error(error, child_error);

    agent_unlock_and_emit(agent);

    return n_sent;
}

int32_t n_agent_send_msgs_nonblocking(n_agent_t * agent, uint32_t stream_id, uint32_t component_id,
        const n_output_msg_t * messages, uint32_t n_messages, GCancellable * cancellable, GError ** error)
{
    g_return_val_if_fail(NICE_IS_AGENT(agent), -1);
    g_return_val_if_fail(stream_id >= 1, -1);
    g_return_val_if_fail(component_id >= 1, -1);
    g_return_val_if_fail(n_messages == 0 || messages != NULL, -1);
    g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), -1);
    g_return_val_if_fail(error == NULL || *error == NULL, -1);

    if (g_cancellable_set_error_if_cancelled(cancellable, error))
        return -1;

    return n_send_msgs_nonblock_internal(agent, stream_id, component_id, messages, n_messages, FALSE, error);
}

int32_t n_agent_send(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, uint32_t len, const char * buf)
{
    n_outvector_t local_buf = { buf, len };
    n_output_msg_t local_message = { &local_buf, 1 };
    int32_t n_sent_bytes;

    g_return_val_if_fail(NICE_IS_AGENT(agent), -1);
    g_return_val_if_fail(stream_id >= 1, -1);
    g_return_val_if_fail(component_id >= 1, -1);
    g_return_val_if_fail(buf != NULL, -1);

    n_sent_bytes = n_send_msgs_nonblock_internal(agent, stream_id, component_id, &local_message, 1, TRUE, NULL);

    return n_sent_bytes;
}

n_slist_t * n_agent_get_local_cands(n_agent_t * agent, uint32_t stream_id, uint32_t component_id)
{
    n_comp_t * component;
	n_slist_t * ret = NULL;
	n_slist_t * item = NULL;

    g_return_val_if_fail(NICE_IS_AGENT(agent), NULL);
    g_return_val_if_fail(stream_id >= 1, NULL);
    g_return_val_if_fail(component_id >= 1, NULL);

    agent_lock();

    if (!agent_find_comp(agent, stream_id, component_id, NULL, &component))
    {
        goto done;
    }

    for (item = component->local_candidates; item; item = item->next)
        ret = n_slist_append(ret, nice_candidate_copy(item->data));

done:
    agent_unlock_and_emit(agent);
    return ret;
}


n_slist_t * n_agent_get_remote_cands(n_agent_t * agent, uint32_t stream_id, uint32_t component_id)
{
    n_comp_t * component;
    n_slist_t * ret = NULL, *item = NULL;

    g_return_val_if_fail(NICE_IS_AGENT(agent), NULL);
    g_return_val_if_fail(stream_id >= 1, NULL);
    g_return_val_if_fail(component_id >= 1, NULL);

    agent_lock();
    if (!agent_find_comp(agent, stream_id, component_id, NULL, &component))
    {
        goto done;
    }

    for (item = component->remote_candidates; item; item = item->next)
        ret = n_slist_append(ret, nice_candidate_copy(item->data));

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t n_agent_restart(n_agent_t * agent)
{
	n_slist_t * i;

    agent_lock();

    /* step: regenerate tie-breaker value */
    _generate_tie_breaker(agent);

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * stream = i->data;

        /* step: reset local credentials for the stream and
         * clean up the list of remote candidates */
        stream_restart(agent, stream);
    }

    agent_unlock_and_emit(agent);
    return TRUE;
}

int32_t n_agent_restart_stream(n_agent_t * agent, uint32_t stream_id)
{
    int32_t res = FALSE;
    n_stream_t * stream;

    agent_lock();

    stream = agent_find_stream(agent, stream_id);
    if (!stream)
    {
        g_warning("Could not find  stream %u", stream_id);
        goto done;
    }

    /* step: reset local credentials for the stream and
     * clean up the list of remote candidates */
    stream_restart(agent, stream);

    res = TRUE;
done:
    agent_unlock_and_emit(agent);
    return res;
}


static void n_agent_dispose(GObject * object)
{
	n_slist_t * i;
    QueuedSignal * sig;
    n_agent_t * agent = NICE_AGENT(object);

    /* step: free resources for the binding discovery timers */
    disc_free(agent);
    g_assert(agent->discovery_list == NULL);
    refresh_free(agent);
    g_assert(agent->refresh_list == NULL);

    /* step: free resources for the connectivity check timers */
    cocheck_free(agent);

    _remove_keepalive_timer(agent);

    for (i = agent->local_addresses; i; i = i->next)
    {
        n_addr_t * a = i->data;

        nice_address_free(a);
    }

    n_slist_free(agent->local_addresses); 
    agent->local_addresses = NULL;

    for (i = agent->streams_list; i; i = i->next)
    {
        n_stream_t * s = i->data;

        stream_close(s);
        stream_free(s);
    }

    n_slist_free(agent->streams_list);
    agent->streams_list = NULL;

    while ((sig = n_queue_pop_head(&agent->pending_signals)))
    {
        free_queued_signal(sig);
    }

	n_free(agent->stun_server_ip);
    agent->stun_server_ip = NULL;

    nice_rng_free(agent->rng);
    agent->rng = NULL;

    if (agent->main_context != NULL)
        g_main_context_unref(agent->main_context);
    agent->main_context = NULL;

    if (G_OBJECT_CLASS(nice_agent_parent_class)->dispose)
        G_OBJECT_CLASS(nice_agent_parent_class)->dispose(object);
}

int32_t comp_io_cb(GSocket * gsocket, GIOCondition condition, void * user_data)
{
    SocketSource * socket_source = user_data;
    n_comp_t * component;
    n_agent_t * agent;
    n_stream_t * stream;
    int32_t has_io_callback;
    int32_t remove_source = FALSE;
	uint8_t local_header_buf[TCP_HEADER_SIZE];
	uint8_t local_body_buf[MAX_BUFFER_SIZE];
	n_invector_t local_bufs[] =
	{
		{ local_header_buf, sizeof(local_header_buf) },
		{ local_body_buf, sizeof(local_body_buf) },
	};
	n_input_msg_t local_message =
	{
		local_bufs, G_N_ELEMENTS(local_bufs), NULL, 0
	};
	n_recv_status_t retval = 0;

    agent_lock();

    if (g_source_is_destroyed(g_main_current_source()))
    {
        /* Silently return FALSE. */
        nice_debug("[%s]: source %p destroyed", G_STRFUNC, g_main_current_source());

        agent_unlock();
        return G_SOURCE_REMOVE;
    }

    component = socket_source->component;
    agent = component->agent;
    stream = component->stream;

    g_object_ref(agent);

    /* Remove disconnected sockets when we get a HUP */
    if (condition & G_IO_HUP)
    {
        nice_debug("[%s]: n_socket_t %p has received HUP", G_STRFUNC,  socket_source->socket);
        if (component->selected_pair.local &&
                component->selected_pair.local->sockptr == socket_source->socket &&
                component->state == COMP_STATE_READY)
        {
            nice_debug("[%s]: Selected pair socket %p has HUP, declaring failed", G_STRFUNC, socket_source->socket);
            agent_sig_comp_state_change(agent, stream->id, component->id, COMP_STATE_FAILED);
        }

        component_detach_socket(component, socket_source->socket);
        agent_unlock();
        return G_SOURCE_REMOVE;
    }

    has_io_callback = component_has_io_callback(component);

    /* Choose which receive buffer to use. If we??re reading for
     * n_agent_attach_recv(), use a local static buffer. If we??re reading for
     * nice_agent_recv_messages(), use the buffer provided by the client.
     *
     * has_io_callback cannot change throughout this function, as we operate
     * entirely with the agent lock held, and comp_set_io_callback() would
     * need to take the agent lock to change the n_comp_t's io_callback. */
    g_assert(!has_io_callback || component->recv_messages == NULL);    
        
    /* FIXME: Currently, the critical path for reliable packet delivery has two
        * memcpy()s: one into the pseudo-TCP receive buffer, and one out of it.
        * This could moderately easily be reduced to one memcpy() in the common
        * case of in-order packet delivery, by replacing local_body_buf with a
        * pointer into the pseudo-TCP receive buffer. If it turns out the packet
        * is out-of-order (which we can only know after parsing its header), the
        * data will need to be moved in the buffer. If the packet *is* in order,
        * however, the only memcpy() then needed is from the pseudo-TCP receive
        * buffer to the client's message buffers.
        *
        * In fact, in the case of a reliable agent with I/O callbacks, zero
        * memcpy()s can be achieved (for in-order packet delivery) by emittin the
        * I/O callback directly from the pseudo-TCP receive buffer. */

    if (pst_is_closed(component->tcp))
    {
        nice_debug("[%s]: not handling incoming packet for s%d:%d "
                    "because pseudo-TCP socket does not exist in reliable mode.", G_STRFUNC,
                    stream->id, component->id);
        remove_source = TRUE;
        goto done;
    }

    while (has_io_callback)
    {
        /* Receive a single message. This will receive it into the given
            * @local_bufs then, for pseudo-TCP, emit I/O callbacks or copy it into
            * component->recv_messages in pst_readable(). STUN packets
            * will be parsed in-place. */
        retval = agent_recv_msg_unlocked(agent, stream, component, socket_source->socket, &local_message);          

        /* Dont expect any valid messages to escape pst_readable()
            * when in reliable mode. */
        //g_assert_cmpint(retval, != , RECV_SUCCESS);

        if (retval == RECV_WOULD_BLOCK)
        {
            /* EWOULDBLOCK. */
            break;
        }
        else if (retval == RECV_ERROR)
        {
            /* Other error. */
            nice_debug("[%s]: error receiving message", G_STRFUNC);
            remove_source = TRUE;
            break;
        }

		nice_debug("[%s]: received %d valid messages with %d bytes", G_STRFUNC, retval, local_message.length);

        has_io_callback = component_has_io_callback(component);
    }

done:
    /* If we're in the middle of a read, don't emit any signals, or we could cause
     * re-entrancy by (e.g.) emitting component-state-changed and having the
     * client perform a read. */
    if (component->n_recv_messages == 0 && component->recv_messages == NULL)
    {
        agent_unlock_and_emit(agent);
    }
    else
    {
        agent_unlock();
    }

    g_object_unref(agent);

    return !remove_source;

//out:
//    g_object_unref(agent);
//    agent_unlock_and_emit(agent);
//    return G_SOURCE_REMOVE;
}

int32_t n_agent_attach_recv(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, GMainContext * ctx, n_agent_recv_func func, void * data)
{
    n_comp_t * component = NULL;
    n_stream_t * stream = NULL;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(component_id >= 1, FALSE);

    agent_lock();

    /* attach candidates */

    /* step: check that params specify an existing pair */
    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
    {
        g_warning("Could not find component %u in stream %u", component_id, stream_id);
        goto done;
    }

    if (ctx == NULL)
        ctx = g_main_context_default();

    /* Set the component's I/O context. */
    comp_set_io_context(component, ctx);
    comp_set_io_callback(component, func, data);
    ret = TRUE;

    if (func)
    {
        /* If we got detached, maybe our readable callback didn't finish reading
         * all available data in the pseudotcp, so we need to make sure we free
         * our recv window, so the readable callback can be triggered again on the
         * next incoming data.
         * but only do this if we know we're already readable, otherwise we might
         * trigger an error in the initial, pre-connection attach. */
        if (agent->reliable && !pst_is_closed(component->tcp) && component->tcp_readable)
            pst_readable(component->tcp, component);
    }

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t n_agent_set_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, const char * lfoundation, const char * rfoundation)
{
    n_comp_t * component;
    n_stream_t * stream;
    n_cand_pair_t pair;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(component_id >= 1, FALSE);
    g_return_val_if_fail(lfoundation, FALSE);
    g_return_val_if_fail(rfoundation, FALSE);

    agent_lock();

    /* step: check that params specify an existing pair */
    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
    {
        goto done;
    }

    if (!comp_find_pair(component, agent, lfoundation, rfoundation, &pair))
    {
        goto done;
    }

    /* step: stop connectivity checks (note: for the whole stream) */
    cocheck_prune_stream(agent, stream);

    if (agent->reliable && !nice_socket_is_reliable(pair.local->sockptr) &&
            pst_is_closed(component->tcp))
    {
        nice_debug("[%s]: not setting selected pair for s%d:%d because "
                   "pseudo tcp socket does not exist in reliable mode", G_STRFUNC,
                   stream->id, component->id);
        goto done;
    }

    /* step: change component state */
    agent_sig_comp_state_change(agent, stream_id, component_id, COMP_STATE_READY);

    /* step: set the selected pair */
    comp_update_selected_pair(component, &pair);
    agent_sig_new_selected_pair(agent, stream_id, component_id, pair.local, pair.remote);

    ret = TRUE;

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t n_agent_get_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t ** local, n_cand_t ** remote)
{
    n_comp_t * component;
    n_stream_t * stream;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(component_id >= 1, FALSE);
    g_return_val_if_fail(local != NULL, FALSE);
    g_return_val_if_fail(remote != NULL, FALSE);

    agent_lock();

    /* step: check that params specify an existing pair */
    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
        goto done;

    if (component->selected_pair.local && component->selected_pair.remote)
    {
        *local = component->selected_pair.local;
        *remote = component->selected_pair.remote;
        ret = TRUE;
    }

done:
    agent_unlock_and_emit(agent);

    return ret;
}

GSocket * n_agent_get_selected_socket(n_agent_t * agent, uint32_t stream_id, uint32_t component_id)
{
    n_comp_t * component;
    n_stream_t * stream;
    n_socket_t * nice_socket;
    GSocket * g_socket = NULL;

    g_return_val_if_fail(NICE_IS_AGENT(agent), NULL);
    g_return_val_if_fail(stream_id >= 1, NULL);
    g_return_val_if_fail(component_id >= 1, NULL);

    agent_lock();

    /* Reliable streams are pseudotcp or MUST use RFC 4571 framing */
    if (agent->reliable)
        goto done;

    /* step: check that params specify an existing pair */
    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
        goto done;

    if (!component->selected_pair.local || !component->selected_pair.remote)
        goto done;

    if (component->selected_pair.local->type == CAND_TYPE_RELAYED)
        goto done;

    /* ICE-TCP requires RFC4571 framing, even if unreliable */
    if (component->selected_pair.local->transport != CAND_TRANS_UDP)
        goto done;

    nice_socket = (n_socket_t *)component->selected_pair.local->sockptr;
    if (nice_socket->fileno)
        g_socket = g_object_ref(nice_socket->fileno);

done:
    agent_unlock_and_emit(agent);

    return g_socket;
}

/* Create a new timer GSource with the given @name, @interval, callback
 * @function and @data, and assign it to @out, destroying and freeing any
 * existing #GSource in @out first.
 *
 * This guarantees that a timer won't be overwritten without being destroyed.
 */
void agent_timeout_add(n_agent_t * agent, GSource ** out, const char * name, uint32_t interval, GSourceFunc function, void * data)
{
    GSource * source;

    g_return_if_fail(function != NULL);
    g_return_if_fail(out != NULL);

    /* Destroy any existing source. */
    if (*out != NULL)
    {
        g_source_destroy(*out);
        g_source_unref(*out);
        *out = NULL;
    }

    /* Create the new source. */
    source = g_timeout_source_new(interval);

    g_source_set_name(source, name);
    g_source_set_callback(source, function, data, NULL);
    g_source_attach(source, agent->main_context);

    /* Return it! */
    *out = source;
}


int32_t n_agent_set_selected_rcand(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t * candidate)
{
    n_comp_t * component;
    n_stream_t * stream;
    n_cand_t * lcandidate = NULL;
    int32_t ret = FALSE;
    n_cand_t * local = NULL, *remote = NULL;
    guint64 priority;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id != 0, FALSE);
    g_return_val_if_fail(component_id != 0, FALSE);
    g_return_val_if_fail(candidate != NULL, FALSE);

    agent_lock();

    /* step: check if the component exists*/
    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
    {
        goto done;
    }

    /* step: stop connectivity checks (note: for the whole stream) */
    cocheck_prune_stream(agent, stream);

    /* Store previous selected pair */
    local = component->selected_pair.local;
    remote = component->selected_pair.remote;
    priority = component->selected_pair.priority;

    /* step: set the selected pair */
    lcandidate = comp_set_selected_remote_cand(agent, component, candidate);
    if (!lcandidate)
        goto done;

    if (agent->reliable && !nice_socket_is_reliable(lcandidate->sockptr) &&  pst_is_closed(component->tcp))
    {
        nice_debug("[%s]: not setting selected remote candidate s%d:%d because"
                   " pseudo tcp socket does not exist in reliable mode", G_STRFUNC,  stream->id, component->id);
        /* Revert back to previous selected pair */
        /* FIXME: by doing this, we lose the keepalive tick */
        component->selected_pair.local = local;
        component->selected_pair.remote = remote;
        component->selected_pair.priority = (uint32_t)priority;
        goto done;
    }

    /* step: change component state */
    agent_sig_comp_state_change(agent, stream_id, component_id, COMP_STATE_READY);
    agent_sig_new_selected_pair(agent, stream_id, component_id, lcandidate, candidate);

    ret = TRUE;

done:
    agent_unlock_and_emit(agent);
    return ret;
}

void _set_socket_tos(n_agent_t * agent, n_socket_t * sock, int32_t tos)
{
    if (sock->fileno == NULL)
        return;

    if (setsockopt(g_socket_get_fd(sock->fileno), IPPROTO_IP, IP_TOS, (const char *) &tos, sizeof(tos)) < 0)
    {
        nice_debug("[%s]: Could not set socket ToS: %s", G_STRFUNC, g_strerror(errno));
    }
}


void n_agent_set_stream_tos(n_agent_t * agent, uint32_t stream_id, int32_t tos)
{
    n_slist_t * i, *j;
    n_stream_t * stream;

    g_return_if_fail(NICE_IS_AGENT(agent));
    g_return_if_fail(stream_id >= 1);

    agent_lock();

    stream = agent_find_stream(agent, stream_id);
    if (stream == NULL)
        goto done;

    stream->tos = tos;
    for (i = stream->components; i; i = i->next)
    {
        n_comp_t * component = i->data;

        for (j = component->local_candidates; j; j = j->next)
        {
            n_cand_t * local_candidate = j->data;

            _set_socket_tos(agent, local_candidate->sockptr, tos);
        }
    }

done:
    agent_unlock_and_emit(agent);
}

int32_t n_agent_set_stream_name(n_agent_t * agent, uint32_t stream_id, const char * name)
{
    n_stream_t * stream_to_name = NULL;
	n_slist_t * i;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(name, FALSE);

    if (strcmp(name, "audio") &&
            strcmp(name, "video") &&
            strcmp(name, "text") &&
            strcmp(name, "application") &&
            strcmp(name, "message") &&
            strcmp(name, "image"))
    {
        g_critical("n_stream_t name %s will produce invalid SDP, only \"audio\","
                   " \"video\", \"text\", \"application\", \"image\" and \"message\""
                   " are valid", name);
    }

    agent_lock();

    if (name != NULL)
    {
        for (i = agent->streams_list; i; i = i->next)
        {
            n_stream_t * stream = i->data;

            if (stream->id != stream_id && g_strcmp0(stream->name, name) == 0)
                goto done;
            else if (stream->id == stream_id)
                stream_to_name = stream;
        }
    }

    if (stream_to_name == NULL)
        goto done;

    if (stream_to_name->name)
        n_free(stream_to_name->name);
    stream_to_name->name = g_strdup(name);
    ret = TRUE;

done:
    agent_unlock_and_emit(agent);

    return ret;
}

const char * n_agent_get_stream_name(n_agent_t * agent, uint32_t stream_id)
{
    n_stream_t * stream;
    char * name = NULL;

    g_return_val_if_fail(NICE_IS_AGENT(agent), NULL);
    g_return_val_if_fail(stream_id >= 1, NULL);

    agent_lock();

    stream = agent_find_stream(agent, stream_id);
    if (stream == NULL)
        goto done;

    name = stream->name;

done:
    agent_unlock_and_emit(agent);
    return name;
}

int32_t n_agent_forget_relays(n_agent_t * agent, uint32_t stream_id, uint32_t component_id)
{
    n_comp_t * component;
    int32_t ret = TRUE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(component_id >= 1, FALSE);

    agent_lock();

    if (!agent_find_comp(agent, stream_id, component_id, NULL, &component))
    {
        ret = FALSE;
        goto done;
    }

    component_clean_turn_servers(component);

done:
    agent_unlock_and_emit(agent);

    return ret;
}

/* Helper function to allow us to send connchecks reliably.
 * If the transport is reliable, then we request a reliable send, which will
 * either send the data, or queue it in the case of unestablished http/socks5
 * proxies or tcp-turn. If the transport is not reliable, then it could be an
 * unreliable tcp-bsd, so we still try a reliable send to see if it can succeed
 * meaning the message was queued, or if it failed, then it was either udp-bsd
 * or turn and so we retry with a non reliable send and let the retransmissions
 * take care of the rest.
 * This is in order to avoid having to retransmit something if the underlying
 * socket layer can queue the message and send it once a connection is
 * established.
 */
int32_t agent_socket_send(n_socket_t * sock, const n_addr_t * addr, uint32_t len, const char * buf)
{
	int32_t ret;
	ret = nice_socket_send(sock, addr, len, buf);
	return ret;
}

n_comp_state_e n_agent_get_comp_state(n_agent_t * agent, uint32_t stream_id, uint32_t component_id)
{
    n_comp_state_e state = COMP_STATE_FAILED;
    n_comp_t * component;

    agent_lock();

    if (agent_find_comp(agent, stream_id, component_id, NULL, &component))
        state = component->state;

    agent_unlock();

    return state;
}

void nice_print_cand(n_agent_t * agent, n_cand_t * l_cand, n_cand_t * r_cand)
{
	if (nice_debug_is_enabled())
	{
		char tmpbuf1[INET6_ADDRSTRLEN];
		char tmpbuf2[INET6_ADDRSTRLEN];

		nice_address_to_string(&l_cand->addr, tmpbuf1);
		nice_address_to_string(&r_cand->addr, tmpbuf2);
		nice_debug("[%s]: '%s:%u' -> '%s:%u'", G_STRFUNC,
			tmpbuf1, nice_address_get_port(&l_cand->addr),
			tmpbuf2, nice_address_get_port(&r_cand->addr));
	}
}
