/* This file is part of the Nice GLib ICE library. */

#ifndef _UDP_TURN_H
#define _UDP_TURN_H


typedef enum
{
    NICE_TURN_SOCKET_COMPATIBILITY_DRAFT9,
    NICE_TURN_SOCKET_COMPATIBILITY_GOOGLE,
    NICE_TURN_SOCKET_COMPATIBILITY_MSN,
    NICE_TURN_SOCKET_COMPATIBILITY_OC2007,
    NICE_TURN_SOCKET_COMPATIBILITY_RFC5766,
} NiceTurnSocketCompatibility;

#include "socket.h"
#include "stun/stunmessage.h"

uint32_t n_udp_turn_socket_parse_recv_msg(n_socket_t * sock, n_socket_t ** from_sock, n_input_msg_t * message);

uint32_t nice_udp_turn_socket_parse_recv(n_socket_t * sock, n_socket_t ** from_sock,
                                n_addr_t * from, uint32_t len, uint8_t * buf,
                                n_addr_t * recv_from, uint8_t * recv_buf, uint32_t recv_len);

int nice_udp_turn_socket_set_peer(n_socket_t * sock, n_addr_t * peer);

n_socket_t * n_udp_turn_new(GMainContext * ctx, n_addr_t * addr,
                         n_socket_t * base_socket, n_addr_t * server_addr, char * username, char * password);

#endif /* _UDP_TURN_H */

