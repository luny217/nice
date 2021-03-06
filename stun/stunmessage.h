/* This file is part of the Nice GLib ICE library. */

#ifndef _STUN_MSG_H
#define _STUN_MSG_H

/**
 * SECTION:stunmessage
 * @short_description: STUN messages parsing and formatting functions
 * @include: stun/stunmessage.h
 * @see_also: #stun_agent_t
 * @stability: Stable
 *
 * The STUN Messages API allows you to create STUN messages easily as well as to
 * parse existing messages.
 *
 */


#ifdef _WIN32
#include "win32_common.h"
#else
#include <stdint.h>
#include <stdbool.h>
#endif

#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include "constants.h"

typedef struct _stun_msg_st stun_msg_t;

/**
 * StunClass:
 * @STUN_REQUEST: A STUN Request message
 * @STUN_INDICATION: A STUN indication message
 * @STUN_RESPONSE: A STUN Response message
 * @STUN_ERROR: A STUN Error message
 *
 * This enum is used to represent the class of
 * a STUN message, as defined in RFC5389
 */

/* Message classes */
typedef enum
{
    STUN_REQUEST = 0,
    STUN_INDICATION = 1,
    STUN_RESPONSE = 2,
    STUN_ERROR = 3
} StunClass;


/**
 * stun_method_e:
 * @STUN_BINDING: The Binding method as defined by the RFC5389
 * @STUN_SHARED_SECRET: The Shared-Secret method as defined by the RFC3489
 * @STUN_ALLOCATE: The Allocate method as defined by the TURN draft 12
 * @STUN_SET_ACTIVE_DST: The Set-Active-Destination method as defined by
 * the TURN draft 4
 * @STUN_REFRESH: The Refresh method as defined by the TURN draft 12
 * @STUN_SEND: The Send method as defined by the TURN draft 00
 * @STUN_CONNECT: The Connect method as defined by the TURN draft 4
 * @STUN_OLD_SET_ACTIVE_DST: The older Set-Active-Destination method as
 * defined by the TURN draft 0
 * @STUN_IND_SEND: The Send method used in indication messages as defined
 * by the TURN draft 12
 * @STUN_IND_DATA: The Data method used in indication messages as defined
 * by the TURN draft 12
 * @STUN_IND_CONNECT_STATUS:  The Connect-Status method used in indication
 * messages as defined by the TURN draft 4
 * @STUN_CREATEPERMISSION: The CreatePermission method as defined by
 * the TURN draft 12
 * @STUN_CHANNELBIND: The ChannelBind method as defined by the TURN draft 12
 *
 * This enum is used to represent the method of
 * a STUN message, as defined by various RFCs
 */
/* Message methods */
typedef enum
{
    STUN_BINDING = 0x001,  /* RFC5389 */
    STUN_SHARED_SECRET = 0x002, /* old RFC3489 */
    STUN_ALLOCATE = 0x003,  /* TURN-12 */
    STUN_SET_ACTIVE_DST = 0x004, /* TURN-04 */
    STUN_REFRESH = 0x004, /* TURN-12 */
    STUN_SEND = 0x004, /* TURN-00 */
    STUN_CONNECT = 0x005,  /* TURN-04 */
    STUN_OLD_SET_ACTIVE_DST = 0x006, /* TURN-00 */
    STUN_IND_SEND = 0x006,  /* TURN-12 */
    STUN_IND_DATA = 0x007,  /* TURN-12 */
    STUN_IND_CONNECT_STATUS = 0x008, /* TURN-04 */
    STUN_CREATEPERMISSION = 0x008, /* TURN-12 */
    STUN_CHANNELBIND = 0x009 /* TURN-12 */
} stun_method_e; 

/**
 * stun_attr_e:
 * @STUN_ATT_MAPPED_ADDRESS: The MAPPED-ADDRESS attribute as defined
 * by RFC5389
 * @STUN_ATT_RESPONSE_ADDRESS: The RESPONSE-ADDRESS attribute as defined
 * by RFC3489
 * @STUN_ATT_CHANGE_REQUEST: The CHANGE-REQUEST attribute as defined by
 * RFC3489
 * @STUN_ATT_SOURCE_ADDRESS: The SOURCE-ADDRESS attribute as defined by
 * RFC3489
 * @STUN_ATT_CHANGED_ADDRESS: The CHANGED-ADDRESS attribute as defined
 * by RFC3489
 * @STUN_ATT_USERNAME: The USERNAME attribute as defined by RFC5389
 * @STUN_ATT_PASSWORD: The PASSWORD attribute as defined by RFC3489
 * @STUN_ATT_MESSAGE_INTEGRITY: The MESSAGE-INTEGRITY attribute as defined
 * by RFC5389
 * @STUN_ATT_ERROR_CODE: The ERROR-CODE attribute as defined by RFC5389
 * @STUN_ATT_UNKNOWN_ATTRIBUTES: The UNKNOWN-ATTRIBUTES attribute as
 * defined by RFC5389
 * @STUN_ATT_REFLECTED_FROM: The REFLECTED-FROM attribute as defined
 * by RFC3489
 * @STUN_ATT_CHANNEL_NUMBER: The CHANNEL-NUMBER attribute as defined by
 * TURN draft 09 and 12
 * @STUN_ATT_LIFETIME: The LIFETIME attribute as defined by TURN
 * draft 04, 09 and 12
 * @STUN_ATT_MS_ALTERNATE_SERVER: The ALTERNATE-SERVER attribute as
 * defined by [MS-TURN]
 * @STUN_ATT_MAGIC_COOKIE: The MAGIC-COOKIE attribute as defined by
 * the rosenberg-midcom TURN draft 08
 * @STUN_ATT_BANDWIDTH: The BANDWIDTH attribute as defined by TURN draft 04
 * @STUN_ATT_DESTINATION_ADDRESS: The DESTINATION-ADDRESS attribute as
 * defined by the rosenberg-midcom TURN draft 08
 * @STUN_ATT_REMOTE_ADDRESS: The REMOTE-ADDRESS attribute as defined by
 * TURN draft 04
 * @STUN_ATT_PEER_ADDRESS: The PEER-ADDRESS attribute as defined by
 * TURN draft 09
 * @STUN_ATT_XOR_PEER_ADDRESS: The XOR-PEER-ADDRESS attribute as defined
 * by TURN draft 12
 * @STUN_ATT_DATA: The DATA attribute as defined by TURN draft 04,
 * 09 and 12
 * @STUN_ATT_REALM: The REALM attribute as defined by RFC5389
 * @STUN_ATT_NONCE: The NONCE attribute as defined by RFC5389
 * @STUN_ATT_RELAY_ADDRESS: The RELAY-ADDRESS attribute as defined by
 * TURN draft 04
 * @STUN_ATT_RELAYED_ADDRESS: The RELAYED-ADDRESS attribute as defined by
 * TURN draft 09
 * @STUN_ATT_XOR_RELAYED_ADDRESS: The XOR-RELAYED-ADDRESS attribute as
 * defined by TURN draft 12
 * @STUN_ATT_REQUESTED_ADDRESS_TYPE: The REQUESTED-ADDRESS-TYPE attribute
 * as defined by TURN-IPV6 draft 05
 * @STUN_ATT_REQUESTED_PORT_PROPS: The REQUESTED-PORT-PROPS attribute
 * as defined by TURN draft 04
 * @STUN_ATT_REQUESTED_PROPS: The REQUESTED-PROPS attribute as defined
 * by TURN draft 09
 * @STUN_ATT_EVEN_PORT: The EVEN-PORT attribute as defined by TURN draft 12
 * @STUN_ATT_REQUESTED_TRANSPORT: The REQUESTED-TRANSPORT attribute as
 * defined by TURN draft 12
 * @STUN_ATT_DONT_FRAGMENT: The DONT-FRAGMENT attribute as defined
 * by TURN draft 12
 * @STUN_ATT_XOR_MAPPED_ADDRESS: The XOR-MAPPED-ADDRESS attribute as
 * defined by RFC5389
 * @STUN_ATT_TIMER_VAL: The TIMER-VAL attribute as defined by TURN draft 04
 * @STUN_ATT_REQUESTED_IP: The REQUESTED-IP attribute as defined by
 * TURN draft 04
 * @STUN_ATT_RESERVATION_TOKEN: The RESERVATION-TOKEN attribute as defined
 * by TURN draft 09 and 12
 * @STUN_ATT_CONNECT_STAT: The CONNECT-STAT attribute as defined by TURN
 * draft 04
 * @STUN_ATT_PRIORITY: The PRIORITY attribute as defined by ICE draft 19
 * @STUN_ATT_USE_CANDIDATE: The USE-CANDIDATE attribute as defined by
 * ICE draft 19
 * @STUN_ATT_OPTIONS: The OPTIONS optional attribute as defined by
 * libjingle
 * @STUN_ATT_MS_VERSION: The MS-VERSION optional attribute as defined
 * by [MS-TURN]
 * @STUN_ATT_MS_XOR_MAPPED_ADDRESS: The XOR-MAPPED-ADDRESS optional
 * attribute as defined by [MS-TURN]
 * @STUN_ATT_SOFTWARE: The SOFTWARE optional attribute as defined by RFC5389
 * @STUN_ATT_ALTERNATE_SERVER: The ALTERNATE-SERVER optional attribute as
 * defined by RFC5389
 * @STUN_ATT_FINGERPRINT: The FINGERPRINT optional attribute as defined
 * by RFC5389
 * @STUN_ATT_ICE_CONTROLLED: The ICE-CONTROLLED optional attribute as
 * defined by ICE draft 19
 * @STUN_ATT_ICE_CONTROLLING: The ICE-CONTROLLING optional attribute as
 * defined by ICE draft 19
 * @STUN_ATT_MS_SEQUENCE_NUMBER: The MS-SEQUENCE NUMBER optional attribute
 * as defined by [MS-TURN]
 * @STUN_ATT_CANDIDATE_IDENTIFIER: The CANDIDATE-IDENTIFIER optional
 * attribute as defined by [MS-ICE2]
 *
 * Known STUN attribute types as defined by various RFCs and drafts
 */
/* Should be in sync with stun_is_unknown() */
typedef enum
{
    /* Mandatory attributes */
    /* 0x0000 */        /* reserved */
    STUN_ATT_MAPPED_ADDRESS = 0x0001,  /* RFC5389 */
    STUN_ATT_RESPONSE_ADDRESS = 0x0002, /* old RFC3489 */
    STUN_ATT_CHANGE_REQUEST = 0x0003,  /* old RFC3489 */
    STUN_ATT_SOURCE_ADDRESS = 0x0004,  /* old RFC3489 */
    STUN_ATT_CHANGED_ADDRESS = 0x0005, /* old RFC3489 */
    STUN_ATT_USERNAME = 0x0006,    /* RFC5389 */
    STUN_ATT_PASSWORD = 0x0007,  /* old RFC3489 */
    STUN_ATT_MESSAGE_INTEGRITY = 0x0008,  /* RFC5389 */
    STUN_ATT_ERROR_CODE = 0x0009,    /* RFC5389 */
    STUN_ATT_UNKNOWN_ATTRIBUTES = 0x000A,  /* RFC5389 */
    STUN_ATT_REFLECTED_FROM = 0x000B,  /* old RFC3489 */
    STUN_ATT_CHANNEL_NUMBER = 0x000C,      /* TURN-12 */
    STUN_ATT_LIFETIME = 0x000D,    /* TURN-12 */
    /* MS_ALTERNATE_SERVER is only used by Microsoft's dialect, probably should
     * not to be placed in STUN_ALL_KNOWN_ATTRS */
    STUN_ATT_MS_ALTERNATE_SERVER = 0x000E, /* MS-TURN */
    STUN_ATT_MAGIC_COOKIE = 0x000F,      /* midcom-TURN 08 */
    STUN_ATT_BANDWIDTH = 0x0010,    /* TURN-04 */
    STUN_ATT_DESTINATION_ADDRESS = 0x0011,      /* midcom-TURN 08 */
    STUN_ATT_REMOTE_ADDRESS = 0x0012,  /* TURN-04 */
    STUN_ATT_PEER_ADDRESS = 0x0012,  /* TURN-09 */
    STUN_ATT_XOR_PEER_ADDRESS = 0x0012,  /* TURN-12 */
    STUN_ATT_DATA = 0x0013,    /* TURN-12 */
    STUN_ATT_REALM = 0x0014,    /* RFC5389 */
    STUN_ATT_NONCE = 0x0015,    /* RFC5389 */
    STUN_ATT_RELAY_ADDRESS = 0x0016,  /* TURN-04 */
    STUN_ATT_RELAYED_ADDRESS = 0x0016,  /* TURN-09 */
    STUN_ATT_XOR_RELAYED_ADDRESS = 0x0016,  /* TURN-12 */
    STUN_ATT_REQUESTED_ADDRESS_TYPE = 0x0017, /* TURN-IPv6-05 */
    STUN_ATT_REQUESTED_PORT_PROPS = 0x0018, /* TURN-04 */
    STUN_ATT_REQUESTED_PROPS = 0x0018, /* TURN-09 */
    STUN_ATT_EVEN_PORT = 0x0018, /* TURN-12 */
    STUN_ATT_REQUESTED_TRANSPORT = 0x0019, /* TURN-12 */
    STUN_ATT_DONT_FRAGMENT = 0x001A, /* TURN-12 */
    /* 0x001B */        /* reserved */
    /* 0x001C */        /* reserved */
    /* 0x001D */        /* reserved */
    /* 0x001E */        /* reserved */
    /* 0x001F */        /* reserved */
    STUN_ATT_XOR_MAPPED_ADDRESS = 0x0020,  /* RFC5389 */
    STUN_ATT_TIMER_VAL = 0x0021,    /* TURN-04 */
    STUN_ATT_REQUESTED_IP = 0x0022,  /* TURN-04 */
    STUN_ATT_RESERVATION_TOKEN = 0x0022,  /* TURN-09 */
    STUN_ATT_CONNECT_STAT = 0x0023,  /* TURN-04 */
    STUN_ATT_PRIORITY = 0x0024,    /* ICE-19 */
    STUN_ATT_USE_CANDIDATE = 0x0025,  /* ICE-19 */
    /* 0x0026 */        /* reserved */
    /* 0x0027 */        /* reserved */
    /* 0x0028 */        /* reserved */
    /* 0x0029 */        /* reserved */
    /* 0x002A-0x7fff */      /* reserved */

    /* Optional attributes */
    /* 0x8000-0x8021 */      /* reserved */
    STUN_ATT_OPTIONS = 0x8001, /* libjingle */
    STUN_ATT_MS_VERSION = 0x8008,  /* MS-TURN */
    STUN_ATT_MS_XOR_MAPPED_ADDRESS = 0x8020,  /* MS-TURN */
    STUN_ATT_SOFTWARE = 0x8022,    /* RFC5389 */
    STUN_ATT_ALTERNATE_SERVER = 0x8023,  /* RFC5389 */
    /* 0x8024 */        /* reserved */
    /* 0x8025 */        /* reserved */
    /* 0x8026 */        /* reserved */
    /* 0x8027 */        /* reserved */
    STUN_ATT_FINGERPRINT = 0x8028,  /* RFC5389 */
    STUN_ATT_ICE_CONTROLLED = 0x8029,  /* ICE-19 */
    STUN_ATT_ICE_CONTROLLING = 0x802A,  /* ICE-19 */
    /* 0x802B-0x804F */      /* reserved */
    STUN_ATT_MS_SEQUENCE_NUMBER = 0x8050,   /* MS-TURN */
    /* 0x8051-0x8053 */      /* reserved */
    STUN_ATT_CANDIDATE_IDENTIFIER = 0x8054  /* MS-ICE2 */
    /* 0x8055-0xFFFF */      /* reserved */
} stun_attr_e; 

/**
 * STUN_ALL_KNOWN_ATTRS:
 *
 * An array containing all the currently known and defined mandatory attributes
 * from stun_attr_e
 */
/* Should be in sync with stun_attr_e */
static const uint16_t STUN_ALL_KNOWN_ATTRS[] =
{
    STUN_ATT_MAPPED_ADDRESS,
    STUN_ATT_RESPONSE_ADDRESS,
    STUN_ATT_CHANGE_REQUEST,
    STUN_ATT_SOURCE_ADDRESS,
    STUN_ATT_CHANGED_ADDRESS,
    STUN_ATT_USERNAME,
    STUN_ATT_PASSWORD,
    STUN_ATT_MESSAGE_INTEGRITY,
    STUN_ATT_ERROR_CODE,
    STUN_ATT_UNKNOWN_ATTRIBUTES,
    STUN_ATT_REFLECTED_FROM,
    STUN_ATT_CHANNEL_NUMBER,
    STUN_ATT_LIFETIME,
    STUN_ATT_MAGIC_COOKIE,
    STUN_ATT_BANDWIDTH,
    STUN_ATT_DESTINATION_ADDRESS,
    STUN_ATT_REMOTE_ADDRESS,
    STUN_ATT_PEER_ADDRESS,
    STUN_ATT_XOR_PEER_ADDRESS,
    STUN_ATT_DATA,
    STUN_ATT_REALM,
    STUN_ATT_NONCE,
    STUN_ATT_RELAY_ADDRESS,
    STUN_ATT_RELAYED_ADDRESS,
    STUN_ATT_XOR_RELAYED_ADDRESS,
    STUN_ATT_REQUESTED_ADDRESS_TYPE,
    STUN_ATT_REQUESTED_PORT_PROPS,
    STUN_ATT_REQUESTED_PROPS,
    STUN_ATT_EVEN_PORT,
    STUN_ATT_REQUESTED_TRANSPORT,
    STUN_ATT_DONT_FRAGMENT,
    STUN_ATT_XOR_MAPPED_ADDRESS,
    STUN_ATT_TIMER_VAL,
    STUN_ATT_REQUESTED_IP,
    STUN_ATT_RESERVATION_TOKEN,
    STUN_ATT_CONNECT_STAT,
    STUN_ATT_PRIORITY,
    STUN_ATT_USE_CANDIDATE,
    0
};

/**
 * stun_trans_id:
 *
 * A type that holds a STUN transaction id.
 */
typedef uint8_t stun_trans_id[STUN_MSG_TRANS_ID_LEN];


/**
 * stun_err_e:
 * @STUN_ERROR_TRY_ALTERNATE: The ERROR-CODE value for the
 * "Try Alternate" error as defined in RFC5389
 * @STUN_ERROR_BAD_REQUEST: The ERROR-CODE value for the
 * "Bad Request" error as defined in RFC5389
 * @STUN_ERROR_UNAUTHORIZED: The ERROR-CODE value for the
 * "Unauthorized" error as defined in RFC5389
 * @STUN_ERROR_UNKNOWN_ATTRIBUTE: The ERROR-CODE value for the
 * "Unknown Attribute" error as defined in RFC5389
 * @STUN_ERROR_ALLOCATION_MISMATCH:The ERROR-CODE value for the
 * "Allocation Mismatch" error as defined in TURN draft 12.
 * Equivalent to the "No Binding" error defined in TURN draft 04.
 * @STUN_ERROR_STALE_NONCE: The ERROR-CODE value for the
 * "Stale Nonce" error as defined in RFC5389
 * @STUN_ERROR_ACT_DST_ALREADY: The ERROR-CODE value for the
 * "Active Destination Already Set" error as defined in TURN draft 04.
 * @STUN_ERROR_UNSUPPORTED_FAMILY: The ERROR-CODE value for the
 * "Address Family not Supported" error as defined in TURN IPV6 Draft 05.
 * @STUN_ERROR_WRONG_CREDENTIALS: The ERROR-CODE value for the
 * "Wrong Credentials" error as defined in TURN Draft 12.
 * @STUN_ERROR_UNSUPPORTED_TRANSPORT:he ERROR-CODE value for the
 * "Unsupported Transport Protocol" error as defined in TURN Draft 12.
 * @STUN_ERROR_INVALID_IP: The ERROR-CODE value for the
 * "Invalid IP Address" error as defined in TURN draft 04.
 * @STUN_ERROR_INVALID_PORT: The ERROR-CODE value for the
 * "Invalid Port" error as defined in TURN draft 04.
 * @STUN_ERROR_OP_TCP_ONLY: The ERROR-CODE value for the
 * "Operation for TCP Only" error as defined in TURN draft 04.
 * @STUN_ERROR_CONN_ALREADY: The ERROR-CODE value for the
 * "Connection Already Exists" error as defined in TURN draft 04.
 * @STUN_ERROR_ALLOCATION_QUOTA_REACHED: The ERROR-CODE value for the
 * "Allocation Quota Reached" error as defined in TURN draft 12.
 * @STUN_ERROR_ROLE_CONFLICT:The ERROR-CODE value for the
 * "Role Conflict" error as defined in ICE draft 19.
 * @STUN_ERROR_SERVER_ERROR: The ERROR-CODE value for the
 * "Server Error" error as defined in RFC5389
 * @STUN_ERROR_SERVER_CAPACITY: The ERROR-CODE value for the
 * "Insufficient Capacity" error as defined in TURN draft 04.
 * @STUN_ERROR_INSUFFICIENT_CAPACITY: The ERROR-CODE value for the
 * "Insufficient Capacity" error as defined in TURN draft 12.
 * @STUN_ERROR_MAX: The maximum possible ERROR-CODE value as defined by RFC 5389.
 *
 * STUN error codes as defined by various RFCs and drafts
 */
/* Should be in sync with stun_strerror() */
typedef enum
{
    STUN_ERROR_TRY_ALTERNATE = 300,    /* RFC5389 */
    STUN_ERROR_BAD_REQUEST = 400,    /* RFC5389 */
    STUN_ERROR_UNAUTHORIZED = 401,    /* RFC5389 */
    STUN_ERROR_UNKNOWN_ATTRIBUTE = 420,  /* RFC5389 */
    STUN_ERROR_ALLOCATION_MISMATCH = 437, /* TURN-12 */
    STUN_ERROR_STALE_NONCE = 438,    /* RFC5389 */
    STUN_ERROR_ACT_DST_ALREADY = 439,  /* TURN-04 */
    STUN_ERROR_UNSUPPORTED_FAMILY = 440,    /* TURN-IPv6-05 */
    STUN_ERROR_WRONG_CREDENTIALS = 441,  /* TURN-12 */
    STUN_ERROR_UNSUPPORTED_TRANSPORT = 442,  /* TURN-12 */
    STUN_ERROR_INVALID_IP = 443,    /* TURN-04 */
    STUN_ERROR_INVALID_PORT = 444,    /* TURN-04 */
    STUN_ERROR_OP_TCP_ONLY = 445,    /* TURN-04 */
    STUN_ERROR_CONN_ALREADY = 446,    /* TURN-04 */
    STUN_ERROR_ALLOCATION_QUOTA_REACHED = 486,  /* TURN-12 */
    STUN_ERROR_ROLE_CONFLICT = 487,    /* ICE-19 */
    STUN_ERROR_SERVER_ERROR = 500,    /* RFC5389 */
    STUN_ERROR_SERVER_CAPACITY = 507,  /* TURN-04 */
    STUN_ERROR_INSUFFICIENT_CAPACITY = 508,  /* TURN-12 */
    STUN_ERROR_MAX = 699
} stun_err_e; 


/**
 * stun_msg_ret_e:
 * @STUN_MSG_RET_SUCCESS: The operation was successful
 * @STUN_MSG_RET_NOT_FOUND: The attribute was not found
 * @STUN_MSG_RET_INVALID: The argument or data is invalid
 * @STUN_MSG_RET_NOT_ENOUGH_SPACE: There is not enough space in the
 * message to append data to it, or not enough in an argument to fill it with
 * the data requested.
 * @STUN_MSG_RET_UNSUPPORTED_ADDRESS: The address in the arguments or in
 * the STUN message is not supported.
 *
 * The return value of most stun_msg_* functions.
 * This enum will report on whether an operation was successful or not
 * and what error occured if any.
 */
typedef enum
{
    STUN_MSG_RET_SUCCESS,
    STUN_MSG_RET_NOT_FOUND,
    STUN_MSG_RET_INVALID,
    STUN_MSG_RET_NOT_ENOUGH_SPACE,
    STUN_MSG_RET_UNSUPPORTED_ADDRESS
} stun_msg_ret_e; 

#include "stunagent.h"

/**
 * STUN_MAX_MESSAGE_SIZE:
 *
 * The Maximum size of a STUN message
 */
#define STUN_MAX_MESSAGE_SIZE 65552

/**
 * stun_msg_t:
 * @agent: The agent that created or validated this message
 * @buffer: The buffer containing the STUN message
 * @buffer_len: The length of the buffer (not the size of the message)
 * @key: The short term credentials key to use for authentication validation
 * or that was used to finalize this message
 * @key_len: The length of the associated key
 * @long_term_key: The long term credential key to use for authentication
 * validation or that was used to finalize this message
 * @long_term_valid: Whether or not the #long_term_key variable contains valid
 * data
 *
 * This structure represents a STUN message
 */
struct _stun_msg_st
{
    stun_agent_t * agent;
    uint8_t * buffer;
    size_t buffer_len;
    uint8_t * key;
    size_t key_len;
    uint8_t long_term_key[16];
    bool long_term_valid;
};

/**
 * stun_msg_init:
 * @msg: The #stun_msg_t to initialize
 * @c: STUN message class (host byte order)
 * @m: STUN message method (host byte order)
 * @id: 16-bytes transaction ID
 *
 * Initializes a STUN message buffer, with no attributes.
 * Returns: %TRUE if the initialization was successful
 */
bool stun_msg_init(stun_msg_t * msg, StunClass c, stun_method_e m,
                       const stun_trans_id id);

/**
 * stun_msg_len:
 * @msg: The #stun_msg_t
 *
 * Get the length of the message (including the header)
 *
 * Returns: The length of the message
 */
uint16_t stun_msg_len(const stun_msg_t * msg);

/**
 * stun_msg_find:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to find
 * @palen: A pointer to store the length of the attribute
 *
 * Finds an attribute in a STUN message and fetches its content
 *
 * Returns: A pointer to the start of the attribute payload if found,
 * otherwise NULL.
 */
const void * stun_msg_find(const stun_msg_t * msg, stun_attr_e type,
                               uint16_t * palen);


/**
 * stun_msg_find_flag:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to find
 *
 * Looks for a flag attribute within a valid STUN message.
 *
 * Returns: A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the attribute's size is not zero.
 */
stun_msg_ret_e stun_msg_find_flag(const stun_msg_t * msg,
        stun_attr_e type);

/**
 * stun_msg_find32:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to find
 * @pval: A pointer where to store the value (host byte order)
 *
 * Extracts a 32-bits attribute from a STUN message.
 *
 * Returns:  A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the attribute's size is not
 * 4 bytes.
 */
stun_msg_ret_e stun_msg_find32(const stun_msg_t * msg,
                                      stun_attr_e type, uint32_t * pval);

/**
 * stun_msg_find64:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to find
 * @pval: A pointer where to store the value (host byte order)
 *
 * Extracts a 64-bits attribute from a STUN message.
 *
 * Returns:  A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the attribute's size is not
 * 8 bytes.
 */
stun_msg_ret_e stun_msg_find64(const stun_msg_t * msg,
                                      stun_attr_e type, uint64_t * pval);

/**
 * stun_msg_find_string:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to find
 * @buf: A pointer where to store the data
 * @buflen: The length of the buffer
 *
 * Extracts an UTF-8 string from a valid STUN message.
 *
 * Returns: A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the attribute is improperly
 * encoded
 * %STUN_MSG_RET_NOT_ENOUGH_SPACE is return if the buffer size is too
 * small to hold the string
 *
 <note>
   <para>
    The string will be nul-terminated.
   </para>
 </note>
 *
 */
stun_msg_ret_e stun_msg_find_string(const stun_msg_t * msg,
        stun_attr_e type, char * buf, size_t buflen);

/**
 * stun_msg_find_addr:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to find
 * @addr: The #sockaddr to be filled
 * @addrlen: The size of the @addr variable. Must be set to the size of the
 * @addr socket address and will be set to the size of the extracted socket
 * address.
 *
 * Extracts a network address attribute from a STUN message.
 *
 * Returns: A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the attribute payload size is
 * wrong or if the @addrlen is too small
 * %STUN_MSG_RET_UNSUPPORTED_ADDRESS if the address family is unknown.
 */
stun_msg_ret_e stun_msg_find_addr(const stun_msg_t * msg,
        stun_attr_e type, struct sockaddr_storage * addr, socklen_t * addrlen);

/**
 * stun_msg_find_xor_addr:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to find
 * @addr: The #sockaddr to be filled
 * @addrlen: The size of the @addr variable. Must be set to the size of the
 * @addr socket address and will be set to the size of the
 * extracted socket address.
 *
 * Extracts an obfuscated network address attribute from a STUN message.
 *
 * Returns: A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the attribute payload size is
 * wrong or if the @addrlen is too small
 * %STUN_MSG_RET_UNSUPPORTED_ADDRESS if the address family is unknown.
 */
stun_msg_ret_e stun_msg_find_xor_addr(const stun_msg_t * msg,
        stun_attr_e type, struct sockaddr_storage * addr, socklen_t * addrlen);

/**
 * stun_msg_find_xor_addr_full:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to find
 * @addr: The #sockaddr to be filled
 * @addrlen: The size of the @addr variable. Must be set to the size of the
 * @addr socket address and will be set to the size of the
 * extracted socket address.
 * @magic_cookie: The magic cookie to use to XOR the address.
 *
 * Extracts an obfuscated network address attribute from a STUN message.
 *
 * Returns: A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the attribute payload size is
 * wrong or if the @addrlen is too small
 * %STUN_MSG_RET_UNSUPPORTED_ADDRESS if the address family is unknown.
 */
stun_msg_ret_e stun_msg_find_xor_addr_full(const stun_msg_t * msg,
        stun_attr_e type, struct sockaddr_storage * addr,
        socklen_t * addrlen, uint32_t magic_cookie);


/**
 * stun_msg_find_error:
 * @msg: The #stun_msg_t
 * @code: A  pointer where to store the value
 *
 * Extract the error response code from a STUN message
 *
 * Returns: A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the value is invalid
 */
stun_msg_ret_e stun_msg_find_error(const stun_msg_t * msg, int * code);


/**
 * stun_msg_append:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to append
 * @length: The length of the attribute
 *
 * Reserves room for appending an attribute to an unfinished STUN message.
 *
 * Returns: A pointer to an unitialized buffer of @length bytes to
 * where the attribute payload must be written, or NULL if there is not
 * enough room in the STUN message buffer.
 */
void * stun_msg_append(stun_msg_t * msg, stun_attr_e type,
                           size_t length);

/**
 * stun_msg_append_bytes:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to append
 * @data: The data to append
 * @len: The length of the attribute
 *
 * Appends a binary value to a STUN message
 *
 * Returns: A #stun_msg_ret_e value.
 */
stun_msg_ret_e stun_msg_append_bytes(stun_msg_t * msg,
        stun_attr_e type, const void * data, size_t len);

/**
 * stun_msg_append_flag:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to append
 *
 * Appends an empty flag attribute to a STUN message
 *
 * Returns: A #stun_msg_ret_e value.
 */
stun_msg_ret_e stun_msg_append_flag(stun_msg_t * msg,
        stun_attr_e type);

/**
 * stun_msg_append32:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to append
 * @value: The value to append (host byte order)
 *
 * Appends a 32-bits value attribute to a STUN message
 *
 * Returns: A #stun_msg_ret_e value.
 */
stun_msg_ret_e stun_msg_append32(stun_msg_t * msg,
                                        stun_attr_e type, uint32_t value);

/**
 * stun_msg_append64:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to append
 * @value: The value to append (host byte order)
 *
 * Appends a 64-bits value attribute to a STUN message
 *
 * Returns: A #stun_msg_ret_e value.
 */
stun_msg_ret_e stun_msg_append64(stun_msg_t * msg,
                                        stun_attr_e type, uint64_t value);

/**
 * stun_msg_append_string:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to append
 * @str: The string to append
 *
 * Adds an attribute from a nul-terminated string to a STUN message
 *
 * Returns: A #stun_msg_ret_e value.
 */
stun_msg_ret_e stun_msg_append_string(stun_msg_t * msg,
        stun_attr_e type, const char * str);

/**
 * stun_msg_append_addr:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to append
 * @addr: The #sockaddr to be append
 * @addrlen: The size of the @addr variable.
 *
 * Append a network address attribute to a STUN message
 *
 * Returns: A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the @addrlen is too small
 * %STUN_MSG_RET_UNSUPPORTED_ADDRESS if the address family is unknown.
 */
stun_msg_ret_e stun_msg_append_addr(stun_msg_t * msg,
        stun_attr_e type, const struct sockaddr * addr, socklen_t addrlen);

/**
 * stun_msg_append_xor_addr:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to append
 * @addr: The #sockaddr to be append
 * @addrlen: The size of the @addr variable.
 *
 * Append an obfuscated network address attribute to a STUN message
 *
 * Returns: A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the @addrlen is too small
 * %STUN_MSG_RET_UNSUPPORTED_ADDRESS if the address family is unknown.
 */
stun_msg_ret_e stun_msg_append_xor_addr(stun_msg_t * msg,
        stun_attr_e type, const struct sockaddr_storage * addr, socklen_t addrlen);

/**
 * stun_msg_append_xor_addr_full:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to append
 * @addr: The #sockaddr to be append
 * @addrlen: The size of the @addr variable.
 * @magic_cookie: The magic cookie to use to XOR the address.
 *
 * Append an obfuscated network address attribute from a STUN message.
 *
 * Returns: A #stun_msg_ret_e value.
 * %STUN_MSG_RET_INVALID is returned if the @addrlen is too small
 * %STUN_MSG_RET_UNSUPPORTED_ADDRESS if the address family is unknown.
 */
stun_msg_ret_e stun_msg_append_xor_addr_full(stun_msg_t * msg,
        stun_attr_e type, const struct sockaddr_storage * addr, socklen_t addrlen,
        uint32_t magic_cookie);

/**
 * stun_msg_append_error:
 * @msg: The #stun_msg_t
 * @code: The error code value
 *
 * Appends the ERROR-CODE attribute to the STUN message and fills it according
 * to #code
 *
 * Returns: A #stun_msg_ret_e value.
 */
stun_msg_ret_e stun_msg_append_error(stun_msg_t * msg,
        stun_err_e code);

/**
 * STUN_MSG_BUFFER_INCOMPLETE:
 *
 * Convenience macro for stun_msg_valid_buflen() meaning that the
 * data to validate does not hold a complete STUN message
 */
#define STUN_MSG_BUFFER_INCOMPLETE 0

/**
 * STUN_MSG_BUFFER_INVALID:
 *
 * Convenience macro for stun_msg_valid_buflen() meaning that the
 * data to validate is not a valid STUN message
 */
#define STUN_MSG_BUFFER_INVALID -1


/**
 * stun_msg_valid_buflen:
 * @msg: The buffer to validate
 * @length: The length of the buffer
 * @has_padding: Set TRUE if attributes should be padded to multiple of 4 bytes
 *
 * This function will take a data buffer and will try to validate whether it is
 * a STUN message or if it's not or if it's an incomplete STUN message and will
 * provide us with the length of the STUN message.
 *
 * Returns: The length of the valid STUN message in the buffer.
 * <para> See also: #STUN_MSG_BUFFER_INCOMPLETE </para>
 * <para> See also: #STUN_MSG_BUFFER_INVALID </para>
 */
int stun_msg_valid_buflen(const uint8_t * msg, uint32_t length,  int has_padding);

/**
 * StunInputVector:
 * @buffer: a buffer containing already-received binary data
 * @size: length of @buffer, in bytes
 *
 * Container for a single buffer which also stores its length. This is designed
 * for vectored I/O: typically an array of #StunInputVectors is passed to
 * functions, providing multiple buffers which store logically contiguous
 * received data.
 *
 * This is guaranteed to be layed out identically in memory to #n_invector_t.
 *
 * Since: 0.1.5
 */
typedef struct
{
    const uint8_t * buffer;
    size_t size;
} StunInputVector;

/**
 * stun_msg_valid_buflen_fast:
 * @buffers: (array length=n_buffers) (in caller-allocated): array of contiguous
 * #StunInputVectors containing already-received message data
 * @n_buffers: number of entries in @buffers or if -1 , then buffers is
 *  terminated by a #StunInputVector with the buffer pointer being %NULL.
 * @total_length: total number of valid bytes stored consecutively in @buffers
 * @has_padding: %TRUE if attributes should be padded to 4-byte boundaries
 *
 * Quickly validate whether the message in the given @buffers is potentially a
 * valid STUN message, an incomplete STUN message, or if it?s definitely not one
 * at all.
 *
 * This is designed as a first-pass validation only, and does not check the
 * message?s attributes for validity. If this function returns success, the
 * buffers can be compacted and a more thorough validation can be performed
 * using stun_msg_valid_buflen(). If it fails, the buffers
 * definitely do not contain a complete, valid STUN message.
 *
 * Returns: The length of the valid STUN message in the buffer, or zero or -1 on
 * failure
 * <para> See also: #STUN_MSG_BUFFER_INCOMPLETE </para>
 * <para> See also: #STUN_MSG_BUFFER_INVALID </para>
 *
 * Since: 0.1.5
 */
int stun_msg_valid_buflen_fast(const char * buf, uint32_t len, int has_padding);
/**
 * stun_msg_id:
 * @msg: The #stun_msg_t
 * @id: The #stun_trans_id to fill
 *
 * Retreive the STUN transaction id from a STUN message
 */
void stun_msg_id(const stun_msg_t * msg, stun_trans_id id);

/**
 * stun_msg_get_class:
 * @msg: The #stun_msg_t
 *
 * Retreive the STUN class from a STUN message
 *
 * Returns: The #StunClass
 */
StunClass stun_msg_get_class(const stun_msg_t * msg);

/**
 * stun_msg_get_method:
 * @msg: The #stun_msg_t
 *
 * Retreive the STUN method from a STUN message
 *
 * Returns: The #stun_method_e
 */
stun_method_e stun_msg_get_method(const stun_msg_t * msg);

/**
 * stun_msg_has_attribute:
 * @msg: The #stun_msg_t
 * @type: The #stun_attr_e to look for
 *
 * Checks if an attribute is present within a STUN message.
 *
 * Returns: %TRUE if the attribute is found, %FALSE otherwise
 */
bool stun_msg_has_attribute(const stun_msg_t * msg, stun_attr_e type);


/* Defined in stun5389.c */
/**
 * stun_msg_has_cookie:
 * @msg: The #stun_msg_t
 *
 * Checks if the STUN message has a RFC5389 compatible cookie
 *
 * Returns: %TRUE if the cookie is present, %FALSE otherwise
 */
bool stun_msg_has_cookie(const stun_msg_t * msg);


/**
 * stun_optional:
 * @t: An attribute type
 *
 * Helper function that checks whether a STUN attribute is a mandatory
 * or an optional attribute
 *
 * Returns: %TRUE if the attribute is an optional one
 */
bool stun_optional(uint16_t t);

/**
 * stun_strerror:
 * @code: host-byte order error code
 *
 * Transforms a STUN error-code into a human readable string
 *
 * Returns: A static pointer to a nul-terminated error message string.
 */
const char * stun_strerror(stun_err_e code);


#endif /* _STUN_MSG_H */
