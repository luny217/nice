/* This file is part of the Nice GLib ICE library. */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "stunmessage.h"
#include "utils.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif


#include <string.h>
#include <stdlib.h>

bool stun_msg_init(stun_msg_t * msg, StunClass c, stun_method_e m,
                   const stun_trans_id id)
{

    if (msg->buffer_len < STUN_MSG_HEADER_LENGTH)
        return FALSE;

    memset(msg->buffer, 0, 4);
    stun_set_type(msg->buffer, c, m);

    memcpy(msg->buffer + STUN_MSG_TRANS_ID_POS, id, STUN_MSG_TRANS_ID_LEN);

    return TRUE;
}

uint16_t stun_msg_len(const stun_msg_t * msg)
{
	return stun_getw(msg->buffer + STUN_MSG_LENGTH_POS) + STUN_MSG_HEADER_LENGTH;
}

const void * stun_msg_find(const stun_msg_t * msg, stun_attr_e type, uint16_t * palen)
{
    size_t length = stun_msg_len(msg);
    size_t offset = 0;

    offset = STUN_MSG_ATTRIBUTES_POS;

    while (offset < length)
    {
        uint16_t atype = stun_getw(msg->buffer + offset);
        size_t alen = stun_getw(msg->buffer + offset + STUN_ATT_TYPE_LEN);


        offset += STUN_ATT_VALUE_POS;

        if (atype == type)
        {
            *palen = (uint16_t)alen;
            return msg->buffer + offset;
        }

        /* Look for and ignore misordered attributes */
        switch (atype)
        {
            case STUN_ATT_MESSAGE_INTEGRITY:
                /* Only fingerprint may come after M-I */
                if (type == STUN_ATT_FINGERPRINT)
                    break;

            case STUN_ATT_FINGERPRINT:
                /* Nothing may come after FPR */
                return NULL;

            default:
                /* Nothing misordered. */
                break;
        }

        if (!(msg->agent &&
                (msg->agent->usage_flags & STUN_AGENT_NO_ALIGNED_ATTRIBUTES)))
            alen = stun_align(alen);

        offset += alen;
    }

    return NULL;
}


stun_msg_ret_e
stun_msg_find_flag(const stun_msg_t * msg, stun_attr_e type)
{
    const void * ptr;
    uint16_t len = 0;

    ptr = stun_msg_find(msg, type, &len);
    if (ptr == NULL)
        return STUN_MSG_RET_NOT_FOUND;
    return (len == 0) ? STUN_MSG_RET_SUCCESS :
           STUN_MSG_RET_INVALID;
}


stun_msg_ret_e stun_msg_find32(const stun_msg_t * msg, stun_attr_e type, uint32_t * pval)
{
    const void * ptr;
    uint16_t len = 0;

    ptr = stun_msg_find(msg, type, &len);
    if (ptr == NULL)
        return STUN_MSG_RET_NOT_FOUND;

    if (len == 4)
    {
        uint32_t val;

        memcpy(&val, ptr, sizeof(val));
        *pval = ntohl(val);
        return STUN_MSG_RET_SUCCESS;
    }
    return STUN_MSG_RET_INVALID;
}


stun_msg_ret_e stun_msg_find64(const stun_msg_t * msg, stun_attr_e type, uint64_t * pval)
{
    const void * ptr;
    uint16_t len = 0;

    ptr = stun_msg_find(msg, type, &len);
    if (ptr == NULL)
        return STUN_MSG_RET_NOT_FOUND;

    if (len == 8)
    {
        uint32_t tab[2];

        memcpy(tab, ptr, sizeof(tab));
        *pval = ((uint64_t)ntohl(tab[0]) << 32) | ntohl(tab[1]);
        return STUN_MSG_RET_SUCCESS;
    }
    return STUN_MSG_RET_INVALID;
}


stun_msg_ret_e stun_msg_find_string(const stun_msg_t * msg, stun_attr_e type, char * buf, size_t buflen)
{
    const unsigned char * ptr;
    uint16_t len = 0;

    ptr = stun_msg_find(msg, type, &len);
    if (ptr == NULL)
        return STUN_MSG_RET_NOT_FOUND;

    if (len >= buflen)
        return STUN_MSG_RET_NOT_ENOUGH_SPACE;

    memcpy(buf, ptr, len);
    buf[len] = '\0';
    return STUN_MSG_RET_SUCCESS;
}


stun_msg_ret_e stun_msg_find_addr(const stun_msg_t * msg, stun_attr_e type, struct sockaddr_storage * addr, socklen_t * addrlen)
{
    const uint8_t * ptr;
    uint16_t len = 0;

    ptr = stun_msg_find(msg, type, &len);
    if (ptr == NULL)
        return STUN_MSG_RET_NOT_FOUND;

    if (len < 4)
        return STUN_MSG_RET_INVALID;

    switch (ptr[1])
    {
        case 1:
        {
            struct sockaddr_in * ip4 = (struct sockaddr_in *)addr;
            if (((size_t) *addrlen < sizeof(*ip4)) || (len != 8))
            {
                *addrlen = sizeof(*ip4);
                return STUN_MSG_RET_INVALID;
            }

            memset(ip4, 0, *addrlen);
            ip4->sin_family = AF_INET;
#ifdef HAVE_SA_LEN
            ip4->sin_len =
#endif
                * addrlen = sizeof(*ip4);
            memcpy(&ip4->sin_port, ptr + 2, 2);
            memcpy(&ip4->sin_addr, ptr + 4, 4);
            return STUN_MSG_RET_SUCCESS;
        }

        case 2:
        {
            struct sockaddr_in6 * ip6 = (struct sockaddr_in6 *)addr;
            if (((size_t) *addrlen < sizeof(*ip6)) || (len != 20))
            {
                *addrlen = sizeof(*ip6);
                return STUN_MSG_RET_INVALID;
            }

            memset(ip6, 0, *addrlen);
            ip6->sin6_family = AF_INET6;
#ifdef HAVE_SA_LEN
            ip6->sin6_len =
#endif
                * addrlen = sizeof(*ip6);
            memcpy(&ip6->sin6_port, ptr + 2, 2);
            memcpy(&ip6->sin6_addr, ptr + 4, 16);
            return STUN_MSG_RET_SUCCESS;
        }

        default:
            return STUN_MSG_RET_UNSUPPORTED_ADDRESS;
    }
}

stun_msg_ret_e stun_msg_find_xor_addr(const stun_msg_t * msg, stun_attr_e type, struct sockaddr_storage * addr, socklen_t * addrlen)
{
    stun_msg_ret_e val = stun_msg_find_addr(msg, type, addr, addrlen);
    if (val)
        return val;

    return stun_xor_address(msg, addr, *addrlen, STUN_MAGIC_COOKIE);
}

stun_msg_ret_e
stun_msg_find_xor_addr_full(const stun_msg_t * msg, stun_attr_e type,
                            struct sockaddr_storage * addr, socklen_t * addrlen, uint32_t magic_cookie)
{
    stun_msg_ret_e val = stun_msg_find_addr(msg, type, addr, addrlen);
    if (val)
        return val;

    return stun_xor_address(msg, addr, *addrlen, magic_cookie);
}

stun_msg_ret_e stun_msg_find_error(const stun_msg_t * msg, int * code)
{
    uint16_t alen = 0;
    const uint8_t * ptr = stun_msg_find(msg, STUN_ATT_ERROR_CODE, &alen);
    uint8_t class, number;

    if (ptr == NULL)
        return STUN_MSG_RET_NOT_FOUND;
    if (alen < 4)
        return STUN_MSG_RET_INVALID;

    class = ptr[2] & 0x7;
    number = ptr[3];
    if ((class < 3) || (class > 6) || (number > 99))
        return STUN_MSG_RET_INVALID;

    *code = (class * 100) + number;
    return STUN_MSG_RET_SUCCESS;
}

void * stun_msg_append(stun_msg_t * msg, stun_attr_e type, size_t length)
{
    uint8_t * a;
    uint16_t mlen = stun_msg_len(msg);

    if ((size_t)mlen + STUN_ATT_HEADER_LENGTH + length > msg->buffer_len)
        return NULL;

    a = msg->buffer + mlen;
    a = stun_setw(a, type);
    if (msg->agent && (msg->agent->usage_flags & STUN_AGENT_NO_ALIGNED_ATTRIBUTES))
    {
        a = stun_setw(a, (uint16_t)length);
    }
    else
    {
        /* NOTE: If cookie is not present, we need to force the attribute length
         * to a multiple of 4 for compatibility with old RFC3489 */
        a = stun_setw(a, (uint16_t)(stun_msg_has_cookie(msg) ? length : stun_align(length)));

        /* Add padding if needed. Avoid a zero-length memset() call. */
        if (stun_padding(length) > 0)
        {
            memset(a + length, ' ', stun_padding(length));
            mlen += (uint16_t) stun_padding(length);
        }
    }

    mlen += (uint16_t)(4 + length);

    stun_setw(msg->buffer + STUN_MSG_LENGTH_POS, mlen - STUN_MSG_HEADER_LENGTH);
    return a;
}


stun_msg_ret_e stun_msg_append_bytes(stun_msg_t * msg, stun_attr_e type, const void * data, size_t len)
{
    void * ptr = stun_msg_append(msg, type, len);
    if (ptr == NULL)
        return STUN_MSG_RET_NOT_ENOUGH_SPACE;

    if (len > 0)
        memcpy(ptr, data, len);

    return STUN_MSG_RET_SUCCESS;
}


stun_msg_ret_e stun_msg_append_flag(stun_msg_t * msg, stun_attr_e type)
{
    return stun_msg_append_bytes(msg, type, NULL, 0);
}


stun_msg_ret_e stun_msg_append32(stun_msg_t * msg, stun_attr_e type, uint32_t value)
{
    value = htonl(value);
    return stun_msg_append_bytes(msg, type, &value, 4);
}


stun_msg_ret_e stun_msg_append64(stun_msg_t * msg, stun_attr_e type, uint64_t value)
{
    uint32_t tab[2];
    tab[0] = htonl((uint32_t)(value >> 32));
    tab[1] = htonl((uint32_t)value);
    return stun_msg_append_bytes(msg, type, tab, 8);
}


stun_msg_ret_e stun_msg_append_string(stun_msg_t * msg, stun_attr_e type, const char * str)
{
    return stun_msg_append_bytes(msg, type, str, strlen(str));
}

stun_msg_ret_e stun_msg_append_addr(stun_msg_t * msg, stun_attr_e type, const struct sockaddr * addr, socklen_t addrlen)
{
    const void * pa;
    uint8_t * ptr;
    uint16_t alen, port;
    uint8_t family;

    if ((size_t) addrlen < sizeof(struct sockaddr))
        return STUN_MSG_RET_INVALID;

    switch (addr->sa_family)
    {
        case AF_INET:
        {
            const struct sockaddr_in * ip4 = (const struct sockaddr_in *)addr;
            family = 1;
            port = ip4->sin_port;
            alen = 4;
            pa = &ip4->sin_addr;
            break;
        }

        case AF_INET6:
        {
            const struct sockaddr_in6 * ip6 = (const struct sockaddr_in6 *)addr;
            if ((size_t) addrlen < sizeof(*ip6))
                return STUN_MSG_RET_INVALID;

            family = 2;
            port = ip6->sin6_port;
            alen = 16;
            pa = &ip6->sin6_addr;
            break;
        }

        default:
            return STUN_MSG_RET_UNSUPPORTED_ADDRESS;
    }

    ptr = stun_msg_append(msg, type, 4 + alen);
    if (ptr == NULL)
        return STUN_MSG_RET_NOT_ENOUGH_SPACE;

    ptr[0] = 0;
    ptr[1] = family;
    memcpy(ptr + 2, &port, 2);
    memcpy(ptr + 4, pa, alen);
    return STUN_MSG_RET_SUCCESS;
}


stun_msg_ret_e stun_msg_append_xor_addr(stun_msg_t * msg, stun_attr_e type, const struct sockaddr_storage * addr, socklen_t addrlen)
{
    stun_msg_ret_e val;
    /* Must be big enough to hold any supported address: */
    struct sockaddr_storage tmpaddr;

    if ((size_t) addrlen > sizeof(tmpaddr))
        addrlen = sizeof(tmpaddr);
    memcpy(&tmpaddr, addr, addrlen);

    val = stun_xor_address(msg, &tmpaddr, addrlen,
                           STUN_MAGIC_COOKIE);
    if (val)
        return val;

    return stun_msg_append_addr(msg, type, (struct sockaddr *) &tmpaddr, addrlen);
}

stun_msg_ret_e stun_msg_append_xor_addr_full(stun_msg_t * msg, stun_attr_e type,
        const struct sockaddr_storage * addr, socklen_t addrlen,  uint32_t magic_cookie)
{
    stun_msg_ret_e val;
    /* Must be big enough to hold any supported address: */
    struct sockaddr_storage tmpaddr;

    if ((size_t) addrlen > sizeof(tmpaddr))
        addrlen = sizeof(tmpaddr);
    memcpy(&tmpaddr, addr, addrlen);

    val = stun_xor_address(msg, &tmpaddr, addrlen, magic_cookie);
    if (val)
        return val;

    return stun_msg_append_addr(msg, type, (struct sockaddr *) &tmpaddr, addrlen);
}

stun_msg_ret_e stun_msg_append_error(stun_msg_t * msg, stun_err_e code)
{
    const char * str = stun_strerror(code);
    size_t len = strlen(str);

    uint8_t * ptr = stun_msg_append(msg, STUN_ATT_ERROR_CODE, 4 + len);
    if (ptr == NULL)
        return STUN_MSG_RET_NOT_ENOUGH_SPACE;

    memset(ptr, 0, 2);
    ptr[2] = code / 100;
    ptr[3] = code % 100;
    memcpy(ptr + 4, str, len);
    return STUN_MSG_RET_SUCCESS;
}

/* Fast validity check for a potential STUN packet. Examines the type and
 * length, but none of the attributes. Designed to allow vectored I/O on all
 * incoming packets, filtering packets for closer inspection as to whether
 * they?re STUN packets. If they look like they might be, their buffers are
 * compacted to allow a more thorough check. */
int stun_msg_valid_buflen_fast(const char * buf, uint32_t len, int has_padding)
{
    size_t mlen;

    if (buf == NULL || len < 1)
    {
        stun_debug("STUN error: No data!");
        return STUN_MSG_BUFFER_INVALID;
    }

    if (buf[0] >> 6)
    {
        stun_debug("STUN error: RTP or other non-protocol packet!");
        return STUN_MSG_BUFFER_INVALID; // RTP or other non-STUN packet
    }

    if (len < STUN_MSG_LENGTH_POS + STUN_MSG_LENGTH_LEN)
    {
        stun_debug("STUN error: Incomplete STUN message header!");
        return STUN_MSG_BUFFER_INCOMPLETE;
    }

    if (len >= STUN_MSG_LENGTH_POS + STUN_MSG_LENGTH_LEN)
    {
        /* Fast path. */
        mlen = stun_getw(buf + STUN_MSG_LENGTH_POS);
    }
    else
    {
#if 0
        /* Slow path. Tiny buffers abound. */
        size_t skip_remaining = STUN_MSG_LENGTH_POS;
        unsigned int i;

        /* Skip bytes. */
        for (i = 0; (n_buffers >= 0 && i < (unsigned int) n_buffers) ||
                (n_buffers < 0 && buffers[i].buffer != NULL); i++)
        {
            if (buffers[i].size <= skip_remaining)
                skip_remaining -= buffers[i].size;
            else
                break;
        }

        /* Read bytes. May be split over two buffers. We've already checked that
         * @total_length is long enough, so @n_buffers should be too. */
        if (buffers[i].size - skip_remaining > 1)
        {
            mlen = stun_getw(buffers[i].buffer + skip_remaining);
        }
        else
        {
            mlen = (*(buffers[i].buffer + skip_remaining) << 8) |
                   (*(buffers[i + 1].buffer));
        }
#endif
    }

    mlen += STUN_MSG_HEADER_LENGTH;

    if (has_padding && stun_padding(mlen))
    {
        stun_debug("STUN error: Invalid message length: %u!", (unsigned)mlen);
        return STUN_MSG_BUFFER_INVALID; // wrong padding
    }

    if (len < mlen)
    {
        stun_debug("STUN error: Incomplete message: %u of %u bytes!", (unsigned) len, (unsigned) mlen);
        return STUN_MSG_BUFFER_INCOMPLETE; // partial message
    }

    return mlen;
}

int stun_msg_valid_buflen(const uint8_t * buf, uint32_t length,  int has_padding)
{
    ssize_t fast_retval;
    size_t mlen;
    size_t len;
    //StunInputVector input_buffer = { msg, length };

    /* Fast pre-check first. */
    fast_retval = stun_msg_valid_buflen_fast(buf, length, has_padding);
    if (fast_retval <= 0)
        return fast_retval;

    mlen = fast_retval;

    /* Skip past the header (validated above). */
    buf += 20;
    len = mlen - 20;

    /* from then on, we know we have the entire packet in buffer */
    while (len > 0)
    {
        size_t alen;

        if (len < 4)
        {
            stun_debug("STUN error: Incomplete STUN attribute header of length "
                       "%u bytes!", (unsigned)len);
            return STUN_MSG_BUFFER_INVALID;
        }

        alen = stun_getw(buf + STUN_ATT_TYPE_LEN);
        if (has_padding)
            alen = stun_align(alen);

        /* thanks to padding check, if (end > msg) then there is not only one
         * but at least 4 bytes left */
        len -= 4;

        if (len < alen)
        {
            stun_debug("STUN error: %u instead of %u bytes for attribute!", (unsigned)len, (unsigned)alen);
            return STUN_MSG_BUFFER_INVALID; // no room for attribute value + padding
        }

        len -= alen;
        buf += 4 + alen;
    }

    return mlen;
}

void stun_msg_id(const stun_msg_t * msg, stun_trans_id id)
{
    memcpy(id, msg->buffer + STUN_MSG_TRANS_ID_POS, STUN_MSG_TRANS_ID_LEN);
}

stun_method_e stun_msg_get_method(const stun_msg_t * msg)
{
    uint16_t t = stun_getw(msg->buffer);
    /* HACK HACK HACK
       A google/msn data indication is 0x0115 which is contrary to the RFC 5389
       which states that 8th and 12th bits are for the class and that 0x01 is
       for indications...
       So 0x0115 is reported as a "connect error response", while it should be
       a data indication, which message type should actually be 0x0017
       This should fix the issue, and it's considered safe since the "connect"
       method doesn't exist anymore */
    if (t == 0x0115)
        t = 0x0017;
    return (stun_method_e)(((t & 0x3e00) >> 2) | ((t & 0x00e0) >> 1) | (t & 0x000f));
}


StunClass stun_msg_get_class(const stun_msg_t * msg)
{
    uint16_t t = stun_getw(msg->buffer);
    /* HACK HACK HACK
       A google/msn data indication is 0x0115 which is contrary to the RFC 5389
       which states that 8th and 12th bits are for the class and that 0x01 is
       for indications...
       So 0x0115 is reported as a "connect error response", while it should be
       a data indication, which message type should actually be 0x0017
       This should fix the issue, and it's considered safe since the "connect"
       method doesn't exist anymore */
    if (t == 0x0115)
        t = 0x0017;
    return (StunClass)(((t & 0x0100) >> 7) | ((t & 0x0010) >> 4));
}

bool stun_msg_has_attribute(const stun_msg_t * msg, stun_attr_e type)
{
    uint16_t dummy;
    return stun_msg_find(msg, type, &dummy) != NULL;
}


bool stun_optional(uint16_t t)
{
    return (t >> 15) == 1;
}

const char * stun_strerror(stun_err_e code)
{
    static const struct
    {
        stun_err_e code;
        char     phrase[32];
    } tab[] =
    {
        { STUN_ERROR_TRY_ALTERNATE, "Try alternate server" },
        { STUN_ERROR_BAD_REQUEST, "Bad request" },
        { STUN_ERROR_UNAUTHORIZED, "Unauthorized" },
        { STUN_ERROR_UNKNOWN_ATTRIBUTE, "Unknown Attribute" },
        { STUN_ERROR_ALLOCATION_MISMATCH, "Allocation Mismatch" },
        { STUN_ERROR_STALE_NONCE, "Stale Nonce" },
        { STUN_ERROR_ACT_DST_ALREADY, "Active Destination Already Set" },
        { STUN_ERROR_UNSUPPORTED_FAMILY, "Address Family not Supported" },
        { STUN_ERROR_UNSUPPORTED_TRANSPORT, "Unsupported Transport Protocol" },
        { STUN_ERROR_INVALID_IP, "Invalid IP Address" },
        { STUN_ERROR_INVALID_PORT, "Invalid Port" },
        { STUN_ERROR_OP_TCP_ONLY, "Operation for TCP Only" },
        { STUN_ERROR_CONN_ALREADY, "Connection Already Exists" },
        { STUN_ERROR_ALLOCATION_QUOTA_REACHED, "Allocation Quota Reached" },
        { STUN_ERROR_ROLE_CONFLICT, "Role conflict" },
        { STUN_ERROR_SERVER_ERROR, "Server Error" },
        { STUN_ERROR_SERVER_CAPACITY, "Insufficient Capacity" },
        { STUN_ERROR_INSUFFICIENT_CAPACITY, "Insufficient Capacity" },
    };
    const char * str = "Unknown error";
    size_t i;

    for (i = 0; i < (sizeof(tab) / sizeof(tab[0])); i++)
    {
        if (tab[i].code == code)
        {
            str = tab[i].phrase;
            break;
        }
    }

    /* Maximum allowed error message length */
    //  assert (strlen (str) < 128);
    return str;
}
