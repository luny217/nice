/* This file is part of the Nice GLib ICE library. */

#include "config.h"
//#include <glib.h>
#include "socket.h"
#include "socket-priv.h"
#include "agent-priv.h"
#include "nqueue.h"
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

struct udp_socket_private_st
{
    n_addr_t niceaddr;
    uv_udp_t * gaddr;
};

int32_t n_socket_recv_msgs(n_socket_t * sock, n_input_msg_t * recv_messages, uint32_t n_recv_messages)
{
    return sock->recv_messages(sock, recv_messages, n_recv_messages);
}

int32_t nice_socket_send_messages(n_socket_t * sock, const n_addr_t * to, const n_output_msg_t * messages, uint32_t n_messages)
{
    return sock->send_messages(sock, to, messages, n_messages);
}

int32_t nice_socket_send_messages_reliable(n_socket_t * sock, const n_addr_t * to,
        const n_output_msg_t * messages, uint32_t n_messages)
{

    return sock->send_messages_reliable(sock, to, messages, n_messages);
}

int32_t nice_socket_recv(n_socket_t * sock, n_addr_t * from, uint32_t len, char * buf)
{
    int32_t ret;
    struct sockaddr_in sender_addr;
    int addr_size = sizeof(sender_addr);

    //ret = sock->recv_messages(sock, &local_message, 1);
	ret = recvfrom(sock->fileno, buf, len, 0, (struct sockaddr *)&sender_addr, &addr_size);
    if (ret < 0)
    {
        
    }
    return ret;
}

int32_t nice_socket_send(n_socket_t * sock, n_addr_t * to, uint32_t len, char * buf)
{
    struct udp_socket_private_st * priv = sock->priv;

    /* Socket has been closed: */
    if (priv == NULL)
        return -1;

    if (!nice_address_equal(&priv->niceaddr, to))
    {
        union
        {
            struct sockaddr_storage storage;
            struct sockaddr addr;
        } sa;

        nice_address_copy_to_sockaddr(to, &sa.addr);
        priv->niceaddr = *to;
        len = sendto(sock->fileno, buf, len, 0, (struct sockaddr *)&sa.addr, sizeof(sa.addr));
        if (len < 0)
        {

        }
    }
    return len;
}

int nice_socket_is_reliable(n_socket_t * sock)
{
    return sock->is_reliable(sock);
}

int nice_socket_can_send(n_socket_t * sock, n_addr_t * addr)
{
    if (sock->can_send)
        return sock->can_send(sock, addr);
    return TRUE;
}

void nice_socket_set_writable_callback(n_socket_t * sock, NiceSocketWritableCb callback, void * user_data)
{
    if (sock->set_writable_callback)
        sock->set_writable_callback(sock, callback, user_data);
}

void nice_socket_free(n_socket_t * sock)
{
    if (sock)
    {
        sock->close(sock);
        n_slice_free(n_socket_t, sock);
    }
}
