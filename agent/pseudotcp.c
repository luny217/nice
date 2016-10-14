/* This file is part of the Nice GLib ICE library. */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <glib.h>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#include "pseudotcp.h"
#include "agent-priv.h"

G_DEFINE_TYPE(pst_socket_t, pst, G_TYPE_OBJECT);


//////////////////////////////////////////////////////////////////////
// Network Constants
//////////////////////////////////////////////////////////////////////

// Standard MTUs
const uint16_t PACKET_MAXIMUMS[] =
{
    65535,    // Theoretical maximum, Hyperchannel
    32000,    // Nothing
    17914,    // 16Mb IBM Token Ring
    8166,   // IEEE 802.4
    //4464,   // IEEE 802.5 (4Mb max)
    4352,   // FDDI
    //2048,   // Wideband Network
    2002,   // IEEE 802.5 (4Mb recommended)
    //1536,   // Expermental Ethernet Networks
    //1500,   // Ethernet, Point-to-Point (default)
    1492,   // IEEE 802.3
    1006,   // SLIP, ARPANET
    //576,    // X.25 Networks
    //544,    // DEC IP Portal
    //512,    // NETBIOS
    508,    // IEEE 802/Source-Rt Bridge, ARCNET
    296,    // Point-to-Point (low delay)
    //68,     // Official minimum
    0,      // End of list marker
};

#define MAX_PACKET 65535
// Note: we removed lowest level because packet overhead was larger!
#define MIN_PACKET 296

// (+ up to 40 bytes of options?)
#define IP_HEADER_SIZE 20
#define ICMP_HEADER_SIZE 8
#define UDP_HEADER_SIZE 8
// TODO: Make JINGLE_HEADER_SIZE transparent to this code?
// when relay framing is in use
#define JINGLE_HEADER_SIZE 64

//////////////////////////////////////////////////////////////////////
// Global Constants and Functions
//////////////////////////////////////////////////////////////////////
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  0 |                      Conversation Number                      |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  4 |                        Sequence Number                        |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  8 |                     Acknowledgment Number                     |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |               |   |U|A|P|R|S|F|                               |
// 12 |    Control    |   |R|C|S|S|Y|I|            Window             |
//    |               |   |G|K|H|T|N|N|                               |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 16 |                       Timestamp sending                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 20 |                      Timestamp receiving                      |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 24 |                             data                              |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//////////////////////////////////////////////////////////////////////

#define MAX_SEQ 0xFFFFFFFF
#define HEADER_SIZE 24

#define PACKET_OVERHEAD (HEADER_SIZE + UDP_HEADER_SIZE + \
      IP_HEADER_SIZE + JINGLE_HEADER_SIZE)

// MIN_RTO = 250 ms (RFC1122, Sec 4.2.3.1 "fractions of a second")
#define MIN_RTO      250
#define DEF_RTO     1000 /* 1 seconds (RFC 6298 sect 2.1) */
#define MAX_RTO    60000 /* 60 seconds */
#define DEFAULT_ACK_DELAY    100 /* 100 milliseconds */
#define DEFAULT_NO_DELAY     FALSE

#define DEFAULT_RCV_BUF_SIZE (60 * 1024)
#define DEFAULT_SND_BUF_SIZE (90 * 1024)

/* NOTE: This must fit in 8 bits. This is used on the wire. */
typedef enum
{
    /* Google-provided options: */
    TCP_OPT_EOL = 0,  /* end of list */
    TCP_OPT_NOOP = 1,  /* no-op */
    TCP_OPT_MSS = 2,  /* maximum segment size */
    TCP_OPT_WND_SCALE = 3,  /* window scale factor */
    /* libnice extensions: */
    TCP_OPT_FIN_ACK = 254,  /* FIN-ACK support */
} TcpOption;


/*
#define FLAG_SYN 0x02
#define FLAG_ACK 0x10
*/

/* NOTE: This must fit in 5 bits. This is used on the wire. */
typedef enum
{
    FLAG_NONE = 0,
    FLAG_FIN = 1 << 0,
    FLAG_CTL = 1 << 1,
    FLAG_RST = 1 << 2,
} TcpFlags;

#define CTL_CONNECT  0
//#define CTL_REDIRECT  1
#define CTL_EXTRA 255


#define CTRL_BOUND 0x80000000

/* Maximum segment lifetime (1 minute).
 * RFC 793, ?3.3 specifies 2 minutes; but Linux uses 1 minute, so let?s go with
 * that. */
#define TCP_MSL (60 * 1000)

// If there are no pending clocks, wake up every 4 seconds
#define DEFAULT_TIMEOUT 4000
// If the connection is closed, once per minute
#define CLOSED_TIMEOUT (60 * 1000)
/* Timeout after reaching the TIME_WAIT state, in milliseconds.
 * See: RFC 1122, ?4.2.2.13.
 *
 * XXX: Since we can control the underlying layer?s channel ID, we can guarantee
 * delayed segments won?t affect subsequent connections, so can radically
 * shorten the TIME-WAIT timeout (to the extent that it basically doesn?t
 * exist). It would normally be (2 * TCP_MSL). */
#define TIME_WAIT_TIMEOUT 1

//////////////////////////////////////////////////////////////////////
// Helper Functions
//////////////////////////////////////////////////////////////////////
#ifndef G_OS_WIN32
#  define min(first, second) ((first) < (second) ? (first) : (second))
#  define max(first, second) ((first) > (second) ? (first) : (second))
#endif

static uint32_t bound(uint32_t lower, uint32_t middle, uint32_t upper)
{
    return min(max(lower, middle), upper);
}

static int time_is_between(uint32_t later, uint32_t middle, uint32_t earlier)
{
    if (earlier <= later)
    {
        return ((earlier <= middle) && (middle <= later));
    }
    else
    {
        return !((later < middle) && (middle < earlier));
    }
}

static int32_t time_diff(uint32_t later, uint32_t earlier)
{
    uint32_t LAST = 0xFFFFFFFF;
    uint32_t HALF = 0x80000000;
    if (time_is_between(earlier + HALF, later, earlier))
    {
        if (earlier <= later)
        {
            return (long)(later - earlier);
        }
        else
        {
            return (long)(later + (LAST - earlier) + 1);
        }
    }
    else
    {
        if (later <= earlier)
        {
            return -(long)(earlier - later);
        }
        else
        {
            return -(long)(earlier + (LAST - later) + 1);
        }
    }
}

////////////////////////////////////////////////////////
// PseudoTcpFifo works exactly like FifoBuffer in libjingle
////////////////////////////////////////////////////////

typedef struct
{
    uint8_t * buffer;
    uint32_t buffer_length;
    uint32_t data_length;
    uint32_t read_position;
} PseudoTcpFifo;


static void pst_fifo_init(PseudoTcpFifo * b, uint32_t size)
{
    b->buffer = n_slice_alloc(size);
    b->buffer_length = size;
}

static void pst_fifo_clear(PseudoTcpFifo * b)
{
    if (b->buffer)
        n_slice_free1(b->buffer_length, b->buffer);
    b->buffer = NULL;
    b->buffer_length = 0;
}

static uint32_t pst_fifo_get_buffered(PseudoTcpFifo * b)
{
    return b->data_length;
}

static int pst_fifo_set_capacity(PseudoTcpFifo * b, uint32_t size)
{
    if (b->data_length > size)
        return FALSE;

    if (size != b->data_length)
    {
        uint8_t * buffer = n_slice_alloc(size);
        uint32_t copy = b->data_length;
        uint32_t tail_copy = min(copy, b->buffer_length - b->read_position);

        memcpy(buffer, &b->buffer[b->read_position], tail_copy);
        memcpy(buffer + tail_copy, &b->buffer[0], copy - tail_copy);
        n_slice_free1(b->buffer_length, b->buffer);
        b->buffer = buffer;
        b->buffer_length = size;
        b->read_position = 0;
    }

    return TRUE;
}

static void pst_fifo_consume_read_data(PseudoTcpFifo * b, uint32_t size)
{
    g_assert(size <= b->data_length);

    b->read_position = (b->read_position + size) % b->buffer_length;
    b->data_length -= size;
}

static void pst_fifo_consume_write_buffer(PseudoTcpFifo * b, uint32_t size)
{
    g_assert(size <= b->buffer_length - b->data_length);

    b->data_length += size;
}

static uint32_t pst_fifo_get_write_remaining(PseudoTcpFifo * b)
{
    return b->buffer_length - b->data_length;
}

static uint32_t pst_fifo_read_offset(PseudoTcpFifo * b, uint8_t * buffer, uint32_t bytes, uint32_t offset)
{
    uint32_t available = b->data_length - offset;
    uint32_t read_position = (b->read_position + offset) % b->buffer_length;
    uint32_t copy = min(bytes, available);
    uint32_t tail_copy = min(copy, b->buffer_length - read_position);

    /* EOS */
    if (offset >= b->data_length)
        return 0;

    memcpy(buffer, &b->buffer[read_position], tail_copy);
    memcpy(buffer + tail_copy, &b->buffer[0], copy - tail_copy);

    return copy;
}

static uint32_t pst_fifo_write_offset(PseudoTcpFifo * b, const uint8_t * buffer, uint32_t bytes, uint32_t offset)
{
    uint32_t available = b->buffer_length - b->data_length - offset;
    uint32_t write_position = (b->read_position + b->data_length + offset) % b->buffer_length;
    uint32_t copy = min(bytes, available);
    uint32_t tail_copy = min(copy, b->buffer_length - write_position);

    if (b->data_length + offset >= b->buffer_length)
    {
        return 0;
    }

    memcpy(&b->buffer[write_position], buffer, tail_copy);
    memcpy(&b->buffer[0], buffer + tail_copy, copy - tail_copy);

    return copy;
}

static uint32_t pst_fifo_read(PseudoTcpFifo * b, uint8_t * buffer, uint32_t bytes)
{
    uint32_t copy;

    copy = pst_fifo_read_offset(b, buffer, bytes, 0);

    b->read_position = (b->read_position + copy) % b->buffer_length;
    b->data_length -= copy;

    return copy;
}

static uint32_t pst_fifo_write(PseudoTcpFifo * b, const uint8_t * buffer, uint32_t bytes)
{
    uint32_t copy;

    copy = pst_fifo_write_offset(b, buffer, bytes, 0);
    b->data_length += copy;

    return copy;
}

//////////////////////////////////////////////////////////////////////
// PseudoTcp
//////////////////////////////////////////////////////////////////////

/* Only used if FIN-ACK support is disabled. */
typedef enum
{
    SD_NONE,
    SD_GRACEFUL,
    SD_FORCEFUL
} Shutdown;

typedef enum
{
    sfNone,
    sfDelayedAck,
    sfImmediateAck,
    sfFin,
    sfRst,
} SendFlags;

typedef struct
{
    uint32_t conv, seq, ack;
    TcpFlags flags;
    guint16 wnd;
    const char * data;
    uint32_t len;
    uint32_t tsval, tsecr;
} Segment;

typedef struct
{
    uint32_t seq, len;
    uint8_t xmit;
    TcpFlags flags;
} SSegment;

typedef struct
{
    uint32_t seq, len;
} RSegment;

/**
 * ClosedownSource:
 * @CLOSEDOWN_LOCAL: Error detected locally, or connection forcefully closed
 * locally.
 * @CLOSEDOWN_REMOTE: RST segment received from the peer.
 *
 * Reasons for calling closedown().
 *
 * Since: 0.1.8
 */
typedef enum
{
    CLOSEDOWN_LOCAL,
    CLOSEDOWN_REMOTE,
} ClosedownSource;


struct _PseudoTcpSocketPrivate
{
    pst_callback_t callbacks;

    Shutdown shutdown;  /* only used if !support_fin_ack */
    int shutdown_reads;
    int32_t error;

    // TCB data
    PseudoTcpState state;
    uint32_t conv;
    int bReadEnable, bWriteEnable, bOutgoing;
    uint32_t last_traffic;

    // Incoming data
    n_dlist_t * rlist;
    uint32_t rbuf_len, rcv_nxt, rcv_wnd, lastrecv;
    uint8_t rwnd_scale; // Window scale factor
    PseudoTcpFifo rbuf;

    // Outgoing data
    n_queue_t slist;
    n_queue_t unsent_slist;
    uint32_t sbuf_len, snd_nxt, snd_wnd, lastsend;
    uint32_t snd_una;  /* oldest unacknowledged sequence number */
    uint8_t swnd_scale; // Window scale factor
    PseudoTcpFifo sbuf;

    // Maximum segment size, estimated protocol level, largest segment sent
    uint32_t mss, msslevel, largest, mtu_advise;
    // Retransmit timer
    uint32_t rto_base;

    // Timestamp tracking
    uint32_t ts_recent, ts_lastack;

    // Round-trip calculation
    uint32_t rx_rttvar, rx_srtt, rx_rto;

    // Congestion avoidance, Fast retransmit/recovery, Delayed ACKs
    uint32_t ssthresh, cwnd;
    uint8_t dup_acks;
    uint32_t recover;
    uint32_t t_ack;  /* time a delayed ack was scheduled; 0 if no acks scheduled */

    int use_nagling;
    uint32_t ack_delay;

    // This is used by unit tests to test backward compatibility of
    // PseudoTcp implementations that don't support window scaling.
    int support_wnd_scale;

    /* Current time. Typically only used for testing, when non-zero. When zero,
     * the system monotonic clock is used. Units: monotonic milliseconds. */
    uint32_t current_time;

    /* This is used by compatible implementations (with the TCP_OPT_FIN_ACK
     * option) to enable correct FIN-ACK connection termination. Defaults to
     * TRUE unless no compatible option is received. */
    int support_fin_ack;
};

#define LARGER(a,b) (((a) - (b) - 1) < (G_MAXUINT32 >> 1))
#define LARGER_OR_EQUAL(a,b) (((a) - (b)) < (G_MAXUINT32 >> 1))
#define SMALLER(a,b) LARGER ((b),(a))
#define SMALLER_OR_EQUAL(a,b) LARGER_OR_EQUAL ((b),(a))

/* properties */
enum
{
    PROP_CONVERSATION = 1,
    PROP_CALLBACKS,
    PROP_STATE,
    PROP_ACK_DELAY,
    PROP_NO_DELAY,
    PROP_RCV_BUF,
    PROP_SND_BUF,
    PROP_SUPPORT_FIN_ACK,
    LAST_PROPERTY
};


static void pst_get_property(GObject * object, uint32_t property_id, GValue * value,  GParamSpec * pspec);
static void pst_set_property(GObject * object, uint32_t property_id, const GValue * value, GParamSpec * pspec);
static void pst_finalize(GObject * object);


static void queue_connect_message(pst_socket_t * self);
static uint32_t queue(pst_socket_t * self, const char * data, uint32_t len, TcpFlags flags);
static pst_wret_e packet(pst_socket_t * self, uint32_t seq, TcpFlags flags, uint32_t offset, uint32_t len, uint32_t now);
static int parse(pst_socket_t * self, const uint8_t * _header_buf, uint32_t header_buf_len, const uint8_t * data_buf, uint32_t data_buf_len);
static int process(pst_socket_t * self, Segment * seg);
static int transmit(pst_socket_t * self, SSegment * sseg, uint32_t now);
static void attempt_send(pst_socket_t * self, SendFlags sflags);
static void closedown(pst_socket_t * self, uint32_t err, ClosedownSource source);
static void adjustMTU(pst_socket_t * self);
static void parse_options(pst_socket_t * self, const uint8_t * data, uint32_t len);
static void resize_send_buffer(pst_socket_t * self, uint32_t new_size);
static void resize_receive_buffer(pst_socket_t * self, uint32_t new_size);
static void set_state(pst_socket_t * self, PseudoTcpState new_state);
static void set_state_established(pst_socket_t * self);
static void set_state_closed(pst_socket_t * self, uint32_t err);

static const char * pseudo_tcp_state_get_name(PseudoTcpState state);
static int pseudo_tcp_state_has_sent_fin(PseudoTcpState state);
static int pseudo_tcp_state_has_received_fin(PseudoTcpState state);

// The following logging is for detailed (packet-level) pseudotcp analysis only.
static PseudoTcpDebugLevel debug_level = PSEUDO_TCP_DEBUG_VERBOSE;

#define DEBUG(level, fmt, ...)                                          \
  if (debug_level >= level)                                             \
    g_log (level == PSEUDO_TCP_DEBUG_NORMAL ? "libnice-pseudotcp" : "libnice-pseudotcp-verbose", G_LOG_LEVEL_DEBUG, "pst_socket_t %p %s: " fmt, \
        self, pseudo_tcp_state_get_name (self->priv->state), ## __VA_ARGS__)

void pseudo_tcp_set_debug_level(PseudoTcpDebugLevel level)
{
    debug_level = level;
}

static uint32_t pseudo_tcp_get_current_time(pst_socket_t * socket)
{
    if (socket->priv->current_time != 0)
        return socket->priv->current_time;

    return (uint32_t)(g_get_monotonic_time() / 1000);
}

void pst_set_time(pst_socket_t * self, uint32_t current_time)
{
    self->priv->current_time = current_time;
}

static void pst_class_init(pst_socket_tClass * cls)
{
    GObjectClass * object_class = G_OBJECT_CLASS(cls);

    object_class->get_property = pst_get_property;
    object_class->set_property = pst_set_property;
    object_class->finalize = pst_finalize;

    g_object_class_install_property(object_class, PROP_CONVERSATION,
                                    g_param_spec_uint("conversation", "TCP Conversation ID",
                                            "The TCP Conversation ID",
                                            0, G_MAXUINT32, 0,
                                            G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class, PROP_CALLBACKS,
                                    g_param_spec_pointer("callbacks", "PseudoTcp socket callbacks",
                                            "Structure with the callbacks to call when PseudoTcp events happen",
                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class, PROP_STATE,
                                    g_param_spec_uint("state", "PseudoTcp State",
                                            "The current state (enum PseudoTcpState) of the PseudoTcp socket",
                                            TCP_LISTEN, TCP_CLOSED, TCP_LISTEN,
                                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class, PROP_ACK_DELAY,
                                    g_param_spec_uint("ack-delay", "ACK Delay",
                                            "Delayed ACK timeout (in milliseconds)",
                                            0, G_MAXUINT, DEFAULT_ACK_DELAY,
                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class, PROP_NO_DELAY,
                                    g_param_spec_boolean("no-delay", "No Delay",
                                            "Disable the Nagle algorithm (like the TCP_NODELAY option)",
                                            DEFAULT_NO_DELAY,
                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class, PROP_RCV_BUF,
                                    g_param_spec_uint("rcv-buf", "Receive Buffer",
                                            "Receive Buffer size",
                                            1, G_MAXUINT, DEFAULT_RCV_BUF_SIZE,
                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class, PROP_SND_BUF,
                                    g_param_spec_uint("snd-buf", "Send Buffer",
                                            "Send Buffer size",
                                            1, G_MAXUINT, DEFAULT_SND_BUF_SIZE,
                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
     * pst_socket_t:support-fin-ack:
     *
     * Whether to support the FIN?ACK extension to the pseudo-TCP protocol for
     * this socket. The extension is only compatible with other libnice pseudo-TCP
     * stacks, and not with Jingle pseudo-TCP stacks. If enabled, support is
     * negotiatied on connection setup, so it is safe for a #pst_socket_t with
     * support enabled to be used with one with it disabled, or with a Jingle
     * pseudo-TCP socket which doesn?t support it at all.
     *
     * Support is enabled by default.
     *
     * Since: 0.1.8
     */
    g_object_class_install_property(object_class, PROP_SUPPORT_FIN_ACK,
                                    g_param_spec_boolean("support-fin-ack", "Support FIN?ACK",
                                            "Whether to enable the optional FIN?ACK support.",
                                            TRUE,
                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}


static void pst_get_property(GObject * object,  uint32_t property_id, GValue * value, GParamSpec * pspec)
{
    pst_socket_t * self = PSEUDO_TCP_SOCKET(object);

    switch (property_id)
    {
        case PROP_CONVERSATION:
            g_value_set_uint(value, self->priv->conv);
            break;
        case PROP_CALLBACKS:
            g_value_set_pointer(value, (void *) &self->priv->callbacks);
            break;
        case PROP_STATE:
            g_value_set_uint(value, self->priv->state);
            break;
        case PROP_ACK_DELAY:
            g_value_set_uint(value, self->priv->ack_delay);
            break;
        case PROP_NO_DELAY:
            g_value_set_boolean(value, !self->priv->use_nagling);
            break;
        case PROP_RCV_BUF:
            g_value_set_uint(value, self->priv->rbuf_len);
            break;
        case PROP_SND_BUF:
            g_value_set_uint(value, self->priv->sbuf_len);
            break;
        case PROP_SUPPORT_FIN_ACK:
            g_value_set_boolean(value, self->priv->support_fin_ack);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void pst_set_property(GObject * object,  uint32_t property_id,  const GValue * value, GParamSpec * pspec)
{
    pst_socket_t * self = PSEUDO_TCP_SOCKET(object);

    switch (property_id)
    {
        case PROP_CONVERSATION:
            self->priv->conv = g_value_get_uint(value);
            break;
        case PROP_CALLBACKS:
        {
            pst_callback_t * c = g_value_get_pointer(value);
            self->priv->callbacks = *c;
        }
        break;
        case PROP_ACK_DELAY:
            self->priv->ack_delay = g_value_get_uint(value);
            break;
        case PROP_NO_DELAY:
            self->priv->use_nagling = !g_value_get_boolean(value);
            break;
        case PROP_RCV_BUF:
            g_return_if_fail(self->priv->state == TCP_LISTEN);
            resize_receive_buffer(self, g_value_get_uint(value));
            break;
        case PROP_SND_BUF:
            g_return_if_fail(self->priv->state == TCP_LISTEN);
            resize_send_buffer(self, g_value_get_uint(value));
            break;
        case PROP_SUPPORT_FIN_ACK:
            self->priv->support_fin_ack = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void pst_finalize(GObject * object)
{
    pst_socket_t * self = PSEUDO_TCP_SOCKET(object);
    PseudoTcpSocketPrivate * priv = self->priv;
    n_dlist_t * i;
    SSegment * sseg;

    if (priv == NULL)
        return;

    while ((sseg = n_queue_pop_head(&priv->slist)))
        n_slice_free(SSegment, sseg);
    n_queue_clear(&priv->unsent_slist);
    for (i = priv->rlist; i; i = i->next)
    {
        RSegment * rseg = i->data;
        n_slice_free(RSegment, rseg);
    }
    n_dlist_free(priv->rlist);
    priv->rlist = NULL;

    pst_fifo_clear(&priv->rbuf);
    pst_fifo_clear(&priv->sbuf);

    free(priv);
    self->priv = NULL;

    if (G_OBJECT_CLASS(pst_parent_class)->finalize)
        G_OBJECT_CLASS(pst_parent_class)->finalize(object);
}


static void pst_init(pst_socket_t * obj)
{
    /* Use g_new0, and do not use g_object_set_private because the size of
     * our private data is too big (150KB+) and the g_slice_allow cannot allocate
     * it. So we handle the private ourselves */
    PseudoTcpSocketPrivate * priv = g_new0(PseudoTcpSocketPrivate, 1);

    obj->priv = priv;

    priv->shutdown = SD_NONE;
    priv->error = 0;

    priv->rbuf_len = DEFAULT_RCV_BUF_SIZE;
    pst_fifo_init(&priv->rbuf, priv->rbuf_len);
    priv->sbuf_len = DEFAULT_SND_BUF_SIZE;
    pst_fifo_init(&priv->sbuf, priv->sbuf_len);

    priv->state = TCP_LISTEN;
    priv->conv = 0;
    n_queue_init(&priv->slist);
    n_queue_init(&priv->unsent_slist);
    priv->rcv_wnd = priv->rbuf_len;
    priv->rwnd_scale = priv->swnd_scale = 0;
    priv->snd_nxt = 0;
    priv->snd_wnd = 1;
    priv->snd_una = priv->rcv_nxt = 0;
    priv->bReadEnable = TRUE;
    priv->bWriteEnable = FALSE;
    priv->t_ack = 0;

    priv->msslevel = 0;
    priv->largest = 0;
    priv->mss = MIN_PACKET - PACKET_OVERHEAD;
    priv->mtu_advise = MAX_PACKET;

    priv->rto_base = 0;

    priv->cwnd = 2 * priv->mss;
    priv->ssthresh = priv->rbuf_len;
    priv->lastrecv = priv->lastsend = priv->last_traffic = 0;
    priv->bOutgoing = FALSE;

    priv->dup_acks = 0;
    priv->recover = 0;

    priv->ts_recent = priv->ts_lastack = 0;

    priv->rx_rto = DEF_RTO;
    priv->rx_srtt = priv->rx_rttvar = 0;

    priv->ack_delay = DEFAULT_ACK_DELAY;
    priv->use_nagling = !DEFAULT_NO_DELAY;

    priv->support_wnd_scale = TRUE;
    priv->support_fin_ack = TRUE;
}

pst_socket_t * pst_new(uint32_t conversation, pst_callback_t * callbacks)
{

    return g_object_new(PSEUDO_TCP_SOCKET_TYPE,  "conversation", conversation, "callbacks", callbacks, NULL);
}

static void queue_connect_message(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint8_t buf[8];
    uint32_t size = 0;

    buf[size++] = CTL_CONNECT;

    if (priv->support_wnd_scale)
    {
        buf[size++] = TCP_OPT_WND_SCALE;
        buf[size++] = 1;
        buf[size++] = priv->rwnd_scale;
    }

    if (priv->support_fin_ack)
    {
        buf[size++] = TCP_OPT_FIN_ACK;
        buf[size++] = 1;  /* option length; zero is invalid (RFC 1122, ?4.2.2.5) */
        buf[size++] = 0;  /* currently unused */
    }

    priv->snd_wnd = size;

    queue(self, (char *) buf, size, FLAG_CTL);
}

static void queue_fin_message(pst_socket_t * self)
{
    g_assert(self->priv->support_fin_ack);

    /* FIN segments are always zero-length. */
    queue(self, "", 0, FLAG_FIN);
}

static void queue_rst_message(pst_socket_t * self)
{
    g_assert(self->priv->support_fin_ack);

    /* RST segments are always zero-length. */
    queue(self, "", 0, FLAG_RST);
}

int pst_connect(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    if (priv->state != TCP_LISTEN)
    {
        priv->error = EINVAL;
        return FALSE;
    }

    set_state(self, TCP_SYN_SENT);

    queue_connect_message(self);
    attempt_send(self, sfNone);

    return TRUE;
}

void pst_notify_mtu(pst_socket_t * self, guint16 mtu)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    priv->mtu_advise = mtu;
    if (priv->state == TCP_ESTABLISHED)
    {
        adjustMTU(self);
    }
}

void pst_notify_clock(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint32_t now = pseudo_tcp_get_current_time(self);

    if (priv->state == TCP_CLOSED)
        return;

    /* If in the TIME-WAIT state, any delayed segments have passed and the
     * connection can be considered closed from both ends.
     * FIXME: This should probably actually compare a timestamp before
     * operating. */
    if (priv->support_fin_ack && priv->state == TCP_TIME_WAIT)
    {
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Notified clock in TIME-WAIT state; closing connection.");
        set_state_closed(self, 0);
    }

    /* If in the LAST-ACK state, resend the FIN because it hasn?t been ACKed yet.
     * FIXME: This should probably actually compare a timestamp before
     * operating. */
    if (priv->support_fin_ack && priv->state == TCP_LAST_ACK)
    {
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL,
              "Notified clock in LAST-ACK state; resending FIN segment.");
        queue_fin_message(self);
        attempt_send(self, sfFin);
    }

    // Check if it's time to retransmit a segment
    if (priv->rto_base &&
            (time_diff(priv->rto_base + priv->rx_rto, now) <= 0))
    {
        if (n_queue_get_length(&priv->slist) == 0)
        {
            g_assert_not_reached();
        }
        else
        {
            // Note: (priv->slist.front().xmit == 0)) {
            // retransmit segments
            uint32_t nInFlight;
            uint32_t rto_limit;

            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "timeout retransmit (rto: %u) "
                  "(rto_base: %u) (now: %u) (dup_acks: %u)",
                  priv->rx_rto, priv->rto_base, now, (uint32_t) priv->dup_acks);

            if (!transmit(self, n_queue_peek_head(&priv->slist), now))
            {
                closedown(self, ECONNABORTED, CLOSEDOWN_LOCAL);
                return;
            }

            nInFlight = priv->snd_nxt - priv->snd_una;
            priv->ssthresh = max(nInFlight / 2, 2 * priv->mss);
            //LOG(LS_INFO) << "priv->ssthresh: " << priv->ssthresh << "  nInFlight: " << nInFlight << "  priv->mss: " << priv->mss;
            priv->cwnd = priv->mss;

            // Back off retransmit timer.  Note: the limit is lower when connecting.
            rto_limit = (priv->state < TCP_ESTABLISHED) ? DEF_RTO : MAX_RTO;
            priv->rx_rto = min(rto_limit, priv->rx_rto * 2);
            priv->rto_base = now;
        }
    }

    // Check if it's time to probe closed windows
    if ((priv->snd_wnd == 0)
            && (time_diff(priv->lastsend + priv->rx_rto, now) <= 0))
    {
        if (time_diff(now, priv->lastrecv) >= 15000)
        {
            closedown(self, ECONNABORTED, CLOSEDOWN_LOCAL);
            return;
        }

        // probe the window
        packet(self, priv->snd_nxt - 1, 0, 0, 0, now);
        priv->lastsend = now;

        // back off retransmit timer
        priv->rx_rto = min(MAX_RTO, priv->rx_rto * 2);
    }

    // Check if it's time to send delayed acks
    if (priv->t_ack && (time_diff(priv->t_ack + priv->ack_delay, now) <= 0))
    {
        packet(self, priv->snd_nxt, 0, 0, 0, now);
    }

}

int pst_notify_packet(pst_socket_t * self, const char * buffer, uint32_t len)
{
    int retval;

    if (len > MAX_PACKET)
    {
        //LOG_F(WARNING) << "packet too large";
        return FALSE;
    }
    else if (len < HEADER_SIZE)
    {
        //LOG_F(WARNING) << "packet too small";
        return FALSE;
    }

    /* Hold a reference to the pst_socket_t during parsing, since it may be
     * closed from within a callback. */
    g_object_ref(self);
    retval = parse(self, (uint8_t *) buffer, HEADER_SIZE,
                   (uint8_t *) buffer + HEADER_SIZE, len - HEADER_SIZE);
    g_object_unref(self);

    return retval;
}

/* Assume there are two buffers in the given #n_input_msg_t: a 24-byte one
 * containing the header, and a bigger one for the data. */
int pst_notify_message(pst_socket_t * self, n_input_msg_t * message)
{
    int retval;

    //g_assert_cmpuint(message->n_buffers, > , 0);

    if (message->n_buffers == 1)
        return pst_notify_packet(self, message->buffers[0].buffer,
                                               message->buffers[0].size);

    //g_assert_cmpuint(message->n_buffers, == , 2);
    //g_assert_cmpuint(message->buffers[0].size, == , HEADER_SIZE);

    if (message->length > MAX_PACKET)
    {
        //LOG_F(WARNING) << "packet too large";
        return FALSE;
    }
    else if (message->length < HEADER_SIZE)
    {
        //LOG_F(WARNING) << "packet too small";
        return FALSE;
    }

    /* Hold a reference to the pst_socket_t during parsing, since it may be
     * closed from within a callback. */
    g_object_ref(self);
    retval = parse(self, message->buffers[0].buffer, message->buffers[0].size,
                   message->buffers[1].buffer, message->length - message->buffers[0].size);
    g_object_unref(self);

    return retval;
}

int pst_get_next_clock(pst_socket_t * self, guint64 * timeout)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint32_t now = pseudo_tcp_get_current_time(self);
    uint32_t snd_buffered;
    uint32_t closed_timeout;

    if (priv->shutdown == SD_FORCEFUL)
    {
        if (priv->support_fin_ack)
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL,
                  "?Forceful? shutdown used when FIN-ACK support is enabled");
        }

        /* Transition to the CLOSED state. */
        closedown(self, 0, CLOSEDOWN_REMOTE);

        return FALSE;
    }

    snd_buffered = pst_fifo_get_buffered(&priv->sbuf);
    if ((priv->shutdown == SD_GRACEFUL)
            && ((priv->state != TCP_ESTABLISHED)
                || ((snd_buffered == 0) && (priv->t_ack == 0))))
    {
        if (priv->support_fin_ack)
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL,
                  "?Graceful? shutdown used when FIN-ACK support is enabled");
        }

        /* Transition to the CLOSED state. */
        closedown(self, 0, CLOSEDOWN_REMOTE);

        return FALSE;
    }

    /* FIN-ACK support. The timeout for closing the socket if nothing is received
     * varies depending on whether the socket is waiting in the TIME-WAIT state
     * for delayed segments to pass.
     *
     * See: http://vincent.bernat.im/en/blog/2014-tcp-time-wait-state-linux.html
     */
    closed_timeout = CLOSED_TIMEOUT;
    if (priv->support_fin_ack && priv->state == TCP_TIME_WAIT)
        closed_timeout = TIME_WAIT_TIMEOUT;

    if (priv->support_fin_ack && priv->state == TCP_CLOSED)
    {
        return FALSE;
    }

    if (*timeout == 0 || *timeout < now)
        *timeout = now + closed_timeout;

    if (priv->support_fin_ack && priv->state == TCP_TIME_WAIT)
    {
        *timeout = min(*timeout, now + TIME_WAIT_TIMEOUT);
        return TRUE;
    }

    if (priv->state == TCP_CLOSED && !priv->support_fin_ack)
    {
        *timeout = min(*timeout, now + CLOSED_TIMEOUT);
        return TRUE;
    }

    *timeout = min(*timeout, now + DEFAULT_TIMEOUT);

    if (priv->t_ack)
    {
        *timeout = min(*timeout, priv->t_ack + priv->ack_delay);
    }
    if (priv->rto_base)
    {
        *timeout = min(*timeout, priv->rto_base + priv->rx_rto);
    }
    if (priv->snd_wnd == 0)
    {
        *timeout = min(*timeout, priv->lastsend + priv->rx_rto);
    }

    return TRUE;
}


int32_t pst_recv(pst_socket_t * self, char * buffer, size_t len)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint32_t bytesread;
    uint32_t available_space;

    /* Received a FIN from the peer, so return 0. RFC 793, ?3.5, Case 2. */
    if (priv->support_fin_ack &&
            (priv->shutdown_reads ||
             pseudo_tcp_state_has_received_fin(priv->state)))
    {
        return 0;
    }

    /* Return 0 if FIN-ACK is not supported but the socket has been closed. */
    if (!priv->support_fin_ack && pst_is_closed(self))
    {
        return 0;
    }

    /* Return ENOTCONN if FIN-ACK is not supported and the connection is not
     * ESTABLISHED. */
    if (!priv->support_fin_ack && priv->state != TCP_ESTABLISHED)
    {
        priv->error = ENOTCONN;
        return -1;
    }

    if (len == 0)
        return 0;

    bytesread = pst_fifo_read(&priv->rbuf, (uint8_t *) buffer, len);

// If there's no data in |m_rbuf|.
    if (bytesread == 0)
    {
        priv->bReadEnable = TRUE;
        priv->error = EWOULDBLOCK;
        return -1;
    }

    available_space = pst_fifo_get_write_remaining(&priv->rbuf);

    if (available_space - priv->rcv_wnd >=
            min(priv->rbuf_len / 2, priv->mss))
    {
        // !?! Not sure about this was closed business
        int bWasClosed = (priv->rcv_wnd == 0);

        priv->rcv_wnd = available_space;

        if (bWasClosed)
        {
            attempt_send(self, sfImmediateAck);
        }
    }

    return bytesread;
}

int32_t pst_send(pst_socket_t * self, const char * buffer, uint32_t len)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    int32_t written;
    uint32_t available_space;

    if (priv->state != TCP_ESTABLISHED)
    {
        priv->error = pseudo_tcp_state_has_sent_fin(priv->state) ? EPIPE : ENOTCONN;
        return -1;
    }

    available_space = pst_fifo_get_write_remaining(&priv->sbuf);

    if (!available_space)
    {
        priv->bWriteEnable = TRUE;
        priv->error = EWOULDBLOCK;
        return -1;
    }

    written = queue(self, buffer, len, FLAG_NONE);
    attempt_send(self, sfNone);

    if (written > 0 && (uint32_t)written < len)
    {
        priv->bWriteEnable = TRUE;
    }

    return written;
}

void pst_close(pst_socket_t * self, int force)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Closing socket %p %s", self,
          force ? "forcefully" : "gracefully");

    /* Forced closure by sending an RST segment. RFC 1122, ?4.2.2.13. */
    if (force && priv->state != TCP_CLOSED)
    {
        closedown(self, ECONNABORTED, CLOSEDOWN_LOCAL);
        return;
    }

    /* Fall back to shutdown(). */
    pst_shutdown(self, PSEUDO_TCP_SHUTDOWN_RDWR);
}

void pst_shutdown(pst_socket_t * self, PseudoTcpShutdown how)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Shutting down socket %p: %u", self, how);

    /* FIN-ACK--only stuff below here. */
    if (!priv->support_fin_ack)
    {
        if (priv->shutdown == SD_NONE)
            priv->shutdown = SD_GRACEFUL;
        return;
    }

    /* What needs shutting down? */
    switch (how)
    {
        case PSEUDO_TCP_SHUTDOWN_RD:
        case PSEUDO_TCP_SHUTDOWN_RDWR:
            priv->shutdown_reads = TRUE;
            break;
        case PSEUDO_TCP_SHUTDOWN_WR:
            /* Handled below. */
            break;
        default:
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Invalid shutdown method: %u.", how);
            break;
    }

    if (how == PSEUDO_TCP_SHUTDOWN_RD)
    {
        return;
    }

    /* Unforced write closure. */
    switch (priv->state)
    {
        case TCP_LISTEN:
        case TCP_SYN_SENT:
            /* Just abort the connection without completing the handshake. */
            set_state_closed(self, 0);
            break;
        case TCP_SYN_RECEIVED:
        case TCP_ESTABLISHED:
            /* Local user initiating the close: RFC 793, ?3.5, Cases 1 and 3.
             * If there is pending receive data, send RST instead of FIN;
             * see RFC 1122, ?4.2.2.13. */
            if (pst_get_available_bytes(self) > 0)
            {
                closedown(self, ECONNABORTED, CLOSEDOWN_LOCAL);
            }
            else
            {
                queue_fin_message(self);
                attempt_send(self, sfFin);
                set_state(self, TCP_FIN_WAIT_1);
            }
            break;
        case TCP_CLOSE_WAIT:
            /* Remote user initiating the close: RFC 793, ?3.5, Case 2.
             * We?ve previously received a FIN from the peer; now the user is closing
             * the local end of the connection. */
            queue_fin_message(self);
            attempt_send(self, sfFin);
            set_state(self, TCP_LAST_ACK);
            break;
        case TCP_CLOSING:
        case TCP_CLOSED:
            /* Already closed on both sides. */
            break;
        case TCP_FIN_WAIT_1:
        case TCP_FIN_WAIT_2:
        case TCP_TIME_WAIT:
        case TCP_LAST_ACK:
            /* Already closed locally. */
            break;
        default:
            /* Do nothing. */
            break;
    }
}

int pst_get_error(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    return priv->error;
}

//
// Internal Implementation
//

static uint32_t queue(pst_socket_t * self, const char * data, uint32_t len, TcpFlags flags)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint32_t available_space;

    available_space = pst_fifo_get_write_remaining(&priv->sbuf);
    if (len > available_space)
    {
        g_assert(flags == FLAG_NONE);
        len = available_space;
    }

    // We can concatenate data if the last segment is the same type
    // (control v. regular data), and has not been transmitted yet
    if (n_queue_get_length(&priv->slist) &&
            (((SSegment *)n_queue_peek_tail(&priv->slist))->flags == flags) &&
            (((SSegment *)n_queue_peek_tail(&priv->slist))->xmit == 0))
    {
        ((SSegment *)n_queue_peek_tail(&priv->slist))->len += len;
    }
    else
    {
        SSegment * sseg = g_slice_new0(SSegment);
        uint32_t snd_buffered = pst_fifo_get_buffered(&priv->sbuf);

        sseg->seq = priv->snd_una + snd_buffered;
        sseg->len = len;
        sseg->flags = flags;
        n_queue_push_tail(&priv->slist, sseg);
        n_queue_push_tail(&priv->unsent_slist, sseg);
    }

    //LOG(LS_INFO) << "PseudoTcp::queue - priv->slen = " << priv->slen;
    return pst_fifo_write(&priv->sbuf, (uint8_t *) data, len);;
}

// Creates a packet and submits it to the network. This method can either
// send payload or just an ACK packet.
//
// |seq| is the sequence number of this packet.
// |flags| is the flags for sending this packet.
// |offset| is the offset to read from |m_sbuf|.
// |len| is the number of bytes to read from |m_sbuf| as payload. If this
// value is 0 then this is an ACK packet, otherwise this packet has payload.

static pst_wret_e packet(pst_socket_t * self, uint32_t seq, TcpFlags flags,
       uint32_t offset, uint32_t len, uint32_t now)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    union
    {
        uint8_t u8[MAX_PACKET];
        guint16 u16[MAX_PACKET / 2];
        uint32_t u32[MAX_PACKET / 4];
    } buffer;
    pst_wret_e wres = WR_SUCCESS;

    g_assert(HEADER_SIZE + len <= MAX_PACKET);

    *buffer.u32 = htonl(priv->conv);
    *(buffer.u32 + 1) = htonl(seq);
    *(buffer.u32 + 2) = htonl(priv->rcv_nxt);
    buffer.u8[12] = 0;
    buffer.u8[13] = flags;
    *(buffer.u16 + 7) = htons((guint16)(priv->rcv_wnd >> priv->rwnd_scale));

    // Timestamp computations
    *(buffer.u32 + 4) = htonl(now);
    *(buffer.u32 + 5) = htonl(priv->ts_recent);
    priv->ts_lastack = priv->rcv_nxt;

    if (len)
    {
        uint32_t bytes_read;

        bytes_read = pst_fifo_read_offset(&priv->sbuf, buffer.u8 + HEADER_SIZE,
                     len, offset);
        g_assert(bytes_read == len);
    }

    DEBUG(PSEUDO_TCP_DEBUG_VERBOSE, "<-- <CONV=%u><FLG=%u><SEQ=%u:%u><ACK=%u>"
          "<WND=%u><TS=%u><TSR=%u><LEN=%u>",
          priv->conv, (unsigned)flags, seq, seq + len, priv->rcv_nxt, priv->rcv_wnd,
          now % 10000, priv->ts_recent % 10000, len);

    wres = priv->callbacks.WritePacket(self, (char *) buffer.u8, len + HEADER_SIZE, priv->callbacks.user_data);
    /* Note: When len is 0, this is an ACK packet.  We don't read the
       return value for those, and thus we won't retry.  So go ahead and treat
       the packet as a success (basically simulate as if it were dropped),
       which will prevent our timers from being messed up. */
    if ((wres != WR_SUCCESS) && (0 != len))
        return wres;

    priv->t_ack = 0;
    if (len > 0)
    {
        priv->lastsend = now;
    }
    priv->last_traffic = now;
    priv->bOutgoing = TRUE;

    return WR_SUCCESS;
}

static int parse(pst_socket_t * self, const uint8_t * _header_buf, uint32_t header_buf_len,
      const uint8_t * data_buf, uint32_t data_buf_len)
{
    Segment seg;

    union
    {
        const uint8_t * u8;
        const guint16 * u16;
        const uint32_t * u32;
    } header_buf;

    header_buf.u8 = _header_buf;

    if (header_buf_len != 24)
        return FALSE;

    seg.conv = ntohl(*header_buf.u32);
    seg.seq = ntohl(*(header_buf.u32 + 1));
    seg.ack = ntohl(*(header_buf.u32 + 2));
    seg.flags = header_buf.u8[13];
    seg.wnd = ntohs(*(header_buf.u16 + 7));

    seg.tsval = ntohl(*(header_buf.u32 + 4));
    seg.tsecr = ntohl(*(header_buf.u32 + 5));

    seg.data = (const char *) data_buf;
    seg.len = data_buf_len;

    DEBUG(PSEUDO_TCP_DEBUG_VERBOSE, "--> <CONV=%u><FLG=%u><SEQ=%u:%u><ACK=%u>"
          "<WND=%u><TS=%u><TSR=%u><LEN=%u>",
          seg.conv, (unsigned)seg.flags, seg.seq, seg.seq + seg.len, seg.ack,
          seg.wnd, seg.tsval % 10000, seg.tsecr % 10000, seg.len);

    return process(self, &seg);
}

/* True iff the @state requires that a FIN has already been sent by this
 * host. */
static int pseudo_tcp_state_has_sent_fin(PseudoTcpState state)
{
    switch (state)
    {
        case TCP_LISTEN:
        case TCP_SYN_SENT:
        case TCP_SYN_RECEIVED:
        case TCP_ESTABLISHED:
        case TCP_CLOSE_WAIT:
            return FALSE;
        case TCP_CLOSED:
        case TCP_FIN_WAIT_1:
        case TCP_FIN_WAIT_2:
        case TCP_CLOSING:
        case TCP_TIME_WAIT:
        case TCP_LAST_ACK:
            return TRUE;
        default:
            return FALSE;
    }
}

/* True iff the @state requires that a FIN has already been received from the
 * peer. */
static int pseudo_tcp_state_has_received_fin(PseudoTcpState state)
{
    switch (state)
    {
        case TCP_LISTEN:
        case TCP_SYN_SENT:
        case TCP_SYN_RECEIVED:
        case TCP_ESTABLISHED:
        case TCP_FIN_WAIT_1:
        case TCP_FIN_WAIT_2:
            return FALSE;
        case TCP_CLOSED:
        case TCP_CLOSING:
        case TCP_TIME_WAIT:
        case TCP_CLOSE_WAIT:
        case TCP_LAST_ACK:
            return TRUE;
        default:
            return FALSE;
    }
}

static int process(pst_socket_t * self, Segment * seg)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint32_t now;
    SendFlags sflags = sfNone;
    int bIgnoreData;
    int bNewData;
    int bConnect = FALSE;
    uint32_t snd_buffered;
    uint32_t available_space;
    uint32_t kIdealRefillSize;
    int is_valuable_ack, is_duplicate_ack, is_fin_ack = FALSE;

    /* If this is the wrong conversation, send a reset!?!
       (with the correct conversation?) */
    if (seg->conv != priv->conv)
    {
        //if ((seg->flags & FLAG_RST) == 0) {
        //  packet(sock, tcb, seg->ack, 0, FLAG_RST, 0, 0);
        //}
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "wrong conversation");
        return FALSE;
    }

    now = pseudo_tcp_get_current_time(self);
    priv->last_traffic = priv->lastrecv = now;
    priv->bOutgoing = FALSE;

    if (priv->state == TCP_CLOSED ||  (pseudo_tcp_state_has_sent_fin(priv->state) && seg->len > 0))
    {
        /* Send an RST segment. See: RFC 1122, ?4.2.2.13. */
        if ((seg->flags & FLAG_RST) == 0)
        {
            closedown(self, 0, CLOSEDOWN_LOCAL);
        }
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Segment received while closed; sent RST.");
        return FALSE;
    }

    // Check if this is a reset segment
    if (seg->flags & FLAG_RST)
    {
        closedown(self, ECONNRESET, CLOSEDOWN_REMOTE);
        return FALSE;
    }

    // Check for control data
    bConnect = FALSE;
    if (seg->flags & FLAG_CTL)
    {
        if (seg->len == 0)
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Missing control code");
            return FALSE;
        }
        else if (seg->data[0] == CTL_CONNECT)
        {
            bConnect = TRUE;

            parse_options(self, (uint8_t *) &seg->data[1], seg->len - 1);

            if (priv->state == TCP_LISTEN)
            {
                set_state(self, TCP_SYN_RECEIVED);
                queue_connect_message(self);
            }
            else if (priv->state == TCP_SYN_SENT)
            {
                set_state_established(self);
            }
        }
        else
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Unknown control code: %u", seg->data[0]);
            return FALSE;
        }
    }

    // Update timestamp
    if (SMALLER_OR_EQUAL(seg->seq, priv->ts_lastack) &&
            SMALLER(priv->ts_lastack, seg->seq + seg->len))
    {
        priv->ts_recent = seg->tsval;
    }

    // Check if this is a valuable ack
    is_valuable_ack = (LARGER(seg->ack, priv->snd_una) &&
                       SMALLER_OR_EQUAL(seg->ack, priv->snd_nxt));
    is_duplicate_ack = (seg->ack == priv->snd_una);

    if (is_valuable_ack)
    {
        uint32_t nAcked;
        uint32_t nFree;

        // Calculate round-trip time
        if (seg->tsecr)
        {
            long rtt = time_diff(now, seg->tsecr);
            if (rtt >= 0)
            {
                if (priv->rx_srtt == 0)
                {
                    priv->rx_srtt = rtt;
                    priv->rx_rttvar = rtt / 2;
                }
                else
                {
                    priv->rx_rttvar = (3 * priv->rx_rttvar +
                                       abs((long)(rtt - priv->rx_srtt))) / 4;
                    priv->rx_srtt = (7 * priv->rx_srtt + rtt) / 8;
                }
                priv->rx_rto = bound(MIN_RTO,
                                     priv->rx_srtt + max(1LU, 4 * priv->rx_rttvar), MAX_RTO);

                DEBUG(PSEUDO_TCP_DEBUG_VERBOSE, "rtt: %ld   srtt: %u  rto: %u",
                      rtt, priv->rx_srtt, priv->rx_rto);
            }
            else
            {
                DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Invalid RTT: %ld", rtt);
                return FALSE;
            }
        }

        priv->snd_wnd = seg->wnd << priv->swnd_scale;

        nAcked = seg->ack - priv->snd_una;
        priv->snd_una = seg->ack;

        priv->rto_base = (priv->snd_una == priv->snd_nxt) ? 0 : now;

        /* ACKs for FIN segments give an increment on nAcked, but there is no
         * corresponding byte to read because the FIN segment is empty (it just has
         * a sequence number). */
        if (nAcked == priv->sbuf.data_length + 1 &&
                pseudo_tcp_state_has_sent_fin(priv->state))
        {
            is_fin_ack = TRUE;
            nAcked--;
        }

        pst_fifo_consume_read_data(&priv->sbuf, nAcked);

        for (nFree = nAcked; nFree > 0;)
        {
            SSegment * data;

            g_assert(n_queue_get_length(&priv->slist) != 0);
            data = (SSegment *) n_queue_peek_head(&priv->slist);

            if (nFree < data->len)
            {
                data->len -= nFree;
                data->seq += nFree;
                nFree = 0;
            }
            else
            {
                if (data->len > priv->largest)
                {
                    priv->largest = data->len;
                }
                nFree -= data->len;
                g_slice_free(SSegment, data);
                n_queue_pop_head(&priv->slist);
            }
        }

        if (priv->dup_acks >= 3)
        {
            if (LARGER_OR_EQUAL(priv->snd_una, priv->recover))    // NewReno
            {
                uint32_t nInFlight = priv->snd_nxt - priv->snd_una;
                // (Fast Retransmit)
                priv->cwnd = min(priv->ssthresh, nInFlight + priv->mss);
                DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "exit recovery");
                priv->dup_acks = 0;
            }
            else
            {
                DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "recovery retransmit");
                if (!transmit(self, n_queue_peek_head(&priv->slist), now))
                {
                    closedown(self, ECONNABORTED, CLOSEDOWN_LOCAL);
                    return FALSE;
                }
                priv->cwnd += priv->mss - min(nAcked, priv->cwnd);
            }
        }
        else
        {
            priv->dup_acks = 0;
            // Slow start, congestion avoidance
            if (priv->cwnd < priv->ssthresh)
            {
                priv->cwnd += priv->mss;
            }
            else
            {
                priv->cwnd += max(1LU, priv->mss * priv->mss / priv->cwnd);
            }
        }
    }
    else if (is_duplicate_ack)
    {
        /* !?! Note, tcp says don't do this... but otherwise how does a
           closed window become open? */
        priv->snd_wnd = seg->wnd << priv->swnd_scale;

        // Check duplicate acks
        if (seg->len > 0)
        {
            // it's a dup ack, but with a data payload, so don't modify priv->dup_acks
        }
        else if (priv->snd_una != priv->snd_nxt)
        {
            uint32_t nInFlight;

            priv->dup_acks += 1;
            if (priv->dup_acks == 3)   // (Fast Retransmit)
            {
                DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "enter recovery");
                DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "recovery retransmit");
                if (!transmit(self, n_queue_peek_head(&priv->slist), now))
                {
                    closedown(self, ECONNABORTED, CLOSEDOWN_LOCAL);
                    return FALSE;
                }
                priv->recover = priv->snd_nxt;
                nInFlight = priv->snd_nxt - priv->snd_una;
                priv->ssthresh = max(nInFlight / 2, 2 * priv->mss);
                //LOG(LS_INFO) << "priv->ssthresh: " << priv->ssthresh << "  nInFlight: " << nInFlight << "  priv->mss: " << priv->mss;
                priv->cwnd = priv->ssthresh + 3 * priv->mss;
            }
            else if (priv->dup_acks > 3)
            {
                priv->cwnd += priv->mss;
            }
        }
        else
        {
            priv->dup_acks = 0;
        }
    }

    // !?! A bit hacky
    if ((priv->state == TCP_SYN_RECEIVED) && !bConnect)
    {
        set_state_established(self);
    }

    /* Check for connection closure. */
    if (priv->support_fin_ack)
    {
        /* For the moment, FIN segments must not contain data. */
        if (seg->flags & FLAG_FIN && seg->len != 0)
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "FIN segment contained data; ignored");
            return FALSE;
        }

        /* Update the state machine, implementing all transitions on ?rcv FIN? or
         * ?rcv ACK of FIN? from RFC 793, Figure 6; and RFC 1122, ?4.2.2.8. */
        switch (priv->state)
        {
            case TCP_ESTABLISHED:
                if (seg->flags & FLAG_FIN)
                {
                    /* Received a FIN from the network, RFC 793, ?3.5, Case 2.
                     * The code below will send an ACK for the FIN. */
                    set_state(self, TCP_CLOSE_WAIT);
                }
                break;
            case TCP_CLOSING:
                if (is_fin_ack)
                {
                    /* Handle the ACK of a locally-sent FIN flag. RFC 793, ?3.5, Case 3. */
                    set_state(self, TCP_TIME_WAIT);
                }
                break;
            case TCP_LAST_ACK:
                if (is_fin_ack)
                {
                    /* Handle the ACK of a locally-sent FIN flag. RFC 793, ?3.5, Case 2. */
                    set_state_closed(self, 0);
                }
                break;
            case TCP_FIN_WAIT_1:
                if (is_fin_ack && seg->flags & FLAG_FIN)
                {
                    /* Simultaneous close with an ACK for a FIN previously sent,
                     * RFC 793, ?3.5, Case 3. */
                    set_state(self, TCP_TIME_WAIT);
                }
                else if (is_fin_ack)
                {
                    /* Handle the ACK of a locally-sent FIN flag. RFC 793, ?3.5, Case 1. */
                    set_state(self, TCP_FIN_WAIT_2);
                }
                else if (seg->flags & FLAG_FIN)
                {
                    /* Simultaneous close, RFC 793, ?3.5, Case 3. */
                    set_state(self, TCP_CLOSING);
                }
                break;
            case TCP_FIN_WAIT_2:
                if (seg->flags & FLAG_FIN)
                {
                    /* Local user closed the connection, RFC 793, ?3.5, Case 1. */
                    set_state(self, TCP_TIME_WAIT);
                }
                break;
            case TCP_LISTEN:
            case TCP_SYN_SENT:
            case TCP_SYN_RECEIVED:
            case TCP_TIME_WAIT:
            case TCP_CLOSED:
            case TCP_CLOSE_WAIT:
                /* Shouldn?t ever hit these cases. */
                if (seg->flags & FLAG_FIN)
                {
                    DEBUG(PSEUDO_TCP_DEBUG_NORMAL,
                          "Unexpected state %u when FIN received", priv->state);
                }
                else if (is_fin_ack)
                {
                    DEBUG(PSEUDO_TCP_DEBUG_NORMAL,
                          "Unexpected state %u when FIN-ACK received", priv->state);
                }
                break;
            default:
                DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Invalid state %u when FIN received",
                      priv->state);
                return FALSE;
        }
    }
    else if (seg->flags & FLAG_FIN)
    {
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL,
              "Invalid FIN received when FIN-ACK support is disabled");
    }
    else if (is_fin_ack)
    {
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL,
              "Invalid FIN-ACK received when FIN-ACK support is disabled");
    }

    // If we make room in the send queue, notify the user
    // The goal it to make sure we always have at least enough data to fill the
    // window.  We'd like to notify the app when we are halfway to that point.
    kIdealRefillSize = (priv->sbuf_len + priv->rbuf_len) / 2;

    snd_buffered = pst_fifo_get_buffered(&priv->sbuf);
    if (priv->bWriteEnable && snd_buffered < kIdealRefillSize)
    {
        priv->bWriteEnable = FALSE;
        if (priv->callbacks.PseudoTcpWritable)
            priv->callbacks.PseudoTcpWritable(self, priv->callbacks.user_data);
    }

    /* Conditions where acks must be sent:
     * 1) Segment is too old (they missed an ACK) (immediately)
     * 2) Segment is too new (we missed a segment) (immediately)
     * 3) Segment has data (so we need to ACK!) (delayed)
     * ... so the only time we don't need to ACK, is an empty segment
     * that points to rcv_nxt!
     * 4) Segment has the FIN flag set (immediately) ? note that the FIN flag
     *    itself has to be included in the ACK as a numbered byte;
     *    see RFC 793, ?3.3. Also see: RFC 793, ?3.5.
     */
    if (seg->seq != priv->rcv_nxt)
    {
        sflags = sfImmediateAck; // (Fast Recovery)
    }
    else if (seg->len != 0)
    {
        if (priv->ack_delay == 0)
        {
            sflags = sfImmediateAck;
        }
        else
        {
            sflags = sfDelayedAck;
        }
    }
    else if (seg->flags & FLAG_FIN)
    {
        sflags = sfImmediateAck;
        priv->rcv_nxt += 1;
    }

    if (sflags == sfImmediateAck)
    {
        if (seg->seq > priv->rcv_nxt)
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "too new");
        }
        else if (SMALLER_OR_EQUAL(seg->seq + seg->len, priv->rcv_nxt))
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "too old");
        }
    }

    // Adjust the incoming segment to fit our receive buffer
    if (SMALLER(seg->seq, priv->rcv_nxt))
    {
        uint32_t nAdjust = priv->rcv_nxt - seg->seq;
        if (nAdjust < seg->len)
        {
            seg->seq += nAdjust;
            seg->data += nAdjust;
            seg->len -= nAdjust;
        }
        else
        {
            seg->len = 0;
        }
    }

    available_space = pst_fifo_get_write_remaining(&priv->rbuf);

    if ((seg->seq + seg->len - priv->rcv_nxt) > available_space)
    {
        uint32_t nAdjust = seg->seq + seg->len - priv->rcv_nxt - available_space;
        if (nAdjust < seg->len)
        {
            seg->len -= nAdjust;
        }
        else
        {
            seg->len = 0;
        }
    }

    bIgnoreData = (seg->flags & FLAG_CTL);
    if (!priv->support_fin_ack)
        bIgnoreData |= (priv->shutdown != SD_NONE);

    bNewData = FALSE;

    if (seg->flags & FLAG_FIN)
    {
        /* FIN flags have a sequence number. */
        if (seg->seq == priv->rcv_nxt)
        {
            priv->rcv_nxt++;
        }
    }
    else if (seg->len > 0)
    {
        if (bIgnoreData)
        {
            if (seg->seq == priv->rcv_nxt)
            {
                priv->rcv_nxt += seg->len;
            }
        }
        else
        {
            uint32_t nOffset = seg->seq - priv->rcv_nxt;
            uint32_t res;

            res = pst_fifo_write_offset(&priv->rbuf, (uint8_t *) seg->data,  seg->len, nOffset);
            g_assert(res == seg->len);

            if (seg->seq == priv->rcv_nxt)
            {
                n_dlist_t * iter = NULL;

                pst_fifo_consume_write_buffer(&priv->rbuf, seg->len);
                priv->rcv_nxt += seg->len;
                priv->rcv_wnd -= seg->len;
                bNewData = TRUE;

                iter = priv->rlist;
                while (iter &&  SMALLER_OR_EQUAL(((RSegment *)iter->data)->seq, priv->rcv_nxt))
                {
                    RSegment * data = (RSegment *)(iter->data);
                    if (LARGER(data->seq + data->len, priv->rcv_nxt))
                    {
                        uint32_t nAdjust = (data->seq + data->len) - priv->rcv_nxt;
                        sflags = sfImmediateAck; // (Fast Recovery)
                        DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Recovered %u bytes (%u -> %u)",
                              nAdjust, priv->rcv_nxt, priv->rcv_nxt + nAdjust);
                        pst_fifo_consume_write_buffer(&priv->rbuf, nAdjust);
                        priv->rcv_nxt += nAdjust;
                        priv->rcv_wnd -= nAdjust;
                    }
                    n_slice_free(RSegment, priv->rlist->data);
                    priv->rlist = n_dlist_delete_link(priv->rlist, priv->rlist);
                    iter = priv->rlist;
                }
            }
            else
            {
                n_dlist_t * iter = NULL;
                RSegment * rseg = g_slice_new0(RSegment);

                DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Saving %u bytes (%u -> %u)",
                      seg->len, seg->seq, seg->seq + seg->len);
                rseg->seq = seg->seq;
                rseg->len = seg->len;
                iter = priv->rlist;
                while (iter && SMALLER(((RSegment *)iter->data)->seq, rseg->seq))
                {
                    iter = n_dlist_next(iter);
                }
                priv->rlist = n_dlist_insert_before(priv->rlist, iter, rseg);
            }
        }
    }

    attempt_send(self, sflags);

    // If we have new data, notify the user
    if (bNewData && priv->bReadEnable)
    {
        /* priv->bReadEnable = FALSE; ? removed so that we?re always notified of
         * incoming pseudo-TCP data, rather than having to read the entire buffer
         * on each readable() callback before the next callback is enabled.
         * (When client-provided buffers are small, this is not possible.) */
        if (priv->callbacks.PseudoTcpReadable)
            priv->callbacks.PseudoTcpReadable(self, priv->callbacks.user_data);
    }

    return TRUE;
}

static int transmit(pst_socket_t * self, SSegment * segment, uint32_t now)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint32_t nTransmit = min(segment->len, priv->mss);

    if (segment->xmit >= ((priv->state == TCP_ESTABLISHED) ? 15 : 30))
    {
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "too many retransmits");
        return FALSE;
    }

    while (TRUE)
    {
        uint32_t seq = segment->seq;
        uint8_t flags = segment->flags;
        pst_wret_e wres;

        /* The packet must not have already been acknowledged. */
        //g_assert_cmpuint(segment->seq - priv->snd_una, <= , 1024 * 1024 * 64);

        /* Write out the packet. */
        wres = packet(self, seq, flags,  segment->seq - priv->snd_una, nTransmit, now);

        if (wres == WR_SUCCESS)
            break;

        if (wres == WR_FAIL)
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "packet failed");
            return FALSE;
        }

        g_assert(wres == WR_TOO_LARGE);

        while (TRUE)
        {
            if (PACKET_MAXIMUMS[priv->msslevel + 1] == 0)
            {
                DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "MTU too small");
                return FALSE;
            }
            /* !?! We need to break up all outstanding and pending packets
               and then retransmit!?! */

            priv->mss = PACKET_MAXIMUMS[++priv->msslevel] - PACKET_OVERHEAD;
            // I added this... haven't researched actual formula
            priv->cwnd = 2 * priv->mss;

            if (priv->mss < nTransmit)
            {
                nTransmit = priv->mss;
                break;
            }
        }
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Adjusting mss to %u bytes ", priv->mss);
    }

    if (nTransmit < segment->len)
    {
        SSegment * subseg = g_slice_new0(SSegment);
        subseg->seq = segment->seq + nTransmit;
        subseg->len = segment->len - nTransmit;
        subseg->flags = segment->flags;
        subseg->xmit = segment->xmit;

        DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "mss reduced to %u", priv->mss);

        segment->len = nTransmit;
        n_queue_insert_after(&priv->slist, n_queue_find(&priv->slist, segment), subseg);
        if (subseg->xmit == 0)
            n_queue_insert_after(&priv->unsent_slist, n_queue_find(&priv->unsent_slist, segment), subseg);
    }

    if (segment->xmit == 0)
    {
        g_assert(n_queue_peek_head(&priv->unsent_slist) == segment);
        n_queue_pop_head(&priv->unsent_slist);
        priv->snd_nxt += segment->len;

        /* FIN flags require acknowledgement. */
        if (segment->len == 0 && segment->flags & FLAG_FIN)
            priv->snd_nxt++;
    }
    segment->xmit += 1;

    if (priv->rto_base == 0)
    {
        priv->rto_base = now;
    }

    return TRUE;
}

static void attempt_send(pst_socket_t * self, SendFlags sflags)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint32_t now = pseudo_tcp_get_current_time(self);
    int bFirst = TRUE;

    if (time_diff(now, priv->lastsend) > (long) priv->rx_rto)
    {
        priv->cwnd = priv->mss;
    }


    while (TRUE)
    {
        uint32_t cwnd;
        uint32_t nWindow;
        uint32_t nInFlight;
        uint32_t nUseable;
        uint32_t nAvailable;
        uint32_t snd_buffered;
        n_dlist_t * iter;
        SSegment * sseg;

        cwnd = priv->cwnd;
        if ((priv->dup_acks == 1) || (priv->dup_acks == 2))   // Limited Transmit
        {
            cwnd += priv->dup_acks * priv->mss;
        }
        nWindow = min(priv->snd_wnd, cwnd);
        nInFlight = priv->snd_nxt - priv->snd_una;
        nUseable = (nInFlight < nWindow) ? (nWindow - nInFlight) : 0;
        snd_buffered = pst_fifo_get_buffered(&priv->sbuf);
        if (snd_buffered < nInFlight)  /* iff a FIN has been sent */
            nAvailable = 0;
        else
            nAvailable = min(snd_buffered - nInFlight, priv->mss);

        if (nAvailable > nUseable)
        {
            if (nUseable * 4 < nWindow)
            {
                // RFC 813 - avoid SWS
                nAvailable = 0;
            }
            else
            {
                nAvailable = nUseable;
            }
        }

        if (bFirst)
        {
            uint32_t available_space = pst_fifo_get_write_remaining(&priv->sbuf);
            bFirst = FALSE;
            DEBUG(PSEUDO_TCP_DEBUG_VERBOSE, "[cwnd: %u  nWindow: %u  nInFlight: %u "
                  "nAvailable: %u nQueued: %" G_GSIZE_FORMAT " nEmpty: %" G_GSIZE_FORMAT
                  "  ssthresh: %u]",
                  priv->cwnd, nWindow, nInFlight, nAvailable, snd_buffered,
                  available_space, priv->ssthresh);
        }

        if (nAvailable == 0 && sflags != sfFin && sflags != sfRst)
        {
            if (sflags == sfNone)
                return;

            // If this is an immediate ack, or the second delayed ack
            if ((sflags == sfImmediateAck) || priv->t_ack)
            {
                packet(self, priv->snd_nxt, 0, 0, 0, now);
            }
            else
            {
                priv->t_ack = now;
            }
            return;
        }

        // Nagle algorithm
        // If there is data already in-flight, and we haven't a full segment of
        // data ready to send then hold off until we get more to send, or the
        // in-flight data is acknowledged.
        if (priv->use_nagling && sflags != sfFin && sflags != sfRst &&
                (priv->snd_nxt > priv->snd_una) &&
                (nAvailable < priv->mss))
        {
            return;
        }

        // Find the next segment to transmit
        iter = n_queue_peek_head_link(&priv->unsent_slist);
        if (iter == NULL)
            return;
        sseg = iter->data;

        // If the segment is too large, break it into two
        if (sseg->len > nAvailable && sflags != sfFin && sflags != sfRst)
        {
            SSegment * subseg = g_slice_new0(SSegment);
            subseg->seq = sseg->seq + nAvailable;
            subseg->len = sseg->len - nAvailable;
            subseg->flags = sseg->flags;

            sseg->len = nAvailable;
            n_queue_insert_after(&priv->unsent_slist, iter, subseg);
            n_queue_insert_after(&priv->slist, n_queue_find(&priv->slist, sseg), subseg);
        }

        if (!transmit(self, sseg, now))
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "transmit failed");
            // TODO: consider closing socket
            return;
        }

        if (sflags == sfImmediateAck || sflags == sfDelayedAck)
            sflags = sfNone;
    }
}

/* If @source is %CLOSEDOWN_REMOTE, don?t send an RST packet, since closedown()
 * has been called as a result of an RST segment being received.
 * See: RFC 1122, ?4.2.2.13. */
static void closedown(pst_socket_t * self, uint32_t err, ClosedownSource source)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    if (source == CLOSEDOWN_LOCAL && priv->support_fin_ack)
    {
        queue_rst_message(self);
        attempt_send(self, sfRst);
    }
    else if (source == CLOSEDOWN_LOCAL)
    {
        priv->shutdown = SD_FORCEFUL;
    }

    /* ?Cute? little navigation through the state machine to avoid breaking the
     * invariant that CLOSED can only be reached from TIME-WAIT or LAST-ACK. */
    switch (priv->state)
    {
        case TCP_LISTEN:
        case TCP_SYN_SENT:
            break;
        case TCP_SYN_RECEIVED:
        case TCP_ESTABLISHED:
            set_state(self, TCP_FIN_WAIT_1);
            /* Fall through. */
        case TCP_FIN_WAIT_1:
            set_state(self, TCP_FIN_WAIT_2);
            /* Fall through. */
        case TCP_FIN_WAIT_2:
        case TCP_CLOSING:
            set_state(self, TCP_TIME_WAIT);
            break;
        case TCP_CLOSE_WAIT:
            set_state(self, TCP_LAST_ACK);
            break;
        case TCP_LAST_ACK:
        case TCP_TIME_WAIT:
        case TCP_CLOSED:
        default:
            break;
    }

    set_state_closed(self, err);
}

static void adjustMTU(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    // Determine our current mss level, so that we can adjust appropriately later
    for (priv->msslevel = 0;
            PACKET_MAXIMUMS[priv->msslevel + 1] > 0;
            ++priv->msslevel)
    {
        if (((guint16)PACKET_MAXIMUMS[priv->msslevel]) <= priv->mtu_advise)
        {
            break;
        }
    }
    priv->mss = priv->mtu_advise - PACKET_OVERHEAD;
    // !?! Should we reset priv->largest here?
    DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Adjusting mss to %u bytes", priv->mss);
    // Enforce minimums on ssthresh and cwnd
    priv->ssthresh = max(priv->ssthresh, 2 * priv->mss);
    priv->cwnd = max(priv->cwnd, priv->mss);
}

static void apply_window_scale_option(pst_socket_t * self, uint8_t scale_factor)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    priv->swnd_scale = scale_factor;
}

static void apply_fin_ack_option(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    priv->support_fin_ack = TRUE;
}

static void apply_option(pst_socket_t * self, uint8_t kind, const uint8_t * data,
             uint32_t len)
{
    switch (kind)
    {
        case TCP_OPT_MSS:
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL,
                  "Peer specified MSS option which is not supported.");
            // TODO: Implement.
            break;
        case TCP_OPT_WND_SCALE:
            // Window scale factor.
            // http://www.ietf.org/rfc/rfc1323.txt
            if (len != 1)
            {
                DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Invalid window scale option received.");
                return;
            }
            apply_window_scale_option(self, data[0]);
            break;
        case TCP_OPT_FIN_ACK:
            // FIN-ACK support.
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "FIN-ACK support enabled.");
            apply_fin_ack_option(self);
            break;
        case TCP_OPT_EOL:
        case TCP_OPT_NOOP:
            /* Nothing to do. */
            break;
        default:
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Invalid TCP option %u", kind);
            break;
    }
}


static void parse_options(pst_socket_t * self, const uint8_t * data, uint32_t len)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    int has_window_scaling_option = FALSE;
    int has_fin_ack_option = FALSE;
    uint32_t pos = 0;

    // See http://www.freesoft.org/CIE/Course/Section4/8.htm for
    // parsing the options list.
    while (pos < len)
    {
        uint8_t kind = TCP_OPT_EOL;
        uint8_t opt_len;

        if (len < pos + 1)
            return;

        kind = data[pos];
        pos++;

        if (kind == TCP_OPT_EOL)
        {
            // End of option list.
            break;
        }
        else if (kind == TCP_OPT_NOOP)
        {
            // No op.
            continue;
        }

        if (len < pos + 1)
            return;

        // Length of this option.
        opt_len = data[pos];
        pos++;

        if (len < pos + opt_len)
            return;

        // Content of this option.
        if (opt_len <= len - pos)
        {
            apply_option(self, kind, data + pos, opt_len);
            pos += opt_len;
        }
        else
        {
            DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Invalid option length received.");
            return;
        }

        if (kind == TCP_OPT_WND_SCALE)
            has_window_scaling_option = TRUE;
        else if (kind == TCP_OPT_FIN_ACK)
            has_fin_ack_option = TRUE;
    }

    if (!has_window_scaling_option)
    {
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Peer doesn't support window scaling");
        if (priv->rwnd_scale > 0)
        {
            // Peer doesn't support TCP options and window scaling.
            // Revert receive buffer size to default value.
            resize_receive_buffer(self, DEFAULT_RCV_BUF_SIZE);
            priv->swnd_scale = 0;
        }
    }

    if (!has_fin_ack_option)
    {
        DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "Peer doesn't support FIN-ACK");
        priv->support_fin_ack = FALSE;
    }
}

static void resize_send_buffer(pst_socket_t * self, uint32_t new_size)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    priv->sbuf_len = new_size;
    pst_fifo_set_capacity(&priv->sbuf, new_size);
}


static void resize_receive_buffer(pst_socket_t * self, uint32_t new_size)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint8_t scale_factor = 0;
    int result;
    uint32_t available_space;

    if (priv->rbuf_len == new_size)
        return;

    // Determine the scale factor such that the scaled window size can fit
    // in a 16-bit unsigned integer.
    while (new_size > 0xFFFF)
    {
        ++scale_factor;
        new_size >>= 1;
    }

    // Determine the proper size of the buffer.
    new_size <<= scale_factor;
    result = pst_fifo_set_capacity(&priv->rbuf, new_size);

    // Make sure the new buffer is large enough to contain data in the old
    // buffer. This should always be true because this method is called either
    // before connection is established or when peers are exchanging connect
    // messages.
    g_assert(result);
    priv->rbuf_len = new_size;
    priv->rwnd_scale = scale_factor;
    priv->ssthresh = new_size;

    available_space = pst_fifo_get_write_remaining(&priv->rbuf);
    priv->rcv_wnd = available_space;
}

int32_t pst_get_available_bytes(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    if (priv->state != TCP_ESTABLISHED)
    {
        return -1;
    }

    return pst_fifo_get_buffered(&priv->rbuf);
}

int pst_can_send(pst_socket_t * self)
{
    return (pst_get_available_send_space(self) > 0);
}

uint32_t pst_get_available_send_space(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    uint32_t ret;


    if (priv->state == TCP_ESTABLISHED)
        ret = pst_fifo_get_write_remaining(&priv->sbuf);
    else
        ret = 0;

    if (ret == 0)
        priv->bWriteEnable = TRUE;

    return ret;
}

/* State names are capitalised and formatted as in RFC 793. */
static const char * pseudo_tcp_state_get_name(PseudoTcpState state)
{
    switch (state)
    {
        case TCP_LISTEN:
            return "LISTEN";
        case TCP_SYN_SENT:
            return "SYN-SENT";
        case TCP_SYN_RECEIVED:
            return "SYN-RECEIVED";
        case TCP_ESTABLISHED:
            return "ESTABLISHED";
        case TCP_CLOSED:
            return "CLOSED";
        case TCP_FIN_WAIT_1:
            return "FIN-WAIT-1";
        case TCP_FIN_WAIT_2:
            return "FIN-WAIT-2";
        case TCP_CLOSING:
            return "CLOSING";
        case TCP_TIME_WAIT:
            return "TIME-WAIT";
        case TCP_CLOSE_WAIT:
            return "CLOSE-WAIT";
        case TCP_LAST_ACK:
            return "LAST-ACK";
        default:
            return "UNKNOWN";
    }
}

static void set_state(pst_socket_t * self, PseudoTcpState new_state)
{
    PseudoTcpSocketPrivate * priv = self->priv;
    PseudoTcpState old_state = priv->state;

    if (new_state == old_state)
        return;

    DEBUG(PSEUDO_TCP_DEBUG_NORMAL, "State %s ? %s.",
          pseudo_tcp_state_get_name(old_state),
          pseudo_tcp_state_get_name(new_state));

    /* Check whether it?s a valid state transition. */
#define TRANSITION(OLD, NEW) \
    (old_state == TCP_##OLD && \
     new_state == TCP_##NEW)

    /* Valid transitions. See: RFC 793, p23; RFC 1122, ?4.2.2.8. */
    g_assert( /* RFC 793, p23. */
        TRANSITION(CLOSED, SYN_SENT) ||
        TRANSITION(SYN_SENT, CLOSED) ||
        TRANSITION(CLOSED, LISTEN) ||
        TRANSITION(LISTEN, CLOSED) ||
        TRANSITION(LISTEN, SYN_SENT) ||
        TRANSITION(LISTEN, SYN_RECEIVED) ||
        TRANSITION(SYN_SENT, SYN_RECEIVED) ||
        TRANSITION(SYN_RECEIVED, ESTABLISHED) ||
        TRANSITION(SYN_SENT, ESTABLISHED) ||
        TRANSITION(SYN_RECEIVED, FIN_WAIT_1) ||
        TRANSITION(ESTABLISHED, FIN_WAIT_1) ||
        TRANSITION(ESTABLISHED, CLOSE_WAIT) ||
        TRANSITION(FIN_WAIT_1, FIN_WAIT_2) ||
        TRANSITION(FIN_WAIT_1, CLOSING) ||
        TRANSITION(CLOSE_WAIT, LAST_ACK) ||
        TRANSITION(FIN_WAIT_2, TIME_WAIT) ||
        TRANSITION(CLOSING, TIME_WAIT) ||
        TRANSITION(LAST_ACK, CLOSED) ||
        TRANSITION(TIME_WAIT, CLOSED) ||
        /* RFC 1122, ?4.2.2.8. */
        TRANSITION(SYN_RECEIVED, LISTEN) ||
        TRANSITION(FIN_WAIT_1, TIME_WAIT));

#undef TRANSITION

    priv->state = new_state;
}

static void set_state_established(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    set_state(self, TCP_ESTABLISHED);

    adjustMTU(self);
    if (priv->callbacks.PseudoTcpOpened)
        priv->callbacks.PseudoTcpOpened(self, priv->callbacks.user_data);
}

/* (err == 0) means no error. */
static void set_state_closed(pst_socket_t * self, uint32_t err)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    set_state(self, TCP_CLOSED);

    /* Only call the callback if there was an error. */
    if (priv->callbacks.PseudoTcpClosed && err != 0)
        priv->callbacks.PseudoTcpClosed(self, err, priv->callbacks.user_data);
}

int pst_is_closed(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    return (priv->state == TCP_CLOSED);
}

int pst_is_closed_remotely(pst_socket_t * self)
{
    PseudoTcpSocketPrivate * priv = self->priv;

    return pseudo_tcp_state_has_received_fin(priv->state);
}
