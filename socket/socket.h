/* This file is part of the Nice GLib ICE library. */

#ifndef _SOCKET_H
#define _SOCKET_H

//#include <uv.h>
#include "agent.h"
#include "address.h"
//#include <gio/gio.h>


/*
#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif*/



typedef struct _socket_st n_socket_t;

typedef enum
{
    NICE_SOCKET_TYPE_UDP_BSD,
    NICE_SOCKET_TYPE_TCP_BSD,
    NICE_SOCKET_TYPE_PSEUDOSSL,
    NICE_SOCKET_TYPE_HTTP,
    NICE_SOCKET_TYPE_SOCKS5,
    NICE_SOCKET_TYPE_UDP_TURN,
    NICE_SOCKET_TYPE_UDP_TURN_OVER_TCP,
    NICE_SOCKET_TYPE_TCP_ACTIVE,
    NICE_SOCKET_TYPE_TCP_PASSIVE,
    NICE_SOCKET_TYPE_TCP_SO
} NiceSocketType;

typedef void (*NiceSocketWritableCb)(n_socket_t * sock, void * user_data);

struct _socket_st
{
    int sock_fd;
    n_addr_t addr;
    NiceSocketType type;
    //GSocket * fileno;    
    /* Implementations must handle any value of n_recv_messages, including 0. Iff
     * n_recv_messages is 0, recv_messages may be NULL. */
    int32_t(*recv_messages)(n_socket_t * sock, n_input_msg_t * recv_messages, uint32_t n_recv_messages);
    /* As above, @n_messages may be zero. Iff so, @messages may be %NULL. */
    int32_t(*send_messages)(n_socket_t * sock, const n_addr_t * to, const n_output_msg_t * messages, uint32_t n_messages);
    int32_t(*send_messages_reliable)(n_socket_t * sock, const n_addr_t * to, const n_output_msg_t * messages, uint32_t n_messages);
    int(*is_reliable)(n_socket_t * sock);
    int(*can_send)(n_socket_t * sock, n_addr_t * addr);
    void (*set_writable_callback)(n_socket_t * sock, NiceSocketWritableCb callback, void * user_data);
    void (*close)(n_socket_t * sock);
    void * priv;
};

n_socket_t * n_socket_new(n_addr_t * addr);
int32_t n_socket_recv_msgs(n_socket_t * sock, n_input_msg_t * recv_messages, uint32_t n_recv_messages);
int32_t nice_socket_send_messages(n_socket_t * sock, const n_addr_t * addr, const n_output_msg_t * messages, uint32_t n_messages);
int32_t nice_socket_send_messages_reliable(n_socket_t * sock, const n_addr_t * addr, const n_output_msg_t * messages, uint32_t n_messages);
int32_t nice_socket_recv(int fd, n_addr_t * from, uint32_t len, char * buf);
int32_t nice_socket_send(n_socket_t * sock, n_addr_t * to, uint32_t len, char * buf);
//int32_t nice_socket_send_reliable(n_socket_t * sock, const n_addr_t * addr, uint32_t len, const char * buf);
int nice_socket_is_reliable(n_socket_t * sock);
int nice_socket_can_send(n_socket_t * sock, n_addr_t * addr);
void nice_socket_set_writable_callback(n_socket_t * sock, NiceSocketWritableCb callback, void * user_data);
void nice_socket_free(n_socket_t * sock);

//#include "udp-bsd.h"
//#include "tcp-bsd.h"
//#include "tcp-active.h"
//#include "tcp-passive.h"
//#include "pseudossl.h"
//#include "socks5.h"
//#include "http.h"
#include "udp-turn.h"
//#include "udp-turn-over-tcp.h"

#endif /* _SOCKET_H */

