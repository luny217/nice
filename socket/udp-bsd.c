/* This file is part of the Nice GLib ICE library. */

/*
 * Implementation of UDP socket interface using Berkeley sockets. (See
 * http://en.wikipedia.org/wiki/Berkeley_sockets.)
 */
#include "config.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "udp-bsd.h"

#ifndef G_OS_WIN32
#include <unistd.h>
#endif

//#include "uv.h"

static void socket_close(n_socket_t * sock);
static int32_t socket_recv_messages(n_socket_t * sock, n_input_msg_t * recv_messages, uint32_t n_recv_messages);
static int32_t socket_send_messages(n_socket_t * sock, const n_addr_t * to, const n_output_msg_t * messages, uint32_t n_messages);
//static int32_t socket_send_messages_reliable(n_socket_t * sock, const n_addr_t * to, const n_output_msg_t * messages, uint32_t n_messages);
static int socket_is_reliable(n_socket_t * sock);
static int socket_can_send(n_socket_t * sock, n_addr_t * addr);
static void socket_set_writable_callback(n_socket_t * sock, NiceSocketWritableCb callback, void * user_data);

struct udp_socket_private_st
{
    n_addr_t niceaddr;
    uv_udp_t * gaddr;
};

n_socket_t * n_udp_socket_new(n_addr_t * addr)
{
    union
    {
        struct sockaddr_storage storage;
        struct sockaddr addr;
    } name;
    
    n_socket_t * sock = n_slice_new0(n_socket_t);
    //GSocket * gsock = NULL;
    int gsock;
    int gret = FALSE;
    //GSocketAddress * gaddr;
    struct udp_socket_private_st * priv;
    int ret;

    if (addr != NULL)
    {
        nice_address_copy_to_sockaddr(addr, &name.addr);
    }
    else
    {
        memset(&name, 0, sizeof(name));
        name.storage.ss_family = AF_UNSPEC;
    }

    if (name.storage.ss_family == AF_UNSPEC || name.storage.ss_family == AF_INET)
    {
        //gsock = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, NULL);
        //ret = uv_udp_init_ex(uv_default_loop(), &sock->fileno, AF_INET);
        gsock = socket(AF_INET, SOCK_DGRAM, 0);
        name.storage.ss_family = AF_INET;
    }

    if (gsock == -1)
    {
        n_slice_free(n_socket_t, sock);
        return NULL;
    }

    /* GSocket: All socket file descriptors are set to be close-on-exec. */
    /*g_socket_set_blocking(gsock, false);
    gaddr = g_socket_address_new_from_native(&name.addr, sizeof(name));
    if (gaddr != NULL)
    {
        gret = g_socket_bind(gsock, gaddr, FALSE, NULL);
        g_object_unref(gaddr);
    }*/

    ret = bind(gsock, (struct sockaddr *)&name.addr, 0);

    if (ret < 0)
    {
        n_slice_free(n_socket_t, sock);
        //g_socket_close(gsock, NULL);
        //g_object_unref(gsock);
        return NULL;
    }

    /*
        gaddr = g_socket_get_local_address(gsock, NULL);
        if (gaddr == NULL || !g_socket_address_to_native(gaddr, &name.addr, sizeof(name), NULL))
        {
            n_slice_free(n_socket_t, sock);
            g_socket_close(gsock, NULL);
            g_object_unref(gsock);
            return NULL;
        }

        g_object_unref(gaddr);*/

    n_addr_set_from_sock(&sock->addr, &name.addr);

    priv = sock->priv = n_slice_new0(struct udp_socket_private_st);
    nice_address_init(&priv->niceaddr);

    sock->type = NICE_SOCKET_TYPE_UDP_BSD;
    sock->sock_fd = gsock;
    sock->send_messages = socket_send_messages;
    sock->recv_messages = socket_recv_messages;
    sock->is_reliable = socket_is_reliable;
    sock->can_send = socket_can_send;
    sock->set_writable_callback = socket_set_writable_callback;
    sock->close = socket_close;

    return sock;
}

static void socket_close(n_socket_t * sock)
{
    struct udp_socket_private_st * priv = sock->priv;

    /*if (priv->gaddr)
        g_object_unref(priv->gaddr);*/
    n_slice_free(struct udp_socket_private_st, sock->priv);
    sock->priv = NULL;

    /*
        if (sock->fileno)
        {
            //g_socket_close(sock->fileno, NULL);
            //g_object_unref(sock->fileno);
            //sock->fileno = NULL;
        }*/
}

static int32_t socket_recv_messages(n_socket_t * sock, n_input_msg_t * recv_messages, uint32_t n_recv_messages)
{
    uint32_t i = 0;
    int error = FALSE;

    /* Socket has been closed: */
    if (sock->priv == NULL)
        return 0;

#if 0
    /* Read messages into recv_messages until one fails or would block, or we
     * reach the end. */
    for (i = 0; i < n_recv_messages; i++)
    {
        n_input_msg_t * recv_message = &recv_messages[i];
        GSocketAddress * gaddr = NULL;
        GError * gerr = NULL;
        int32_t recvd;
        int32_t flags = G_SOCKET_MSG_NONE;

        recvd = g_socket_receive_message(sock->fileno,
                                         (recv_message->from != NULL) ? &gaddr : NULL,
                                         recv_message->buffers, recv_message->n_buffers, NULL, NULL,
                                         &flags, NULL, &gerr);

        recv_message->length = MAX(recvd, 0);

        if (recvd < 0)
        {
            if (g_error_matches(gerr, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
                recvd = 0;
            else
                error = TRUE;

            g_error_free(gerr);
        }

        if (recvd > 0 && recv_message->from != NULL && gaddr != NULL)
        {
            union
            {
                struct sockaddr_storage storage;
                struct sockaddr addr;
            } sa;

            g_socket_address_to_native(gaddr, &sa.addr, sizeof(sa), NULL);
            n_addr_set_from_sock(recv_message->from, &sa.addr);
        }

        if (gaddr != NULL)
            g_object_unref(gaddr);

        /* Return early on error or EWOULDBLOCK. */
        if (recvd <= 0)
            break;
    }
#endif

    /* Was there an error processing the first message? */
    if (error && i == 0)
        return -1;

    return i;
}

static int32_t socket_send_message(n_socket_t * sock, const n_addr_t * to, const n_output_msg_t * msg)
{
    struct udp_socket_private_st * priv = sock->priv;
    int32_t len = -1;

    /* Socket has been closed: */
    if (priv == NULL)
        return -1;

   #if 0
 if (!n_addr_is_valid(&priv->niceaddr) || !nice_address_equal(&priv->niceaddr, to))
    {
        union
        {
            struct sockaddr_storage storage;
            struct sockaddr addr;
        } sa;
        //GSocketAddress * gaddr;

        /*if (priv->gaddr)
            g_object_unref(priv->gaddr);*/

        nice_address_copy_to_sockaddr(to, &sa.addr);
        /*gaddr = g_socket_address_new_from_native(&sa.addr, sizeof(sa));
        priv->gaddr = gaddr;*/

        /*if (gaddr == NULL)
            return -1;*/

        priv->niceaddr = *to;

        len = sendto(sock->fileno, msg->buffers, msg->n_buffers, (struct sockaddr *)&sa.addr, NULL);

        if (len < 0)
        {
            /*if (g_error_matches(child_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
            len = 0;

            g_error_free(child_error);*/
        }
    }
#endif

    // len = g_socket_send_message(sock->fileno, priv->gaddr, msg->buffers, msg->n_buffers, NULL, 0, G_SOCKET_MSG_NONE, NULL, &child_error);

    return len;
}

static int32_t socket_send_messages(n_socket_t * sock, const n_addr_t * to, const n_output_msg_t * messages, uint32_t n_messages)
{
    uint32_t i;

    /* Socket has been closed: */
    if (sock->priv == NULL)
        return -1;

    for (i = 0; i < n_messages; i++)
    {
        const n_output_msg_t * message = &messages[i];
        int32_t len;

        len = socket_send_message(sock, to, message);

        if (len < 0)
        {
            /* Error. */
            if (i > 0)
                break;
            return len;
        }
        else if (len == 0)
        {
            /* EWOULDBLOCK. */
            break;
        }
    }

    return i;
}

static int socket_is_reliable(n_socket_t * sock)
{
    return FALSE;
}

static int socket_can_send(n_socket_t * sock, n_addr_t * addr)
{
    return TRUE;
}

static void socket_set_writable_callback(n_socket_t * sock, NiceSocketWritableCb callback, void * user_data)
{
}

