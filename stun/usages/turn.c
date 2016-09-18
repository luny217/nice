/* This file is part of the Nice GLib ICE library */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "stun/stunagent.h"
#include "turn.h"

#include <string.h>
#include <stdlib.h>
#include <fcntl.h>



#define REQUESTED_PROPS_E 0x80000000
#define REQUESTED_PROPS_R 0x40000000
#define REQUESTED_PROPS_P 0x20000000


#define STUN_ATT_MSN_MAPPED_ADDRESS 0x8000


#define TURN_REQUESTED_TRANSPORT_UDP 0x11000000

/** Non-blocking mode STUN TURN usage */

size_t turn_create(stun_agent_t * agent, stun_msg_t * msg,
                              uint8_t * buffer, size_t buffer_len,
                              stun_msg_t * previous_response,
                              StunUsageTurnRequestPorts request_props,
                              int32_t bandwidth, int32_t lifetime,
                              uint8_t * username, size_t username_len,
                              uint8_t * password, size_t password_len)
{
    stun_agent_init_request(agent, msg, buffer, buffer_len, STUN_ALLOCATE);

	if (stun_msg_append32(msg, STUN_ATT_REQUESTED_TRANSPORT,
		TURN_REQUESTED_TRANSPORT_UDP) != STUN_MSG_RET_SUCCESS)
		return 0;
	if (bandwidth >= 0)
	{
		if (stun_msg_append32(msg, STUN_ATT_BANDWIDTH, bandwidth) !=
			STUN_MSG_RET_SUCCESS)
			return 0;
	}

    if (previous_response)
    {
        uint8_t * realm;
        uint8_t * nonce;
        uint64_t reservation;
        uint16_t len;

        realm = (uint8_t *) stun_msg_find(previous_response, STUN_ATT_REALM, &len);
        if (realm != NULL)
        {
            if (stun_msg_append_bytes(msg, STUN_ATT_REALM, realm, len) !=
                    STUN_MSG_RET_SUCCESS)
                return 0;
        }
        nonce = (uint8_t *) stun_msg_find(previous_response, STUN_ATT_NONCE, &len);
        if (nonce != NULL)
        {
            if (stun_msg_append_bytes(msg, STUN_ATT_NONCE, nonce, len) !=
                    STUN_MSG_RET_SUCCESS)
                return 0;
        }
        if (stun_msg_find64(previous_response, STUN_ATT_RESERVATION_TOKEN,
                                &reservation) == STUN_MSG_RET_SUCCESS)
        {
            if (stun_msg_append64(msg, STUN_ATT_RESERVATION_TOKEN,
                                      reservation) != STUN_MSG_RET_SUCCESS)
                return 0;
        }
    }

    if (username != NULL && username_len > 0)
    {
        if (stun_msg_append_bytes(msg, STUN_ATT_USERNAME,
                                      username, username_len) != STUN_MSG_RET_SUCCESS)
            return 0;
    }

    return stun_agent_finish_message(agent, msg, password, password_len);
}

size_t turn_create_refresh(stun_agent_t * agent, stun_msg_t * msg,
                                      uint8_t * buffer, size_t buffer_len,
                                      stun_msg_t * previous_response, int32_t lifetime,
                                      uint8_t * username, size_t username_len,
                                      uint8_t * password, size_t password_len)
{

    stun_agent_init_request(agent, msg, buffer, buffer_len, STUN_REFRESH);
    if (previous_response)
    {
        uint8_t * realm;
        uint8_t * nonce;
        uint16_t len;

        realm = (uint8_t *) stun_msg_find(previous_response, STUN_ATT_REALM, &len);
        if (realm != NULL)
        {
            if (stun_msg_append_bytes(msg, STUN_ATT_REALM, realm, len) != STUN_MSG_RET_SUCCESS)
                return 0;
        }
        nonce = (uint8_t *) stun_msg_find(previous_response, STUN_ATT_NONCE, &len);
        if (nonce != NULL)
        {
            if (stun_msg_append_bytes(msg, STUN_ATT_NONCE, nonce, len) != STUN_MSG_RET_SUCCESS)
                return 0;
        }
    }

    if (username != NULL && username_len > 0)
    {
        if (stun_msg_append_bytes(msg, STUN_ATT_USERNAME, username, username_len) != STUN_MSG_RET_SUCCESS)
            return 0;
    }

    return stun_agent_finish_message(agent, msg, password, password_len);
}

size_t stun_usage_turn_create_permission(stun_agent_t * agent, stun_msg_t * msg,
        uint8_t * buffer, size_t buffer_len,
        uint8_t * username, size_t username_len,
        uint8_t * password, size_t password_len,
        uint8_t * realm, size_t realm_len,
        uint8_t * nonce, size_t nonce_len,
        struct sockaddr_storage * peer,
        StunUsageTurnCompatibility compatibility)
{
    if (!peer)
        return 0;

    stun_agent_init_request(agent, msg, buffer, buffer_len, STUN_CREATEPERMISSION);

    /* PEER address */
    if (stun_msg_append_xor_addr(msg, STUN_ATT_XOR_PEER_ADDRESS,
                                     peer, sizeof(*peer)) != STUN_MSG_RET_SUCCESS)
    {
        return 0;
    }

    /* nonce */
    if (nonce != NULL)
    {
        if (stun_msg_append_bytes(msg, STUN_ATT_NONCE,
                                      nonce, nonce_len) != STUN_MSG_RET_SUCCESS)
            return 0;
    }

    /* realm */
    if (realm != NULL)
    {
        if (stun_msg_append_bytes(msg, STUN_ATT_REALM,
                                      realm, realm_len) != STUN_MSG_RET_SUCCESS)
            return 0;
    }

    /* username */
    if (username != NULL)
    {
        if (stun_msg_append_bytes(msg, STUN_ATT_USERNAME,
                                      username, username_len) != STUN_MSG_RET_SUCCESS)
            return 0;
    }

    return stun_agent_finish_message(agent, msg, password, password_len);
}

StunUsageTurnReturn stun_usage_turn_process(stun_msg_t * msg,
        struct sockaddr_storage * relay_addr, socklen_t * relay_addrlen,
        struct sockaddr_storage * addr, socklen_t * addrlen,
        struct sockaddr_storage * alternate_server, socklen_t * alternate_server_len,
        uint32_t * bandwidth, uint32_t * lifetime,
        StunUsageTurnCompatibility compatibility)
{
    int val, code = -1;
    StunUsageTurnReturn ret = STUN_TURN_RET_RELAY_SUCCESS;

    if (stun_msg_get_method(msg) != STUN_ALLOCATE)
        return STUN_TURN_RET_INVALID;

    switch (stun_msg_get_class(msg))
    {
        case STUN_REQUEST:
        case STUN_INDICATION:
            return STUN_TURN_RET_INVALID;

        case STUN_RESPONSE:
            break;

        case STUN_ERROR:
            if (stun_msg_find_error(msg, &code) != STUN_MSG_RET_SUCCESS)
            {
                /* missing ERROR-CODE: ignore message */
                return STUN_TURN_RET_INVALID;
            }

            /* NOTE: currently we ignore unauthenticated messages if the context
             * is authenticated, for security reasons. */
            stun_debug(" STUN error message received (code: %d)", code);

            /* ALTERNATE-SERVER mechanism */
            if ((code / 100) == 3)
            {
                if (alternate_server && alternate_server_len)
                {
                    if (stun_msg_find_addr(msg, STUN_ATT_ALTERNATE_SERVER,
                                               alternate_server, alternate_server_len) !=
                            STUN_MSG_RET_SUCCESS)
                    {
                        stun_debug(" Unexpectedly missing ALTERNATE-SERVER attribute");
                        return STUN_TURN_RET_ERROR;
                    }
                }
                else
                {
                    if (!stun_msg_has_attribute(msg,
                                                    STUN_ATT_ALTERNATE_SERVER))
                    {
                        stun_debug(" Unexpectedly missing ALTERNATE-SERVER attribute");
                        return STUN_TURN_RET_ERROR;
                    }
                }

                stun_debug("Found alternate server");
                return STUN_TURN_RET_ALTERNATE_SERVER;

            }
            return STUN_TURN_RET_ERROR;

        default:
            /* Fall through. */
            break;
    }

    stun_debug("Received %u-bytes TURN message", stun_msg_len(msg));

    if (compatibility == STUN_USAGE_TURN_COMPATIBILITY_DRAFT9 ||
            compatibility == STUN_USAGE_TURN_COMPATIBILITY_RFC5766)
    {
        val = stun_msg_find_xor_addr(msg, STUN_ATT_XOR_MAPPED_ADDRESS, addr, addrlen);

        if (val == STUN_MSG_RET_SUCCESS)
            ret = STUN_TURN_RET_MAPPED_SUCCESS;
        val = stun_msg_find_xor_addr(msg, STUN_ATT_RELAY_ADDRESS, relay_addr, relay_addrlen);
        if (val != STUN_MSG_RET_SUCCESS)
        {
            stun_debug(" No RELAYED-ADDRESS: %d", val);
            return STUN_TURN_RET_ERROR;
        }
    }
    else if (compatibility == STUN_USAGE_TURN_COMPATIBILITY_GOOGLE)
    {
        val = stun_msg_find_addr(msg,
                                     STUN_ATT_MAPPED_ADDRESS, relay_addr, relay_addrlen);
        if (val != STUN_MSG_RET_SUCCESS)
        {
            stun_debug(" No MAPPED-ADDRESS: %d", val);
            return STUN_TURN_RET_ERROR;
        }
    }
    else if (compatibility == STUN_USAGE_TURN_COMPATIBILITY_MSN)
    {
        val = stun_msg_find_addr(msg,
                                     STUN_ATT_MSN_MAPPED_ADDRESS, addr, addrlen);

        if (val == STUN_MSG_RET_SUCCESS)
            ret = STUN_TURN_RET_MAPPED_SUCCESS;

        val = stun_msg_find_addr(msg,
                                     STUN_ATT_MAPPED_ADDRESS, relay_addr, relay_addrlen);
        if (val != STUN_MSG_RET_SUCCESS)
        {
            stun_debug(" No MAPPED-ADDRESS: %d", val);
            return STUN_TURN_RET_ERROR;
        }
    }
    else if (compatibility == STUN_USAGE_TURN_COMPATIBILITY_OC2007)
    {
        union
        {
            stun_trans_id id;
            uint32_t u32[4];
        } transid;
        uint32_t magic_cookie;

        stun_msg_id(msg, transid.id);
        magic_cookie = transid.u32[0];

        val = stun_msg_find_xor_addr_full(msg,
                                              STUN_ATT_MS_XOR_MAPPED_ADDRESS, addr, addrlen,
                                              htonl(magic_cookie));

        if (val == STUN_MSG_RET_SUCCESS)
            ret = STUN_TURN_RET_MAPPED_SUCCESS;

        val = stun_msg_find_addr(msg,
                                     STUN_ATT_MAPPED_ADDRESS, relay_addr, relay_addrlen);
        if (val != STUN_MSG_RET_SUCCESS)
        {
            stun_debug(" No MAPPED-ADDRESS: %d", val);
            return STUN_TURN_RET_ERROR;
        }
    }

    stun_msg_find32(msg, STUN_ATT_LIFETIME, lifetime);
    stun_msg_find32(msg, STUN_ATT_BANDWIDTH, bandwidth);

    stun_debug(" Mapped address found!");
    return ret;

}



StunUsageTurnReturn stun_usage_turn_refresh_process(stun_msg_t * msg,
        uint32_t * lifetime, StunUsageTurnCompatibility compatibility)
{
    int code = -1;
    StunUsageTurnReturn ret = STUN_TURN_RET_RELAY_SUCCESS;

    if (compatibility == STUN_USAGE_TURN_COMPATIBILITY_DRAFT9 ||
            compatibility == STUN_USAGE_TURN_COMPATIBILITY_RFC5766)
    {
        if (stun_msg_get_method(msg) != STUN_REFRESH)
            return STUN_TURN_RET_INVALID;
    }
    else
    {
        if (stun_msg_get_method(msg) != STUN_ALLOCATE)
            return STUN_TURN_RET_INVALID;
    }

    switch (stun_msg_get_class(msg))
    {
        case STUN_REQUEST:
        case STUN_INDICATION:
            return STUN_TURN_RET_INVALID;

        case STUN_RESPONSE:
            break;

        case STUN_ERROR:
            if (stun_msg_find_error(msg, &code) != STUN_MSG_RET_SUCCESS)
            {
                /* missing ERROR-CODE: ignore message */
                return STUN_TURN_RET_INVALID;
            }

            return STUN_TURN_RET_ERROR;

        default:
            /* Fall through. */
            break;
    }

    stun_msg_find32(msg, STUN_ATT_LIFETIME, lifetime);

    stun_debug("TURN Refresh successful!");
    return ret;

}
