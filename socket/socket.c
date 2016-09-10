/* This file is part of the Nice GLib ICE library. */

#include "config.h"
#include <glib.h>
#include "socket.h"
#include "socket-priv.h"
#include "agent-priv.h"
#include "nqueue.h"
#include <string.h>

#ifndef G_OS_WIN32
#include <unistd.h>
#endif

typedef struct _NiceSocketQueuedSend NiceSocketQueuedSend;

struct _NiceSocketQueuedSend
{
    uint8_t * buf; /* owned */
    uint32_t length;
    n_addr_t to;
};

/**
 * n_socket_recv_msgs:
 * @sock: a #n_socket_t
 * @recv_messages: (array length=n_recv_messages) (out caller-allocates):
 * array of #NiceInputMessages to return received messages in
 * @n_recv_messages: number of elements in the @recv_messages array
 *
 * Receive up to @n_recv_messages message on the socket, in a non-reliable,
 * non-blocking fashion. The total size of the buffers in each #n_input_msg_t
 * must be big enough to contain an entire message (65536 bytes), or excess
 * bytes will be silently dropped.
 *
 * On success, the number of messages received into @recv_messages is returned,
 * which may be less than @n_recv_messages if the call would have blocked
 * part-way through. If the socket would have blocked to begin with, or if
 * @n_recv_messages is zero, zero is returned. On failure, a negative value is
 * returned, but no further error information is available. Calling this
 * function on a socket which has closed is an error, and a negative value is
 * returned.
 *
 * If a positive N is returned, the first N messages in @recv_messages are
 * valid. Each valid message is guaranteed to have a non-zero
 * #n_input_msg_t::length, and its buffers are guaranteed to be filled
 * sequentially up to that number of bytes  If #n_input_msg_t::from was
 * non-%NULL for a valid message, it may be set to the address of the sender of
 * that received message.
 *
 * If the return value is zero or negative, the from return address and length
 * in every #n_input_msg_t in @recv_messages are guaranteed to be unmodified.
 * The buffers may have been modified.
 *
 * The base addresses and sizes of the buffers in a #n_input_msg_t are never
 * modified. Neither is the base address of #n_input_msg_t::from, nor the
 * base address and length of the #n_input_msg_t::buffers array.
 *
 * Returns: number of valid messages returned in @recv_messages, or a negative
 * value on error
 *
 * Since: 0.1.5
 */
int32_t n_socket_recv_msgs(n_socket_t * sock, n_input_msg_t * recv_messages, uint32_t n_recv_messages)
{
    g_return_val_if_fail(sock != NULL, -1);
    g_return_val_if_fail(n_recv_messages == 0 || recv_messages != NULL, -1);

    return sock->recv_messages(sock, recv_messages, n_recv_messages);
}

/**
 * nice_socket_send_messages:
 * @sock: a #n_socket_t
 * @messages: (array length=n_messages) (in caller-allocates):
 * array of #NiceOutputMessages containing the messages to send
 * @n_messages: number of elements in the @messages array
 *
 * Send up to @n_messages on the socket, in a non-reliable, non-blocking
 * fashion. The total size of the buffers in each #n_output_msg_t
 * must be at most the maximum UDP payload size (65535 bytes), or excess
 * bytes will be silently dropped.
 *
 * On success, the number of messages transmitted from @messages is returned,
 * which may be less than @n_messages if the call would have blocked
 * part-way through. If the socket would have blocked to begin with, or if
 * @n_messages is zero, zero is returned. On failure, a negative value is
 * returned, but no further error information is available. Calling this
 * function on a socket which has closed is an error, and a negative value is
 * returned.
 *
 * If a positive N is returned, the first N messages in @messages have been
 * sent in full, and the remaining messages have not been sent at all.
 *
 * If #n_output_msg_t::to is specified for a message, that will be used as
 * the destination address for the message. Otherwise, if %NULL, the default
 * destination for @sock will be used.
 *
 * Every field of every #n_output_msg_t is guaranteed to be unmodified when
 * this function returns.
 *
 * Returns: number of messages successfully sent from @messages, or a negative
 * value on error
 *
 * Since: 0.1.5
 */
int32_t nice_socket_send_messages(n_socket_t * sock, const n_addr_t * to, const n_output_msg_t * messages, uint32_t n_messages)
{
    g_return_val_if_fail(sock != NULL, -1);
    g_return_val_if_fail(n_messages == 0 || messages != NULL, -1);

    return sock->send_messages(sock, to, messages, n_messages);
}

/**
 * nice_socket_send_messages_reliable:
 * @sock: a #n_socket_t
 * @messages: (array length=n_messages) (in caller-allocates):
 * array of #NiceOutputMessages containing the messages to send
 * @n_messages: number of elements in the @messages array
 *
 * Send @n_messages on the socket, in a reliable, non-blocking fashion.
 * The total size of the buffers in each #n_output_msg_t
 * must be at most the maximum UDP payload size (65535 bytes), or excess
 * bytes will be silently dropped.
 *
 * On success, the number of messages transmitted from @messages is returned,
 * which will be equal to @n_messages. If the call would have blocked part-way
 * though, the remaining bytes will be queued for sending later.
 * On failure, a negative value is returned, but no further error information
 * is available. Calling this function on a socket which has closed is an error,
 * and a negative value is returned. Calling this function on a socket which
 * is not TCP or does not have a TCP base socket, will result in an error.
 *
 * If #n_output_msg_t::to is specified for a message, that will be used as
 * the destination address for the message. Otherwise, if %NULL, the default
 * destination for @sock will be used.
 *
 * Every field of every #n_output_msg_t is guaranteed to be unmodified when
 * this function returns.
 *
 * Returns: number of messages successfully sent from @messages, or a negative
 * value on error
 *
 * Since: 0.1.5
 */
int32_t nice_socket_send_messages_reliable(n_socket_t * sock, const n_addr_t * to,
                                   const n_output_msg_t * messages, uint32_t n_messages)
{
    g_return_val_if_fail(sock != NULL, -1);
    g_return_val_if_fail(n_messages == 0 || messages != NULL, -1);

    return sock->send_messages_reliable(sock, to, messages, n_messages);
}

/* Convenience wrapper around n_socket_recv_msgs(). Returns the number of
 * bytes received on success (which will be @len), zero if sending would block, or
 * -1 on error. */
int32_t nice_socket_recv(n_socket_t * sock, n_addr_t * from, uint32_t len, char * buf)
{
    n_invector_t local_buf = { buf, len };
    n_input_msg_t local_message = { &local_buf, 1, from, 0};
    int32_t ret;

    ret = sock->recv_messages(sock, &local_message, 1);
    if (ret == 1)
        return local_message.length;
    return ret;
}

/* Convenience wrapper around nice_socket_send_messages(). Returns the number of
 * bytes sent on success (which will be @len), zero if sending would block, or
 * -1 on error. */
int32_t nice_socket_send(n_socket_t * sock, const n_addr_t * to, uint32_t len, const char * buf)
{
    n_outvector_t local_buf = { buf, len };
    n_output_msg_t local_message = { &local_buf, 1};
    int32_t ret;

    ret = sock->send_messages(sock, to, &local_message, 1);
    if (ret == 1)
        return len;
    return ret;
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
