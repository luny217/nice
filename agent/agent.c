/* This file is part of the Nice GLib ICE library. */

#include <config.h>
//#include <glib.h>
//#include <gobject/gvaluecollector.h>

#include <string.h>
#include <errno.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <WinSock2.h>
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
#include "stream.h"
#include "interfaces.h"
#include "pseudotcp.h"


#include "base.h"
#include "nlist.h"
#include "nqueue.h"
#include "event.h"
#include "timer.h"
#include "pthread.h"

/* Maximum size of a UDP packet's payload, as the packet's length field is 16b
 * wide. */
#define MAX_BUFFER_SIZE ((1 << 16) - 1)  /* 65535 */

#define DEFAULT_STUN_PORT  3478
#define DEFAULT_UPNP_TIMEOUT 200  /* milliseconds */

#define MAX_TCP_MTU 1400 /* Use 1400 because of VPNs and we assume IEE 802.3 */
#define TCP_HEADER_SIZE 24 /* bytes */

static void n_debug_input_msg(const n_input_msg_t * messages,  uint32_t n_messages);

static pthread_t worker_tid;


//G_DEFINE_TYPE(n_agent_t, nice_agent, G_TYPE_OBJECT);

static void n_agent_init(n_agent_t * self);

static pthread_mutex_t agent_mutex;    /* Mutex used for thread-safe lib */

static void pst_opened(pst_socket_t * sock, void * user_data);
static void pst_readable(pst_socket_t * sock, void * user_data);
static void pst_writable(pst_socket_t * sock, void * user_data);
static void pst_closed(pst_socket_t * sock, uint32_t err, void * user_data);
static pst_wret_e pst_write_packet(pst_socket_t * sock, char * buffer, uint32_t len, void * user_data);
static void adjust_tcp_clock(n_agent_t * agent, n_stream_t * stream, n_comp_t * component);
//static void n_agent_dispose(GObject * object);

#if 0// _WIN32
typedef struct _GWin32WinsockFuncs
{
    PFN_InetPton pInetPton;
    PFN_InetNtop pInetNtop;
    PFN_IfNameToIndex pIfNameToIndex;
} GWin32WinsockFuncs;
#endif

void n_networking_init(void)
{
#ifdef _WIN32
    //GWin32WinsockFuncs ws2funcs;
    static volatile uint32_t inited = 0;

    if (!inited)
    {
        WSADATA wsadata;
        //HMODULE ws2dll, iphlpapidll;

        if (WSAStartup(MAKEWORD(2, 0), &wsadata) != 0)
            printf("Windows Sockets could not be initialized");

        inited = 1;
    }
#endif
}

int _poll(struct pollfd * fds, nfds_t numfds, int timeout)
{
    fd_set read_set;
    fd_set write_set;
    fd_set exception_set;
    nfds_t i;
    uint32_t n;
    int rc;

    if (numfds >= FD_SETSIZE)
    {
        errno = EINVAL;
        return -1;
    }

    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_ZERO(&exception_set);

    n = 0;
    for (i = 0; i < numfds; i++)
    {
        if (fds[i].fd < 0)
            continue;

        if (fds[i].events & POLLIN)
            FD_SET(fds[i].fd, &read_set);
        if (fds[i].events & POLLOUT)
            FD_SET(fds[i].fd, &write_set);
        if (fds[i].events & POLLERR)
            FD_SET(fds[i].fd, &exception_set);

        if (fds[i].fd >= n)
            n = fds[i].fd + 1;
    }

    if (n == 0)
        /* Hey!? Nothing to poll, in fact!!! */
        return 0;

    if (timeout < 0)
    {
        rc = select(n, &read_set, &write_set, &exception_set, NULL);
    }
    else
    {
        struct timeval tv;
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = 1000 * (timeout % 1000);
        rc = select(n, &read_set, &write_set, &exception_set, &tv);
    }

    if (rc < 0)
        return rc;

    for (i = 0; i < numfds; i++)
    {
        fds[i].revents = 0;

        if (FD_ISSET(fds[i].fd, &read_set))
            fds[i].revents |= POLLIN;
        if (FD_ISSET(fds[i].fd, &write_set))
            fds[i].revents |= POLLOUT;
        if (FD_ISSET(fds[i].fd, &exception_set))
            fds[i].revents |= POLLERR;
    }

    return rc;
}

void agent_lock(void)
{
	//nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    //pthread_mutex_lock(&agent_mutex);
}

void agent_unlock(void)
{
	//nice_debug("[%s]: agent_unlock-------------------", G_STRFUNC);
    //pthread_mutex_unlock(&agent_mutex);
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

int32_t agent_find_comp(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, n_stream_t ** stream, n_comp_t ** comp)
{
    n_stream_t * s;
    n_comp_t * c;

    s = agent_find_stream(agent, stream_id);

    if (s == NULL)
        return FALSE;

    c = stream_find_comp_by_id(s, comp_id);

    if (c == NULL)
        return FALSE;

    if (stream)
        *stream = s;

    if (comp)
        *comp = c;

    return TRUE;
}

static void _generate_tie_breaker(n_agent_t * agent)
{
    nice_rng_generate_bytes(agent->rng, 8, (char *)&agent->tie_breaker);
}

static void n_agent_init(n_agent_t * agent)
{
    agent->next_candidate_id = 1;
    agent->next_stream_id = 1;

    /* set defaults; not construct params, so set here */
    agent->stun_server_port = DEFAULT_STUN_PORT;
    agent->controlling_mode = TRUE;
    agent->max_conn_checks = _AGENT_MAX_CONNECTIVITY_CHECKS;

    agent->timer_ta = _AGENT_TIMER_TA_DEFAULT;

    agent->discovery_list = NULL;
    agent->disc_unsched_items = 0;
    agent->refresh_list = NULL;
    agent->media_after_tick = FALSE;

    agent->disc_timer = 0;
    agent->cocheck_timer = 0;
    agent->keepalive_timer = 0;

    agent->reliable = TRUE;
    agent->use_ice_udp = TRUE;
    agent->use_ice_tcp = FALSE;
    agent->full_mode = TRUE;

    agent->rng = nice_rng_new();
    _generate_tie_breaker(agent);

    pthread_mutex_init(&agent_mutex, NULL);
    n_queue_init(&agent->pending_signals);
}

n_agent_t * n_agent_new()
{
    //n_agent_t * agent = g_object_new(NICE_TYPE_AGENT, NULL);
    n_agent_t * agent = n_slice_new0(n_agent_t);
    if (agent)
    {
        n_agent_init(agent);
    }
    return agent;
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

static void agent_signal_socket_writable(n_agent_t * agent, n_comp_t * comp)
{
    //g_cancellable_cancel(comp->tcp_writable_cancellable);
    /*agent_queue_signal(agent, signals[SIGNAL_RELIABLE_TRANSPORT_WRITABLE], comp->stream->id, comp->id);*/
    if (agent->n_event)
    {
        ev_trans_writable_t * ev_trans_writable = n_slice_new0(ev_trans_writable_t);
        ev_trans_writable->comp_id = comp->id;
        ev_trans_writable->stream_id = comp->stream->id;

        nice_debug("[%s] event_post stream_id [%d]", G_STRFUNC, ev_trans_writable->stream_id);
        event_post(agent->n_event, N_EVENT_NEW_REMOTE_CAND, ev_trans_writable);
    }
}

static void pst_create(n_agent_t * agent, n_stream_t * stream, n_comp_t * comp)
{
    pst_callback_t tcp_callbacks =
    {
        comp,
        pst_opened,
        pst_readable,
        pst_writable,
        pst_closed,
        pst_write_packet
    };
    comp->tcp = pst_new(0x8989, &tcp_callbacks);
    nice_debug("[%s]: create pst 0x%p", G_STRFUNC, comp->tcp);
}

static void _pseudo_tcp_error(n_agent_t * agent, n_stream_t * stream, n_comp_t * comp)
{
    /*if (comp->tcp_writable_cancellable)
    {
        g_cancellable_cancel(comp->tcp_writable_cancellable);
        g_clear_object(&comp->tcp_writable_cancellable);
    }*/

    if (comp->tcp)
    {
        agent_sig_comp_state_change(agent, stream->id, comp->id, COMP_STATE_FAILED);
        component_detach_all_sockets(comp);
        pst_close(comp->tcp, TRUE);
    }

    if (comp->tcp_clock != 0)
    {
        /*g_source_destroy(comp->tcp_clock);
        g_source_unref(comp->tcp_clock);
        comp->tcp_clock = NULL;*/
        timer_stop(comp->tcp_clock);
        timer_destroy(comp->tcp_clock);
        comp->tcp_clock = 0;
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

/* This is called with the agent lock held. */
static void pst_readable(pst_socket_t * sock, void * user_data)
{
    n_comp_t * comp = user_data;
    n_agent_t * agent = comp->agent;
    n_stream_t * stream = comp->stream;
    int32_t has_io_callback = 1;
    uint32_t stream_id = stream->id;
    uint32_t comp_id = comp->id;
    int ret;

//    g_object_ref(agent);

    nice_debug("[%s]: s%d:%d pseudo Tcp socket readable", G_STRFUNC, stream->id, comp->id);

    comp->tcp_readable = TRUE;

    //has_io_callback = component_has_io_callback(comp);

    /* Only dequeue pseudo-TCP data if we can reliably inform the client. The
     * agent lock is held here, so has_io_callback can only change during
     * comp_emit_io_cb(), after which it's re-queried. This ensures
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

            nice_debug("%s: I/O callback case: recv %d bytes", G_STRFUNC, len);

            if (len == 0)
            {
                /* Reached EOS. */
                comp->tcp_readable = FALSE;
                pst_close(comp->tcp, FALSE);
                break;
            }
            else if (len < 0)
            {
                ret = pst_get_error(sock);
                /* Handle errors. */
                if (ret != EWOULDBLOCK)
                {
                    nice_debug("%s: pst_recv error(%d)\n", G_STRFUNC, ret);
                    _pseudo_tcp_error(agent, stream, comp);
                }

                /*if (comp->recv_buf_error != NULL)
                {
                    GIOErrorEnum error_code;

                    if (pst_get_error(sock) == ENOTCONN)
                        error_code = G_IO_ERROR_BROKEN_PIPE;
                    else if (pst_get_error(sock) == EWOULDBLOCK)
                        error_code = G_IO_ERROR_WOULD_BLOCK;
                    else
                        error_code = G_IO_ERROR_FAILED;

                    g_set_error(comp->recv_buf_error, G_IO_ERROR, error_code, "Error reading data from pseudo-TCP socket.");
                }*/
                break;
            }

            comp_emit_io_cb(comp, buf, len);

            if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
            {
                nice_debug("n_stream_t or n_comp_t disappeared during the callback");
                goto out;
            }
            if (pst_is_closed(comp->tcp))
            {
                nice_debug("PseudoTCP socket got destroyed in readable callback!");
                goto out;
            }

            //has_io_callback = component_has_io_callback(comp);
        }
        while (has_io_callback);
    }
    else if (comp->recv_messages != NULL)
    {
#if 0
        int32_t n_valid_messages;
        GError * child_error = NULL;

        /* Fill up every buffer in every message until the connection closes or an
         * error occurs. Copy the data directly into the client's receive message
         * array without making any callbacks. Update component->recv_messages_iter
         * as we go. */
        n_valid_messages = pst_recv_messages(sock, comp->recv_messages, comp->n_recv_messages,
                                             &comp->recv_messages_iter, &child_error);

        nice_debug("%s: Client buffers case: Received %d valid messages:", G_STRFUNC, n_valid_messages);
        n_debug_input_msg(comp->recv_messages, comp->n_recv_messages);

        if (n_valid_messages < 0)
        {
            g_propagate_error(comp->recv_buf_error, child_error);
        }
        else
        {
            g_clear_error(&child_error);
        }

        if (n_valid_messages < 0 && g_error_matches(child_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
        {
            comp->tcp_readable = FALSE;
        }
        else if (n_valid_messages < 0)
        {
            nice_debug("%s: calling _pseudo_tcp_error()", G_STRFUNC);
            _pseudo_tcp_error(agent, stream, comp);
        }
        else if (n_valid_messages == 0)
        {
            /* Reached EOS. */
            comp->tcp_readable = FALSE;
            pst_close(comp->tcp, FALSE);
        }
#endif
    }
    else
    {
        nice_debug("%s: no data read", G_STRFUNC);
    }

    if (stream && comp)
        adjust_tcp_clock(agent, stream, comp);

out:
    // g_object_unref(agent);
    return;
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

    nice_debug("[%s]: s%d:%d pst socket closed\n", G_STRFUNC,  stream->id, comp->id);
    _pseudo_tcp_error(agent, stream, comp);
}


static pst_wret_e pst_write_packet(pst_socket_t * psocket, char * buffer, uint32_t len, void * user_data)
{
    n_comp_t * comp = user_data;

    if (comp->selected_pair.local != NULL)
    {
        n_socket_t * sock;
        n_addr_t * addr;

        sock = comp->selected_pair.local->sockptr;
        addr = &comp->selected_pair.remote->addr;

        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN];
            nice_address_to_string(addr, tmpbuf);

            nice_debug("[%s]: %d:%d: sending %d bytes on socket  (FD %d) to [%s]:%d",
                       G_STRFUNC, comp->stream->id, comp->id, len,
                       &sock->sock_fd, tmpbuf, n_addr_get_port(addr));
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
        nice_debug("[%s]: failed to send pseudo-TCP packet from agent %p "
                   "as no pair has been selected yet.", G_STRFUNC, comp->agent);
    }

    return WR_FAIL;
}

static int notify_pst_clock(void * user_data)
{
    n_comp_t * component = user_data;
    n_stream_t * stream;
    n_agent_t * agent;

    //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    agent_lock();

    stream = component->stream;
    agent = component->agent;

    /*
        if (g_source_is_destroyed(g_main_current_source()))
        {
            nice_debug("Source was destroyed. " "Avoided race condition in notify_pst_clock");
            agent_unlock();
            return FALSE;
        }*/

    pst_notify_clock(component->tcp);
    adjust_tcp_clock(agent, stream, component);

    agent_unlock();

    return TRUE;
}

static void adjust_tcp_clock(n_agent_t * agent, n_stream_t * stream, n_comp_t * comp)
{
    if (!pst_is_closed(comp->tcp))
    {
        uint64_t timeout = comp->last_clock_timeout;

        if (pst_get_next_clock(comp->tcp, &timeout))
        {
            if (timeout != comp->last_clock_timeout)
            {
                comp->last_clock_timeout = timeout;
                if (comp->tcp_clock)
                {
                    //g_source_set_ready_time(comp->tcp_clock, timeout * 1000);
                    timer_set_mono(comp->tcp_clock, timeout * 1000);
                }

                if (comp->tcp_clock == 0)
                {
                    int32_t interval = (int32_t)timeout - (uint32_t)(get_monotonic_time() / 1000);

                    /* Prevent integer overflows */
                    if (interval < 0 || interval > INT_MAX)
                        interval = INT_MAX;
                    /*agent_timeout_add(agent, &comp->tcp_clock, "Pseudo-TCP clock", interval, notify_pst_clock, comp);*/

                    comp->tcp_clock = timer_create();
                    timer_init(comp->tcp_clock, 0, interval, (notifycallback)notify_pst_clock, (void *)comp, "pseudo-tcp clock");
                    timer_start(comp->tcp_clock);
                }
            }
        }
        else
        {
            nice_debug("[%s]: comp %d pseudo-TCP socket should be "
                       "destroyed. Calling _pseudo_tcp_error().", G_STRFUNC, comp->id);
            _pseudo_tcp_error(agent, stream, comp);
        }
    }
}

static void _tcp_sock_is_writable(n_socket_t * sock, void * user_data)
{
    n_comp_t * component = user_data;
    n_agent_t * agent = component->agent;
    n_stream_t * stream = component->stream;

    nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
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

    agent_unlock();
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
                    nice_debug("[%s]: gathered local candidate : [%s]:%u for s%d/c%d", G_STRFUNC,
                               tmpbuf, n_addr_get_port(&local_candidate->addr),
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
                        cocheck_add_cand_pair(agent, stream->id, component, local_candidate, remote_candidate);
                    }
                }
            }
        }
    }

    if (agent->disc_timer == 0)
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
            /*agent_queue_signal(agent, signals[SIGNAL_CANDIDATE_GATHERING_DONE], stream->id);*/
            if (agent->n_event)
            {
                uint32_t  * id = n_slice_new0(uint32_t);
                *id = stream->id;
                nice_debug("[%s] event_post n_event_cand_gathering_done [%d]", G_STRFUNC, *id);
                event_post(agent->n_event, N_EVENT_CAND_GATHERING_DONE, id);
            }
        }
    }
}

void agent_sig_initial_binding_request_received(n_agent_t * agent, n_stream_t * stream)
{
    if (stream->initial_binding_request_received != TRUE)
    {
        stream->initial_binding_request_received = TRUE;
        /*agent_queue_signal(agent, signals[SIGNAL_INITIAL_BINDING_REQUEST_RECEIVED], stream->id);*/
        if (agent->n_event)
        {
            uint32_t  * id = n_slice_new0(uint32_t);
            *id = stream->id;
            nice_debug("[%s] event_post stream->id [%d]", G_STRFUNC, *id);
            event_post(agent->n_event, N_EVENT_INITIAL_BINDING_REQUEST_RECEIVED, id);
        }
    }
}

/* If the n_comp_t now has a selected_pair, and has pending TCP packets which
 * it couldn??t receive before due to not being able to send out ACKs (or
 * SYNACKs, for the initial SYN packet), handle them now.
 *
 * Must be called with the agent lock held. */
static void process_queued_tcp_packets(n_agent_t * agent, n_stream_t * stream, n_comp_t * comp)
{
    n_outvector_t * vec;
    uint32_t stream_id = stream->id;
    uint32_t comp_id = comp->id;

    //g_assert(agent->reliable);

    if (comp->selected_pair.local == NULL || pst_is_closed(comp->tcp))
    {
        return;
    }

    //nice_debug("[%s]: sending outstanding packets", G_STRFUNC);

    while ((vec = n_queue_peek_head(&comp->queued_tcp_packets)) != NULL)
    {
        int32_t retval;

        nice_debug("[%s]: sending queued %u bytes for n_outvector_t %p", G_STRFUNC, vec->size, vec);
        retval =  pst_notify_packet(comp->tcp, vec->buffer, vec->size);

        if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
        {
            nice_debug("n_stream_t or n_comp_t disappeared during pst_notify_packet");
            return;
        }

        if (pst_is_closed(comp->tcp))
        {
            nice_debug("PseudoTCP socket got destroyed in pst_notify_packet!");
            return;
        }

        adjust_tcp_clock(agent, stream, comp);

        if (!retval)
        {
            /* Failed to send; try again later. */
            break;
        }

        n_queue_pop_head(&comp->queued_tcp_packets);
        n_free((void *) vec->buffer);
        n_slice_free(n_outvector_t, vec);
    }
}

void agent_sig_new_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, n_cand_t * lcand, n_cand_t * rcand)
{
    n_comp_t * comp;
    n_stream_t * stream;

    if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
        return;

    if (((n_socket_t *)lcand->sockptr)->type == NICE_SOCKET_TYPE_UDP_TURN)
    {
        /*nice_udp_turn_socket_set_peer(lcandidate->sockptr, &rcandidate->addr);*/
    }

    if (agent->reliable)
    {
        if (!comp->tcp)
            pst_create(agent, stream, comp);
        process_queued_tcp_packets(agent, stream, comp);

        pst_connect(comp->tcp);
        pst_notify_mtu(comp->tcp, MAX_TCP_MTU);
        adjust_tcp_clock(agent, stream, comp);
    }

    if (nice_debug_is_enabled())
    {
        char ip[100];
        uint32_t port;

        port = n_addr_get_port(&lcand->addr);
        nice_address_to_string(&lcand->addr, ip);

        nice_debug("[%s]: local selected pair: %d:%d %s %s:%d %s", G_STRFUNC,
                   stream_id, comp_id, lcand->foundation,
                   ip, port, lcand->type == CAND_TYPE_HOST ? "HOST" :
                   lcand->type == CAND_TYPE_SERVER ?
                   "SRV-RFLX" :
                   lcand->type == CAND_TYPE_RELAYED ?
                   "RELAYED" :
                   lcand->type == CAND_TYPE_PEER ?
                   "PEER-RFLX" : "???");

        port = n_addr_get_port(&rcand->addr);
        nice_address_to_string(&rcand->addr, ip);

        nice_debug("[%s]: Remote selected pair: %d:%d %s %s:%d %s", G_STRFUNC,
                   stream_id, comp_id, rcand->foundation,
                   ip, port, rcand->type == CAND_TYPE_HOST ? "HOST" :
                   rcand->type == CAND_TYPE_SERVER ?
                   "SRV-RFLX" :
                   rcand->type == CAND_TYPE_RELAYED ?
                   "RELAYED" :
                   rcand->type == CAND_TYPE_PEER ?
                   "PEER-RFLX" : "???");
        nice_print_cand(agent, lcand, rcand);
    }

    /*agent_queue_signal(agent, signals[SIGNAL_NEW_SELECTED_PAIR_FULL],
                       stream_id, component_id, lcandidate, rcandidate);*/
    if (agent->n_event)
    {
        ev_new_pair_full_t * ev_new_pair_full = n_slice_new0(ev_new_pair_full_t);
        ev_new_pair_full->component_id = comp_id;
        ev_new_pair_full->stream_id = stream_id;
        ev_new_pair_full->lcandidate = lcand;
        ev_new_pair_full->rcandidate = rcand;

        //nice_debug("[%s] event_post stream_id [%d]", G_STRFUNC, ev_new_pair_full->stream_id);
        event_post(agent->n_event, N_EVENT_NEW_SELECTED_PAIR_FULL, ev_new_pair_full);
    }

    /*agent_queue_signal(agent, signals[SIGNAL_NEW_SELECTED_PAIR],
                       stream_id, component_id, lcandidate->foundation, rcandidate->foundation);*/
    if (agent->n_event)
    {
        ev_new_pair_t * ev_new_pair = n_slice_new0(ev_new_pair_t);
        ev_new_pair->component_id = comp_id;
        ev_new_pair->stream_id = stream_id;
        strncpy(ev_new_pair->lfoundation, lcand->foundation, CAND_MAX_FOUNDATION);
        strncpy(ev_new_pair->rfoundation, rcand->foundation, CAND_MAX_FOUNDATION);

        //nice_debug("[%s] event_post stream_id [%d]", G_STRFUNC, ev_new_pair->stream_id);
        event_post(agent->n_event, N_EVENT_NEW_SELECTED_PAIR, ev_new_pair);
    }

	/*????
    if (agent->reliable && nice_socket_is_reliable(lcand->sockptr))
    {
        agent_signal_socket_writable(agent, comp);
    }*/
}

void agent_sig_new_cand(n_agent_t * agent, n_cand_t * candidate)
{
    /*agent_queue_signal(agent, signals[SIGNAL_NEW_CANDIDATE_FULL],  candidate);*/
    if (agent->n_event)
    {
        n_cand_t * cand = n_slice_new0(n_cand_t);
        memcpy(cand, candidate, sizeof(n_cand_t));
        nice_debug("[%s] event_post N_EVENT_NEW_CAND_FULL [%x]\n", G_STRFUNC, N_EVENT_NEW_CAND_FULL);
        event_post(agent->n_event, N_EVENT_NEW_CAND_FULL, cand);
    }
    /*agent_queue_signal(agent, signals[SIGNAL_NEW_CANDIDATE],
                       candidate->stream_id, candidate->component_id, candidate->foundation);*/

    if (agent->n_event)
    {
        ev_new_cand_t * ev_new_cand = n_slice_new0(ev_new_cand_t);
        ev_new_cand->comp_id = candidate->component_id;
        ev_new_cand->stream_id = candidate->stream_id;
        strncpy(ev_new_cand->foundation, candidate->foundation, CAND_MAX_FOUNDATION);

        nice_debug("[%s] event_post N_EVENT_NEW_CAND [%x]\n", G_STRFUNC, N_EVENT_NEW_CAND);
        event_post(agent->n_event, N_EVENT_NEW_CAND, ev_new_cand);
    }

}

void agent_sig_new_remote_cand(n_agent_t * agent, n_cand_t * candidate)
{
    /*agent_queue_signal(agent, signals[SIGNAL_NEW_REMOTE_CANDIDATE_FULL], candidate);*/

    if (agent->n_event)
    {
        n_cand_t * cand = n_slice_new0(n_cand_t);
        memcpy(cand, candidate, sizeof(n_cand_t));
        nice_debug("[%s] event_post stream_id [%d]", G_STRFUNC, cand->stream_id);
        event_post(agent->n_event, N_EVENT_NEW_REMOTE_CAND_FULL, cand);
    }

    /*agent_queue_signal(agent, signals[SIGNAL_NEW_REMOTE_CANDIDATE],
                       candidate->stream_id, candidate->component_id, candidate->foundation);*/

    if (agent->n_event)
    {
        ev_new_cand_t * ev_new_cand = n_slice_new0(ev_new_cand_t);
        ev_new_cand->comp_id = candidate->component_id;
        ev_new_cand->stream_id = candidate->stream_id;
        strncpy(ev_new_cand->foundation, candidate->foundation, CAND_MAX_FOUNDATION);

        nice_debug("[%s] event_post stream_id [%d]", G_STRFUNC, ev_new_cand->stream_id);
        event_post(agent->n_event, N_EVENT_NEW_REMOTE_CAND, ev_new_cand);
    }
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

        /*agent_queue_signal(agent, signals[SIGNAL_COMP_STATE_CHANGED], stream_id, component_id, state);*/

        if (agent->n_event)
        {
            ev_state_changed_t * ev_state_changed = n_slice_new0(ev_state_changed_t);
            ev_state_changed->comp_id = component_id;
            ev_state_changed->stream_id = stream_id;
            ev_state_changed->state = state;

            nice_debug("[%s] event_post state [%d]", G_STRFUNC, state);
            event_post(agent->n_event, N_EVENT_COMP_STATE_CHANGED, ev_state_changed);
        }
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

static void _add_new_cdisc_turn(n_agent_t * agent, n_socket_t * nicesock, turn_server_t * turn, n_stream_t * stream, uint32_t comp_id)
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

/*在agent增加一个stream，一个stream可以有多个components，管理多个流?*/
uint32_t n_agent_add_stream(n_agent_t * agent, uint32_t n_comps)
{
    n_stream_t * stream;
    uint32_t ret = 0;
    uint32_t i;

    agent_lock();
    stream = stream_new(agent, n_comps);

    agent->streams_list = n_slist_append(agent->streams_list, stream);
    stream->id = agent->next_stream_id++;
    nice_debug("[%s]: allocating stream id %u (%p)", G_STRFUNC, stream->id, stream);

    for (i = 0; i < n_comps; i++)
    {
        n_comp_t * comp = stream_find_comp_by_id(stream, i + 1);
        if (comp)
        {
            pst_create(agent, stream, comp);
        }
        else
        {
            nice_debug("[%s]: couldn't find component %d", G_STRFUNC, i + 1);
        }
    }

    stream_initialize_credentials(stream, agent->rng);

    ret = stream->id;

    agent_unlock();
    return ret;
}


int agent_set_relay_info(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id,  const char * server_ip,
                         uint32_t server_port, const char * username, const char * password)
{
    n_comp_t * comp = NULL;
    n_stream_t * stream = NULL;
    int32_t ret = TRUE;
    turn_server_t * turn;

    agent_lock();

    if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
    {
        ret = FALSE;
        goto done;
    }

    turn = turn_server_new(server_ip, server_port, username, password);

    if (!turn)
    {
        ret = FALSE;
        goto done;
    }

    nice_debug("[%s]: added relay server [%s]:%d  to s/c %d/%d "
               "with user/pass : %s -- %s", G_STRFUNC, server_ip, server_port,
               stream_id, comp_id, username, password);

    comp->turn_servers = n_dlist_append(comp->turn_servers, turn);

    if (stream->gathering_started)
    {
        n_slist_t * i;

        stream->gathering = TRUE;

        for (i = comp->local_candidates; i; i = i->next)
        {
            n_cand_t * candidate = i->data;

            if (candidate->type == CAND_TYPE_HOST)
                _add_new_cdisc_turn(agent, candidate->sockptr, turn, stream, comp_id);
        }

        if (agent->disc_unsched_items)
            disc_schedule(agent);
    }

done:
    agent_unlock();
    return ret;
}

int n_agent_gather_cands(n_agent_t * agent, uint32_t stream_id)
{
    int32_t ret = TRUE;
    uint32_t cid;
    n_slist_t * l, * local_addresses = NULL;
    n_stream_t * stream;

    agent_lock();

    stream = agent_find_stream(agent, stream_id);
    if (stream == NULL)
    {
        agent_unlock();
        return FALSE;
    }

    if (stream->gathering_started)
    {
        /* n_stream_t is already gathering, ignore this call */
        agent_unlock();
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
        for (l = agent->local_addresses; l; l = l->next)
        {
            n_addr_t * addr = l->data;
            n_addr_t * dupaddr = nice_address_dup(addr);

            local_addresses = n_slist_append(local_addresses, dupaddr);
        }
    }

    /* 为所有本地地址生成候选地址 */
    for (l = local_addresses; l; l = l->next)
    {
        n_addr_t * addr = l->data;
        n_cand_t * host_candidate;
        uint32_t  current_port, start_port;
        HostCandidateResult res = CANDIDATE_CANT_CREATE_SOCKET;

        for (cid = 1; cid <= stream->n_components; cid++)
        {
            n_comp_t * comp = stream_find_comp_by_id(stream, cid);

            if (comp == NULL)
                continue;

            start_port = comp->min_port;
            if (comp->min_port != 0)
            {
                start_port = n_rng_gen_int(agent->rng, comp->min_port, comp->max_port + 1);
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
                if (current_port > comp->max_port) current_port = comp->min_port;
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
                               " s%d:%d. Invalid interface?", G_STRFUNC, ip, stream->id, comp->id);
                }
                ret = FALSE;
                goto error;
            }

            /*添加stun外网映射候选*/
            if (agent->stun_server_ip)
            {
                n_addr_t stun_server;
                if (nice_address_set_from_string(&stun_server, agent->stun_server_ip))
                {
                    nice_address_set_port(&stun_server, agent->stun_server_port);

                    _add_new_cdisc_stun(agent, host_candidate->sockptr, stun_server, stream, cid);
                }
            }

            /*添加turn转发候选*/
            if (comp->turn_servers)
            {
                n_dlist_t * item;
                for (item = comp->turn_servers; item; item = item->next)
                {
                    turn_server_t * turn = item->data;

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
        n_comp_t * comp = stream_find_comp_by_id(stream, cid);
        for (l = comp->local_candidates; l; l = l->next)
        {
            n_cand_t * candidate = l->data;
            //agent_sig_new_cand(agent, candidate);
        }
    }

    /* note: no async discoveries pending, signal that we are ready */
    if (agent->disc_unsched_items == 0)
    {
        nice_debug("[%s]: candidate gathering finished, no scheduled items.", G_STRFUNC);
        agent_gathering_done(agent);
    }
    else if (agent->disc_unsched_items)
    {
        disc_schedule(agent);
    }

error:
    for (l = local_addresses; l; l = l->next)
    {
        nice_address_free(l->data);
    }
    n_slist_free(local_addresses);

    if (ret == FALSE)
    {
        for (cid = 1; cid <= stream->n_components; cid++)
        {
            n_comp_t * comp = stream_find_comp_by_id(stream, cid);

            component_free_socket_sources(comp);

            for (l = comp->local_candidates; l; l = l->next)
            {
                n_cand_t * candidate = l->data;

                agent_remove_local_candidate(agent, candidate);

                n_cand_free(candidate);
            }
            n_slist_free(comp->local_candidates);
            comp->local_candidates = NULL;
        }
        disc_prune_stream(agent, stream_id);
    }
    agent_unlock();
    return ret;
}

void agent_remove_local_candidate(n_agent_t * agent, n_cand_t * candidate)
{
}

static void _remove_keepalive_timer(n_agent_t * agent)
{
    /*
        if (agent->keepalive_timer_source != NULL)
        {
            g_source_destroy(agent->keepalive_timer_source);
            g_source_unref(agent->keepalive_timer_source);
            agent->keepalive_timer_source = NULL;
        }*/


    if (agent->keepalive_timer != 0)
    {
        timer_stop(agent->keepalive_timer);
        timer_destroy(agent->keepalive_timer);
        agent->keepalive_timer = 0;
    }
}

void n_agent_remove_stream(n_agent_t * agent, uint32_t stream_id)
{
    uint32_t stream_ids[] = { stream_id, 0 };

    /* note that streams/candidates can be in use by other threads */

    n_stream_t * stream;

    agent_lock();
    stream = agent_find_stream(agent, stream_id);

    if (!stream)
    {
        agent_unlock();
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

    /*agent_queue_signal(agent, signals[SIGNAL_STREAMS_REMOVED], g_memdup(stream_ids, sizeof(stream_ids)));*/

    agent_unlock();

    /* Actually free the stream. This should be done with the lock released, as
     * it could end up disposing of a NiceIOStream, which tries to take the
     * agent lock itself. */
    stream_free(stream);

    return;
}

void agent_set_port_range(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, uint32_t min_port, uint32_t max_port)
{
    n_stream_t * stream;
    n_comp_t * comp;

    agent_lock();

    if (agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
    {
        if (stream->gathering_started)
        {
            nice_debug("n_agent_gather_cands (stream_id=%u) already called for this stream", stream_id);
        }
        else
        {
            comp->min_port = min_port;
            comp->max_port = max_port;
        }
    }

    agent_unlock();
}

int32_t n_agent_add_local_addr(n_agent_t * agent, n_addr_t * addr)
{
    n_addr_t * dupaddr;

    agent_lock();

    dupaddr = nice_address_dup(addr);
    nice_address_set_port(dupaddr, 0);
    agent->local_addresses = n_slist_append(agent->local_addresses, dupaddr);

    agent_unlock();
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
                       n_addr_get_port(addr), stream_id, comp_id,
                       username, password, priority);
        }
        /* case 1: an existing candidate, update the attributes */
        candidate->type = type;
        if (base_addr)
            candidate->base_addr = *base_addr;
        candidate->priority = priority;
        if (foundation)
            strncpy(candidate->foundation, foundation, CAND_MAX_FOUNDATION);
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
            candidate->username = n_strdup(username);
        }
        if (password)
        {
            n_free(candidate->password);
            candidate->password = n_strdup(password);
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
            nice_debug("[%s]: adding remote candidate with addr [%s]:%u"
                       " for s%d/c%d. U/P '%s'/'%s' prio: %u", G_STRFUNC, tmpbuf,
                       addr ? n_addr_get_port(addr) : 0, stream_id, comp_id,
                       username, password, priority);
        }

        if (base_addr)
            candidate->base_addr = *base_addr;

        candidate->transport = transport;
        candidate->priority = priority;
        candidate->username = n_strdup(username);
        candidate->password = n_strdup(password);

        if (foundation)
            strncpy(candidate->foundation, foundation, CAND_MAX_FOUNDATION);
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

    //g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    //g_return_val_if_fail(stream_id >= 1, FALSE);

    nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    //agent_lock();

    stream = agent_find_stream(agent, stream_id);
    /* note: oddly enough, ufrag and pwd can be empty strings */
    if (stream && ufrag && pwd)
    {

        strncpy(stream->remote_ufrag, ufrag, N_STREAM_MAX_UFRAG);
        strncpy(stream->remote_password, pwd, N_STREAM_MAX_PWD);

        ret = TRUE;
        goto done;
    }

done:
    //agent_unlock();
    nice_debug("[%s]: agent_unlock+++++++++++", G_STRFUNC);
    return ret;
}

int32_t n_agent_set_local_credentials(n_agent_t * agent, uint32_t stream_id, const char * ufrag, const char * pwd)
{
    n_stream_t * stream;
    int32_t ret = FALSE;

    //g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    //g_return_val_if_fail(stream_id >= 1, FALSE);

    nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    agent_lock();

    stream = agent_find_stream(agent, stream_id);

    /* note: oddly enough, ufrag and pwd can be empty strings */
    if (stream && ufrag && pwd)
    {
        strncpy(stream->local_ufrag, ufrag, N_STREAM_MAX_UFRAG);
        strncpy(stream->local_password, pwd, N_STREAM_MAX_PWD);

        ret = TRUE;
        goto done;
    }

done:
    agent_unlock();
    return ret;
}

int32_t n_agent_get_local_credentials(n_agent_t * agent, uint32_t stream_id, char ** ufrag, char ** pwd)
{
    n_stream_t * stream;
    int32_t ret = TRUE;

    //g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    //g_return_val_if_fail(stream_id >= 1, FALSE);

    //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
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

    *ufrag = n_strdup(stream->local_ufrag);
    *pwd = n_strdup(stream->local_password);
    ret = TRUE;

done:
    agent_unlock();
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
            nice_debug("[%s]: warning: unable to schedule any conn checks!", G_STRFUNC);
    }

    return added;
}


int32_t n_agent_set_remote_cands(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, const n_slist_t * candidates)
{
    int32_t added = 0;
    n_stream_t * stream;
    n_comp_t * component;

    //g_return_val_if_fail(NICE_IS_AGENT(agent), 0);
    //g_return_val_if_fail(stream_id >= 1, 0);
    //g_return_val_if_fail(component_id >= 1, 0);

    nice_debug("[%s]: set_remote_candidates %d %d", G_STRFUNC, stream_id, component_id);

    //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    /*agent_lock();*/

    if (!agent_find_comp(agent, stream_id, component_id, &stream, &component))
    {
        nice_debug("Could not find component %u in stream %u", component_id, stream_id);
        added = -1;
        goto done;
    }

    added = _set_remote_cands_locked(agent, stream, component, candidates);

done:
    /*agent_unlock();*/

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

        nice_debug("[%s]: Message %p (from: %p, length: %u)", G_STRFUNC, message, message->from, message->length);
        for (j = 0;
                (message->n_buffers >= 0 && j < (uint32_t) message->n_buffers) ||
                (message->n_buffers < 0 && message->buffers[j].buffer != NULL);
                j++)
        {
            n_invector_t * buffer = &message->buffers[j];

            nice_debug("[%s]: Buffer %p (length: %u)", G_STRFUNC, buffer->buffer, buffer->size);
        }
    }
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
        nice_debug("Dropped %u bytes of data from the end of "
                   "buffer %p (length: %u) due to not fitting in "
                   "message %p", buffer_length, buffer - message->length,
                   message->length + buffer_length, message);
    }

    return message->length;
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
 * @iter: a #n_input_msg_iter_t
 *
 * Reset the given @iter to point to the beginning of the array of messages.
 * This may be used both to initialise it and to reset it after use.
 *
 * Since: 0.1.5
 */
void n_input_msg_iter_reset(n_input_msg_iter_t * iter)
{
    iter->message = 0;
    iter->buffer = 0;
    iter->offset = 0;
}

/*
 * n_input_msg_iter_is_at_end:
 * @iter: a #n_input_msg_iter_t
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
int32_t n_input_msg_iter_is_at_end(n_input_msg_iter_t * iter, n_input_msg_t * messages, uint32_t n_messages)
{
    return (iter->message == n_messages && iter->buffer == 0 && iter->offset == 0);
}

/*
 * n_input_msg_iter_get_n_valid_msgs:
 * @iter: a #n_input_msg_iter_t
 *
 * Calculate the number of valid messages in the messages array. A valid message
 * is one which contains at least one valid byte of data in its buffers.
 *
 * Returns: number of valid messages (may be zero)
 *
 * Since: 0.1.5
 */
uint32_t n_input_msg_iter_get_n_valid_msgs(n_input_msg_iter_t * iter)
{
    if (iter->buffer == 0 && iter->offset == 0)
        return iter->message;
    else
        return iter->message + 1;
}

int32_t n_agent_send(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, uint32_t len, const char * buf)
{
    //n_outvector_t local_buf = { buf, len };
    //n_output_msg_t local_message = { &local_buf, 1 };
    //int32_t n_sent_bytes;

    //n_sent_bytes = n_send_msgs_nonblock_internal(agent, stream_id, component_id, &local_message, 1, FALSE, NULL);

    n_stream_t * stream;
    n_comp_t * comp;
    int32_t n_sent = -1; /* is in bytes if allow_partial is TRUE, otherwise in messages */

    agent_lock();

    if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
    {
        nice_debug("[%s]: Invalid stream/component", G_STRFUNC);
        goto done;
    }

    /* FIXME: Cancellation isnt yet supported, but it doesnt matter because
     * we only deal with non-blocking writes. */
    if (comp->selected_pair.local != NULL)
    {
        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN];
            nice_address_to_string(&comp->selected_pair.remote->addr, tmpbuf);

            nice_debug("[%s]: s%d:%d: sending %d bytes to [%s]:%d\n", G_STRFUNC, stream_id, comp_id, len, tmpbuf,
                       n_addr_get_port(&comp->selected_pair.remote->addr));
        }
        
        if (!pst_is_closed(comp->tcp))
        {
            /* Send on the pseudo-TCP socket. */
            //n_sent = pst_send_messages(comp->tcp, messages, n_messages, allow_partial, &child_error);
            n_sent = pst_send(comp->tcp, buf, len);
            adjust_tcp_clock(agent, stream, comp);

            if (!pst_can_send(comp->tcp))
            {
                //g_cancellable_reset(component->tcp_writable_cancellable);

            }

            if (n_sent < 0 /*&& !g_error_matches(child_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)*/)
            {
                /* Signal errors */
                _pseudo_tcp_error(agent, stream, comp);
            }
        }
        else
        {
            nice_debug("[%s]: Pseudo-TCP socket not connected", G_STRFUNC);
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
        //g_set_error_literal(&child_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK, g_strerror(EAGAIN));
        n_sent = -1;
    }

    //nice_debug("[%s]: n_sent: %d, n_messages: %u", G_STRFUNC,  n_sent, n_messages);

done:

    agent_unlock();

    return n_sent;
}

n_slist_t * n_agent_get_local_cands(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id)
{
    n_comp_t * comp;
    n_slist_t * ret = NULL;
    n_slist_t * item = NULL;

    agent_lock();

    if (!agent_find_comp(agent, stream_id, comp_id, NULL, &comp))
    {
        goto done;
    }

    for (item = comp->local_candidates; item; item = item->next)
        ret = n_slist_append(ret, nice_candidate_copy(item->data));

done:
    agent_unlock();
    return ret;
}


n_slist_t * n_agent_get_remote_cands(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id)
{
    n_comp_t * comp;
    n_slist_t * ret = NULL, *item = NULL;

    agent_lock();
    if (!agent_find_comp(agent, stream_id, comp_id, NULL, &comp))
    {
        goto done;
    }

    for (item = comp->remote_candidates; item; item = item->next)
        ret = n_slist_append(ret, nice_candidate_copy(item->data));

done:
    agent_unlock();
    return ret;
}

int32_t n_agent_restart(n_agent_t * agent)
{
    n_slist_t * i;

    //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
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

    agent_unlock();
    return TRUE;
}

int32_t n_agent_restart_stream(n_agent_t * agent, uint32_t stream_id)
{
    int32_t res = FALSE;
    n_stream_t * stream;

    //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
    agent_lock();

    stream = agent_find_stream(agent, stream_id);
    if (!stream)
    {
        nice_debug("Could not find  stream %u", stream_id);
        goto done;
    }

    /* step: reset local credentials for the stream and
     * clean up the list of remote candidates */
    stream_restart(agent, stream);

    res = TRUE;
done:
    agent_unlock();
    return res;
}


static void n_agent_dispose(n_agent_t * agent)
{
    n_slist_t * i;
    //QueuedSignal * sig;
    //n_agent_t * agent = NICE_AGENT(object);

    /* step: free resources for the binding discovery timers */
    disc_free(agent);
    //g_assert(agent->discovery_list == NULL);
    refresh_free(agent);
    //g_assert(agent->refresh_list == NULL);

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

    /*while ((sig = n_queue_pop_head(&agent->pending_signals)))
    {
        free_queued_signal(sig);
    }*/

    n_free(agent->stun_server_ip);
    agent->stun_server_ip = NULL;

    nice_rng_free(agent->rng);
    agent->rng = NULL;

}

#if 1

int32_t n_agent_attach_recv(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, n_agent_recv_func func, void * data)
{
	n_comp_t * comp = NULL;
	n_stream_t * stream = NULL;
	int32_t ret = FALSE;


	//nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
	agent_lock();

	/* attach candidates */

	/* step: check that params specify an existing pair */
	if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
	{
		nice_debug("Could not find component %u in stream %u", comp_id, stream_id);
		goto done;
	}

/*
	if (ctx == NULL)
		ctx = g_main_context_default();*/

	/* Set the component's I/O context. */
	//comp_set_io_context(component, ctx);
	comp_set_io_callback(comp, func, data);
	ret = TRUE;

	if (func)
	{
		/* If we got detached, maybe our readable callback didn't finish reading
		* all available data in the pseudotcp, so we need to make sure we free
		* our recv window, so the readable callback can be triggered again on the
		* next incoming data.
		* but only do this if we know we're already readable, otherwise we might
		* trigger an error in the initial, pre-connection attach. */
		if (agent->reliable && !pst_is_closed(comp->tcp) && comp->tcp_readable)
			pst_readable(comp->tcp, comp);
	}

done:
	agent_unlock();
	return ret;
}

/**/
int32_t agent_recv_packet(n_socket_source_t * s_source)
{
    int length;
    int fd = s_source->socket->sock_fd;
    //n_dlist_t * item;
    //n_input_msg_t * message;
    n_addr_t from;
    n_comp_t * comp;
    n_agent_t * agent;
    n_stream_t * stream;
    int32_t has_io_callback = 1;
    int32_t remove_source = FALSE;
	uint8_t local_header_buf[TCP_HEADER_SIZE] = {0};
	uint8_t local_body_buf[MAX_BUFFER_SIZE] = {0};
    n_invector_t local_bufs[] =
    {
        { local_header_buf, sizeof(local_header_buf) },
        { local_body_buf, sizeof(local_body_buf) },
    };
    n_input_msg_t local_message =
    {
        local_bufs, N_ELEMENTS(local_bufs), NULL, 0
    };
    n_recv_status_t retval = 0;

    agent_lock();

    comp = s_source->component;
    agent = comp->agent;
    stream = comp->stream;

    /*if (nread == -1)
    {
        nice_debug("[%s]: n_socket_t %p has received HUP", G_STRFUNC,  socket_source->socket);
        if (comp->selected_pair.local &&
                comp->selected_pair.local->sockptr == socket_source->socket &&
                comp->state == COMP_STATE_READY)
        {
            nice_debug("[%s]: Selected pair socket %p has HUP, declaring failed", G_STRFUNC, socket_source->socket);
            agent_sig_comp_state_change(agent, stream->id, comp->id, COMP_STATE_FAILED);
        }

        component_detach_socket(comp, socket_source->socket);
        agent_unlock();
        return -1;
    }*/

    //has_io_callback = component_has_io_callback(comp);

    if (pst_is_closed(comp->tcp))
    {
        nice_debug("[%s]: not handling incoming packet for s%d:%d "
                   "because pseudo-TCP socket does not exist in reliable mode.", G_STRFUNC,
                   stream->id, comp->id);
        remove_source = TRUE;
        goto done;
    }

    while (has_io_callback)
    {
        length = nice_socket_recv(fd, &from, MAX_BUFFER_SIZE, local_body_buf);

        if (nice_debug_is_enabled())
        {
            char tmpbuf[INET6_ADDRSTRLEN];
            nice_address_to_string(&from, tmpbuf);
            //nice_debug("[%s]: %d bytes received on [%d] from [%s:%u]", G_STRFUNC, length, fd, tmpbuf, n_addr_get_port(&from));
        }

#if 0
        for (item = comp->turn_servers; item; item = n_dlist_next(item))
        {
            turn_server_t * turn = item->data;
            n_slist_t * l = NULL;

            if (!nice_address_equal(&from, &turn->server))
                continue;

            nice_debug("[%s]: Packet received from TURN server candidate", G_STRFUNC);

            for (l = comp->local_candidates; l; l = l->next)
            {
                n_cand_t * cand = l->data;

                if (cand->type == CAND_TYPE_RELAYED && cand->stream_id == stream->id && cand->component_id == comp->id)
                {
                    retval = turn_message_parse(cand->sockptr, &nicesock, message);
                    break;
                }
            }
            break;
        }
#endif

        agent->media_after_tick = TRUE;

        if (stun_msg_valid_buflen_fast(local_body_buf, length, 1) == length)
        {
            int32_t validated_len;

            validated_len = stun_msg_valid_buflen(local_body_buf, length, 1);

            if (validated_len == (int32_t) length)
            {
                int32_t handled;

                handled = cocheck_handle_in_stun(agent, stream, comp, s_source->socket, &from, (char *)local_body_buf, validated_len);

                if (handled)
                {
                    /* Handled STUN message. */
                    //nice_debug("[%s]: Valid STUN packet received.", G_STRFUNC);
                    retval = RECV_OOB;
                    goto done;
                }
            }

            nice_debug("[%s]: Packet passed fast STUN validation but failed slow validation.", G_STRFUNC);

        }

        /* 不是stun数据包，尝试按照TCP数据包处理 */
        if (length > 0)
        {
            if (!pst_is_closed(comp->tcp))
            {
                if (comp->selected_pair.local == NULL)
                {
                    n_outvector_t * vec = n_slice_new(n_outvector_t);
                    //vec->buffer = compact_input_message(message, &vec->size);
					vec->buffer = n_slice_copy(length, local_body_buf);
					vec->size = length;
                    n_queue_push_tail(&comp->queued_tcp_packets, vec);
                    nice_debug("%s: queued %d bytes for n_outvector_t %p", G_STRFUNC, length, vec);

                    return RECV_OOB;
                }
                else
                {
                    process_queued_tcp_packets(agent, stream, comp);
                }

                /* Received data on a reliable connection. */

                nice_debug("%s: notifying pseudo-TCP of packet, length %u", G_STRFUNC, length);
                //pst_notify_message(comp->tcp, local_body_buf, length);
				pst_notify_packet(comp->tcp, local_body_buf, length);

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
    }

done:
    agent_unlock();
    return retval;
}

#define ANENT_MAX_FD 128

void _agent_worker(void * arg)
{
    n_comp_t * comp = (n_comp_t *)arg;
    int poll_delay = 10;
    int n, i, fd_idx = 0;
    n_slist_t  * l;
    n_socket_source_t * s_source, * s_srcs[ANENT_MAX_FD] = {NULL};
    struct pollfd p[ANENT_MAX_FD] = {0};


    while (1)
    {
        for (l = comp->socket_srcs_slist; l != NULL; l = l->next)
        {
            s_source = l->data;

            if (s_source->socket && s_source->socket->sock_fd > 0)
            {
                p[fd_idx].fd = s_source->socket->sock_fd;
                p[fd_idx].events = POLLIN;
                s_srcs[fd_idx] = s_source;
                if (fd_idx < ANENT_MAX_FD)
                {
                    fd_idx++;
                }
                else
                {
                    nice_debug("pollfd is overload\n");
                }
            }
        }

        n = _poll(p, fd_idx, poll_delay);
        if (n > 0)
        {
            for (i = 0; i < fd_idx; i++)
            {
                if (!(p[i].revents & POLLIN))
                    continue;
                agent_recv_packet(s_srcs[i]);
            }
        }
        else if (n < 0)
        {
            nice_debug("poll err = %d\n", net_errno());
            continue;
        }
        else
        {
            sleep_ms(1);
        }
        fd_idx = 0;
    }
}

int32_t n_agent_dispatcher(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id)
{
    n_comp_t * comp = NULL;
    n_stream_t * stream = NULL;
    int32_t ret = -1;

    if (!agent_find_comp(agent, stream_id, comp_id, &stream, &comp))
    {
        nice_debug("could not find component %u in stream %u", comp_id, stream_id);
    }

    ret = pthread_create(&worker_tid, 0, (void *)_agent_worker, (void *)comp);

    return ret;
}

#endif

int32_t n_agent_set_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, const char * lfoundation, const char * rfoundation)
{
    n_comp_t * component;
    n_stream_t * stream;
    n_cand_pair_t pair;
    int32_t ret = FALSE;

    //g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    //g_return_val_if_fail(stream_id >= 1, FALSE);
    //g_return_val_if_fail(component_id >= 1, FALSE);
    //g_return_val_if_fail(lfoundation, FALSE);
    //g_return_val_if_fail(rfoundation, FALSE);

    //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
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

    if  (pst_is_closed(component->tcp))
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
    agent_unlock();
    return ret;
}

int32_t n_agent_get_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t ** local, n_cand_t ** remote)
{
    n_comp_t * component;
    n_stream_t * stream;
    int32_t ret = FALSE;

    //g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    //g_return_val_if_fail(stream_id >= 1, FALSE);
    //g_return_val_if_fail(component_id >= 1, FALSE);
    //g_return_val_if_fail(local != NULL, FALSE);
    //g_return_val_if_fail(remote != NULL, FALSE);

    //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
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
    agent_unlock();

    return ret;
}

int32_t n_agent_set_selected_rcand(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t * candidate)
{
    n_comp_t * component;
    n_stream_t * stream;
    n_cand_t * lcandidate = NULL;
    int32_t ret = FALSE;
    n_cand_t * local = NULL, *remote = NULL;
    uint64_t priority;

    //g_return_val_if_fail(NICE_IS_AGENT(agent), FALSE);
    //g_return_val_if_fail(stream_id != 0, FALSE);
    //g_return_val_if_fail(component_id != 0, FALSE);
    //g_return_val_if_fail(candidate != NULL, FALSE);

    //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
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

    if (agent->reliable && pst_is_closed(component->tcp))
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
    agent_unlock();
    return ret;
}

void _set_socket_tos(n_agent_t * agent, n_socket_t * sock, int32_t tos)
{
    if (setsockopt(sock->sock_fd, IPPROTO_IP, IP_TOS, (const char *) &tos, sizeof(tos)) < 0)
    {
        nice_debug("[%s]: Could not set socket ToS: %d", G_STRFUNC, errno);
    }
}


void n_agent_set_stream_tos(n_agent_t * agent, uint32_t stream_id, int32_t tos)
{
    n_slist_t * i, *j;
    n_stream_t * stream;

    //g_return_if_fail(NICE_IS_AGENT(agent));
    //g_return_if_fail(stream_id >= 1);

    nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
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
    agent_unlock();
}

int32_t n_agent_forget_relays(n_agent_t * agent, uint32_t stream_id, uint32_t component_id)
{
    n_comp_t * component;
    int32_t ret = TRUE;

    agent_lock();

    if (!agent_find_comp(agent, stream_id, component_id, NULL, &component))
    {
        ret = FALSE;
        goto done;
    }

    component_clean_turn_servers(component);

done:
    agent_unlock();

    return ret;
}

int32_t agent_socket_send(n_socket_t * sock, n_addr_t * addr, uint32_t len, char * buf)
{
    int32_t ret;
    ret = nice_socket_send(sock, addr, len, buf);
    return ret;
}

n_comp_state_e n_agent_get_comp_state(n_agent_t * agent, uint32_t stream_id, uint32_t component_id)
{
    n_comp_state_e state = COMP_STATE_FAILED;
    n_comp_t * component;

    //nice_debug("[%s]: agent_lock+++++++++++", G_STRFUNC);
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
                   tmpbuf1, n_addr_get_port(&l_cand->addr),
                   tmpbuf2, n_addr_get_port(&r_cand->addr));
    }
}
