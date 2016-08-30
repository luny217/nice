/* This file is part of the Nice GLib ICE library. */

#include <config.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include "stunagent.h"

/** ICE connectivity checks **/
#include "ice.h"

size_t stun_usage_ice_conncheck_create(StunAgent * agent, StunMessage * msg,
                                       uint8_t * buffer, size_t buffer_len,
                                       const uint8_t * username, const size_t username_len,
                                       const uint8_t * password, const size_t password_len,
                                       int cand_use, int controlling, uint32_t priority, uint64_t tie)
{
    StunMessageReturn val;

    stun_agent_init_request(agent, msg, buffer, buffer_len, STUN_BINDING);

    if (cand_use)
    {
        val = stun_message_append_flag(msg, STUN_ATTRIBUTE_USE_CANDIDATE);
        if (val != STUN_MESSAGE_RETURN_SUCCESS)
            return 0;
    }

    val = stun_message_append32(msg, STUN_ATTRIBUTE_PRIORITY, priority);
    if (val != STUN_MESSAGE_RETURN_SUCCESS)
        return 0;

    if (controlling)
        val = stun_message_append64(msg, STUN_ATTRIBUTE_ICE_CONTROLLING, tie);
    else
        val = stun_message_append64(msg, STUN_ATTRIBUTE_ICE_CONTROLLED, tie);
    if (val != STUN_MESSAGE_RETURN_SUCCESS)
        return 0;

    if (username && username_len > 0)
    {
        val = stun_message_append_bytes(msg, STUN_ATTRIBUTE_USERNAME,  username, username_len);
        if (val != STUN_MESSAGE_RETURN_SUCCESS)
            return 0;
    }

    return stun_agent_finish_message(agent, msg, password, password_len);
}


StunUsageIceReturn stun_usage_ice_conncheck_process(StunMessage * msg, struct sockaddr_storage * addr, socklen_t * addrlen)
{
    int code = -1;
    StunMessageReturn val;

    if (stun_message_get_method(msg) != STUN_BINDING)
        return STUN_USAGE_ICE_RETURN_INVALID;

    switch (stun_message_get_class(msg))
    {
        case STUN_REQUEST:
        case STUN_INDICATION:
            return STUN_USAGE_ICE_RETURN_INVALID;

        case STUN_RESPONSE:
            break;

        case STUN_ERROR:
        default:
            if (stun_message_find_error(msg, &code) != STUN_MESSAGE_RETURN_SUCCESS)
            {
                /* missing ERROR-CODE: ignore message */
                return STUN_USAGE_ICE_RETURN_INVALID;
            }

            if (code  == STUN_ERROR_ROLE_CONFLICT)
                return STUN_USAGE_ICE_RETURN_ROLE_CONFLICT;

            /* NOTE: currently we ignore unauthenticated messages if the context
             * is authenticated, for security reasons. */
            stun_debug(" STUN error message received (code: %d)", code);

            return STUN_USAGE_ICE_RETURN_ERROR;
    }

    stun_debug("Received %u-bytes STUN message", stun_message_length(msg));

    val = stun_message_find_xor_addr(msg, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS, addr, addrlen);
    if (val != STUN_MESSAGE_RETURN_SUCCESS)
    {
        stun_debug(" No XOR-MAPPED-ADDRESS: %d", val);
        return STUN_USAGE_ICE_RETURN_NO_MAPPED_ADDRESS;
    }
    stun_debug("Mapped address found!");
    return STUN_USAGE_ICE_RETURN_SUCCESS;
}

static int stun_bind_error(StunAgent * agent, StunMessage * msg,
                           uint8_t * buf, size_t * plen, const StunMessage * req,  StunError code)
{
    size_t len = *plen;
    int val;

    *plen = 0;
    stun_debug("STUN Error Reply (buffer size: %u)...", (unsigned)len);

    val = stun_agent_init_error(agent, msg, buf, len, req, code);
    if (!val)
        return val;

    len = stun_agent_finish_message(agent, msg, NULL, 0);
    if (len == 0)
        return 0;

    *plen = len;
    stun_debug(" Error response (%u) of %u bytes", (unsigned)code,
               (unsigned)*plen);
    return 1;
}

StunUsageIceReturn
stun_usage_ice_conncheck_create_reply(StunAgent * agent, StunMessage * req,
                                      StunMessage * msg, uint8_t * buf, size_t * plen,
                                      const struct sockaddr_storage * src, socklen_t srclen,
                                      int * control, uint64_t tie)
{
    const char * username = NULL;
    uint16_t username_len;
    size_t len = *plen;
    uint64_t q;
    StunMessageReturn val = STUN_MESSAGE_RETURN_SUCCESS;
    StunUsageIceReturn ret = STUN_USAGE_ICE_RETURN_SUCCESS;

    *plen = 0;
    stun_debug("STUN Reply (buffer size = %u)...", (unsigned)len);

    if (stun_message_get_class(req) != STUN_REQUEST)
    {
        stun_debug(" Unhandled non-request (class %u) message.",
                   stun_message_get_class(req));
        return STUN_USAGE_ICE_RETURN_INVALID_REQUEST;
    }

    if (stun_message_get_method(req) != STUN_BINDING)
    {
        stun_debug(" Bad request (method %u) message.", stun_message_get_method(req));
        stun_bind_error(agent, msg, buf, &len, req, STUN_ERROR_BAD_REQUEST);
        *plen = len;
        return STUN_USAGE_ICE_RETURN_INVALID_METHOD;
    }

    /* Role conflict handling */
    assert(control != NULL);
    if (stun_message_find64(req, *control ? STUN_ATTRIBUTE_ICE_CONTROLLING
                            : STUN_ATTRIBUTE_ICE_CONTROLLED, &q) == STUN_MESSAGE_RETURN_SUCCESS)
    {
        stun_debug("STUN Role Conflict detected:");

        if (tie < q)
        {
            stun_debug(" switching role from \"controll%s\" to \"controll%s\"",
                       *control ? "ing" : "ed", *control ? "ed" : "ing");
            *control = !*control;
            ret = STUN_USAGE_ICE_RETURN_ROLE_CONFLICT;
        }
        else
        {
            stun_debug(" staying \"controll%s\" (sending error)", *control ? "ing" : "ed");
            stun_bind_error(agent, msg, buf, &len, req, STUN_ERROR_ROLE_CONFLICT);
            *plen = len;
            return STUN_USAGE_ICE_RETURN_SUCCESS;
        }
    }
    else
    {
        stun_debug("STUN Role not specified by peer!");
    }

    if (stun_agent_init_response(agent, msg, buf, len, req) == FALSE)
    {
        stun_debug("Unable to create response");
        goto failure;
    }

    if (stun_message_has_cookie(msg))
    {
        val = stun_message_append_xor_addr(msg, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS, src, srclen);
    }
    else
    {
        val = stun_message_append_addr(msg, STUN_ATTRIBUTE_MAPPED_ADDRESS, (struct sockaddr *) src, srclen);
    }

    if (val != STUN_MESSAGE_RETURN_SUCCESS)
    {
        stun_debug(" Mapped address problem: %d", val);
        goto failure;
    }

    username = (const char *)stun_message_find(req, STUN_ATTRIBUTE_USERNAME, &username_len);
    if (username)
    {
        val = stun_message_append_bytes(msg, STUN_ATTRIBUTE_USERNAME, username, username_len);
    }

    if (val != STUN_MESSAGE_RETURN_SUCCESS)
    {
        stun_debug("Error appending username: %d", val);
        goto failure;
    }

    /* the stun agent will automatically use the password of the request */
    len = stun_agent_finish_message(agent, msg, NULL, 0);
    if (len == 0)
        goto failure;

    *plen = len;
    stun_debug(" All done (response size: %u)", (unsigned)len);
    return ret;

failure:
    assert(*plen == 0);
    stun_debug(" Fatal error formatting Response: %d", val);

    switch (val)
    {
        case STUN_MESSAGE_RETURN_NOT_ENOUGH_SPACE:
            return STUN_USAGE_ICE_RETURN_MEMORY_ERROR;
        case STUN_MESSAGE_RETURN_INVALID:
        case STUN_MESSAGE_RETURN_UNSUPPORTED_ADDRESS:
            return STUN_USAGE_ICE_RETURN_INVALID_ADDRESS;
        case STUN_MESSAGE_RETURN_SUCCESS:
            assert(0);   /* shouldnt be reached */
        case STUN_MESSAGE_RETURN_NOT_FOUND:
        default:
            return STUN_USAGE_ICE_RETURN_ERROR;
    }
}

uint32_t stun_usage_ice_conncheck_priority(const StunMessage * msg)
{
    uint32_t value;

    if (stun_message_find32(msg, STUN_ATTRIBUTE_PRIORITY, &value) != STUN_MESSAGE_RETURN_SUCCESS)
        return 0;
    return value;
}


bool stun_usage_ice_conncheck_use_candidate(const StunMessage * msg)
{
    return (stun_message_find_flag(msg, STUN_ATTRIBUTE_USE_CANDIDATE) == STUN_MESSAGE_RETURN_SUCCESS);
}

