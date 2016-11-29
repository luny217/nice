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
 * nice_socket_free_send_queue:
 * @send_queue: The send queue
 *
 * Frees every item in the send queue without sending them and empties the queue
 */
void nice_socket_free_send_queue (n_queue_t * send_queue);

#endif /* _SOCKET_PRIV_H */

