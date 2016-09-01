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

uint32_t
nice_udp_turn_socket_parse_recv_message(NiceSocket * sock, NiceSocket ** from_sock,
                                        NiceInputMessage * message);

uint32_t
nice_udp_turn_socket_parse_recv(NiceSocket * sock, NiceSocket ** from_sock,
                                NiceAddress * from, uint32_t len, uint8_t * buf,
                                NiceAddress * recv_from, uint8_t * recv_buf, uint32_t recv_len);

int
nice_udp_turn_socket_set_peer(NiceSocket * sock, NiceAddress * peer);

NiceSocket *
nice_udp_turn_socket_new(GMainContext * ctx, NiceAddress * addr,
                         NiceSocket * base_socket, NiceAddress * server_addr,
                         char * username, char * password, NiceTurnSocketCompatibility compatibility);

#endif /* _UDP_TURN_H */

