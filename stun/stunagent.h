/* This file is part of the Nice GLib ICE library. */

#ifndef _STUN_AGENT_H
#define _STUN_AGENT_H

/**
 * SECTION:stunagent
 * @short_description: STUN agent for building and validating STUN messages
 * @include: stun/stunagent.h
 * @see_also: #stun_msg_t
 * @stability: Stable
 *
 * The STUN Agent allows you to create and validate STUN messages easily.
 * It's main purpose is to make sure the building and validation methods used
 * are compatible with the RFC you create it with. It also tracks the transaction
 * ids of the requests you send, so you can validate if a STUN response you
 * received should be processed by that agent or not.
 *
 */


#ifdef _WIN32
#include "win32_common.h"
#else
#include <stdint.h>
#include <stdbool.h>
#endif


#include <sys/types.h>

/**
 * stun_agent_t:
 *
 * An opaque structure representing the STUN agent.
 */
typedef struct _stun_agent_st stun_agent_t;

#include "stunmessage.h"
#include "stundebug.h"

/**
 * stun_valid_status_e:
 * @STUN_VALIDATION_SUCCESS: The message is validated
 * @STUN_VALIDATION_NOT_STUN: This is not a valid STUN message
 * @STUN_VALIDATION_INCOMPLETE_STUN: The message seems to be valid but incomplete
 * @STUN_VALIDATION_BAD_REQUEST: The message does not have the cookie or the
 * fingerprint while the agent needs it with its usage
 * @STUN_VALIDATION_UNAUTHORIZED_BAD_REQUEST: The message is valid but
 * unauthorized with no username and message-integrity attributes.
 * A BAD_REQUEST error must be generated
 * @STUN_VALIDATION_UNAUTHORIZED: The message is valid but unauthorized as
 * the username/password do not match.
 * An UNAUTHORIZED error must be generated
 * @STUN_VALIDATION_UNMATCHED_RESPONSE: The message is valid but this is a
 * response/error that doesn't match a previously sent request
 * @STUN_VALIDATION_UNKNOWN_REQUEST_ATTRIBUTE: The message is valid but
 * contains one or more unknown comprehension attributes.
 * stun_agent_build_unknown_attributes_error() should be called
 * @STUN_VALIDATION_UNKNOWN_ATTRIBUTE: The message is valid but contains one
 * or more unknown comprehension attributes. This is a response, or error,
 * or indication message and no error response should be sent
 *
 * This enum is used as the return value of stun_agent_validate() and represents
 * the status result of the validation of a STUN message.
 */
typedef enum
{
    STUN_VALIDATION_SUCCESS,
    STUN_VALIDATION_NOT_STUN,
    STUN_VALIDATION_INCOMPLETE_STUN,
    STUN_VALIDATION_BAD_REQUEST,
    STUN_VALIDATION_UNAUTHORIZED_BAD_REQUEST,
    STUN_VALIDATION_UNAUTHORIZED,
    STUN_VALIDATION_UNMATCHED_RESPONSE,
    STUN_VALIDATION_UNKNOWN_REQUEST_ATTRIBUTE,
    STUN_VALIDATION_UNKNOWN_ATTRIBUTE,
} stun_valid_status_e; 

/**
 * stun_flags_e:
 * @STUN_AGENT_SHORT_TERM_CREDENTIALS: The agent should be using the short
 * term credentials mechanism for authenticating STUN messages
 * @STUN_AGENT_LONG_TERM_CREDENTIALS: The agent should be using the long
 * term credentials mechanism for authenticating STUN messages
 * @STUN_AGENT_USE_FINGERPRINT: The agent should add the FINGERPRINT
 * attribute to the STUN messages it creates.
 * @STUN_AGENT_ADD_SOFTWARE: The agent should add the SOFTWARE attribute
 * to the STUN messages it creates. Calling nice_agent_set_software() will have
 * the same effect as enabling this Usage. STUN Indications do not have the
 * SOFTWARE attributes added to them though. The SOFTWARE attribute is only
 * added for the RFC5389 and WLM2009 compatibility modes.
 * @STUN_AGENT_IGNORE_CREDENTIALS: The agent should ignore any credentials
 * in the STUN messages it receives (the MESSAGE-INTEGRITY attribute
 * will never be validated by stun_agent_validate())
 * @STUN_AGENT_NO_INDICATION_AUTH: The agent should ignore credentials
 * in the STUN messages it receives if the #StunClass of the message is
 * #STUN_INDICATION (some implementation require #STUN_INDICATION messages to
 * be authenticated, while others never add a MESSAGE-INTEGRITY attribute to a
 * #STUN_INDICATION message)
 * @STUN_AGENT_FORCE_VALIDATER: The agent should always try to validate
 * the password of a STUN message, even if it already knows what the password
 * should be (a response to a previously created request). This means that the
 * #StunMessageIntegrityValidate callback will always be called when there is
 * a MESSAGE-INTEGRITY attribute.
 * @STUN_AGENT_NO_ALIGNED_ATTRIBUTES: The agent should not assume STUN
 * attributes are aligned on 32-bit boundaries when parsing messages and also
 * do not add padding when creating messages.
 *
 * This enum defines a bitflag usages for a #stun_agent_t and they will define how
 * the agent should behave, independently of the compatibility mode it uses.
 * <para> See also: stun_agent_init() </para>
 * <para> See also: stun_agent_validate() </para>
 */
typedef enum
{
    STUN_AGENT_SHORT_TERM_CREDENTIALS    = (1 << 0),
    STUN_AGENT_LONG_TERM_CREDENTIALS     = (1 << 1),
    STUN_AGENT_USE_FINGERPRINT           = (1 << 2),
    STUN_AGENT_ADD_SOFTWARE              = (1 << 3),
    STUN_AGENT_IGNORE_CREDENTIALS        = (1 << 4),
    STUN_AGENT_NO_INDICATION_AUTH        = (1 << 5),
    STUN_AGENT_FORCE_VALIDATER           = (1 << 6),
    STUN_AGENT_NO_ALIGNED_ATTRIBUTES     = (1 << 7),
} stun_flags_e; 

typedef struct
{
    stun_trans_id id;
    stun_method_e method;
    uint8_t * key;
    size_t key_len;
    uint8_t long_term_key[16];
    bool long_term_valid;
    bool valid;
} stun_save_ids_t;

struct _stun_agent_st
{
	stun_save_ids_t sent_ids[STUN_AGENT_MAX_SAVED_IDS];
    uint16_t * known_attributes;
    stun_flags_e usage_flags;
};

/**
 * StunDefaultValidaterData:
 * @username: The username
 * @username_len: The length of the @username
 * @password: The password
 * @password_len: The length of the @password
 *
 * This structure is used as an element of the user_data to the
 * stun_agent_default_validater() function for authenticating a STUN
 * message during validationg.
 * <para> See also: stun_agent_default_validater() </para>
 */
typedef struct
{
    uint8_t * username;
    size_t username_len;
    uint8_t * password;
    size_t password_len;
} StunDefaultValidaterData;


/**
 * StunMessageIntegrityValidate:
 * @agent: The #stun_agent_t
 * @message: The #stun_msg_t being validated
 * @username: The username found in the @message
 * @username_len: The length of @username
 * @password: The password associated with that username. This argument is a
 * pointer to a byte array that must be set by the validater function.
 * @password_len: The length of @password which must also be set by the
 * validater function.
 * @user_data: Data to give the function
 *
 * This is the prototype for the @validater argument of the stun_agent_validate()
 * function.
 * <para> See also: stun_agent_validate() </para>
 * Returns: %TRUE if the authentication was successful,
 * %FALSE if the authentication failed
 */
typedef bool (*StunMessageIntegrityValidate)(stun_agent_t * agent,
        stun_msg_t * message, uint8_t * username, uint16_t username_len,
        uint8_t ** password, size_t * password_len, void * user_data);

/**
 * stun_agent_default_validater:
 * @agent: The #stun_agent_t
 * @message: The #stun_msg_t being validated
 * @username: The username found in the @message
 * @username_len: The length of @username
 * @password: The password associated with that username. This argument is a
 * pointer to a byte array that must be set by the validater function.
 * @password_len: The length of @password which must also be set by the
 * validater function.
 * @user_data: This must be an array of #StunDefaultValidaterData structures.
 * The last element in the array must have a username set to NULL
 *
 * This is a helper function to be used with stun_agent_validate(). If no
 * complicated processing of the username needs to be done, this function can
 * be used with stun_agent_validate() to quickly and easily match the username
 * of a STUN message with its password. Its @user_data argument must be an array
 * of #StunDefaultValidaterData which will allow us to map a username to a
 * password
 * <para> See also: stun_agent_validate() </para>
 * Returns: %TRUE if the authentication was successful,
 * %FALSE if the authentication failed
 */
bool stun_agent_default_validater(stun_agent_t * agent,
                                  stun_msg_t * message, uint8_t * username, uint16_t username_len,
                                  uint8_t ** password, size_t * password_len, void * user_data);

/**
 * stun_agent_init:
 * @agent: The #stun_agent_t to initialize
 * @known_attributes: An array of #uint16_t specifying which attributes should
 * be known by the agent. Any STUN message received that contains a mandatory
 * attribute that is not in this array will yield a
 * #STUN_VALIDATION_UNKNOWN_REQUEST_ATTRIBUTE or a
 * #STUN_VALIDATION_UNKNOWN_ATTRIBUTE error when calling stun_agent_validate()
 * @compatibility: The #StunCompatibility to use for this agent. This will affect
 * how the agent builds and validates the STUN messages
 * @usage_flags: A bitflag using #stun_flags_e values to define which
 * STUN usages the agent should use.
 *
 * This function must be called to initialize an agent before it is being used.
 *
 <note>
   <para>
    The @known_attributes data must exist in memory as long as the @agent is used
    </para>
    <para>
    If the #STUN_AGENT_SHORT_TERM_CREDENTIALS and
    #STUN_AGENT_LONG_TERM_CREDENTIALS usage flags are not set, then the
    agent will default in using the short term credentials mechanism
    </para>
    <para>
    The #STUN_AGENT_USE_FINGERPRINT and #STUN_AGENT_ADD_SOFTWARE
    usage flags are only valid if the #STUN_COMPATIBILITY_RFC5389 or
    #STUN_COMPATIBILITY_WLM2009 @compatibility is used
    </para>
 </note>
 */
void stun_agent_init(stun_agent_t * agent, stun_flags_e usage_flags);

/**
 * stun_agent_validate:
 * @agent: The #stun_agent_t
 * @msg: The #stun_msg_t to build
 * @buffer: The data buffer of the STUN message
 * @buffer_len: The length of @buffer
 * @validater: A #StunMessageIntegrityValidate function callback that will
 * be called if the agent needs to validate a MESSAGE-INTEGRITY attribute. It
 * will only be called if the agent finds a message that needs authentication
 * and a USERNAME is present in the STUN message, but no password is known.
 * The validater will not be called if the #STUN_AGENT_IGNORE_CREDENTIALS
 * usage flag is set on the agent, and it will always be called if the
 * #STUN_AGENT_FORCE_VALIDATER usage flag is set on the agent.
 * @validater_data: A user data to give to the @validater callback when it gets
 * called.
 *
 * This function is used to validate an inbound STUN message and transform its
 * data buffer into a #stun_msg_t. It will take care of various validation
 * algorithms to make sure that the STUN message is valid and correctly
 * authenticated.
 * <para> See also: stun_agent_default_validater() </para>
 * Returns: A #stun_valid_status_e
 <note>
   <para>
   if the return value is different from #STUN_VALIDATION_NOT_STUN or
   #STUN_VALIDATION_INCOMPLETE_STUN, then the @msg argument will contain a valid
   STUN message that can be used.
   This means that you can use the @msg variable as the @request argument to
   functions like stun_agent_init_error() or
   stun_agent_build_unknown_attributes_error().
   If the return value is #STUN_VALIDATION_BAD_REQUEST,
   #STUN_VALIDATION_UNAUTHORIZED or #STUN_VALIDATION_UNAUTHORIZED_BAD_REQUEST
   then the @key in the #stun_msg_t will not be set, so that error responses
   will not have a MESSAGE-INTEGRITY attribute.
   </para>
 </note>
 */
stun_valid_status_e stun_agent_validate(stun_agent_t * agent, stun_msg_t * msg, const uint8_t * buffer, size_t buffer_len);

/**
 * stun_agent_init_request:
 * @agent: The #stun_agent_t
 * @msg: The #stun_msg_t to build
 * @buffer: The buffer to use in the #stun_msg_t
 * @buffer_len: The length of the buffer
 * @m: The #stun_method_e of the request
 *
 * Creates a new STUN message of class #STUN_REQUEST and with the method @m
 * Returns: %TRUE if the message was initialized correctly, %FALSE otherwise
 */
bool stun_agent_init_request(stun_agent_t * agent, stun_msg_t * msg,
                             uint8_t * buffer, size_t buffer_len, stun_method_e m);

/**
 * stun_agent_init_indication:
 * @agent: The #stun_agent_t
 * @msg: The #stun_msg_t to build
 * @buffer: The buffer to use in the #stun_msg_t
 * @buffer_len: The length of the buffer
 * @m: The #stun_method_e of the indication
 *
 * Creates a new STUN message of class #STUN_INDICATION and with the method @m
 * Returns: %TRUE if the message was initialized correctly, %FALSE otherwise
 */
bool stun_agent_init_indication(stun_agent_t * agent, stun_msg_t * msg,
                                uint8_t * buffer, size_t buffer_len, stun_method_e m);

/**
 * stun_agent_init_response:
 * @agent: The #stun_agent_t
 * @msg: The #stun_msg_t to build
 * @buffer: The buffer to use in the #stun_msg_t
 * @buffer_len: The length of the buffer
 * @request: The #stun_msg_t of class #STUN_REQUEST that this response is for
 *
 * Creates a new STUN message of class #STUN_RESPONSE and with the same method
 * and transaction ID as the message @request. This will also copy the pointer
 * to the key that was used to authenticate the request, so you won't need to
 * specify the key with stun_agent_finish_message()
 * Returns: %TRUE if the message was initialized correctly, %FALSE otherwise
 */
bool stun_agent_init_response(stun_agent_t * agent, stun_msg_t * msg,
                              uint8_t * buffer, size_t buffer_len, const stun_msg_t * request);

/**
 * stun_agent_init_error:
 * @agent: The #stun_agent_t
 * @msg: The #stun_msg_t to build
 * @buffer: The buffer to use in the #stun_msg_t
 * @buffer_len: The length of the buffer
 * @request: The #stun_msg_t of class #STUN_REQUEST that this error response
 * is for
 * @err: The #stun_err_e to put in the ERROR-CODE attribute of the error response
 *
 * Creates a new STUN message of class #STUN_ERROR and with the same method
 * and transaction ID as the message @request. This will also copy the pointer
 * to the key that was used to authenticate the request (if authenticated),
 * so you won't need to specify the key with stun_agent_finish_message().
 * It will then add the ERROR-CODE attribute with code @err and the associated
 * string.
 * Returns: %TRUE if the message was initialized correctly, %FALSE otherwise
 */
bool stun_agent_init_error(stun_agent_t * agent, stun_msg_t * msg,
                           uint8_t * buffer, size_t buffer_len, const stun_msg_t * request,
                           stun_err_e err);

/**
 * stun_agent_build_unknown_attributes_error:
 * @agent: The #stun_agent_t
 * @msg: The #stun_msg_t to build
 * @buffer: The buffer to use in the #stun_msg_t
 * @buffer_len: The length of the buffer
 * @request: The #stun_msg_t of class #STUN_REQUEST that this response is for
 *
 * Creates a new STUN message of class #STUN_ERROR and with the same method
 * and transaction ID as the message @request.  It will then add the ERROR-CODE
 * attribute with code #STUN_ERROR_UNKNOWN_ATTRIBUTE and add all the unknown
 * mandatory attributes from the @request STUN message in the
 * #STUN_ATT_UNKNOWN_ATTRIBUTES attribute, it will then finish the message
 * by calling stun_agent_finish_message()
 * Returns: The size of the message built
 */
size_t stun_agent_build_unknown_attributes_error(stun_agent_t * agent,
        stun_msg_t * msg, uint8_t * buffer, size_t buffer_len,
        const stun_msg_t * request);


/**
 * stun_agent_finish_message:
 * @agent: The #stun_agent_t
 * @msg: The #stun_msg_t to finish
 * @key: The key to use for the MESSAGE-INTEGRITY attribute
 * @key_len: The length of the @key
 *
 * This function will 'finish' a message and make it ready to be sent. It will
 * add the MESSAGE-INTEGRITY and FINGERPRINT attributes if necessary. If the
 * STUN message has a #STUN_REQUEST class, it will save the transaction id of
 * the message in the agent for future matching of the response.
 * <para>See also: stun_agent_forget_trans()</para>
 * Returns: The final size of the message built or 0 if an error occured
 * <note>
     <para>
       The return value must always be checked. a value of 0 means the either
       the buffer's size is too small to contain the finishing attributes
       (MESSAGE-INTEGRITY, FINGERPRINT), or that there is no more free slots
       for saving the sent id in the agent's state.
     </para>
     <para>
       Everytime stun_agent_finish_message() is called for a #STUN_REQUEST
       message, you must make sure to call stun_agent_forget_trans() in
       case the response times out and is never received. This is to avoid
       filling up the #stun_agent_t's sent ids state preventing any further
       use of the stun_agent_finish_message()
     </para>
   </note>
 */
size_t stun_agent_finish_message(stun_agent_t * agent, stun_msg_t * msg,
                                 const uint8_t * key, size_t key_len);

/**
 * stun_agent_forget_trans:
 * @agent: The #stun_agent_t
 * @id: The #stun_trans_id of the transaction to forget
 *
 * This function is used to make the #stun_agent_t forget about a previously
 * created transaction.
 * <para>
 * This function should be called when a STUN request was previously
 * created with stun_agent_finish_message() and for which no response was ever
 * received (timed out). The #stun_agent_t keeps a list of the sent transactions
 * in order to validate the responses received. If the response is never received
 * this will allow the #stun_agent_t to forget about the timed out transaction and
 * free its slot for future transactions.
 * </para>
 * Since: 0.0.6
 * Returns: %TRUE if the transaction was found, %FALSE otherwise
 */
bool stun_agent_forget_trans(stun_agent_t * agent, stun_trans_id id);
#endif /* _STUN_AGENT_H */
