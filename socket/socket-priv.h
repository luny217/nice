/* This file is part of the Nice GLib ICE library */

#ifndef _SOCKET_PRIV_H
#define _SOCKET_PRIV_H

#include "nqueue.h"
#include "socket.h"

/**
 * nice_socket_queue_send:
 * @send_queue: The queue to add to
 * @to : Destination
 * @messages: Messages to queue
 * @n_messages: Number of messages to queue
 *
 * queue messages to be sent later into the n_queue_t
 */
void nice_socket_queue_send (n_queue_t * send_queue, const n_addr_t * to, const n_output_msg_t *messages, uint32_t n_messages);

/**
 * nice_socket_queue_send_with_callback:
 * @send_queue: The queue to add to
 * @message: The message to queue
 * @message_offset: Number of bytes to skip in the message
 * @message_len: Total length of the message
 * @head: Whether to add the message to the head of the queue or the tail
 * @gsock: The #GSocket to create the callback on
 * @io_source: Pointer to #GSource pointer to store the created source
 * @context: #GMainContext to attach the @io_source to
 * @cb: Callback function to call when the @gsock is writable
 * @user_data: User data for @cb
 *
 * Queue (partial) message to be sent later and create a source to call @cb
 * when the @gsock becomes writable.
 * The @message_offset can be used if a partial write happened and some bytes
 * were already written, in which case @head should be set to TRUE to add the
 * message to the head of the queue.
 */
void nice_socket_queue_send_with_callback (n_queue_t * send_queue,
    const n_output_msg_t *message, uint32_t message_offset, uint32_t message_len,
    int head, GSocket *gsock, GSource **io_source, GMainContext *context,
    GSourceFunc cb, void * user_data);

/**
 * nice_socket_flush_send_queue:
 * @base_socket: Base socket to send on
 * @send_queue: Queue to flush
 *
 * Send all the queued messages reliably to the base socket. We assume only
 * reliable messages were queued and the underlying socket will handle the
 * send.
 */
void nice_socket_flush_send_queue (n_socket_t *base_socket, n_queue_t * send_queue);

/**
 * nice_socket_flush_send_queue_to_socket:
 * @gsock: GSocket to send on
 * @send_queue: Queue to flush
 *
 * Send all the queued messages to the socket. If any message fails to be sent
 * it will be readded to the queue and #FALSE will be returned, in which case
 * the IO source must be kept to allow flushing the next time the socket
 * is writable.
 * If the queue gets flushed, #TRUE will be returned, in which case, the IO
 * source should be destroyed.
 *
 * Returns: #TRUE if the queue was emptied, #FALSE if the socket would block.
 */
int nice_socket_flush_send_queue_to_socket (GSocket *gsock, n_queue_t * send_queue);

/**
 * nice_socket_free_send_queue:
 * @send_queue: The send queue
 *
 * Frees every item in the send queue without sending them and empties the queue
 */
void nice_socket_free_send_queue (n_queue_t * send_queue);

#endif /* _SOCKET_PRIV_H */

