/* This file is part of the Nice GLib ICE library. */

#ifndef STUN_BIND_H
#define STUN_BIND_H

/**
 * SECTION:bind
 * @short_description: STUN Binding Usage
 * @include: stun/usages/bind.h
 * @stability: Stable
 *
 * The STUN Binding usage allows for easily creating and parsing STUN Binding
 * requests and responses. It offers both an asynchronous and a synchronous API
 * that uses the STUN timer usage.
 */


#ifdef _WIN32
#  include "../win32_common.h"
#else
# include <stdbool.h>
# include <stdint.h>
#endif

#include "stun/stunagent.h"

/**
 * StunUsageBindReturn:
 * @STUN_USAGE_BIND_RETURN_SUCCESS: The binding usage succeeded
 * @STUN_USAGE_BIND_RETURN_ERROR: There was an unknown error in the bind usage
 * @STUN_USAGE_BIND_RETURN_INVALID: The message is invalid and should be ignored
 * @STUN_USAGE_BIND_RETURN_ALTERNATE_SERVER: The binding request has an
 * ALTERNATE-SERVER attribute
 * @STUN_USAGE_BIND_RETURN_TIMEOUT: The binding was unsuccessful because it has
 * timed out.
 *
 * Return value of stun_usage_bind_process() and stun_usage_bind_run() which
 * allows you to see what status the function call returned.
 */
typedef enum
{
    STUN_USAGE_BIND_RETURN_SUCCESS,
    STUN_USAGE_BIND_RETURN_ERROR,
    STUN_USAGE_BIND_RETURN_INVALID,
    STUN_USAGE_BIND_RETURN_ALTERNATE_SERVER,
    STUN_USAGE_BIND_RETURN_TIMEOUT,
}
StunUsageBindReturn;


/**
 * stun_usage_bind_create:
 * @agent: The #StunAgent to use to create the binding request
 * @msg: The #StunMessage to build
 * @buffer: The buffer to use for creating the #StunMessage
 * @buffer_len: The size of the @buffer
 *
 * Create a new STUN binding request to use with a STUN server.
 * Returns: The length of the built message.
 */
size_t stun_usage_bind_create(StunAgent * agent, StunMessage * msg,
                              uint8_t * buffer, size_t buffer_len);

/**
 * stun_usage_bind_process:
 * @msg: The #StunMessage to process
 * @addr: A pointer to a #sockaddr structure to fill with the mapped address
 * that the STUN server gives us
 * @addrlen: The length of @add. rMust be set to the size of the @addr socket
 * address and will be set to the actual length of the socket address.
 * @alternate_server: A pointer to a #sockaddr structure to fill with the
 * address of an alternate server to which we should send our new STUN
 * binding request, in case the currently used STUN server is requesting the use
 * of an alternate server. This argument will only be filled if the return value
 * of the function is #STUN_USAGE_BIND_RETURN_ALTERNATE_SERVER
 * @alternate_server_len: The length of @alternate_server. Must be set to
 * the size of the @alternate_server socket address and will be set to the
 * actual length of the socket address.
 *
 * Process a STUN binding response and extracts the mapped address from the STUN
 * message. Also checks for the ALTERNATE-SERVER attribute.
 * Returns: A #StunUsageBindReturn value.
 * Note that #STUN_USAGE_BIND_RETURN_TIMEOUT cannot be returned by this function
 */
StunUsageBindReturn stun_usage_bind_process(StunMessage * msg,
        struct sockaddr * addr, socklen_t * addrlen,
        struct sockaddr * alternate_server, socklen_t * alternate_server_len);

/**
 * stun_usage_bind_keepalive:
 * @agent: The #StunAgent to use to build the message
 * @msg: The #StunMessage to build
 * @buf: The buffer to use for creating the #StunMessage
 * @len: The size of the @buf
 *
 * Creates a STUN binding indication that can be used for a keepalive.
 * Since this is an indication message, no STUN response will be generated
 * and it can only be used as a keepalive message.
 * Returns: The length of the message to send
 */
size_t stun_usage_bind_keepalive(StunAgent * agent, StunMessage * msg,
                                 uint8_t * buf, size_t len);

/**
 * stun_usage_bind_run:
 * @srv: A pointer to the #sockaddr structure representing the STUN server's
 * address
 * @srvlen: The length of @srv
 * @addr: A pointer to a #sockaddr structure to fill with the mapped address
 * that the STUN server gives us
 * @addrlen: The length of @addr
 *
 * This is a convenience function that will do a synchronous Binding request to
 * a server and wait for its answer. It will create the socket transports and
 * use the #StunTimer usage to send the request and handle the response.
 * Returns: A #StunUsageBindReturn.
 * Possible return values are #STUN_USAGE_BIND_RETURN_SUCCESS,
 * #STUN_USAGE_BIND_RETURN_ERROR and #STUN_USAGE_BIND_RETURN_TIMEOUT
 */
StunUsageBindReturn stun_usage_bind_run(const struct sockaddr * srv,
                                        socklen_t srvlen, struct sockaddr_storage * addr, socklen_t * addrlen);
#endif
