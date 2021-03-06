/* This file is part of the Nice GLib ICE library. */

#ifndef STUN_TURN_H
# define STUN_TURN_H 1

/**
 * SECTION:turn
 * @short_description: TURN Allocation Usage
 * @include: stun/usages/turn.h
 * @stability: Stable
 *
 * The STUN TURN usage allows for easily creating and parsing STUN Allocate
 * requests and responses used for TURN. The API allows you to create a new
 * allocation or refresh an existing one as well as to parse a response to
 * an allocate or refresh request.
 */


#ifdef _WIN32
#include "../win32_common.h"
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "stun/stunagent.h"

/**
 * StunUsageTurnRequestPorts:
 * @STUN_USAGE_TURN_REQUEST_PORT_NORMAL: Request a normal port
 * @STUN_USAGE_TURN_REQUEST_PORT_EVEN: Request an even port
 * @STUN_USAGE_TURN_REQUEST_PORT_EVEN_AND_RESERVE: Request an even port and
 * reserve the next higher port
 *
 * This enum is used to specify which port configuration you want when creating
 * a new Allocation
 */
typedef enum
{
   TURN_REQUEST_PORT_NORMAL = 0,
   TURN_REQUEST_PORT_EVEN = 1,
   TURN_REQUEST_PORT_EVEN_AND_RESERVE = 2
}
StunUsageTurnRequestPorts;

/**
 * StunUsageTurnCompatibility:
 * @STUN_USAGE_TURN_COMPATIBILITY_DRAFT9: Use the specification compatible with
 * TURN Draft 09
 * @STUN_USAGE_TURN_COMPATIBILITY_GOOGLE: Use the specification compatible with
 * Google Talk's relay server
 * @STUN_USAGE_TURN_COMPATIBILITY_MSN: Use the specification compatible with
 * MSN TURN servers
 * @STUN_USAGE_TURN_COMPATIBILITY_OC2007: Use the specification compatible with
 * Microsoft Office Communicator 2007
 * @STUN_USAGE_TURN_COMPATIBILITY_RFC5766: Use the specification compatible with
 * RFC 5766 (the final, canonical TURN standard)
 *
 * Specifies which TURN specification compatibility to use
 */
typedef enum
{
    STUN_USAGE_TURN_COMPATIBILITY_DRAFT9,
    STUN_USAGE_TURN_COMPATIBILITY_GOOGLE,
    STUN_USAGE_TURN_COMPATIBILITY_MSN,
    STUN_USAGE_TURN_COMPATIBILITY_OC2007,
    STUN_USAGE_TURN_COMPATIBILITY_RFC5766,
} StunUsageTurnCompatibility;

/**
 * StunUsageTurnReturn:
 * @STUN_TURN_RET_RELAY_SUCCESS: The response was successful and a relay
 * address is provided
 * @STUN_TURN_RET_MAPPED_SUCCESS: The response was successful and a
 * relay address as well as a mapped address are provided
 * @STUN_TURN_RET_ERROR: The response resulted in an error
 * @STUN_TURN_RET_INVALID: The response is not a valid response
 * @STUN_TURN_RET_ALTERNATE_SERVER: The server requests the message
 * to be sent to an alternate server
 *
 * Return value of stun_usage_turn_process() and
 * stun_usage_turn_refresh_process() which allows you to see what status the
 * function call returned.
 */
typedef enum
{
    STUN_TURN_RET_RELAY_SUCCESS,
    STUN_TURN_RET_MAPPED_SUCCESS,
    STUN_TURN_RET_ERROR,
    STUN_TURN_RET_INVALID,
    STUN_TURN_RET_ALTERNATE_SERVER,
} StunUsageTurnReturn;


/**
 * turn_create:
 * @agent: The #stun_agent_t to use to build the request
 * @msg: The #stun_msg_t to build
 * @buffer: The buffer to use for creating the #stun_msg_t
 * @buffer_len: The size of the @buffer
 * @previous_response: If this is the first request you are sending, set this
 * argument to NULL, if it's a subsequent request you are building, then set this
 * argument to the response you have received. This argument is used for building
 * long term credentials (using the REALM and NONCE attributes) as well as for
 * getting the RESERVATION-TOKEN attribute when you previously requested an
 * allocation which reserved two ports
 * @request_ports: Specify how you want to request the allocated port(s).
 * This is only used if the compatibility is set to
 * #STUN_USAGE_TURN_COMPATIBILITY_DRAFT9
 * <para>See #StunUsageTurnRequestPorts </para>
 * @bandwidth: The bandwidth to request from the server for the allocation. If
 * this value is negative, then no BANDWIDTH attribute is added to the request.
 * This is only used if the compatibility is set to
 * #STUN_USAGE_TURN_COMPATIBILITY_DRAFT9
 * @lifetime: The lifetime of the allocation to request from the server. If
 * this value is negative, then no LIFETIME attribute is added to the request.
 * This is only used if the compatibility is set to
 * #STUN_USAGE_TURN_COMPATIBILITY_DRAFT9
 * @username: The username to use in the request
 * @username_len: The length of @username
 * @password: The key to use for building the MESSAGE-INTEGRITY
 * @password_len: The length of @password
 * @compatibility: The compatibility mode to use for building the Allocation
 * request
 *
 * Create a new TURN Allocation request
 * Returns: The length of the message to send
 */
size_t turn_create(stun_agent_t * agent, stun_msg_t * msg,
                              uint8_t * buffer, size_t buffer_len,
                              stun_msg_t * previous_response,
                              StunUsageTurnRequestPorts request_ports,
                              int32_t bandwidth, int32_t lifetime,
                              uint8_t * username, size_t username_len,
                              uint8_t * password, size_t password_len);

/**
 * turn_create_refresh:
 * @agent: The #stun_agent_t to use to build the request
 * @msg: The #stun_msg_t to build
 * @buffer: The buffer to use for creating the #stun_msg_t
 * @buffer_len: The size of the @buffer
 * @previous_response: If this is the first request you are sending, set this
 * argument to NULL, if it's a subsequent request you are building, then set this
 * argument to the response you have received. This argument is used for building
 * long term credentials (using the REALM and NONCE attributes)
 * @lifetime: The lifetime of the allocation to request from the server. If
 * this value is negative, then no LIFETIME attribute is added to the request.
 * This is only used if the compatibility is set to
 * #STUN_USAGE_TURN_COMPATIBILITY_DRAFT9
 * @username: The username to use in the request
 * @username_len: The length of @username
 * @password: The key to use for building the MESSAGE-INTEGRITY
 * @password_len: The length of @password
 * @compatibility: The compatibility mode to use for building the Allocation
 * request
 *
 * Create a new TURN Refresh request
 * Returns: The length of the message to send
 */
size_t turn_create_refresh(stun_agent_t * agent, stun_msg_t * msg,
                                      uint8_t * buffer, size_t buffer_len,
                                      stun_msg_t * previous_response, int32_t lifetime,
                                      uint8_t * username, size_t username_len,
                                      uint8_t * password, size_t password_len);

/**
 * stun_usage_turn_create_permission:
 * @agent: The #stun_agent_t to use to build the request
 * @msg: The #stun_msg_t to build
 * @buffer: The buffer to use for creating the #stun_msg_t
 * @buffer_len: The size of the @buffer
 * @username: The username to use in the request
 * @username_len: The length of @username
 * @password: The key to use for building the MESSAGE-INTEGRITY
 * @password_len: The length of @password
 * @realm: The realm identifier to use in the request
 * @realm_len: The length of @realm
 * @nonce: Unique and securely random nonce to use in the request
 * @nonce_len: The length of @nonce
 * @peer: Server-reflexive host address to request permission for
 * @compatibility: The compatibility mode to use for building the
 * CreatePermission request
 *
 * Create a new TURN CreatePermission request
 *
 * Returns: The length of the message to send
 */
size_t stun_usage_turn_create_permission(stun_agent_t * agent, stun_msg_t * msg,
        uint8_t * buffer, size_t buffer_len,
        uint8_t * username, size_t username_len,
        uint8_t * password, size_t password_len,
        uint8_t * realm, size_t realm_len,
        uint8_t * nonce, size_t nonce_len,
        struct sockaddr_storage * peer,
        StunUsageTurnCompatibility compatibility);

/**
 * stun_usage_turn_process:
 * @msg: The message containing the response
 * @relay_addr: A pointer to a #sockaddr structure to fill with the relay address
 * that the TURN server allocated for us
 * @relay_addrlen: The length of @relay_addr
 * @addr: A pointer to a #sockaddr structure to fill with the mapped address
 * that the STUN response contains.
 * This argument will only be filled if the return value
 * of the function is #STUN_TURN_RET_MAPPED_SUCCESS
 * @addrlen: The length of @addr
 * @alternate_server: A pointer to a #sockaddr structure to fill with the
 * address of an alternate server to which we should send our new STUN
 * Allocate request, in case the currently used TURN server is requesting the use
 * of an alternate server. This argument will only be filled if the return value
 * of the function is #STUN_TURN_RET_ALTERNATE_SERVER
 * @alternate_server_len: The length of @alternate_server
 * @bandwidth: A pointer to fill with the bandwidth the TURN server allocated us
 * @lifetime: A pointer to fill with the lifetime of the allocation
 * @compatibility: The compatibility mode to use for processing the Allocation
 * response
 *
 * Process a TURN Allocate response and extract the necessary information from
 * the message
 * Returns: A #StunUsageTurnReturn value
 */
StunUsageTurnReturn stun_usage_turn_process(stun_msg_t * msg,
        struct sockaddr_storage * relay_addr, socklen_t * relay_addrlen,
        struct sockaddr_storage * addr, socklen_t * addrlen,
        struct sockaddr_storage * alternate_server, socklen_t * alternate_server_len,
        uint32_t * bandwidth, uint32_t * lifetime);

/**
 * stun_usage_turn_refresh_process:
 * @msg: The message containing the response
 * @lifetime: A pointer to fill with the lifetime of the allocation
 * @compatibility: The compatibility mode to use for processing the Refresh
 * response
 *
 * Process a TURN Refresh response and extract the necessary information from
 * the message
 * Returns: A #StunUsageTurnReturn value. A #STUN_TURN_RET_RELAY_SUCCESS
 * means the Refresh was successful, but no relay address is given (kept the same
 * as for the original allocation)
 */
StunUsageTurnReturn stun_usage_turn_refresh_process(stun_msg_t * msg, uint32_t * lifetime);

#endif
