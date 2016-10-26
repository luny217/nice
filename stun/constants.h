/* This file is part of the Nice GLib ICE library. */

#ifndef _STUN_CONSTANTS_H
#define _STUN_CONSTANTS_H


/**
 * SECTION:stunconstants
 * @short_description: STUN constants
 * @include: stun/constants.h
 * @stability: Stable
 *
 * Various constants defining parts of the STUN and TURN protocols and
 * on-the-wire packet formats.
 */

/**
 * STUN_ATT_LENGTH_LEN:
 *
 * Length of the length field of a STUN attribute (in bytes).
 */
/**
 * STUN_ATT_LENGTH_POS:
 *
 * Offset of the length field of a STUN attribute (in bytes).
 */
/**
 * STUN_ATT_TYPE_LEN:
 *
 * Length of the type field of a STUN attribute (in bytes).
 */
/**
 * STUN_ATT_TYPE_POS:
 *
 * Offset of the type field of a STUN attribute (in bytes).
 */
/**
 * STUN_ATT_VALUE_POS:
 *
 * Offset of the value field of a STUN attribute (in bytes).
 */
/**
 * STUN_ID_LEN:
 *
 * Length of the ID field of a STUN message (in bytes).
 */
/**
 * STUN_MAGIC_COOKIE:
 *
 * Magic cookie value used to identify STUN messages.
 */
/**
 * TURN_MAGIC_COOKIE:
 *
 * Magic cookie value used to identify TURN messages.
 */
/**
 * STUN_MAX_MESSAGE_SIZE_IPV4:
 *
 * Maximum size of a STUN message sent over IPv4 (in bytes).
 */
/**
 * STUN_MAX_MESSAGE_SIZE_IPV6:
 *
 * Maximum size of a STUN message sent over IPv6 (in bytes).
 */
/**
 * STUN_MSG_ATTRIBUTES_POS:
 *
 * Offset of the attributes of a STUN message (in bytes).
 */
/**
 * STUN_MSG_HEADER_LENGTH:
 *
 * Total length of a STUN message header (in bytes).
 */
/**
 * STUN_MSG_LENGTH_LEN:
 *
 * Length of the length field of a STUN message (in bytes).
 */
/**
 * STUN_MSG_LENGTH_POS:
 *
 * Offset of the length field of a STUN message (in bytes).
 */
/**
 * STUN_MSG_TRANS_ID_LEN:
 *
 * Length of the transaction ID field of a STUN message (in bytes).
 */
/**
 * STUN_MSG_TRANS_ID_POS:
 *
 * Offset of the transaction ID field of a STUN message (in bytes).
 */
/**
 * STUN_MSG_TYPE_LEN:
 *
 * Length of the type field of a STUN message (in bytes).
 */
/**
 * STUN_MSG_TYPE_POS:
 *
 * Offset of the type field of a STUN message (in bytes).
 */

#define STUN_MSG_TYPE_POS 0
#define STUN_MSG_TYPE_LEN 2
#define STUN_MSG_LENGTH_POS  (STUN_MSG_TYPE_POS + STUN_MSG_TYPE_LEN)
#define STUN_MSG_LENGTH_LEN 2
#define STUN_MSG_TRANS_ID_POS  (STUN_MSG_LENGTH_POS + STUN_MSG_LENGTH_LEN)
#define STUN_MSG_TRANS_ID_LEN 16
#define STUN_MSG_ATTRIBUTES_POS (STUN_MSG_TRANS_ID_POS + STUN_MSG_TRANS_ID_LEN)

#define STUN_MSG_HEADER_LENGTH STUN_MSG_ATTRIBUTES_POS

#define STUN_ATT_TYPE_POS 0
#define STUN_ATT_TYPE_LEN 2
#define STUN_ATT_LENGTH_POS  (STUN_ATT_TYPE_POS + STUN_ATT_TYPE_LEN)
#define STUN_ATT_LENGTH_LEN 2
#define STUN_ATT_VALUE_POS  (STUN_ATT_LENGTH_POS + STUN_ATT_LENGTH_LEN)

/**
 * STUN_ATT_HEADER_LENGTH:
 *
 * Length of a single STUN attribute header (in bytes).
 */
#define STUN_ATT_HEADER_LENGTH STUN_ATT_VALUE_POS


#define STUN_MAX_MESSAGE_SIZE_IPV4 576
#define STUN_MAX_MESSAGE_SIZE_IPV6 1280
/* #define STUN_MAX_MESSAGE_SIZE STUN_MAX_MESSAGE_SIZE_IPV4 */

#define STUN_ID_LEN 16

/**
 * STUN_AGENT_MAX_SAVED_IDS:
 *
 * Maximum number of simultaneously ongoing STUN transactions.
 */
#define STUN_AGENT_MAX_SAVED_IDS 200

/**
 * STUN_AGENT_MAX_UNKNOWN_ATTRIBUTES:
 *
 * Maximum number of unknown attribute which can be handled in a single STUN
 * message.
 */
#define STUN_AGENT_MAX_UNKNOWN_ATTRIBUTES 256

#define STUN_MAGIC_COOKIE 0x2112A442
#define TURN_MAGIC_COOKIE 0x72c64bc6

#ifndef TRUE
#define TRUE (1 == 1)
#endif

#ifndef FALSE
#define FALSE (0 == 1)
#endif

#endif /* _STUN_CONSTANTS_H */
