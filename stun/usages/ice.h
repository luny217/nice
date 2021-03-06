/* This file is part of the Nice GLib ICE library. */

#ifndef STUN_CONNCHECK_H
#define STUN_CONNCHECK_H 1

/**
 * SECTION:ice
 * @short_description: STUN ICE Usage
 * @include: stun/usages/ice.h
 * @stability: Stable
 *
 * The STUN ICE usage allows for easily creating and parsing STUN Binding
 * requests and responses used for ICE connectivity checks. The API allows you
 * to create a connectivity check message, parse a response or create a reply
 * to an incoming connectivity check request.
 */

#include "stun/stunagent.h"

/**
 * StunUsageIceCompatibility:
 * @STUN_USAGE_ICE_COMPATIBILITY_RFC5245: The ICE compatibility with RFC 5245
 * @STUN_USAGE_ICE_COMPATIBILITY_GOOGLE: The ICE compatibility with Google's
 * implementation of ICE
 * @STUN_USAGE_ICE_COMPATIBILITY_MSN: The ICE compatibility with MSN's
 * implementation of ICE
 * @STUN_USAGE_ICE_COMPATIBILITY_WLM2009: The ICE compatibility with Windows
 * Live Messenger and Microsoft Office Communicator 2007 R2 implementation of ICE
 * @STUN_USAGE_ICE_COMPATIBILITY_DRAFT19: The ICE compatibility with draft 19
 *
 * This enum defines which compatibility modes this ICE usage can use
 *
 * <warning>@STUN_USAGE_ICE_COMPATIBILITY_DRAFT19 is deprecated and should not
 * be used in newly-written code. It is kept for compatibility reasons and
 * represents the same compatibility as @STUN_USAGE_ICE_COMPATIBILITY_RFC5245
 * </warning>
 */
typedef enum
{
    STUN_USAGE_ICE_COMPATIBILITY_RFC5245,
    STUN_USAGE_ICE_COMPATIBILITY_GOOGLE,
    STUN_USAGE_ICE_COMPATIBILITY_MSN,
    STUN_USAGE_ICE_COMPATIBILITY_WLM2009,
    STUN_USAGE_ICE_COMPATIBILITY_DRAFT19 = STUN_USAGE_ICE_COMPATIBILITY_RFC5245,
} StunUsageIceCompatibility;


/**
 * stun_ice_ret_e:
 * @STUN_ICE_RET_SUCCESS: The function succeeded
 * @STUN_ICE_RET_ERROR: There was an unspecified error
 * @STUN_ICE_RET_INVALID: The message is invalid for processing
 * @STUN_ICE_RET_ROLE_CONFLICT: A role conflict was detected
 * @STUN_ICE_RET_INVALID_REQUEST: The message is an not a request
 * @STUN_ICE_RET_INVALID_METHOD: The method of the request is invalid
 * @STUN_ICE_RET_MEMORY_ERROR: The buffer size is too small to hold
 * the STUN reply
 * @STUN_ICE_RET_INVALID_ADDRESS: The mapped address argument has
 * an invalid address family
 * @STUN_ICE_RET_NO_MAPPED_ADDRESS: The response is valid but no
 * MAPPED-ADDRESS or XOR-MAPPED-ADDRESS attribute was found
 *
 * Return value of stun_ice_cocheck_process() and
 * stun_ice_cocheck_create_reply() which allows you to see what
 * status the function call returned.
 */
typedef enum
{
    STUN_ICE_RET_SUCCESS,
    STUN_ICE_RET_ERROR,
    STUN_ICE_RET_INVALID,
    STUN_ICE_RET_ROLE_CONFLICT,
    STUN_ICE_RET_INVALID_REQUEST,
    STUN_ICE_RET_INVALID_METHOD,
    STUN_ICE_RET_MEMORY_ERROR,
    STUN_ICE_RET_INVALID_ADDRESS,
    STUN_ICE_RET_NO_MAPPED_ADDRESS,
} stun_ice_ret_e; 


/**
 * stun_ice_cocheck_create:
 * @agent: The #stun_agent_t to use to build the request
 * @msg: The #stun_msg_t to build
 * @buffer: The buffer to use for creating the #stun_msg_t
 * @buffer_len: The size of the @buffer
 * @username: The username to use in the request
 * @username_len: The length of @username
 * @password: The key to use for building the MESSAGE-INTEGRITY
 * @password_len: The length of @password
 * @cand_use: Set to %TRUE to append the USE-CANDIDATE flag to the request
 * @controlling: Set to %TRUE if you are the controlling agent or set to
 * %FALSE if you are the controlled agent.
 * @priority: The value of the PRIORITY attribute
 * @tie: The value of the tie-breaker to put in the ICE-CONTROLLED or
 * ICE-CONTROLLING attribute
 * @candidate_identifier: The foundation value to put in the
 * CANDIDATE-IDENTIFIER attribute
 * @compatibility: The compatibility mode to use for building the conncheck
 * request
 *
 * Builds an ICE connectivity check STUN message.
 * If the compatibility is not #STUN_USAGE_ICE_COMPATIBILITY_RFC5245, the
 * @cand_use, @controlling, @priority and @tie arguments are not used.
 * If the compatibility is not #STUN_USAGE_ICE_COMPATIBILITY_WLM2009, the
 * @candidate_identifier argument is not used.
 * Returns: The length of the message built.
 */
size_t stun_ice_cocheck_create(stun_agent_t * agent, stun_msg_t * msg,
                                uint8_t * buffer, size_t buffer_len,
                                const uint8_t * username, const size_t username_len,
                                const uint8_t * password, const size_t password_len,
                                int cand_use, int controlling, uint32_t priority, uint64_t tie);

/**
 * stun_ice_cocheck_process:
 * @msg: The #stun_msg_t to process
 * @addr: A pointer to a #sockaddr structure to fill with the mapped address
 * that the STUN connectivity check response contains
 * @addrlen: The length of @addr
 * @compatibility: The compatibility mode to use for processing the conncheck
 * response
 *
 * Process an ICE connectivity check STUN message and retreive the
 * mapped address from the message
 * <para> See also stun_ice_cocheck_priority() and
 * stun_ice_cocheck_use_cand() </para>
 * Returns: A #stun_ice_ret_e value
 */
stun_ice_ret_e stun_ice_cocheck_process(stun_msg_t * msg, struct sockaddr_storage * addr, socklen_t * addrlen);

/**
 * stun_ice_cocheck_create_reply:
 * @agent: The #stun_agent_t to use to build the response
 * @req: The original STUN request to reply to
 * @msg: The #stun_msg_t to build
 * @buf: The buffer to use for creating the #stun_msg_t
 * @plen: A pointer containing the size of the @buffer on input.
 * Will contain the length of the message built on output.
 * @src: A pointer to a #sockaddr structure containing the source address from
 * which the request was received. Will be used as the mapped address in the
 * response
 * @srclen: The length of @addr
 * @control: Set to %TRUE if you are the controlling agent or set to
 * %FALSE if you are the controlled agent.
 * @tie: The value of the tie-breaker to put in the ICE-CONTROLLED or
 * ICE-CONTROLLING attribute
 * @compatibility: The compatibility mode to use for building the conncheck
 * response
 *
 * Tries to parse a STUN connectivity check request and builds a
 * response accordingly.
 <note>
   <para>
     In case of error, the @msg is filled with the appropriate error response
     to be sent and the value of @plen is set to the size of that message.
     If @plen has a size of 0, then no error response should be sent.
   </para>
 </note>
 * Returns: A #stun_ice_ret_e value
 */
stun_ice_ret_e
stun_ice_cocheck_create_reply(stun_agent_t * agent, stun_msg_t * req,
                                      stun_msg_t * msg, uint8_t * buf, size_t * plen,
                                      const struct sockaddr_storage * src, socklen_t srclen,
                                      int * control, uint64_t tie);

/**
 * stun_ice_cocheck_priority:
 * @msg: The #stun_msg_t to parse
 *
 * Extracts the priority from a STUN message.
 * Returns: host byte order priority, or 0 if not specified.
 */
uint32_t stun_ice_cocheck_priority(const stun_msg_t * msg);

/**
 * stun_ice_cocheck_use_cand:
 * @msg: The #stun_msg_t to parse
 *
 * Extracts the USE-CANDIDATE attribute flag from a STUN message.
 * Returns: %TRUE if the flag is set, %FALSE if not.
 */
bool stun_ice_cocheck_use_cand(const stun_msg_t * msg);

#endif
