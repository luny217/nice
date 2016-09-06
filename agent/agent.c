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
#include "iostream.h"
#include "stream.h"
#include "interfaces.h"
#include "pseudotcp.h"

/* Maximum size of a UDP packet??s payload, as the packet??s length field is 16b
 * wide. */
#define MAX_BUFFER_SIZE ((1 << 16) - 1)  /* 65535 */

#define DEFAULT_STUN_PORT  3478
#define DEFAULT_UPNP_TIMEOUT 200  /* milliseconds */

#define MAX_TCP_MTU 1400 /* Use 1400 because of VPNs and we assume IEE 802.3 */

static void nice_debug_input_message_composition(const NiceInputMessage * messages,  uint32_t n_messages);

//G_DEFINE_TYPE(NiceAgent, nice_agent, G_TYPE_OBJECT);

static void nice_agent_init(NiceAgent * self);
static void nice_agent_class_init(NiceAgentClass * klass);
static gpointer nice_agent_parent_class = ((void *)0);
static gint NiceAgent_private_offset;
static void nice_agent_class_intern_init(gpointer klass)
{
    nice_agent_parent_class = g_type_class_peek_parent(klass);
    if (NiceAgent_private_offset != 0) g_type_class_adjust_private_offset(klass, &NiceAgent_private_offset);
    nice_agent_class_init((NiceAgentClass *) klass);
}
static __inline gpointer nice_agent_get_instance_private(NiceAgent * self)
{
    return (((gpointer)((guint8 *)(self) + (glong)(NiceAgent_private_offset))));
}
GType nice_agent_get_type(void)
{
    static volatile gsize g_define_type_id__volatile = 0;
    if ((g_once_init_enter((&g_define_type_id__volatile))))
    {
        GType g_define_type_id = g_type_register_static_simple(((GType)((20) << (2))), g_intern_static_string("NiceAgent"), sizeof(NiceAgentClass), (GClassInitFunc) nice_agent_class_intern_init, sizeof(NiceAgent), (GInstanceInitFunc) nice_agent_init, (GTypeFlags) 0);
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
    SIGNAL_COMPONENT_STATE_CHANGED,
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

static void pseudo_tcp_socket_opened(PseudoTcpSocket * sock, void * user_data);
static void pseudo_tcp_socket_readable(PseudoTcpSocket * sock, void * user_data);
static void pseudo_tcp_socket_writable(PseudoTcpSocket * sock, void * user_data);
static void pseudo_tcp_socket_closed(PseudoTcpSocket * sock, uint32_t err, void * user_data);
static PseudoTcpWriteResult pseudo_tcp_socket_write_packet(PseudoTcpSocket * sock, const char * buffer, uint32_t len, void * user_data);
static void adjust_tcp_clock(NiceAgent * agent, Stream * stream, Component * component);
static void nice_agent_dispose(GObject * object);
static void nice_agent_get_property(GObject * object, uint32_t property_id, GValue * value, GParamSpec * pspec);
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
            g_free(g_value_get_pointer(&sig->params[i + 1]));
        g_value_unset(&sig->params[i + 1]);
    }

    g_slice_free1(sizeof(GValue) * (sig->query.n_params + 1), sig->params);
    g_slice_free(QueuedSignal, sig);
}

void agent_unlock_and_emit(NiceAgent * agent)
{
    GQueue queue = G_QUEUE_INIT;
    QueuedSignal * sig;

    queue = agent->pending_signals;
    g_queue_init(&agent->pending_signals);

    agent_unlock();

    while ((sig = g_queue_pop_head(&queue)))
    {
        g_signal_emitv(sig->params, sig->signal_id, 0, NULL);

        free_queued_signal(sig);
    }
}

static void agent_queue_signal(NiceAgent * agent, uint32_t signal_id, ...)
{
    QueuedSignal * sig;
    uint32_t i;
    char * error = NULL;
    va_list var_args;

    sig = g_slice_new(QueuedSignal);
    g_signal_query(signal_id, &sig->query);

    sig->signal_id = signal_id;
    sig->params = g_slice_alloc0(sizeof(GValue) * (sig->query.n_params + 1));

    g_value_init(&sig->params[0], G_TYPE_OBJECT);
    g_value_set_object(&sig->params[0], agent);

    va_start(var_args, signal_id);
    for (i = 0; i < sig->query.n_params; i++)
    {
        G_VALUE_COLLECT_INIT(&sig->params[i + 1], sig->query.param_types[i],
                             var_args, 0, &error);
        if (error)
            break;
    }
    va_end(var_args);

    if (error)
    {
        free_queued_signal(sig);
        g_critical("Error collecting values for signal: %s", error);
        g_free(error);
        return;
    }

    g_queue_push_tail(&agent->pending_signals, sig);
}


StunUsageIceCompatibility agent_to_ice_compatibility(NiceAgent * agent)
{
    return STUN_USAGE_ICE_COMPATIBILITY_RFC5245;
}


StunUsageTurnCompatibility agent_to_turn_compatibility(NiceAgent * agent)
{
    return STUN_USAGE_TURN_COMPATIBILITY_RFC5766;
}

NiceTurnSocketCompatibility agent_to_turn_socket_compatibility(NiceAgent * agent)
{
    return NICE_TURN_SOCKET_COMPATIBILITY_RFC5766;
}

Stream * agent_find_stream(NiceAgent * agent, uint32_t stream_id)
{
    GSList * i;

    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * s = i->data;

        if (s->id == stream_id)
            return s;
    }

    return NULL;
}

int32_t agent_find_component(NiceAgent * agent, uint32_t stream_id, uint32_t component_id, Stream ** stream, Component ** component)
{
    Stream * s;
    Component * c;

    s = agent_find_stream(agent, stream_id);

    if (s == NULL)
        return FALSE;

    c = stream_find_component_by_id(s, component_id);

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

    gobject_class->get_property = nice_agent_get_property;
    gobject_class->set_property = nice_agent_set_property;
    gobject_class->dispose = nice_agent_dispose;

    /* install properties */
    /**
     * NiceAgent:main-context:
     *
     * A GLib main context is needed for all timeouts used by libnice.
     * This is a property being set by the nice_agent_new() call.
     */
    g_object_class_install_property(gobject_class, PROP_MAIN_CONTEXT,
                                    g_param_spec_pointer(
                                        "main-context",
                                        "The GMainContext to use for timeouts",
                                        "The GMainContext to use for timeouts",
                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  
    /* install signals */

    /**
     * NiceAgent::component-state-changed
     * @agent: The #NiceAgent object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @state: The #NiceComponentState of the component
     *
     * This signal is fired whenever a component's state changes
     */
    signals[SIGNAL_COMPONENT_STATE_CHANGED] =
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
     * NiceAgent::candidate-gathering-done:
     * @agent: The #NiceAgent object
     * @stream_id: The ID of the stream
     *
     * This signal is fired whenever a stream has finished gathering its
     * candidates after a call to nice_agent_gather_candidates()
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
     * NiceAgent::new-selected-pair
     * @agent: The #NiceAgent object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @lfoundation: The local foundation of the selected candidate pair
     * @rfoundation: The remote foundation of the selected candidate pair
     *
     * This signal is fired once a candidate pair is selected for data
     * transfer for a stream's component This is emitted along with
     * #NiceAgent::new-selected-pair-full which has the whole candidate,
     * the Foundation of a Candidate is not a unique identifier.
     *
     * See also: #NiceAgent::new-selected-pair-full
     * Deprecated: 0.1.8: Use #NiceAgent::new-selected-pair-full
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
     * NiceAgent::new-candidate
     * @agent: The #NiceAgent object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @foundation: The foundation of the new candidate
     *
     * This signal is fired when the agent discovers a new local candidate.
     * When this signal is emitted, a matching #NiceAgent::new-candidate-full is
     * also emitted with the candidate.
     *
     * See also: #NiceAgent::candidate-gathering-done,
     * #NiceAgent::new-candidate-full
     * Deprecated: 0.1.8: Use #NiceAgent::new-candidate-full
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
     * NiceAgent::new-remote-candidate
     * @agent: The #NiceAgent object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @foundation: The foundation of the new candidate
     *
     * This signal is fired when the agent discovers a new remote
     * candidate.  This can happen with peer reflexive candidates.  When
     * this signal is emitted, a matching
     * #NiceAgent::new-remote-candidate-full is also emitted with the
     * candidate.
     *
     * See also: #NiceAgent::new-remote-candidate-full
     * Deprecated: 0.1.8: Use #NiceAgent::new-remote-candidate-full
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
     * NiceAgent::initial-binding-request-received
     * @agent: The #NiceAgent object
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
     * NiceAgent::reliable-transport-writable
     * @agent: The #NiceAgent object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     *
     * This signal is fired on the reliable #NiceAgent when the underlying reliable
     * transport becomes writable.
     * This signal is only emitted when the nice_agent_send() function returns less
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
     * NiceAgent::streams-removed
     * @agent: The #NiceAgent object
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
     * NiceAgent::new-selected-pair-full
     * @agent: The #NiceAgent object
     * @stream_id: The ID of the stream
     * @component_id: The ID of the component
     * @lcandidate: The local #NiceCandidate of the selected candidate pair
     * @rcandidate: The remote #NiceCandidate of the selected candidate pair
     *
     * This signal is fired once a candidate pair is selected for data
     * transfer for a stream's component. This is emitted along with
     * #NiceAgent::new-selected-pair.
     *
     * See also: #NiceAgent::new-selected-pair
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
     * NiceAgent::new-candidate-full
     * @agent: The #NiceAgent object
     * @candidate: The new #NiceCandidate
     *
     * This signal is fired when the agent discovers a new local candidate.
     * When this signal is emitted, a matching #NiceAgent::new-candidate is
     * also emitted with the candidate's foundation.
     *
     * See also: #NiceAgent::candidate-gathering-done,
     * #NiceAgent::new-candidate
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
     * NiceAgent::new-remote-candidate-full
     * @agent: The #NiceAgent object
     * @candidate: The new #NiceCandidate
     *
     * This signal is fired when the agent discovers a new remote candidate.
     * This can happen with peer reflexive candidates.
     * When this signal is emitted, a matching #NiceAgent::new-remote-candidate is
     * also emitted with the candidate's foundation.
     *
     * See also: #NiceAgent::new-remote-candidate
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

static void priv_generate_tie_breaker(NiceAgent * agent)
{
    nice_rng_generate_bytes(agent->rng, 8, (char *)&agent->tie_breaker);
}

static void nice_agent_init(NiceAgent * agent)
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
    agent->discovery_unsched_items = 0;
    agent->discovery_timer_source = NULL;
    agent->conncheck_timer_source = NULL;
    agent->keepalive_timer_source = NULL;
    agent->refresh_list = NULL;
    agent->media_after_tick = FALSE;
    agent->software_attribute = NULL;

    agent->compatibility = NICE_COMPATIBILITY_RFC5245;
    agent->reliable = TRUE;
    agent->use_ice_udp = TRUE;
    agent->use_ice_tcp = FALSE;
	agent->full_mode = TRUE;

    agent->rng = nice_rng_new();
    priv_generate_tie_breaker(agent);

    g_queue_init(&agent->pending_signals);
}


NiceAgent * nice_agent_new(GMainContext * ctx)
{
    NiceAgent * agent = g_object_new(NICE_TYPE_AGENT, NULL);
    return agent;
}

static void nice_agent_get_property(GObject * object, uint32_t property_id, GValue * value, GParamSpec * pspec)
{
    NiceAgent * agent = NICE_AGENT(object);

    agent_lock();

    switch (property_id)
    {
        case PROP_MAIN_CONTEXT:
            g_value_set_pointer(value, agent->main_context);
            break;

        case PROP_COMPATIBILITY:
            g_value_set_uint(value, agent->compatibility);
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

void nice_agent_init_stun_agent(NiceAgent * agent, StunAgent * stun_agent)
{
    stun_agent_init(stun_agent, 0);
}

static void nice_agent_reset_all_stun_agents(NiceAgent * agent, int32_t only_software)
{
    GSList * stream_item, *component_item;

    for (stream_item = agent->streams_list; stream_item;
            stream_item = stream_item->next)
    {
        Stream * stream = stream_item->data;

        for (component_item = stream->components; component_item;
                component_item = component_item->next)
        {
            Component * component = component_item->data;
            nice_agent_init_stun_agent(agent, &component->stun_agent);
        }
    }
}

static void nice_agent_set_property(GObject * object, uint32_t property_id, const GValue * value, GParamSpec * pspec)
{
    NiceAgent * agent = NICE_AGENT(object);

    agent_lock();

    switch (property_id)
    {
        case PROP_MAIN_CONTEXT:
            agent->main_context = g_value_get_pointer(value);
            if (agent->main_context != NULL)
                g_main_context_ref(agent->main_context);
            break;

        case PROP_STUN_SERVER:
            g_free(agent->stun_server_ip);
            agent->stun_server_ip = g_value_dup_string(value);
            break;

        case PROP_STUN_SERVER_PORT:
            agent->stun_server_port = g_value_get_uint(value);
            break;

        case PROP_CONTROLLING_MODE:
            agent->controlling_mode = g_value_get_boolean(value);
            break;

        case PROP_FULL_MODE:
            agent->full_mode = g_value_get_boolean(value);
            break;

        case PROP_STUN_PACING_TIMER:
            agent->timer_ta = g_value_get_uint(value);
            break;

        case PROP_MAX_CONNECTIVITY_CHECKS:
            agent->max_conn_checks = g_value_get_uint(value);
            break;

        case PROP_RELIABLE:
            agent->reliable = g_value_get_boolean(value);
            break;

            /* Don't allow ice-udp and ice-tcp to be disabled at the same time */
        case PROP_ICE_UDP:
            if (agent->use_ice_tcp == TRUE || g_value_get_boolean(value) == TRUE)
                agent->use_ice_udp = g_value_get_boolean(value);
            break;

        case PROP_ICE_TCP:
            if ((agent->use_ice_udp == TRUE || g_value_get_boolean(value) == TRUE))
                agent->use_ice_tcp = g_value_get_boolean(value);
            break;

        case PROP_BYTESTREAM_TCP:
            /* TODO: support bytestream mode and set property to writable */
            break;

        case PROP_KEEPALIVE_CONNCHECK:
            agent->keepalive_conncheck = g_value_get_boolean(value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }

    agent_unlock_and_emit(agent);
}

static void agent_signal_socket_writable(NiceAgent * agent, Component * component)
{
    g_cancellable_cancel(component->tcp_writable_cancellable);
    agent_queue_signal(agent, signals[SIGNAL_RELIABLE_TRANSPORT_WRITABLE], component->stream->id, component->id);
}

static void pseudo_tcp_socket_create(NiceAgent * agent, Stream * stream, Component * component)
{
    PseudoTcpCallbacks tcp_callbacks = {component,
                                        pseudo_tcp_socket_opened,
                                        pseudo_tcp_socket_readable,
                                        pseudo_tcp_socket_writable,
                                        pseudo_tcp_socket_closed,
                                        pseudo_tcp_socket_write_packet
                                       };
    component->tcp = pseudo_tcp_socket_new(0, &tcp_callbacks);
    component->tcp_writable_cancellable = g_cancellable_new();
    nice_debug("Agent %p : Create Pseudo Tcp Socket for component %d", agent, component->id);
}

static void priv_pseudo_tcp_error(NiceAgent * agent, Stream * stream,
                                  Component * component)
{
    if (component->tcp_writable_cancellable)
    {
        g_cancellable_cancel(component->tcp_writable_cancellable);
        g_clear_object(&component->tcp_writable_cancellable);
    }

    if (component->tcp)
    {
        agent_signal_component_state_change(agent, stream->id, component->id, COMPONENT_STATE_FAILED);
        component_detach_all_sockets(component);
        pseudo_tcp_socket_close(component->tcp, TRUE);
    }

    if (component->tcp_clock)
    {
        g_source_destroy(component->tcp_clock);
        g_source_unref(component->tcp_clock);
        component->tcp_clock = NULL;
    }
}

static void pseudo_tcp_socket_opened(PseudoTcpSocket * sock, void * user_data)
{
    Component * component = user_data;
    NiceAgent * agent = component->agent;
    Stream * stream = component->stream;

    nice_debug("Agent %p: s%d:%d pseudo Tcp socket Opened", agent,  stream->id, component->id);

    agent_signal_socket_writable(agent, component);
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
static int32_t pseudo_tcp_socket_send_messages(PseudoTcpSocket * self, const NiceOutputMessage * messages,
        uint32_t n_messages, int32_t allow_partial, GError ** error)
{
    uint32_t i;
    int32_t bytes_sent = 0;

    for (i = 0; i < n_messages; i++)
    {
        const NiceOutputMessage * message = &messages[i];
        uint32_t j;

        /* If allow_partial is FALSE and there??s not enough space for the
         * entire message, bail now before queuing anything. This doesn??t
         * gel with the fact this function is only used in reliable mode,
         * and there is no concept of a ??message??, but is necessary
         * because the calling API has no way of returning to the client
         * and indicating that a message was partially sent. */
        if (!allow_partial &&
                output_message_get_size(message) >
                pseudo_tcp_socket_get_available_send_space(self))
        {
            return i;
        }

        for (j = 0;
                (message->n_buffers >= 0 && j < (uint32_t) message->n_buffers) ||
                (message->n_buffers < 0 && message->buffers[j].buffer != NULL);
                j++)
        {
            const GOutputVector * buffer = &message->buffers[j];
            int32_t ret;

            /* Send on the pseudo-TCP socket. */
            ret = pseudo_tcp_socket_send(self, buffer->buffer, buffer->size);

            /* In case of -1, the error is either EWOULDBLOCK or ENOTCONN, which both
             * need the user to wait for the reliable-transport-writable signal */
            if (ret < 0)
            {
                if (pseudo_tcp_socket_get_error(self) == EWOULDBLOCK)
                    goto out;

                if (pseudo_tcp_socket_get_error(self) == ENOTCONN ||  pseudo_tcp_socket_get_error(self) == EPIPE)
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
pseudo_tcp_socket_recv_messages(PseudoTcpSocket * self,
                                NiceInputMessage * messages, uint32_t n_messages, NiceInputMessageIter * iter,
                                GError ** error)
{
    for (; iter->message < n_messages; iter->message++)
    {
        NiceInputMessage * message = &messages[iter->message];

        if (iter->buffer == 0 && iter->offset == 0)
        {
            message->length = 0;
        }

        for (;
                (message->n_buffers >= 0 && iter->buffer < (uint32_t) message->n_buffers) ||
                (message->n_buffers < 0 && message->buffers[iter->buffer].buffer != NULL);
                iter->buffer++)
        {
            GInputVector * buffer = &message->buffers[iter->buffer];

            do
            {
                int32_t len;

                len = pseudo_tcp_socket_recv(self,
                                             (char *) buffer->buffer + iter->offset,
                                             buffer->size - iter->offset);

                nice_debug("%s: Received %" G_GSSIZE_FORMAT " bytes into "
                           "buffer %p (offset %" G_GSIZE_FORMAT ", length %" G_GSIZE_FORMAT
                           ").", G_STRFUNC, len, buffer->buffer, iter->offset, buffer->size);

                if (len == 0)
                {
                    /* Reached EOS. */
                    len = 0;
                    goto done;
                }
                else if (len < 0 &&
                         pseudo_tcp_socket_get_error(self) == EWOULDBLOCK)
                {
                    /* EWOULDBLOCK. If we??ve already received something, return that;
                     * otherwise, error. */
                    if (nice_input_message_iter_get_n_valid_messages(iter) > 0)
                    {
                        goto done;
                    }
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK,
                                "Error reading data from pseudo-TCP socket: would block.");
                    return len;
                }
                else if (len < 0 && pseudo_tcp_socket_get_error(self) == ENOTCONN)
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
    return nice_input_message_iter_get_n_valid_messages(iter);
}

/* This is called with the agent lock held. */
static void pseudo_tcp_socket_readable(PseudoTcpSocket * sock, void * user_data)
{
    Component * component = user_data;
    NiceAgent * agent = component->agent;
    Stream * stream = component->stream;
    int32_t has_io_callback;
    uint32_t stream_id = stream->id;
    uint32_t component_id = component->id;

    g_object_ref(agent);

    nice_debug("Agent %p: s%d:%d pseudo Tcp socket readable", agent, stream->id, component->id);

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
            len = pseudo_tcp_socket_recv(sock, (char *) buf, sizeof(buf));

            nice_debug("%s: I/O callback case: Received %" G_GSSIZE_FORMAT " bytes", G_STRFUNC, len);

            if (len == 0)
            {
                /* Reached EOS. */
                component->tcp_readable = FALSE;
                pseudo_tcp_socket_close(component->tcp, FALSE);
                break;
            }
            else if (len < 0)
            {
                /* Handle errors. */
                if (pseudo_tcp_socket_get_error(sock) != EWOULDBLOCK)
                {
                    nice_debug("%s: calling priv_pseudo_tcp_error()", G_STRFUNC);
                    priv_pseudo_tcp_error(agent, stream, component);
                }

                if (component->recv_buf_error != NULL)
                {
                    GIOErrorEnum error_code;

                    if (pseudo_tcp_socket_get_error(sock) == ENOTCONN)
                        error_code = G_IO_ERROR_BROKEN_PIPE;
                    else if (pseudo_tcp_socket_get_error(sock) == EWOULDBLOCK)
                        error_code = G_IO_ERROR_WOULD_BLOCK;
                    else
                        error_code = G_IO_ERROR_FAILED;

                    g_set_error(component->recv_buf_error, G_IO_ERROR, error_code, "Error reading data from pseudo-TCP socket.");
                }
                break;
            }

            component_emit_io_callback(component, buf, len);

            if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
            {
                nice_debug("Stream or Component disappeared during the callback");
                goto out;
            }
            if (pseudo_tcp_socket_is_closed(component->tcp))
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
        n_valid_messages = pseudo_tcp_socket_recv_messages(sock,
                           component->recv_messages, component->n_recv_messages,
                           &component->recv_messages_iter, &child_error);

        nice_debug("%s: Client buffers case: Received %d valid messages:", G_STRFUNC, n_valid_messages);
        nice_debug_input_message_composition(component->recv_messages, component->n_recv_messages);

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
            nice_debug("%s: calling priv_pseudo_tcp_error()", G_STRFUNC);
            priv_pseudo_tcp_error(agent, stream, component);
        }
        else if (n_valid_messages == 0)
        {
            /* Reached EOS. */
            component->tcp_readable = FALSE;
            pseudo_tcp_socket_close(component->tcp, FALSE);
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

static void pseudo_tcp_socket_writable(PseudoTcpSocket * sock, void * user_data)
{
    Component * component = user_data;
    NiceAgent * agent = component->agent;
    Stream * stream = component->stream;

    nice_debug("Agent %p: s%d:%d pseudo Tcp socket writable", agent, stream->id, component->id);

    agent_signal_socket_writable(agent, component);
}

static void pseudo_tcp_socket_closed(PseudoTcpSocket * sock, uint32_t err, void * user_data)
{
    Component * component = user_data;
    NiceAgent * agent = component->agent;
    Stream * stream = component->stream;

    nice_debug("Agent %p: s%d:%d pseudo Tcp socket closed. " "Calling priv_pseudo_tcp_error().",  agent, stream->id, component->id);
    priv_pseudo_tcp_error(agent, stream, component);
}


static PseudoTcpWriteResult pseudo_tcp_socket_write_packet(PseudoTcpSocket * psocket, const char * buffer, uint32_t len, void * user_data)
{
    Component * component = user_data;

    if (component->selected_pair.local != NULL)
    {
        NiceSocket * sock;
        NiceAddress * addr;

        sock = component->selected_pair.local->sockptr;
        addr = &component->selected_pair.remote->addr;

        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN];
            nice_address_to_string(addr, tmpbuf);

            nice_debug(
                "Agent %p : s%d:%d: sending %d bytes on socket %p (FD %d) to [%s]:%d",
                component->agent, component->stream->id, component->id, len,
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


static int notify_pseudo_tcp_socket_clock(void * user_data)
{
    Component * component = user_data;
    Stream * stream;
    NiceAgent * agent;

    agent_lock();

    stream = component->stream;
    agent = component->agent;

    if (g_source_is_destroyed(g_main_current_source()))
    {
        nice_debug("Source was destroyed. " "Avoided race condition in notify_pseudo_tcp_socket_clock");
        agent_unlock();
        return FALSE;
    }

    pseudo_tcp_socket_notify_clock(component->tcp);
    adjust_tcp_clock(agent, stream, component);

    agent_unlock_and_emit(agent);

    return G_SOURCE_CONTINUE;
}

static void adjust_tcp_clock(NiceAgent * agent, Stream * stream, Component * component)
{
    if (!pseudo_tcp_socket_is_closed(component->tcp))
    {
        guint64 timeout = component->last_clock_timeout;

        if (pseudo_tcp_socket_get_next_clock(component->tcp, &timeout))
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
                    agent_timeout_add_with_context(agent, &component->tcp_clock, "Pseudo-TCP clock", interval, notify_pseudo_tcp_socket_clock, component);
                }
            }
        }
        else
        {
            nice_debug("Agent %p: component %d pseudo-TCP socket should be "
                       "destroyed. Calling priv_pseudo_tcp_error().", agent, component->id);
            priv_pseudo_tcp_error(agent, stream, component);
        }
    }
}

static void _tcp_sock_is_writable(NiceSocket * sock, void * user_data)
{
    Component * component = user_data;
    NiceAgent * agent = component->agent;
    Stream * stream = component->stream;

    agent_lock();

    /* Don't signal writable if the socket that has become writable is not
     * the selected pair */
    if (component->selected_pair.local == NULL || component->selected_pair.local->sockptr != sock)
    {
        agent_unlock();
        return;
    }

    nice_debug("Agent %p: s%d:%d Tcp socket writable", agent, stream->id, component->id);
    agent_signal_socket_writable(agent, component);

    agent_unlock_and_emit(agent);
}

static const char * _transport_to_string(NiceCandidateTransport type)
{
    switch (type)
    {
        case CANDIDATE_TRANSPORT_UDP:
            return "UDP";
        case CANDIDATE_TRANSPORT_TCP_ACTIVE:
            return "TCP-ACT";
        case CANDIDATE_TRANSPORT_TCP_PASSIVE:
            return "TCP-PASS";
        case CANDIDATE_TRANSPORT_TCP_SO:
            return "TCP-SO";
        default:
            return "???";
    }
}

void agent_gathering_done(NiceAgent * agent)
{

    GSList * i, *j, *k, *l, *m;

    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * stream = i->data;
        for (j = stream->components; j; j = j->next)
        {
            Component * component = j->data;

            for (k = component->local_candidates; k; k = k->next)
            {
                NiceCandidate * local_candidate = k->data;
                if (nice_debug_is_enabled())
                {
                    char tmpbuf[INET6_ADDRSTRLEN];
                    nice_address_to_string(&local_candidate->addr, tmpbuf);
                    nice_debug("Agent %p: gathered %s local candidate : [%s]:%u"
                               " for s%d/c%d", agent,
                               _transport_to_string(local_candidate->transport),
                               tmpbuf, nice_address_get_port(&local_candidate->addr),
                               local_candidate->stream_id, local_candidate->component_id);
                }
                for (l = component->remote_candidates; l; l = l->next)
                {
                    NiceCandidate * remote_candidate = l->data;

                    for (m = stream->conncheck_list; m; m = m->next)
                    {
                        CandidateCheckPair * p = m->data;

                        if (p->local == local_candidate && p->remote == remote_candidate)
                            break;
                    }
                    if (m == NULL)
                    {
                        conn_check_add_for_candidate_pair(agent, stream->id, component,
                                                          local_candidate, remote_candidate);
                    }
                }
            }
        }
    }

    if (agent->discovery_timer_source == NULL)
        agent_signal_gathering_done(agent);
}

void agent_signal_gathering_done(NiceAgent * agent)
{
    GSList * i;

    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * stream = i->data;
        if (stream->gathering)
        {
            stream->gathering = FALSE;
            agent_queue_signal(agent, signals[SIGNAL_CANDIDATE_GATHERING_DONE], stream->id);
        }
    }
}

void agent_signal_initial_binding_request_received(NiceAgent * agent, Stream * stream)
{
    if (stream->initial_binding_request_received != TRUE)
    {
        stream->initial_binding_request_received = TRUE;
        agent_queue_signal(agent, signals[SIGNAL_INITIAL_BINDING_REQUEST_RECEIVED], stream->id);
    }
}

/* If the Component now has a selected_pair, and has pending TCP packets which
 * it couldn??t receive before due to not being able to send out ACKs (or
 * SYNACKs, for the initial SYN packet), handle them now.
 *
 * Must be called with the agent lock held. */
static void process_queued_tcp_packets(NiceAgent * agent, Stream * stream, Component * component)
{
    GOutputVector * vec;
    uint32_t stream_id = stream->id;
    uint32_t component_id = component->id;

    g_assert(agent->reliable);

    if (component->selected_pair.local == NULL ||
            pseudo_tcp_socket_is_closed(component->tcp) ||
            nice_socket_is_reliable(component->selected_pair.local->sockptr))
    {
        return;
    }

    nice_debug("%s: Sending outstanding packets for agent %p.", G_STRFUNC, agent);

    while ((vec = g_queue_peek_head(&component->queued_tcp_packets)) != NULL)
    {
        int32_t retval;

        nice_debug("%s: Sending %" G_GSIZE_FORMAT " bytes.", G_STRFUNC, vec->size);
        retval =  pseudo_tcp_socket_notify_packet(component->tcp, vec->buffer, vec->size);

        if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
        {
            nice_debug("Stream or Component disappeared during " "pseudo_tcp_socket_notify_packet()");
            return;
        }
        if (pseudo_tcp_socket_is_closed(component->tcp))
        {
            nice_debug("PseudoTCP socket got destroyed in" " pseudo_tcp_socket_notify_packet()!");
            return;
        }

        adjust_tcp_clock(agent, stream, component);

        if (!retval)
        {
            /* Failed to send; try again later. */
            break;
        }

        g_queue_pop_head(&component->queued_tcp_packets);
        g_free((void *) vec->buffer);
        g_slice_free(GOutputVector, vec);
    }
}

void agent_signal_new_selected_pair(NiceAgent * agent, uint32_t stream_id,
                                    uint32_t component_id, NiceCandidate * lcandidate, NiceCandidate * rcandidate)
{
    Component * component;
    Stream * stream;

    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
        return;

    if (((NiceSocket *)lcandidate->sockptr)->type == NICE_SOCKET_TYPE_UDP_TURN)
    {
        nice_udp_turn_socket_set_peer(lcandidate->sockptr, &rcandidate->addr);
    }

    if (agent->reliable && !nice_socket_is_reliable(lcandidate->sockptr))
    {
        if (!component->tcp)
            pseudo_tcp_socket_create(agent, stream, component);
        process_queued_tcp_packets(agent, stream, component);

        pseudo_tcp_socket_connect(component->tcp);
        pseudo_tcp_socket_notify_mtu(component->tcp, MAX_TCP_MTU);
        adjust_tcp_clock(agent, stream, component);
    }

    if (nice_debug_is_enabled())
    {
        char ip[100];
        uint32_t port;

        port = nice_address_get_port(&lcandidate->addr);
        nice_address_to_string(&lcandidate->addr, ip);

        nice_debug("Agent %p: Local selected pair: %d:%d %s %s %s:%d %s",
                   agent, stream_id, component_id, lcandidate->foundation,                   
                   lcandidate->transport == CANDIDATE_TRANSPORT_UDP ? "UDP" : "???",
                   ip, port, lcandidate->type == CANDIDATE_TYPE_HOST ? "HOST" :
                   lcandidate->type == CANDIDATE_TYPE_SERVER_REFLEXIVE ?
                   "SRV-RFLX" :
                   lcandidate->type == CANDIDATE_TYPE_RELAYED ?
                   "RELAYED" :
                   lcandidate->type == CANDIDATE_TYPE_PEER_REFLEXIVE ?
                   "PEER-RFLX" : "???");

        port = nice_address_get_port(&rcandidate->addr);
        nice_address_to_string(&rcandidate->addr, ip);

        nice_debug("Agent %p: Remote selected pair: %d:%d %s %s %s:%d %s",
                   agent, stream_id, component_id, rcandidate->foundation,
                   rcandidate->transport == CANDIDATE_TRANSPORT_UDP ? "UDP" : "???",
                   ip, port, rcandidate->type == CANDIDATE_TYPE_HOST ? "HOST" :
                   rcandidate->type == CANDIDATE_TYPE_SERVER_REFLEXIVE ?
                   "SRV-RFLX" :
                   rcandidate->type == CANDIDATE_TYPE_RELAYED ?
                   "RELAYED" :
                   rcandidate->type == CANDIDATE_TYPE_PEER_REFLEXIVE ?
                   "PEER-RFLX" : "???");
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

void agent_signal_new_candidate(NiceAgent * agent, NiceCandidate * candidate)
{
    agent_queue_signal(agent, signals[SIGNAL_NEW_CANDIDATE_FULL],  candidate);
    agent_queue_signal(agent, signals[SIGNAL_NEW_CANDIDATE],
                       candidate->stream_id, candidate->component_id, candidate->foundation);
}

void agent_signal_new_remote_candidate(NiceAgent * agent, NiceCandidate * candidate)
{
    agent_queue_signal(agent, signals[SIGNAL_NEW_REMOTE_CANDIDATE_FULL], candidate);
    agent_queue_signal(agent, signals[SIGNAL_NEW_REMOTE_CANDIDATE],
                       candidate->stream_id, candidate->component_id, candidate->foundation);
}

const char * nice_component_state_to_string(NiceComponentState state)
{
    switch (state)
    {
        case COMPONENT_STATE_DISCONNECTED:
            return "disconnected";
        case COMPONENT_STATE_GATHERING:
            return "gathering";
        case COMPONENT_STATE_CONNECTING:
            return "connecting";
        case COMPONENT_STATE_CONNECTED:
            return "connected";
        case COMPONENT_STATE_READY:
            return "ready";
        case COMPONENT_STATE_FAILED:
            return "failed";
        case COMPONENT_STATE_LAST:
        default:
            return "invalid";
    }
}

void agent_signal_component_state_change(NiceAgent * agent, uint32_t stream_id, uint32_t component_id, NiceComponentState state)
{
    Component * component;
    Stream * stream;

    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
        return;

    if (component->state != state && state < COMPONENT_STATE_LAST)
    {
        nice_debug("Agent %p : stream %u component %u STATE-CHANGE %s -> %s.", agent,
                   stream_id, component_id, nice_component_state_to_string(component->state),
                   nice_component_state_to_string(state));

        component->state = state;

        if (agent->reliable)
            process_queued_tcp_packets(agent, stream, component);

        agent_queue_signal(agent, signals[SIGNAL_COMPONENT_STATE_CHANGED], stream_id, component_id, state);
    }
}

uint64_t agent_candidate_pair_priority(NiceAgent * agent, NiceCandidate * local, NiceCandidate * remote)
{
    if (agent->controlling_mode)
        return nice_candidate_pair_priority(local->priority, remote->priority);
    else
        return nice_candidate_pair_priority(remote->priority, local->priority);
}

static void priv_add_new_candidate_discovery_stun(NiceAgent * agent,
        NiceSocket * nicesock, NiceAddress server,
        Stream * stream, uint32_t component_id)
{
    CandidateDiscovery * cdisco;

    /* note: no need to check for redundant candidates, as this is
     *       done later on in the process */

    cdisco = g_slice_new0(CandidateDiscovery);

    cdisco->type = CANDIDATE_TYPE_SERVER_REFLEXIVE;
    cdisco->nicesock = nicesock;
    cdisco->server = server;
    cdisco->stream = stream;
    cdisco->component = stream_find_component_by_id(stream, component_id);
    cdisco->agent = agent;
    stun_agent_init(&cdisco->stun_agent, 0);

    nice_debug("Agent %p : Adding new srv-rflx candidate discovery %p", agent, cdisco);

    agent->discovery_list = g_slist_append(agent->discovery_list, cdisco);
    ++agent->discovery_unsched_items;
}

static void priv_add_new_candidate_discovery_turn(NiceAgent * agent,
        NiceSocket * nicesock, TurnServer * turn,
        Stream * stream, uint32_t component_id)
{
    CandidateDiscovery * cdisco;
    Component * component = stream_find_component_by_id(stream, component_id);
    //NiceAddress local_address;

    /* note: no need to check for redundant candidates, as this is
     *       done later on in the process */

    cdisco = g_slice_new0(CandidateDiscovery);
    cdisco->type = CANDIDATE_TYPE_RELAYED;
    cdisco->nicesock = nicesock;
    cdisco->turn = turn_server_ref(turn);
    cdisco->server = turn->server;

    cdisco->stream = stream;
    cdisco->component = stream_find_component_by_id(stream, component_id);
    cdisco->agent = agent;

    stun_agent_init(&cdisco->stun_agent, STUN_AGENT_USAGE_LONG_TERM_CREDENTIALS);

    nice_debug("Agent %p : Adding new relay-rflx candidate discovery %p", agent, cdisco);
    agent->discovery_list = g_slist_append(agent->discovery_list, cdisco);
    ++agent->discovery_unsched_items;
}

uint32_t nice_agent_add_stream(NiceAgent * agent, uint32_t n_components)
{
    Stream * stream;
    uint32_t ret = 0;
    uint32_t i;

    g_return_val_if_fail(NICE_IS_AGENT(agent), 0);
    g_return_val_if_fail(n_components >= 1, 0);

    agent_lock();
    stream = stream_new(n_components, agent);

    agent->streams_list = g_slist_append(agent->streams_list, stream);
    stream->id = agent->next_stream_id++;
    nice_debug("Agent %p : allocating stream id %u (%p)", agent, stream->id, stream);
    if (agent->reliable)
    {
        nice_debug("Agent %p : reliable stream", agent);
        for (i = 0; i < n_components; i++)
        {
            Component * component = stream_find_component_by_id(stream, i + 1);
            if (component)
            {
                pseudo_tcp_socket_create(agent, stream, component);
            }
            else
            {
                nice_debug("Agent %p: couldn't find component %d", agent, i + 1);
            }
        }
    }

    stream_initialize_credentials(stream, agent->rng);

    ret = stream->id;

    agent_unlock_and_emit(agent);
    return ret;
}


int nice_agent_set_relay_info(NiceAgent * agent, uint32_t stream_id, uint32_t component_id,  const char * server_ip,
                              uint32_t server_port, const char * username, const char * password, NiceRelayType type)
{
    Component * component = NULL;
    Stream * stream = NULL;
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

    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
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

    nice_debug("Agent %p: added relay server [%s]:%d of type %d to s/c %d/%d "
               "with user/pass : %s -- %s", agent, server_ip, server_port, type,
               stream_id, component_id, username, password);

    component->turn_servers = g_list_append(component->turn_servers, turn);

    if (stream->gathering_started)
    {
        GSList * i;

        stream->gathering = TRUE;

        for (i = component->local_candidates; i; i = i->next)
        {
            NiceCandidate * candidate = i->data;

            if (candidate->type == CANDIDATE_TYPE_HOST)
                priv_add_new_candidate_discovery_turn(agent, candidate->sockptr, turn, stream, component_id);
        }

        if (agent->discovery_unsched_items)
            discovery_schedule(agent);
    }

done:

    agent_unlock_and_emit(agent);
    return ret;
}

int nice_agent_gather_candidates(NiceAgent * agent, uint32_t stream_id)
{
    uint32_t cid;
    GSList * i;
    Stream * stream;
    GSList * local_addresses = NULL;
    int32_t ret = TRUE;

	agent_lock();

    stream = agent_find_stream(agent, stream_id);
    if (stream == NULL)
    {
        agent_unlock_and_emit(agent);
        return FALSE;
    }

    if (stream->gathering_started)
    {
        /* Stream is already gathering, ignore this call */
        agent_unlock_and_emit(agent);
        return TRUE;
    }

    /* if no local addresses added, generate them ourselves */
    if (agent->local_addresses == NULL)
    {
        GList * addresses = nice_interfaces_get_local_ips(FALSE);
        GList * item;

        for (item = addresses; item; item = g_list_next(item))
        {
            const char * addr_string = item->data;
            NiceAddress * addr = nice_address_new();

            if (nice_address_set_from_string(addr, addr_string))
            {
                local_addresses = g_slist_append(local_addresses, addr);
            }
            else
            {
                nice_debug("Error: Failed to parse local address %s", addr_string);
                nice_address_free(addr);
            }
        }

        g_list_foreach(addresses, (GFunc) g_free, NULL);
        g_list_free(addresses);
    }
    else
    {
        for (i = agent->local_addresses; i; i = i->next)
        {
            NiceAddress * addr = i->data;
            NiceAddress * dupaddr = nice_address_dup(addr);

            local_addresses = g_slist_append(local_addresses, dupaddr);
        }
    }

    /* generate a local host candidate for each local address */
    for (i = local_addresses; i; i = i->next)
    {
        NiceAddress * addr = i->data;
        NiceCandidate * host_candidate;

        for (cid = 1; cid <= stream->n_components; cid++)
        {
            Component * component = stream_find_component_by_id(stream, cid);
            enum
            {
                ADD_HOST_MIN = 0,
                ADD_HOST_UDP = ADD_HOST_MIN,
                ADD_HOST_TCP_ACTIVE,
                ADD_HOST_TCP_PASSIVE,
                ADD_HOST_MAX = ADD_HOST_TCP_PASSIVE
            } add_type;

            if (component == NULL)
                continue;

            for (add_type = ADD_HOST_MIN; add_type <= ADD_HOST_MAX; add_type++)
            {
                NiceCandidateTransport transport;
                uint32_t current_port;
                uint32_t start_port;
                HostCandidateResult res = HOST_CANDIDATE_CANT_CREATE_SOCKET;

                if ((agent->use_ice_udp == FALSE && add_type == ADD_HOST_UDP) ||
                        (agent->use_ice_tcp == FALSE && add_type != ADD_HOST_UDP))
                    continue;

				transport = CANDIDATE_TRANSPORT_UDP;
                start_port = component->min_port;
                if (component->min_port != 0)
                {
                    start_port = nice_rng_generate_int(agent->rng, component->min_port, component->max_port + 1);
                }
                current_port = start_port;

                host_candidate = NULL;
                while (res == HOST_CANDIDATE_CANT_CREATE_SOCKET)
                {
                    nice_debug("Agent %p: Trying to create host candidate on port %d", agent, current_port);
                    nice_address_set_port(addr, current_port);
                    res =  discovery_add_local_host_candidate(agent, stream->id, cid, addr, &host_candidate);
                    if (current_port > 0)
                        current_port++;
                    if (current_port > component->max_port) current_port = component->min_port;
                    if (current_port == 0 || current_port == start_port)
                        break;
                }

                if (res == HOST_CANDIDATE_REDUNDANT)
                {
                    nice_debug("Agent %p: Ignoring local candidate, it's redundant", agent);
                    continue;
                }
                else if (res == HOST_CANDIDATE_FAILED)
                {
                    nice_debug("Agent %p: Could ot retrieive component %d/%d", agent, stream->id, cid);
                    ret = FALSE;
                    goto error;
                }
                else if (res == HOST_CANDIDATE_CANT_CREATE_SOCKET)
                {
                    if (nice_debug_is_enabled())
                    {
                        char ip[NICE_ADDRESS_STRING_LEN];
                        nice_address_to_string(addr, ip);
                        nice_debug("Agent %p: Unable to add local host candidate %s for"
                                   " s%d:%d. Invalid interface?", agent, ip, stream->id,
                                   component->id);
                    }
                    ret = FALSE;
                    goto error;
                }

                nice_address_set_port(addr, 0);

                nice_socket_set_writable_callback(host_candidate->sockptr, _tcp_sock_is_writable, component);

                /* TODO: Add server-reflexive support for TCP candidates */
                if (agent->stun_server_ip)
                {
                    NiceAddress stun_server;
                    if (nice_address_set_from_string(&stun_server, agent->stun_server_ip))
                    {
                        nice_address_set_port(&stun_server, agent->stun_server_port);

                        priv_add_new_candidate_discovery_stun(agent, host_candidate->sockptr, stun_server, stream, cid);
                    }
                }

                if (component)
                {
                    GList * item;

                    for (item = component->turn_servers; item; item = item->next)
                    {
                        TurnServer * turn = item->data;
                        priv_add_new_candidate_discovery_turn(agent, host_candidate->sockptr, turn, stream, cid);
                    }
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
        Component * component = stream_find_component_by_id(stream, cid);
        for (i = component->local_candidates; i; i = i->next)
        {
            NiceCandidate * candidate = i->data;
            agent_signal_new_candidate(agent, candidate);
        }
    }

    /* note: no async discoveries pending, signal that we are ready */
    if (agent->discovery_unsched_items == 0)
    {
        nice_debug("Agent %p: Candidate gathering FINISHED, no scheduled items.",  agent);
        agent_gathering_done(agent);
    }
    else if (agent->discovery_unsched_items)
    {
        discovery_schedule(agent);
    }

error:
    for (i = local_addresses;
            i;
            i = i->next)
        nice_address_free(i->data);
    g_slist_free(local_addresses);

    if (ret == FALSE)
    {
        for (cid = 1; cid <= stream->n_components; cid++)
        {
            Component * component = stream_find_component_by_id(stream, cid);

            component_free_socket_sources(component);

            for (i = component->local_candidates; i; i = i->next)
            {
                NiceCandidate * candidate = i->data;

                agent_remove_local_candidate(agent, candidate);

                nice_candidate_free(candidate);
            }
            g_slist_free(component->local_candidates);
            component->local_candidates = NULL;
        }
        discovery_prune_stream(agent, stream_id);
    }

    agent_unlock_and_emit(agent);

    return ret;
}

void agent_remove_local_candidate(NiceAgent * agent, NiceCandidate * candidate)
{
}

static void priv_remove_keepalive_timer(NiceAgent * agent)
{
    if (agent->keepalive_timer_source != NULL)
    {
        g_source_destroy(agent->keepalive_timer_source);
        g_source_unref(agent->keepalive_timer_source);
        agent->keepalive_timer_source = NULL;
    }
}

void nice_agent_remove_stream(NiceAgent * agent, uint32_t stream_id)
{
    uint32_t stream_ids[] = { stream_id, 0 };

    /* note that streams/candidates can be in use by other threads */

    Stream * stream;

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
    conn_check_prune_stream(agent, stream);
    discovery_prune_stream(agent, stream_id);
    refresh_prune_stream(agent, stream_id);

    /* Remove the stream and signal its removal. */
    agent->streams_list = g_slist_remove(agent->streams_list, stream);
    stream_close(stream);

    if (!agent->streams_list)
        priv_remove_keepalive_timer(agent);

    agent_queue_signal(agent, signals[SIGNAL_STREAMS_REMOVED], g_memdup(stream_ids, sizeof(stream_ids)));

    agent_unlock_and_emit(agent);

    /* Actually free the stream. This should be done with the lock released, as
     * it could end up disposing of a NiceIOStream, which tries to take the
     * agent lock itself. */
    stream_free(stream);

    return;
}

void nice_agent_set_port_range(NiceAgent * agent, uint32_t stream_id, uint32_t component_id, uint32_t min_port, uint32_t max_port)
{
    Stream * stream;
    Component * component;

    g_return_if_fail(NICE_IS_AGENT(agent));
    g_return_if_fail(stream_id >= 1);
    g_return_if_fail(component_id >= 1);

    agent_lock();

    if (agent_find_component(agent, stream_id, component_id, &stream, &component))
    {
        if (stream->gathering_started)
        {
            g_critical("nice_agent_gather_candidates (stream_id=%u) already called for this stream", stream_id);
        }
        else
        {
            component->min_port = min_port;
            component->max_port = max_port;
        }
    }

    agent_unlock_and_emit(agent);
}

int32_t nice_agent_add_local_address(NiceAgent * agent, NiceAddress * addr)
{
    NiceAddress * dupaddr;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(addr != NULL, FALSE);

    agent_lock();

    dupaddr = nice_address_dup(addr);
    nice_address_set_port(dupaddr, 0);
    agent->local_addresses = g_slist_append(agent->local_addresses, dupaddr);

    agent_unlock_and_emit(agent);
    return TRUE;
}

static int32_t priv_add_remote_candidate(
    NiceAgent * agent,
    uint32_t stream_id,
    uint32_t component_id,
    NiceCandidateType type,
    const NiceAddress * addr,
    const NiceAddress * base_addr,
    NiceCandidateTransport transport,
    guint32 priority,
    const char * username,
    const char * password,
    const char * foundation)
{
    Component * component;
    NiceCandidate * candidate;

    if (!agent_find_component(agent, stream_id, component_id, NULL, &component))
        return FALSE;

    /* step: check whether the candidate already exists */
    candidate = component_find_remote_candidate(component, addr, transport);
    if (candidate)
    {
        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN];
            nice_address_to_string(addr, tmpbuf);
            nice_debug("Agent %p : Updating existing remote candidate with addr [%s]:%u"
                       " for s%d/c%d. U/P '%s'/'%s' prio: %u", agent, tmpbuf,
                       nice_address_get_port(addr), stream_id, component_id,
                       username, password, priority);
        }
        /* case 1: an existing candidate, update the attributes */
        candidate->type = type;
        if (base_addr)
            candidate->base_addr = *base_addr;
        candidate->priority = priority;
        if (foundation)
            g_strlcpy(candidate->foundation, foundation, CANDIDATE_MAX_FOUNDATION);
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
            g_free(candidate->username);
            candidate->username = g_strdup(username);
        }
        if (password)
        {
            g_free(candidate->password);
            candidate->password = g_strdup(password);
        }
    }
    else
    {
        /* case 2: add a new candidate */

        if (type == CANDIDATE_TYPE_PEER_REFLEXIVE)
        {
            nice_debug("Agent %p : Warning: ignoring externally set peer-reflexive candidate!", agent);
            return FALSE;
        }
        candidate = nice_candidate_new(type);
        component->remote_candidates = g_slist_append(component->remote_candidates, candidate);

        candidate->stream_id = stream_id;
        candidate->component_id = component_id;

        candidate->type = type;
        if (addr)
            candidate->addr = *addr;

        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN] = {0};
            if (addr)
                nice_address_to_string(addr, tmpbuf);
            nice_debug("Agent %p : Adding %s remote candidate with addr [%s]:%u"
                       " for s%d/c%d. U/P '%s'/'%s' prio: %u", agent,
                       _transport_to_string(transport), tmpbuf,
                       addr ? nice_address_get_port(addr) : 0, stream_id, component_id,
                       username, password, priority);
        }

        if (base_addr)
            candidate->base_addr = *base_addr;

        candidate->transport = transport;
        candidate->priority = priority;
        candidate->username = g_strdup(username);
        candidate->password = g_strdup(password);

        if (foundation)
            g_strlcpy(candidate->foundation, foundation, CANDIDATE_MAX_FOUNDATION);
    }

    if (conn_check_add_for_candidate(agent, stream_id, component, candidate) < 0)
    {
        goto errors;
    }

    return TRUE;

errors:
    nice_candidate_free(candidate);
    return FALSE;
}

int32_t nice_agent_set_remote_credentials(NiceAgent * agent, uint32_t stream_id, const char * ufrag, const char * pwd)
{
    Stream * stream;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);

    agent_lock();

    stream = agent_find_stream(agent, stream_id);
    /* note: oddly enough, ufrag and pwd can be empty strings */
    if (stream && ufrag && pwd)
    {

        g_strlcpy(stream->remote_ufrag, ufrag, NICE_STREAM_MAX_UFRAG);
        g_strlcpy(stream->remote_password, pwd, NICE_STREAM_MAX_PWD);

        ret = TRUE;
        goto done;
    }

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t nice_agent_set_local_credentials(NiceAgent * agent, uint32_t stream_id, const char * ufrag, const char * pwd)
{
    Stream * stream;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);

    agent_lock();

    stream = agent_find_stream(agent, stream_id);

    /* note: oddly enough, ufrag and pwd can be empty strings */
    if (stream && ufrag && pwd)
    {
        g_strlcpy(stream->local_ufrag, ufrag, NICE_STREAM_MAX_UFRAG);
        g_strlcpy(stream->local_password, pwd, NICE_STREAM_MAX_PWD);

        ret = TRUE;
        goto done;
    }

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t nice_agent_get_local_credentials(NiceAgent * agent, uint32_t stream_id, char ** ufrag, char ** pwd)
{
    Stream * stream;
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

static int32_t _set_remote_candidates_locked(NiceAgent * agent, Stream * stream, Component * component, const GSList * candidates)
{
    const GSList * i;
    int32_t added = 0;

    for (i = candidates; i && added >= 0; i = i->next)
    {
        NiceCandidate * d = (NiceCandidate *) i->data;

        if (nice_address_is_valid(&d->addr) == TRUE)
        {
            int32_t res =
                priv_add_remote_candidate(agent,
                                          stream->id,
                                          component->id,
                                          d->type,
                                          &d->addr,
                                          &d->base_addr,
                                          d->transport,
                                          d->priority,
                                          d->username,
                                          d->password,
                                          d->foundation);
            if (res)
                ++added;
        }
    }

    conn_check_remote_candidates_set(agent);

    if (added > 0)
    {
        int32_t res = conn_check_schedule_next(agent);
        if (res != TRUE)
            nice_debug("Agent %p : Warning: unable to schedule any conn checks!", agent);
    }

    return added;
}


int32_t nice_agent_set_remote_candidates(NiceAgent * agent, uint32_t stream_id, uint32_t component_id, const GSList * candidates)
{
    int32_t added = 0;
    Stream * stream;
    Component * component;

    g_return_val_if_fail(NICE_IS_AGENT(agent), 0);
    g_return_val_if_fail(stream_id >= 1, 0);
    g_return_val_if_fail(component_id >= 1, 0);

    nice_debug("Agent %p: set_remote_candidates %d %d", agent, stream_id, component_id);

    agent_lock();

    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
    {
        g_warning("Could not find component %u in stream %u", component_id,
                  stream_id);
        added = -1;
        goto done;
    }

    added = _set_remote_candidates_locked(agent, stream, component, candidates);

done:
    agent_unlock_and_emit(agent);

    return added;
}

/* Return values for agent_recv_message_unlocked(). Needed purely because it
 * must differentiate between RECV_OOB and RECV_SUCCESS. */
typedef enum
{
    RECV_ERROR = -2,
    RECV_WOULD_BLOCK = -1,
    RECV_OOB = 0,
    RECV_SUCCESS = 1,
} RecvStatus;

/*
 * agent_recv_message_unlocked:
 * @agent: a #NiceAgent
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
static RecvStatus agent_recv_message_unlocked(NiceAgent * agent, Stream * stream, Component * component, NiceSocket * nicesock, NiceInputMessage * message)
{
    NiceAddress from;
    GList * item;
    int32_t retval;

    /* We need an address for packet parsing, below. */
    if (message->from == NULL)
    {
        message->from = &from;
    }

    retval = nice_socket_recv_messages(nicesock, message, 1);

    //nice_debug("%s: Received %d valid messages of length %" G_GSIZE_FORMAT  " from base socket %p.", G_STRFUNC, retval, message->length, nicesock);

    if (retval == 0)
    {
        retval = RECV_WOULD_BLOCK;  /* EWOULDBLOCK */
        goto done;
    }
    else if (retval < 0)
    {
        nice_debug("Agent %p: %s returned %d, errno (%d) : %s", agent, G_STRFUNC, retval, errno, g_strerror(errno));
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
        nice_debug("Agent %p : Packet received on local socket %d from [%s]:%u (%" G_GSSIZE_FORMAT " octets).", agent,
                   g_socket_get_fd(nicesock->fileno), tmpbuf,  nice_address_get_port(message->from), message->length);
    }

    for (item = component->turn_servers; item; item = g_list_next(item))
    {
        TurnServer * turn = item->data;
        GSList * i = NULL;

        if (!nice_address_equal(message->from, &turn->server))
            continue;

        nice_debug("Agent %p : Packet received from TURN server candidate.", agent);

        for (i = component->local_candidates; i; i = i->next)
        {
            NiceCandidate * cand = i->data;

            if (cand->type == CANDIDATE_TYPE_RELAYED &&
                    cand->stream_id == stream->id &&
                    cand->component_id == component->id)
            {
                retval = nice_udp_turn_socket_parse_recv_message(cand->sockptr, &nicesock, message);
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
    if (stun_message_validate_buffer_length_fast((StunInputVector *) message->buffers, message->n_buffers, message->length, 1) == (ssize_t) message->length)
    {
        /* Slow path: If this message isnt obviously *not* a STUN packet, compact
         * its buffers  into a single monolithic one and parse the packet properly. */
        uint8_t * big_buf;
        uint32_t big_buf_len;
        int32_t validated_len;

        big_buf = compact_input_message(message, &big_buf_len);

        validated_len = stun_message_validate_buffer_length(big_buf, big_buf_len, 1);

        if (validated_len == (int32_t) big_buf_len)
        {
            int32_t handled;

            handled = conn_check_handle_inbound_stun(agent, stream, component, nicesock, message->from, (char *) big_buf, big_buf_len);

            if (handled)
            {
                /* Handled STUN message. */
                nice_debug("%s: Valid STUN packet received.", G_STRFUNC);
                retval = RECV_OOB;
                g_free(big_buf);
                goto done;
            }
        }

        nice_debug("%s: Packet passed fast STUN validation but failed " "slow validation.", G_STRFUNC);

        g_free(big_buf);
    }

    /* Unhandled STUN; try handling TCP data, then pass to the client. */
    if (message->length > 0  && agent->reliable)
    {
        if (!nice_socket_is_reliable(nicesock) && !pseudo_tcp_socket_is_closed(component->tcp))
        {
            /* If we don??t yet have an underlying selected socket, queue up the
             * incoming data to handle later. This is because we can??t send ACKs (or,
             * more importantly for the first few packets, SYNACKs) without an
             * underlying socket. We??d rather wait a little longer for a pair to be
             * selected, then process the incoming packets and send out ACKs, than try
             * to process them now, fail to send the ACKs, and incur a timeout in our
             * pseudo-TCP state machine. */
            if (component->selected_pair.local == NULL)
            {
                GOutputVector * vec = g_slice_new(GOutputVector);
                vec->buffer = compact_input_message(message, &vec->size);
                g_queue_push_tail(&component->queued_tcp_packets, vec);
                nice_debug("%s: Queued %" G_GSSIZE_FORMAT " bytes for agent %p.", G_STRFUNC, vec->size, agent);

                return RECV_OOB;
            }
            else
            {
                process_queued_tcp_packets(agent, stream, component);
            }

            /* Received data on a reliable connection. */

            nice_debug("%s: notifying pseudo-TCP of packet, length %" G_GSIZE_FORMAT, G_STRFUNC, message->length);
            pseudo_tcp_socket_notify_message(component->tcp, message);

            adjust_tcp_clock(agent, stream, component);

            /* Success! Handled out-of-band. */
            retval = RECV_OOB;
            goto done;
        }
        else if (pseudo_tcp_socket_is_closed(component->tcp))
        {
            nice_debug("Received data on a pseudo tcp FAILED component. Ignoring.");

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
static void nice_debug_input_message_composition(const NiceInputMessage * messages, uint32_t n_messages)
{
    uint32_t i;

    if (!nice_debug_is_enabled())
        return;

    for (i = 0; i < n_messages; i++)
    {
        const NiceInputMessage * message = &messages[i];
        uint32_t j;

        nice_debug("Message %p (from: %p, length: %" G_GSIZE_FORMAT ")", message, message->from, message->length);

        for (j = 0;
                (message->n_buffers >= 0 && j < (uint32_t) message->n_buffers) ||
                (message->n_buffers < 0 && message->buffers[j].buffer != NULL);
                j++)
        {
            GInputVector * buffer = &message->buffers[j];

            nice_debug("Buffer %p (length: %" G_GSIZE_FORMAT ")", buffer->buffer, buffer->size);
        }
    }
}

static uint8_t * compact_message(const NiceOutputMessage * message, uint32_t buffer_length)
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
 * The return value must be freed with g_free(). */
uint8_t * compact_input_message(const NiceInputMessage * message, uint32_t * buffer_length)
{
    //nice_debug("%s: **WARNING: SLOW PATH**", G_STRFUNC);
    nice_debug_input_message_composition(message, 1);

    /* This works as long as NiceInputMessage is a subset of eNiceOutputMessage */

    *buffer_length = message->length;

    return compact_message((NiceOutputMessage *) message, *buffer_length);
}

/* Returns the number of bytes copied. Silently drops any data from @buffer
 * which doesn??t fit in @message. */
uint32_t memcpy_buffer_to_input_message(NiceInputMessage * message, const uint8_t * buffer, uint32_t buffer_length)
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

    nice_debug_input_message_composition(message, 1);

    if (buffer_length > 0)
    {
        g_warning("Dropped %" G_GSIZE_FORMAT " bytes of data from the end of "
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
 * The return value must be freed with g_free(). */
uint8_t * compact_output_message(const NiceOutputMessage * message, uint32_t * buffer_length)
{
    //nice_debug("%s: **WARNING: SLOW PATH**", G_STRFUNC);

    *buffer_length = output_message_get_size(message);

    return compact_message(message, *buffer_length);
}

uint32_t output_message_get_size(const NiceOutputMessage * message)
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

static uint32_t input_message_get_size(const NiceInputMessage * message)
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
 * nice_input_message_iter_reset:
 * @iter: a #NiceInputMessageIter
 *
 * Reset the given @iter to point to the beginning of the array of messages.
 * This may be used both to initialise it and to reset it after use.
 *
 * Since: 0.1.5
 */
void nice_input_message_iter_reset(NiceInputMessageIter * iter)
{
    iter->message = 0;
    iter->buffer = 0;
    iter->offset = 0;
}

/*
 * nice_input_message_iter_is_at_end:
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
int32_t nice_input_message_iter_is_at_end(NiceInputMessageIter * iter, NiceInputMessage * messages, uint32_t n_messages)
{
    return (iter->message == n_messages && iter->buffer == 0 && iter->offset == 0);
}

/*
 * nice_input_message_iter_get_n_valid_messages:
 * @iter: a #NiceInputMessageIter
 *
 * Calculate the number of valid messages in the messages array. A valid message
 * is one which contains at least one valid byte of data in its buffers.
 *
 * Returns: number of valid messages (may be zero)
 *
 * Since: 0.1.5
 */
uint32_t nice_input_message_iter_get_n_valid_messages(NiceInputMessageIter * iter)
{
    if (iter->buffer == 0 && iter->offset == 0)
        return iter->message;
    else
        return iter->message + 1;
}

/* nice_agent_send_messages_nonblocking_internal:
 *
 * Returns: number of bytes sent if allow_partial is %TRUE, the number
 * of messages otherwise.
 */

static int32_t nice_agent_send_messages_nonblocking_internal(
    NiceAgent * agent,
    uint32_t stream_id,
    uint32_t component_id,
    const NiceOutputMessage * messages,
    uint32_t n_messages,
    int32_t allow_partial,
    GError ** error)
{
    Stream * stream;
    Component * component;
    int32_t n_sent = -1; /* is in bytes if allow_partial is TRUE, otherwise in messages */
    GError * child_error = NULL;

    g_assert(n_messages == 1 || !allow_partial);

    agent_lock();

    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
    {
        g_set_error(&child_error, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE,  "Invalid stream/component.");
        goto done;
    }

    /* FIXME: Cancellation isnt yet supported, but it doesnt matter because
     * we only deal with non-blocking writes. */
    if (component->selected_pair.local != NULL)
    {
        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN];
            nice_address_to_string(&component->selected_pair.remote->addr, tmpbuf);

            nice_debug("Agent %p : s%d:%d: sending %u messages to "
                       "[%s]:%d", agent, stream_id, component_id, n_messages, tmpbuf,
                       nice_address_get_port(&component->selected_pair.remote->addr));
        }

        if (!nice_socket_is_reliable(component->selected_pair.local->sockptr))
        {
            if (!pseudo_tcp_socket_is_closed(component->tcp))
            {
                /* Send on the pseudo-TCP socket. */
                n_sent = pseudo_tcp_socket_send_messages(component->tcp, messages, n_messages, allow_partial, &child_error);
                adjust_tcp_clock(agent, stream, component);

                if (!pseudo_tcp_socket_can_send(component->tcp))
                    g_cancellable_reset(component->tcp_writable_cancellable);
                if (n_sent < 0 && !g_error_matches(child_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
                {
                    /* Signal errors */
                    priv_pseudo_tcp_error(agent, stream, component);
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
        /* Socket isn??t properly open yet. */
        n_sent = 0;  /* EWOULDBLOCK */
    }

    /* Handle errors and cancellations. */
    if (n_sent == 0)
    {
        g_set_error_literal(&child_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK, g_strerror(EAGAIN));
        n_sent = -1;
    }

    nice_debug("%s: n_sent: %d, n_messages: %u", G_STRFUNC,  n_sent, n_messages);

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

int32_t nice_agent_send_messages_nonblocking(NiceAgent * agent, uint32_t stream_id, uint32_t component_id,
        const NiceOutputMessage * messages, uint32_t n_messages, GCancellable * cancellable, GError ** error)
{
    g_return_val_if_fail(NICE_IS_AGENT(agent), -1);
    g_return_val_if_fail(stream_id >= 1, -1);
    g_return_val_if_fail(component_id >= 1, -1);
    g_return_val_if_fail(n_messages == 0 || messages != NULL, -1);
    g_return_val_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable), -1);
    g_return_val_if_fail(error == NULL || *error == NULL, -1);

    if (g_cancellable_set_error_if_cancelled(cancellable, error))
        return -1;

    return nice_agent_send_messages_nonblocking_internal(agent, stream_id, component_id, messages, n_messages, FALSE, error);
}

int32_t nice_agent_send(NiceAgent * agent, uint32_t stream_id, uint32_t component_id, uint32_t len, const char * buf)
{
    GOutputVector local_buf = { buf, len };
    NiceOutputMessage local_message = { &local_buf, 1 };
    int32_t n_sent_bytes;

    g_return_val_if_fail(NICE_IS_AGENT(agent), -1);
    g_return_val_if_fail(stream_id >= 1, -1);
    g_return_val_if_fail(component_id >= 1, -1);
    g_return_val_if_fail(buf != NULL, -1);

    n_sent_bytes = nice_agent_send_messages_nonblocking_internal(agent, stream_id, component_id, &local_message, 1, TRUE, NULL);

    return n_sent_bytes;
}

GSList * nice_agent_get_local_candidates(NiceAgent * agent, uint32_t stream_id, uint32_t component_id)
{
    Component * component;
    GSList * ret = NULL;
    GSList * item = NULL;

    g_return_val_if_fail(NICE_IS_AGENT(agent), NULL);
    g_return_val_if_fail(stream_id >= 1, NULL);
    g_return_val_if_fail(component_id >= 1, NULL);

    agent_lock();

    if (!agent_find_component(agent, stream_id, component_id, NULL, &component))
    {
        goto done;
    }

    for (item = component->local_candidates; item; item = item->next)
        ret = g_slist_append(ret, nice_candidate_copy(item->data));

done:
    agent_unlock_and_emit(agent);
    return ret;
}


GSList * nice_agent_get_remote_candidates(NiceAgent * agent, uint32_t stream_id, uint32_t component_id)
{
    Component * component;
    GSList * ret = NULL, *item = NULL;

    g_return_val_if_fail(NICE_IS_AGENT(agent), NULL);
    g_return_val_if_fail(stream_id >= 1, NULL);
    g_return_val_if_fail(component_id >= 1, NULL);

    agent_lock();
    if (!agent_find_component(agent, stream_id, component_id, NULL, &component))
    {
        goto done;
    }

    for (item = component->remote_candidates; item; item = item->next)
        ret = g_slist_append(ret, nice_candidate_copy(item->data));

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t nice_agent_restart(NiceAgent * agent)
{
    GSList * i;

    agent_lock();

    /* step: regenerate tie-breaker value */
    priv_generate_tie_breaker(agent);

    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * stream = i->data;

        /* step: reset local credentials for the stream and
         * clean up the list of remote candidates */
        stream_restart(agent, stream);
    }

    agent_unlock_and_emit(agent);
    return TRUE;
}

int32_t nice_agent_restart_stream(NiceAgent * agent, uint32_t stream_id)
{
    int32_t res = FALSE;
    Stream * stream;

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


static void nice_agent_dispose(GObject * object)
{
    GSList * i;
    QueuedSignal * sig;
    NiceAgent * agent = NICE_AGENT(object);

    /* step: free resources for the binding discovery timers */
    discovery_free(agent);
    g_assert(agent->discovery_list == NULL);
    refresh_free(agent);
    g_assert(agent->refresh_list == NULL);

    /* step: free resources for the connectivity check timers */
    conn_check_free(agent);

    priv_remove_keepalive_timer(agent);

    for (i = agent->local_addresses; i; i = i->next)
    {
        NiceAddress * a = i->data;

        nice_address_free(a);
    }

    g_slist_free(agent->local_addresses);
    agent->local_addresses = NULL;

    for (i = agent->streams_list; i; i = i->next)
    {
        Stream * s = i->data;

        stream_close(s);
        stream_free(s);
    }

    g_slist_free(agent->streams_list);
    agent->streams_list = NULL;

    while ((sig = g_queue_pop_head(&agent->pending_signals)))
    {
        free_queued_signal(sig);
    }

    g_free(agent->stun_server_ip);
    agent->stun_server_ip = NULL;

    nice_rng_free(agent->rng);
    agent->rng = NULL;

    g_free(agent->software_attribute);
    agent->software_attribute = NULL;

    if (agent->main_context != NULL)
        g_main_context_unref(agent->main_context);
    agent->main_context = NULL;

    if (G_OBJECT_CLASS(nice_agent_parent_class)->dispose)
        G_OBJECT_CLASS(nice_agent_parent_class)->dispose(object);

}

int32_t component_io_cb(GSocket * gsocket, GIOCondition condition, void * user_data)
{
    SocketSource * socket_source = user_data;
    Component * component;
    NiceAgent * agent;
    Stream * stream;
    int32_t has_io_callback;
    int32_t remove_source = FALSE;

    agent_lock();

    if (g_source_is_destroyed(g_main_current_source()))
    {
        /* Silently return FALSE. */
        nice_debug("%s: source %p destroyed", G_STRFUNC, g_main_current_source());

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
        nice_debug("Agent %p: NiceSocket %p has received HUP", agent,  socket_source->socket);
        if (component->selected_pair.local &&
                component->selected_pair.local->sockptr == socket_source->socket &&
                component->state == COMPONENT_STATE_READY)
        {
            nice_debug("Agent %p: Selected pair socket %p has HUP, declaring failed", agent, socket_source->socket);
            agent_signal_component_state_change(agent, stream->id, component->id, COMPONENT_STATE_FAILED);
        }

        component_detach_socket(component, socket_source->socket);
        agent_unlock();
        return G_SOURCE_REMOVE;
    }

    has_io_callback = component_has_io_callback(component);

    /* Choose which receive buffer to use. If we??re reading for
     * nice_agent_attach_recv(), use a local static buffer. If we??re reading for
     * nice_agent_recv_messages(), use the buffer provided by the client.
     *
     * has_io_callback cannot change throughout this function, as we operate
     * entirely with the agent lock held, and component_set_io_callback() would
     * need to take the agent lock to change the Component??s io_callback. */
    g_assert(!has_io_callback || component->recv_messages == NULL);

    if (agent->reliable && !nice_socket_is_reliable(socket_source->socket))
    {
#define TCP_HEADER_SIZE 24 /* bytes */
        uint8_t local_header_buf[TCP_HEADER_SIZE];
        /* FIXME: Currently, the critical path for reliable packet delivery has two
         * memcpy()s: one into the pseudo-TCP receive buffer, and one out of it.
         * This could moderately easily be reduced to one memcpy() in the common
         * case of in-order packet delivery, by replacing local_body_buf with a
         * pointer into the pseudo-TCP receive buffer. If it turns out the packet
         * is out-of-order (which we can only know after parsing its header), the
         * data will need to be moved in the buffer. If the packet *is* in order,
         * however, the only memcpy() then needed is from the pseudo-TCP receive
         * buffer to the client??s message buffers.
         *
         * In fact, in the case of a reliable agent with I/O callbacks, zero
         * memcpy()s can be achieved (for in-order packet delivery) by emittin the
         * I/O callback directly from the pseudo-TCP receive buffer. */
        uint8_t local_body_buf[MAX_BUFFER_SIZE];
        GInputVector local_bufs[] =
        {
            { local_header_buf, sizeof(local_header_buf) },
            { local_body_buf, sizeof(local_body_buf) },
        };
        NiceInputMessage local_message =
        {
            local_bufs, G_N_ELEMENTS(local_bufs), NULL, 0
        };
        RecvStatus retval = 0;

        if (pseudo_tcp_socket_is_closed(component->tcp))
        {
            nice_debug("Agent %p: not handling incoming packet for s%d:%d "
                       "because pseudo-TCP socket does not exist in reliable mode.", agent,
                       stream->id, component->id);
            remove_source = TRUE;
            goto done;
        }

        while (has_io_callback || (component->recv_messages != NULL &&
                                   !nice_input_message_iter_is_at_end(&component->recv_messages_iter,
                                           component->recv_messages, component->n_recv_messages)))
        {
            /* Receive a single message. This will receive it into the given
             * @local_bufs then, for pseudo-TCP, emit I/O callbacks or copy it into
             * component->recv_messages in pseudo_tcp_socket_readable(). STUN packets
             * will be parsed in-place. */
            retval = agent_recv_message_unlocked(agent, stream, component, socket_source->socket, &local_message);

            nice_debug("%s: %p: received %d valid messages with %" G_GSSIZE_FORMAT  " bytes", G_STRFUNC, agent, retval, local_message.length);

            /* Dont expect any valid messages to escape pseudo_tcp_socket_readable()
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
                nice_debug("%s: error receiving message", G_STRFUNC);
                remove_source = TRUE;
                break;
            }

            has_io_callback = component_has_io_callback(component);
        }
    }
    else if (has_io_callback)
    {
        while (has_io_callback)
        {
            uint8_t local_buf[MAX_BUFFER_SIZE];
            GInputVector local_bufs = { local_buf, sizeof(local_buf) };
            NiceInputMessage local_message = { &local_bufs, 1, NULL, 0 };
            RecvStatus retval;

            /* Receive a single message. */
            retval = agent_recv_message_unlocked(agent, stream, component, socket_source->socket, &local_message);

            nice_debug("%s: %p: received %d valid messages with %" G_GSSIZE_FORMAT  " bytes", G_STRFUNC, agent, retval, local_message.length);

            if (retval == RECV_WOULD_BLOCK)
            {
                /* EWOULDBLOCK. */
                break;
            }
            else if (retval == RECV_ERROR)
            {
                /* Other error. */
                nice_debug("%s: error receiving message", G_STRFUNC);
                remove_source = TRUE;
                break;
            }

            if (retval == RECV_SUCCESS && local_message.length > 0)
                component_emit_io_callback(component, local_buf, local_message.length);

            if (g_source_is_destroyed(g_main_current_source()))
            {
                nice_debug("Component IO source disappeared during the callback");
                goto out;
            }
            has_io_callback = component_has_io_callback(component);
        }
    }
    else if (component->recv_messages != NULL)
    {
        RecvStatus retval;

        /* Don??t want to trample over partially-valid buffers. */
        g_assert(component->recv_messages_iter.buffer == 0);
        g_assert(component->recv_messages_iter.offset == 0);

        while (!nice_input_message_iter_is_at_end(&component->recv_messages_iter,
                component->recv_messages, component->n_recv_messages))
        {
            /* Receive a single message. This will receive it into the given
             * user-provided #NiceInputMessage, which it??s the user??s responsibility
             * to ensure is big enough to avoid data loss (since we??re in non-reliable
             * mode). Iterate to receive as many messages as possible.
             *
             * STUN packets will be parsed in-place. */
            retval = agent_recv_message_unlocked(agent, stream, component,
                                                 socket_source->socket,
                                                 &component->recv_messages[component->recv_messages_iter.message]);

            nice_debug("%s: %p: received %d valid messages", G_STRFUNC, agent, retval);

            if (retval == RECV_SUCCESS)
            {
                /* Successfully received a single message. */
                component->recv_messages_iter.message++;
                g_clear_error(component->recv_buf_error);
            }
            else if (retval == RECV_WOULD_BLOCK)
            {
                /* EWOULDBLOCK. */
                if (component->recv_messages_iter.message == 0 &&
                        component->recv_buf_error != NULL &&
                        *component->recv_buf_error == NULL)
                {
                    g_set_error_literal(component->recv_buf_error, G_IO_ERROR,
                                        G_IO_ERROR_WOULD_BLOCK, g_strerror(EAGAIN));
                }
                break;
            }
            else if (retval == RECV_ERROR)
            {
                /* Other error. */
                remove_source = TRUE;
                break;
            } /* else if (retval == RECV_OOB) { ignore me and continue; } */
        }
    }

done:
    /* If we??re in the middle of a read, don??t emit any signals, or we could cause
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

out:
    g_object_unref(agent);
    agent_unlock_and_emit(agent);
    return G_SOURCE_REMOVE;
}

int32_t nice_agent_attach_recv(NiceAgent * agent, uint32_t stream_id, uint32_t component_id, GMainContext * ctx, NiceAgentRecvFunc func, void * data)
{
    Component * component = NULL;
    Stream * stream = NULL;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(component_id >= 1, FALSE);

    agent_lock();

    /* attach candidates */

    /* step: check that params specify an existing pair */
    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
    {
        g_warning("Could not find component %u in stream %u", component_id, stream_id);
        goto done;
    }

    if (ctx == NULL)
        ctx = g_main_context_default();

    /* Set the component??s I/O context. */
    component_set_io_context(component, ctx);
    component_set_io_callback(component, func, data, NULL, 0, NULL);
    ret = TRUE;

    if (func)
    {
        /* If we got detached, maybe our readable callback didn't finish reading
         * all available data in the pseudotcp, so we need to make sure we free
         * our recv window, so the readable callback can be triggered again on the
         * next incoming data.
         * but only do this if we know we're already readable, otherwise we might
         * trigger an error in the initial, pre-connection attach. */
        if (agent->reliable && !pseudo_tcp_socket_is_closed(component->tcp) && component->tcp_readable)
            pseudo_tcp_socket_readable(component->tcp, component);
    }

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t nice_agent_set_selected_pair(NiceAgent * agent, uint32_t stream_id, uint32_t component_id, const char * lfoundation, const char * rfoundation)
{
    Component * component;
    Stream * stream;
    CandidatePair pair;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(component_id >= 1, FALSE);
    g_return_val_if_fail(lfoundation, FALSE);
    g_return_val_if_fail(rfoundation, FALSE);

    agent_lock();

    /* step: check that params specify an existing pair */
    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
    {
        goto done;
    }

    if (!component_find_pair(component, agent, lfoundation, rfoundation, &pair))
    {
        goto done;
    }

    /* step: stop connectivity checks (note: for the whole stream) */
    conn_check_prune_stream(agent, stream);

    if (agent->reliable && !nice_socket_is_reliable(pair.local->sockptr) &&
            pseudo_tcp_socket_is_closed(component->tcp))
    {
        nice_debug("Agent %p: not setting selected pair for s%d:%d because "
                   "pseudo tcp socket does not exist in reliable mode", agent,
                   stream->id, component->id);
        goto done;
    }

    /* step: change component state */
    agent_signal_component_state_change(agent, stream_id, component_id, COMPONENT_STATE_READY);

    /* step: set the selected pair */
    component_update_selected_pair(component, &pair);
    agent_signal_new_selected_pair(agent, stream_id, component_id, pair.local, pair.remote);

    ret = TRUE;

done:
    agent_unlock_and_emit(agent);
    return ret;
}

int32_t nice_agent_get_selected_pair(NiceAgent * agent, uint32_t stream_id, uint32_t component_id, NiceCandidate ** local, NiceCandidate ** remote)
{
    Component * component;
    Stream * stream;
    int32_t ret = FALSE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(component_id >= 1, FALSE);
    g_return_val_if_fail(local != NULL, FALSE);
    g_return_val_if_fail(remote != NULL, FALSE);

    agent_lock();

    /* step: check that params specify an existing pair */
    if (!agent_find_component(agent, stream_id, component_id,
                              &stream, &component))
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

GSocket * nice_agent_get_selected_socket(NiceAgent * agent, uint32_t stream_id, uint32_t component_id)
{
    Component * component;
    Stream * stream;
    NiceSocket * nice_socket;
    GSocket * g_socket = NULL;

    g_return_val_if_fail(NICE_IS_AGENT(agent), NULL);
    g_return_val_if_fail(stream_id >= 1, NULL);
    g_return_val_if_fail(component_id >= 1, NULL);

    agent_lock();

    /* Reliable streams are pseudotcp or MUST use RFC 4571 framing */
    if (agent->reliable)
        goto done;

    /* step: check that params specify an existing pair */
    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
        goto done;

    if (!component->selected_pair.local || !component->selected_pair.remote)
        goto done;

    if (component->selected_pair.local->type == CANDIDATE_TYPE_RELAYED)
        goto done;

    /* ICE-TCP requires RFC4571 framing, even if unreliable */
    if (component->selected_pair.local->transport != CANDIDATE_TRANSPORT_UDP)
        goto done;

    nice_socket = (NiceSocket *)component->selected_pair.local->sockptr;
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
void agent_timeout_add_with_context(NiceAgent * agent, GSource ** out, const char * name, uint32_t interval, GSourceFunc function, void * data)
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


int32_t nice_agent_set_selected_remote_candidate(NiceAgent * agent, uint32_t stream_id, uint32_t component_id, NiceCandidate * candidate)
{
    Component * component;
    Stream * stream;
    NiceCandidate * lcandidate = NULL;
    int32_t ret = FALSE;
    NiceCandidate * local = NULL, *remote = NULL;
    guint64 priority;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id != 0, FALSE);
    g_return_val_if_fail(component_id != 0, FALSE);
    g_return_val_if_fail(candidate != NULL, FALSE);

    agent_lock();

    /* step: check if the component exists*/
    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
    {
        goto done;
    }

    /* step: stop connectivity checks (note: for the whole stream) */
    conn_check_prune_stream(agent, stream);

    /* Store previous selected pair */
    local = component->selected_pair.local;
    remote = component->selected_pair.remote;
    priority = component->selected_pair.priority;

    /* step: set the selected pair */
    lcandidate = component_set_selected_remote_candidate(agent, component, candidate);
    if (!lcandidate)
        goto done;

    if (agent->reliable && !nice_socket_is_reliable(lcandidate->sockptr) &&  pseudo_tcp_socket_is_closed(component->tcp))
    {
        nice_debug("Agent %p: not setting selected remote candidate s%d:%d because"
                   " pseudo tcp socket does not exist in reliable mode", agent,  stream->id, component->id);
        /* Revert back to previous selected pair */
        /* FIXME: by doing this, we lose the keepalive tick */
        component->selected_pair.local = local;
        component->selected_pair.remote = remote;
        component->selected_pair.priority = (uint32_t)priority;
        goto done;
    }

    /* step: change component state */
    agent_signal_component_state_change(agent, stream_id, component_id, COMPONENT_STATE_READY);
    agent_signal_new_selected_pair(agent, stream_id, component_id, lcandidate, candidate);

    ret = TRUE;

done:
    agent_unlock_and_emit(agent);
    return ret;
}

void _priv_set_socket_tos(NiceAgent * agent, NiceSocket * sock, int32_t tos)
{
    if (sock->fileno == NULL)
        return;

    if (setsockopt(g_socket_get_fd(sock->fileno), IPPROTO_IP, IP_TOS, (const char *) &tos, sizeof(tos)) < 0)
    {
        nice_debug("Agent %p: Could not set socket ToS: %s", agent, g_strerror(errno));
    }
}


void nice_agent_set_stream_tos(NiceAgent * agent, uint32_t stream_id, int32_t tos)
{
    GSList * i, *j;
    Stream * stream;

    g_return_if_fail(NICE_IS_AGENT(agent));
    g_return_if_fail(stream_id >= 1);

    agent_lock();

    stream = agent_find_stream(agent, stream_id);
    if (stream == NULL)
        goto done;

    stream->tos = tos;
    for (i = stream->components; i; i = i->next)
    {
        Component * component = i->data;

        for (j = component->local_candidates; j; j = j->next)
        {
            NiceCandidate * local_candidate = j->data;

            _priv_set_socket_tos(agent, local_candidate->sockptr, tos);
        }
    }

done:
    agent_unlock_and_emit(agent);
}

int32_t nice_agent_set_stream_name(NiceAgent * agent, uint32_t stream_id, const char * name)
{
    Stream * stream_to_name = NULL;
    GSList * i;
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
        g_critical("Stream name %s will produce invalid SDP, only \"audio\","
                   " \"video\", \"text\", \"application\", \"image\" and \"message\""
                   " are valid", name);
    }

    agent_lock();

    if (name != NULL)
    {
        for (i = agent->streams_list; i; i = i->next)
        {
            Stream * stream = i->data;

            if (stream->id != stream_id && g_strcmp0(stream->name, name) == 0)
                goto done;
            else if (stream->id == stream_id)
                stream_to_name = stream;
        }
    }

    if (stream_to_name == NULL)
        goto done;

    if (stream_to_name->name)
        g_free(stream_to_name->name);
    stream_to_name->name = g_strdup(name);
    ret = TRUE;

done:
    agent_unlock_and_emit(agent);

    return ret;
}

const char * nice_agent_get_stream_name(NiceAgent * agent, uint32_t stream_id)
{
    Stream * stream;
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

static NiceCandidate * _get_default_local_candidate_locked(NiceAgent * agent, Stream * stream,  Component * component)
{
    GSList * i;
    NiceCandidate * default_candidate = NULL;
    NiceCandidate * default_rtp_candidate = NULL;

    if (component->id != NICE_COMPONENT_TYPE_RTP)
    {
        Component * rtp_component;

        if (!agent_find_component(agent, stream->id, NICE_COMPONENT_TYPE_RTP, NULL, &rtp_component))
            goto done;

        default_rtp_candidate = _get_default_local_candidate_locked(agent, stream,
                                rtp_component);
        if (default_rtp_candidate == NULL)
            goto done;
    }


    for (i = component->local_candidates; i; i = i->next)
    {
        NiceCandidate * local_candidate = i->data;

        /* Only check for ipv4 candidates */
        if (nice_address_ip_version(&local_candidate->addr) != 4)
            continue;
        if (component->id == NICE_COMPONENT_TYPE_RTP)
        {
            if (default_candidate == NULL ||
                    local_candidate->priority < default_candidate->priority)
            {
                default_candidate = local_candidate;
            }
        }
        else if (strncmp(local_candidate->foundation, default_rtp_candidate->foundation, CANDIDATE_MAX_FOUNDATION) == 0)
        {
            default_candidate = local_candidate;
            break;
        }
    }

done:
    return default_candidate;
}

NiceCandidate * nice_agent_get_default_local_candidate(NiceAgent * agent, uint32_t stream_id,  uint32_t component_id)
{
    Stream * stream = NULL;
    Component * component = NULL;
    NiceCandidate * default_candidate = NULL;

    g_return_val_if_fail(NICE_IS_AGENT(agent), NULL);
    g_return_val_if_fail(stream_id >= 1, NULL);
    g_return_val_if_fail(component_id >= 1, NULL);

    agent_lock();

    /* step: check if the component exists*/
    if (!agent_find_component(agent, stream_id, component_id, &stream, &component))
        goto done;

    default_candidate = _get_default_local_candidate_locked(agent, stream,
                        component);
    if (default_candidate)
        default_candidate = nice_candidate_copy(default_candidate);

done:
    agent_unlock_and_emit(agent);

    return default_candidate;
}

int32_t nice_agent_forget_relays(NiceAgent * agent, uint32_t stream_id, uint32_t component_id)
{
    Component * component;
    int32_t ret = TRUE;

    g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    g_return_val_if_fail(stream_id >= 1, FALSE);
    g_return_val_if_fail(component_id >= 1, FALSE);

    agent_lock();

    if (!agent_find_component(agent, stream_id, component_id, NULL, &component))
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
int32_t agent_socket_send(NiceSocket * sock, const NiceAddress * addr, uint32_t len, const char * buf)
{
    if (nice_socket_is_reliable(sock))
    {
        guint16 rfc4571_frame = htons(len);
        GOutputVector local_buf[2] = {{&rfc4571_frame, 2}, { buf, len }};
        NiceOutputMessage local_message = { local_buf, 2};
        int32_t ret;

        /* ICE-TCP requires that all packets be framed with RFC4571 */
        ret = nice_socket_send_messages_reliable(sock, addr, &local_message, 1);
        if (ret == 1)
            return len;
        return ret;
    }
    else
    {
        int32_t ret = nice_socket_send_reliable(sock, addr, len, buf);
        if (ret < 0)
            ret = nice_socket_send(sock, addr, len, buf);
        return ret;
    }
}

NiceComponentState nice_agent_get_component_state(NiceAgent * agent, uint32_t stream_id, uint32_t component_id)
{
    NiceComponentState state = COMPONENT_STATE_FAILED;
    Component * component;

    agent_lock();

    if (agent_find_component(agent, stream_id, component_id, NULL, &component))
        state = component->state;

    agent_unlock();

    return state;
}
