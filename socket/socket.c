/* This file is part of the Nice GLib ICE library. */

#include "config.h"
#include <glib.h>
#include "socket.h"
#include "socket-priv.h"
#include "agent-priv.h"

#include <string.h>

#ifndef G_OS_WIN32
#include <unistd.h>
#endif

typedef struct _NiceSocketQueuedSend NiceSocketQueuedSend;

struct _NiceSocketQueuedSend
{
    uint8_t * buf; /* owned */
    uint32_t length;
    NiceAddress to;
};

/**
 * nice_socket_recv_messages:
 * @sock: a #NiceSocket
 * @recv_messages: (array length=n_recv_messages) (out caller-allocates):
 * array of #NiceInputMessages to return received messages in
 * @n_recv_messages: number of elements in the @recv_messages array
 *
 * Receive up to @n_recv_messages message on the socket, in a non-reliable,
 * non-blocking fashion. The total size of the buffers in each #NiceInputMessage
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
 * #NiceInputMessage::length, and its buffers are guaranteed to be filled
 * sequentially up to that number of bytes  If #NiceInputMessage::from was
 * non-%NULL for a valid message, it may be set to the address of the sender of
 * that received message.
 *
 * If the return value is zero or negative, the from return address and length
 * in every #NiceInputMessage in @recv_messages are guaranteed to be unmodified.
 * The buffers may have been modified.
 *
 * The base addresses and sizes of the buffers in a #NiceInputMessage are never
 * modified. Neither is the base address of #NiceInputMessage::from, nor the
 * base address and length of the #NiceInputMessage::buffers array.
 *
 * Returns: number of valid messages returned in @recv_messages, or a negative
 * value on error
 *
 * Since: 0.1.5
 */
int32_t
nice_socket_recv_messages(NiceSocket * sock,
                          NiceInputMessage * recv_messages, uint32_t n_recv_messages)
{
    g_return_val_if_fail(sock != NULL, -1);
    g_return_val_if_fail(n_recv_messages == 0 || recv_messages != NULL, -1);

    return sock->recv_messages(sock, recv_messages, n_recv_messages);
}

/**
 * nice_socket_send_messages:
 * @sock: a #NiceSocket
 * @messages: (array length=n_messages) (in caller-allocates):
 * array of #NiceOutputMessages containing the messages to send
 * @n_messages: number of elements in the @messages array
 *
 * Send up to @n_messages on the socket, in a non-reliable, non-blocking
 * fashion. The total size of the buffers in each #NiceOutputMessage
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
 * If #NiceOutputMessage::to is specified for a message, that will be used as
 * the destination address for the message. Otherwise, if %NULL, the default
 * destination for @sock will be used.
 *
 * Every field of every #NiceOutputMessage is guaranteed to be unmodified when
 * this function returns.
 *
 * Returns: number of messages successfully sent from @messages, or a negative
 * value on error
 *
 * Since: 0.1.5
 */
int32_t nice_socket_send_messages(NiceSocket * sock, const NiceAddress * to, const NiceOutputMessage * messages, uint32_t n_messages)
{
    g_return_val_if_fail(sock != NULL, -1);
    g_return_val_if_fail(n_messages == 0 || messages != NULL, -1);

    return sock->send_messages(sock, to, messages, n_messages);
}

/**
 * nice_socket_send_messages_reliable:
 * @sock: a #NiceSocket
 * @messages: (array length=n_messages) (in caller-allocates):
 * array of #NiceOutputMessages containing the messages to send
 * @n_messages: number of elements in the @messages array
 *
 * Send @n_messages on the socket, in a reliable, non-blocking fashion.
 * The total size of the buffers in each #NiceOutputMessage
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
 * If #NiceOutputMessage::to is specified for a message, that will be used as
 * the destination address for the message. Otherwise, if %NULL, the default
 * destination for @sock will be used.
 *
 * Every field of every #NiceOutputMessage is guaranteed to be unmodified when
 * this function returns.
 *
 * Returns: number of messages successfully sent from @messages, or a negative
 * value on error
 *
 * Since: 0.1.5
 */
int32_t nice_socket_send_messages_reliable(NiceSocket * sock, const NiceAddress * to,
                                   const NiceOutputMessage * messages, uint32_t n_messages)
{
    g_return_val_if_fail(sock != NULL, -1);
    g_return_val_if_fail(n_messages == 0 || messages != NULL, -1);

    return sock->send_messages_reliable(sock, to, messages, n_messages);
}

/* Convenience wrapper around nice_socket_recv_messages(). Returns the number of
 * bytes received on success (which will be @len), zero if sending would block, or
 * -1 on error. */
int32_t nice_socket_recv(NiceSocket * sock, NiceAddress * from, uint32_t len, gchar * buf)
{
    GInputVector local_buf = { buf, len };
    NiceInputMessage local_message = { &local_buf, 1, from, 0};
    int32_t ret;

    ret = sock->recv_messages(sock, &local_message, 1);
    if (ret == 1)
        return local_message.length;
    return ret;
}

/* Convenience wrapper around nice_socket_send_messages(). Returns the number of
 * bytes sent on success (which will be @len), zero if sending would block, or
 * -1 on error. */
int32_t
nice_socket_send(NiceSocket * sock, const NiceAddress * to, uint32_t len,
                 const gchar * buf)
{
    GOutputVector local_buf = { buf, len };
    NiceOutputMessage local_message = { &local_buf, 1};
    int32_t ret;

    ret = sock->send_messages(sock, to, &local_message, 1);
    if (ret == 1)
        return len;
    return ret;
}

int32_t
nice_socket_send_reliable(NiceSocket * sock, const NiceAddress * to, uint32_t len,
                          const gchar * buf)
{
    GOutputVector local_buf = { buf, len };
    NiceOutputMessage local_message = { &local_buf, 1};
    int32_t ret;

    ret = sock->send_messages_reliable(sock, to, &local_message, 1);
    if (ret == 1)
        return len;
    return ret;
}

int
nice_socket_is_reliable(NiceSocket * sock)
{
    return sock->is_reliable(sock);
}

int
nice_socket_can_send(NiceSocket * sock, NiceAddress * addr)
{
    if (sock->can_send)
        return sock->can_send(sock, addr);
    return TRUE;
}

void
nice_socket_set_writable_callback(NiceSocket * sock,
                                  NiceSocketWritableCb callback, void * user_data)
{
    if (sock->set_writable_callback)
        sock->set_writable_callback(sock, callback, user_data);
}

void
nice_socket_free(NiceSocket * sock)
{
    if (sock)
    {
        sock->close(sock);
        g_slice_free(NiceSocket, sock);
    }
}

static void
nice_socket_free_queued_send(NiceSocketQueuedSend * tbs)
{
    g_free(tbs->buf);
    g_slice_free(NiceSocketQueuedSend, tbs);
}

void
nice_socket_queue_send(GQueue * send_queue, const NiceAddress * to,
                       const NiceOutputMessage * messages, uint32_t n_messages)
{
    uint32_t i;

    if (n_messages == 0)
        return;

    /* Compact the message?s buffers before queueing. */
    for (i = 0; i < n_messages; i++)
    {
        NiceSocketQueuedSend * tbs;
        const NiceOutputMessage * message = &messages[i];
        uint32_t message_len_remaining = output_message_get_size(message);
        uint32_t j;
        uint32_t offset = 0;

        if (message_len_remaining == 0)
            continue;

        /* Compact the buffer. */
        tbs = g_slice_new0(NiceSocketQueuedSend);
        tbs->buf = g_malloc(message_len_remaining);
        tbs->length = message_len_remaining;

        if (to)
            tbs->to = *to;
        else
            memset(&tbs->to, 0, sizeof(NiceAddress));
        g_queue_push_tail(send_queue, tbs);

        for (j = 0;
                (message->n_buffers >= 0 && j < (uint32_t) message->n_buffers) ||
                (message->n_buffers < 0 && message->buffers[j].buffer != NULL);
                j++)
        {
            const GOutputVector * buffer = &message->buffers[j];
            uint32_t len;

            len = MIN(buffer->size, message_len_remaining);
            memcpy(tbs->buf + offset, buffer->buffer, len);
            message_len_remaining -= len;
            offset += len;
        }

        g_assert(offset == tbs->length);
    }
}

void nice_socket_queue_send_with_callback(GQueue * send_queue,
        const NiceOutputMessage * message, uint32_t message_offset, uint32_t message_len,
        int head, GSocket * gsock, GSource ** io_source, GMainContext * context,
        GSourceFunc cb, void * user_data)
{
    NiceSocketQueuedSend * tbs;
    uint32_t j;
    uint32_t offset = 0;

    if (message_offset >= message_len)
        return;

    tbs = g_slice_new0(NiceSocketQueuedSend);
    tbs->length = message_len - message_offset;
    tbs->buf = g_malloc(tbs->length);

    if (head)
        g_queue_push_head(send_queue, tbs);
    else
        g_queue_push_tail(send_queue, tbs);

    if (io_source && gsock && context && cb && *io_source == NULL)
    {
        *io_source = g_socket_create_source(gsock, G_IO_OUT, NULL);
        g_source_set_callback(*io_source, (GSourceFunc) cb, user_data, NULL);
        g_source_attach(*io_source, context);
    }

    /* Move the data into the buffer. */
    for (j = 0;
            (message->n_buffers >= 0 && j < (uint32_t) message->n_buffers) ||
            (message->n_buffers < 0 && message->buffers[j].buffer != NULL);
            j++)
    {
        const GOutputVector * buffer = &message->buffers[j];
        uint32_t len;

        /* Skip this buffer if it?s within @message_offset. */
        if (buffer->size <= message_offset)
        {
            message_offset -= buffer->size;
            continue;
        }

        len = MIN(tbs->length - offset, buffer->size - message_offset);
        memcpy(tbs->buf + offset, (uint8_t *) buffer->buffer + message_offset, len);
        offset += len;
        if (message_offset >= len)
            message_offset -= len;
        else
            message_offset = 0;
    }
}

void nice_socket_flush_send_queue(NiceSocket * base_socket, GQueue * send_queue)
{
    NiceSocketQueuedSend * tbs;

    while ((tbs = g_queue_pop_head(send_queue)))
    {
        NiceAddress * to = &tbs->to;

        if (!nice_address_is_valid(to))
            to = NULL;

        /* We only queue reliable data */
        nice_socket_send_reliable(base_socket, to,
                                  tbs->length, (const gchar *) tbs->buf);
        nice_socket_free_queued_send(tbs);
    }
}

int nice_socket_flush_send_queue_to_socket(GSocket * gsock,
        GQueue * send_queue)
{
    NiceSocketQueuedSend * tbs;
    GError * gerr = NULL;


    while ((tbs = g_queue_pop_head(send_queue)) != NULL)
    {
        int ret;

        GOutputVector local_bufs = { tbs->buf, tbs->length };
        ret = g_socket_send_message(gsock, NULL, &local_bufs, 1, NULL, 0,
                                    G_SOCKET_MSG_NONE, NULL, &gerr);

        if (ret < 0)
        {
            if (g_error_matches(gerr, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
            {
                GOutputVector local_buf = { tbs->buf, tbs->length };
                NiceOutputMessage local_message = {&local_buf, 1};

                nice_socket_queue_send_with_callback(send_queue, &local_message,
                                                     0, local_buf.size, TRUE, NULL, NULL, NULL, NULL, NULL);
                nice_socket_free_queued_send(tbs);
                g_error_free(gerr);
                return FALSE;
            }
            g_clear_error(&gerr);
        }
        else if (ret < (int) tbs->length)
        {
            GOutputVector local_buf = { tbs->buf + ret, tbs->length - ret };
            NiceOutputMessage local_message = {&local_buf, 1};

            nice_socket_queue_send_with_callback(send_queue, &local_message,
                                                 0, local_buf.size, TRUE, NULL, NULL, NULL, NULL, NULL);
            nice_socket_free_queued_send(tbs);
            return FALSE;
        }

        nice_socket_free_queued_send(tbs);
    }

    return TRUE;
}

void nice_socket_free_send_queue(GQueue * send_queue)
{
    g_queue_foreach(send_queue, (GFunc) nice_socket_free_queued_send, NULL);
    g_queue_clear(send_queue);
}
