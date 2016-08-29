/* This file is part of the Nice GLib ICE library. */

#ifndef _HTTP_H
#define _HTTP_H

#include "socket.h"
#include "agent.h"

NiceSocket * nice_http_socket_new (NiceSocket *base_socket, NiceAddress *addr, gchar *username, gchar *password);

#endif /* _HTTP_H */

