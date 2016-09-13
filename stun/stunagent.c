/* This file is part of the Nice GLib ICE library. */

#include <config.h>
#include "stunmessage.h"
#include "stunagent.h"
#include "stunhmac.h"
#include "stun5389.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static int stun_agent_is_unknown(stun_agent_t * agent, uint16_t type);
static unsigned stun_agent_find_unknowns(stun_agent_t * agent, const stun_msg_t * msg, uint16_t * list, unsigned max);

void stun_agent_init(stun_agent_t * agent, stun_flags_e usage_flags)
{
    int i;

    agent->known_attributes = (uint16_t *) STUN_ALL_KNOWN_ATTRS;
    agent->usage_flags = usage_flags;

    for (i = 0; i < STUN_AGENT_MAX_SAVED_IDS; i++)
    {
        agent->sent_ids[i].valid = FALSE;
    }
}

stun_valid_status_e stun_agent_validate(stun_agent_t * agent, stun_msg_t * msg, const uint8_t * buffer, size_t buffer_len)
{
    stun_trans_id msg_id;
    int len;
    uint8_t * key = NULL;
    size_t key_len;
    int sent_id_idx = -1;
    uint16_t unknown;
    uint8_t long_term_key[16] = { 0 };
    bool long_term_key_valid = FALSE;

    len = stun_msg_valid_buflen(buffer, buffer_len,  1);
    if (len == STUN_MSG_BUFFER_INVALID)
    {
        return STUN_VALIDATION_NOT_STUN;
    }
    else if (len == STUN_MSG_BUFFER_INCOMPLETE)
    {
        return STUN_VALIDATION_INCOMPLETE_STUN;
    }
    else if (len != (int) buffer_len)
    {
        return STUN_VALIDATION_NOT_STUN;
    }

    msg->buffer = (uint8_t *) buffer;
    msg->buffer_len = buffer_len;
    msg->agent = agent;
    msg->key = NULL;
    msg->key_len = 0;
    msg->long_term_valid = FALSE;

    /* TODO: reject it or not ? */
    if (!stun_msg_has_cookie(msg))
    {
        stun_debug("STUN demux error: no cookie!");
        return STUN_VALIDATION_BAD_REQUEST;
    }

    if (stun_msg_get_class(msg) == STUN_RESPONSE || stun_msg_get_class(msg) == STUN_ERROR)
    {
        stun_msg_id(msg, msg_id);
        for (sent_id_idx = 0; sent_id_idx < STUN_AGENT_MAX_SAVED_IDS; sent_id_idx++)
        {
            if (agent->sent_ids[sent_id_idx].valid == TRUE &&
                    agent->sent_ids[sent_id_idx].method == stun_msg_get_method(msg) &&
                    memcmp(msg_id, agent->sent_ids[sent_id_idx].id, sizeof(stun_trans_id)) == 0)
            {
                key = agent->sent_ids[sent_id_idx].key;
                key_len = agent->sent_ids[sent_id_idx].key_len;
                memcpy(long_term_key, agent->sent_ids[sent_id_idx].long_term_key, sizeof(long_term_key));
                long_term_key_valid = agent->sent_ids[sent_id_idx].long_term_valid;
                break;
            }
        }
        if (sent_id_idx == STUN_AGENT_MAX_SAVED_IDS)
        {
            return STUN_VALIDATION_UNMATCHED_RESPONSE;
        }
    }

    if (sent_id_idx != -1 && sent_id_idx < STUN_AGENT_MAX_SAVED_IDS)
    {
        agent->sent_ids[sent_id_idx].valid = FALSE;
    }

    if (stun_agent_find_unknowns(agent, msg, &unknown, 1) > 0)
    {
        if (stun_msg_get_class(msg) == STUN_REQUEST)
            return STUN_VALIDATION_UNKNOWN_REQUEST_ATTRIBUTE;
        else
            return STUN_VALIDATION_UNKNOWN_ATTRIBUTE;
    }
    return STUN_VALIDATION_SUCCESS;
}

bool stun_agent_forget_trans(stun_agent_t * agent, stun_trans_id id)
{
    int i;

    for (i = 0; i < STUN_AGENT_MAX_SAVED_IDS; i++)
    {
        if (agent->sent_ids[i].valid == TRUE && memcmp(id, agent->sent_ids[i].id, sizeof(stun_trans_id)) == 0)
        {
            agent->sent_ids[i].valid = FALSE;
            return TRUE;
        }
    }

    return FALSE;
}

bool stun_agent_init_request(stun_agent_t * agent, stun_msg_t * msg, uint8_t * buffer, size_t buffer_len, stun_method_e m)
{
    bool ret;
    stun_trans_id id;

    msg->buffer = buffer;
    msg->buffer_len = buffer_len;
    msg->agent = agent;
    msg->key = NULL;
    msg->key_len = 0;
    msg->long_term_valid = FALSE;

    stun_make_transid(id);

    ret = stun_msg_init(msg, STUN_REQUEST, m, id);

    if (ret)
    {
        uint32_t cookie = htonl(STUN_MAGIC_COOKIE);
        memcpy(msg->buffer + STUN_MSG_TRANS_ID_POS, &cookie, sizeof(cookie));
    }

    return ret;
}


bool stun_agent_init_indication(stun_agent_t * agent, stun_msg_t * msg, uint8_t * buffer, size_t buffer_len, stun_method_e m)
{
    bool ret;
    stun_trans_id id;

    msg->buffer = buffer;
    msg->buffer_len = buffer_len;
    msg->agent = agent;
    msg->key = NULL;
    msg->key_len = 0;
    msg->long_term_valid = FALSE;

    stun_make_transid(id);
    ret = stun_msg_init(msg, STUN_INDICATION, m, id);

    if (ret)
    {
        uint32_t cookie = htonl(STUN_MAGIC_COOKIE);
        memcpy(msg->buffer + STUN_MSG_TRANS_ID_POS, &cookie, sizeof(cookie));
    }

    return ret;
}


bool stun_agent_init_response(stun_agent_t * agent, stun_msg_t * msg, uint8_t * buffer, size_t buffer_len, const stun_msg_t * request)
{

    stun_trans_id id;

    if (stun_msg_get_class(request) != STUN_REQUEST)
    {
        return FALSE;
    }

    msg->buffer = buffer;
    msg->buffer_len = buffer_len;
    msg->agent = agent;
    msg->key = request->key;
    msg->key_len = request->key_len;
    memmove(msg->long_term_key, request->long_term_key, sizeof(msg->long_term_key));
    msg->long_term_valid = request->long_term_valid;

    stun_msg_id(request, id);

    if (stun_msg_init(msg, STUN_RESPONSE, stun_msg_get_method(request), id))
    {
        return TRUE;
    }
    return FALSE;
}


bool stun_agent_init_error(stun_agent_t * agent, stun_msg_t * msg,
                           uint8_t * buffer, size_t buffer_len, const stun_msg_t * request,
                           stun_err_e err)
{
    stun_trans_id id;

    if (stun_msg_get_class(request) != STUN_REQUEST)
    {
        return FALSE;
    }

    msg->buffer = buffer;
    msg->buffer_len = buffer_len;
    msg->agent = agent;
    msg->key = request->key;
    msg->key_len = request->key_len;
    memmove(msg->long_term_key, request->long_term_key, sizeof(msg->long_term_key));
    msg->long_term_valid = request->long_term_valid;

    stun_msg_id(request, id);


    if (stun_msg_init(msg, STUN_ERROR, stun_msg_get_method(request), id))
    {
        if (stun_msg_append_error(msg, err) == STUN_MSG_RET_SUCCESS)
        {
            return TRUE;
        }
    }
    return FALSE;
}


size_t stun_agent_build_unknown_attributes_error(stun_agent_t * agent,
        stun_msg_t * msg, uint8_t * buffer, size_t buffer_len,
        const stun_msg_t * request)
{

    unsigned counter;
    uint16_t ids[STUN_AGENT_MAX_UNKNOWN_ATTRIBUTES];

    counter = stun_agent_find_unknowns(agent, request, ids, STUN_AGENT_MAX_UNKNOWN_ATTRIBUTES);

    if (stun_agent_init_error(agent, msg, buffer, buffer_len, request, STUN_ERROR_UNKNOWN_ATTRIBUTE) == FALSE)
    {
        return 0;
    }

    /* NOTE: Old RFC3489 compatibility:
     * When counter is odd, duplicate one value for 32-bits padding. */
    if (!stun_msg_has_cookie(request) && (counter & 1))
        ids[counter++] = ids[0];

    if (stun_msg_append_bytes(msg, STUN_ATT_UNKNOWN_ATTRIBUTES,
                                  ids, counter * 2) == STUN_MSG_RET_SUCCESS)
    {
        return stun_agent_finish_message(agent, msg, request->key, request->key_len);
    }

    return 0;
}


size_t stun_agent_finish_message(stun_agent_t * agent, stun_msg_t * msg, const uint8_t * key, size_t key_len)
{
    uint8_t * ptr;
    //uint32_t fpr;
    int saved_id_idx = 0;
    uint8_t md5[16];

    if (stun_msg_get_class(msg) == STUN_REQUEST)
    {
        for (saved_id_idx = 0; saved_id_idx < STUN_AGENT_MAX_SAVED_IDS; saved_id_idx++)
        {
            if (agent->sent_ids[saved_id_idx].valid == FALSE)
            {
                break;
            }
        }
    }
    if (saved_id_idx == STUN_AGENT_MAX_SAVED_IDS)
    {
        stun_debug("WARNING: Saved IDs full. STUN message dropped.");
        return 0;
    }

    if (msg->key != NULL)
    {
        key = msg->key;
        key_len = msg->key_len;
    }

    if (key != NULL)
    {
        bool skip = FALSE;
		uint8_t * realm = NULL;
		uint8_t * username = NULL;
		uint16_t realm_len;
		uint16_t username_len;

		realm = (uint8_t *)stun_msg_find(msg, STUN_ATT_REALM, &realm_len);
		username = (uint8_t *)stun_msg_find(msg, STUN_ATT_USERNAME, &username_len);
		if (username == NULL || realm == NULL)
		{
			skip = TRUE;
		}
		else
		{
			stun_hash_creds(realm, realm_len, username, username_len, key, key_len, md5);
			memcpy(msg->long_term_key, md5, sizeof(msg->long_term_key));
			msg->long_term_valid = TRUE;
		}

        /* If no realm/username and long term credentials,
        then don't send the message integrity */
        if (skip == FALSE)
        {
            ptr = stun_msg_append(msg, STUN_ATT_MESSAGE_INTEGRITY, 20);
            if (ptr == NULL)
            {
                return 0;
            }

            stun_sha1(msg->buffer, stun_msg_len(msg), stun_msg_len(msg) - 20, ptr, md5, sizeof(md5), FALSE);          

            stun_debug(" Message HMAC-SHA1 message integrity:");
            stun_debug_bytes("  key     : ", key, key_len);
            stun_debug_bytes("  sent    : ", ptr, 20);
        }
    }

    if (stun_msg_get_class(msg) == STUN_REQUEST)
    {
        stun_msg_id(msg, agent->sent_ids[saved_id_idx].id);
        agent->sent_ids[saved_id_idx].method = stun_msg_get_method(msg);
        agent->sent_ids[saved_id_idx].key = (uint8_t *) key;
        agent->sent_ids[saved_id_idx].key_len = key_len;
        memcpy(agent->sent_ids[saved_id_idx].long_term_key, msg->long_term_key,  sizeof(msg->long_term_key));
        agent->sent_ids[saved_id_idx].long_term_valid = msg->long_term_valid;
        agent->sent_ids[saved_id_idx].valid = TRUE;
    }

    msg->key = (uint8_t *) key;
    msg->key_len = key_len;
    return stun_msg_len(msg);
}

static int stun_agent_is_unknown(stun_agent_t * agent, uint16_t type)
{

    uint16_t * known_attr = (uint16_t *)STUN_ALL_KNOWN_ATTRS; //agent->known_attributes;

    while (*known_attr != 0)
    {
        if (*known_attr == type)
        {
            return FALSE;
        }
        known_attr++;
    }

    return TRUE;
}

static uint32_t stun_agent_find_unknowns(stun_agent_t * agent, const stun_msg_t * msg, uint16_t * list, unsigned max)
{
    unsigned count = 0;
    uint16_t len = stun_msg_len(msg);
    size_t offset = 0;

    offset = STUN_MSG_ATTRIBUTES_POS;

    while ((offset < len) && (count < max))
    {
        size_t alen = stun_getw(msg->buffer + offset + STUN_ATT_TYPE_LEN);
        uint16_t atype = stun_getw(msg->buffer + offset);

        if (!stun_optional(atype) && stun_agent_is_unknown(agent, atype))
        {
            stun_debug("STUN unknown: attribute 0x%04x(%u bytes)", (unsigned)atype, (unsigned)alen);
            list[count++] = htons(atype);
        }

        if (!(agent->usage_flags & STUN_AGENT_NO_ALIGNED_ATTRIBUTES))
            alen = stun_align(alen);

        offset += STUN_ATT_VALUE_POS + alen;
    }

    stun_debug("STUN unknown: %u mandatory attribute(s)!", count);
    return count;
}
