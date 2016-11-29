/* This file is part of the Nice GLib ICE library. */

#ifndef __LIBNICE_AGENT_H__
#define __LIBNICE_AGENT_H__

#include <stdint.h>
//#include <glib-object.h>
//#include <gio/gio.h>

/**
 * n_agent_t:
 *
 * The #n_agent_t is the main GObject of the libnice library and represents
 * the ICE agent.
 */
typedef struct _agent_st n_agent_t;

#include "address.h"
#include "candidate.h"
#include "debug.h"
#include "nlist.h"
#include "nqueue.h"

/**
 * n_input_msg_t:
 * @buffers: (array length=n_buffers): unowned array of #n_invector_t buffers to
 * store data in for this message
 * @n_buffers: number of #GInputVectors in @buffers, or -1 to indicate @buffers
 * is %NULL-terminated
 * @from: (allow-none): return location to store the address of the peer who
 * transmitted the message, or %NULL
 * @length: total number of valid bytes contiguously stored in @buffers
 *
 * Represents a single message received off the network. For reliable
 * connections, this is essentially just an array of buffers (specifically,
 * @from can be ignored). for non-reliable connections, it represents a single
 * packet as received from the OS.
 *
 * @n_buffers may be -1 to indicate that @buffers is terminated by a
 * #n_invector_t with a %NULL buffer pointer.
 *
 * By providing arrays of #NiceInputMessages to functions like
 * nice_agent_recv_messages(), multiple messages may be received with a single
 * call, which is more efficient than making multiple calls in a loop. In this
 * manner, nice_agent_recv_messages() is analogous to recvmmsg(); and
 * #n_input_msg_t to struct mmsghdr.
 *
 * Since: 0.1.5
 */
typedef struct
{
    n_invector_t * buffers;
    int32_t n_buffers;  /* may be -1 to indicate @buffers is NULL-terminated */
    n_addr_t * from; /* return location for address of message sender */
    uint32_t length;  /* sum of the lengths of @buffers */
} n_input_msg_t;

/**
 * n_output_msg_t:
 * @buffers: (array length=n_buffers): unowned array of #n_outvector_t buffers
 * which contain data to transmit for this message
 * @n_buffers: number of #GOutputVectors in @buffers, or -1 to indicate @buffers
 * is %NULL-terminated
 *
 * Represents a single message to transmit on the network. For
 * reliable connections, this is essentially just an array of
 * buffer. for non-reliable connections, it represents a single packet
 * to send to the OS.
 *
 * @n_buffers may be -1 to indicate that @buffers is terminated by a
 * #n_outvector_t with a %NULL buffer pointer.
 *
 * By providing arrays of #NiceOutputMessages to functions like
 * n_agent_send_msgs_nonblocking(), multiple messages may be transmitted
 * with a single call, which is more efficient than making multiple calls in a
 * loop. In this manner, n_agent_send_msgs_nonblocking() is analogous to
 * sendmmsg(); and #n_output_msg_t to struct mmsghdr.
 *
 * Since: 0.1.5
 */
typedef struct
{
    n_outvector_t * buffers;
    uint32_t n_buffers;
} n_output_msg_t;


#if 0
#define NICE_TYPE_AGENT nice_agent_get_type()

#define NICE_AGENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  NICE_TYPE_AGENT, n_agent_t))

#define NICE_AGENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  NICE_TYPE_AGENT, NiceAgentClass))

#define NICE_IS_AGENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  NICE_TYPE_AGENT))

#define NICE_IS_AGENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  NICE_TYPE_AGENT))

#define NICE_AGENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  NICE_TYPE_AGENT, NiceAgentClass))

typedef struct _NiceAgentClass NiceAgentClass;

struct _NiceAgentClass
{
    GObjectClass parent_class;
};


GType nice_agent_get_type(void);
#endif


/**
 * MAX_REMOTE_CANDIDATES:
 *
 * A hard limit for the number of remote candidates. This
 * limit is enforced to protect against malevolent remote
 * clients.
 */
#define MAX_REMOTE_CANDIDATES    25

/**
 * n_comp_state_e:
 * @NICE_COMP_STATE_DISCONNECTED: No activity scheduled
 * @NICE_COMP_STATE_GATHERING: Gathering local candidates
 * @NICE_COMP_STATE_CONNECTING: Establishing connectivity
 * @NICE_COMP_STATE_CONNECTED: At least one working candidate pair
 * @NICE_COMP_STATE_READY: ICE concluded, candidate pair selection
 * is now final
 * @NICE_COMP_STATE_FAILED: Connectivity checks have been completed,
 * but connectivity was not established
 * @NICE_COMP_STATE_LAST: Dummy state
 *
 * An enum representing the state of a component.
 * <para> See also: #n_agent_t::component-state-changed </para>
 */
typedef enum
{
    COMP_STATE_DISCONNECTED,
    COMP_STATE_GATHERING,
    COMP_STATE_CONNECTING,
    COMP_STATE_CONNECTED,
    COMP_STATE_READY,
    COMP_STATE_FAILED,
    COMP_STATE_LAST
} n_comp_state_e;

/**
 * n_comp_type_e:
 * @NICE_COMPONENT_TYPE_RTP: RTP n_comp_t type
 * @NICE_COMPONENT_TYPE_RTCP: RTCP n_comp_t type
 *
 * Convenience enum representing the type of a component for use as the
 * component_id for RTP/RTCP usages.
 <example>
   <title>Example of use.</title>
   <programlisting>
   n_agent_send (agent, stream_id, NICE_COMPONENT_TYPE_RTP, len, buf);
   </programlisting>
  </example>
 */
typedef enum
{
    NICE_COMPONENT_TYPE_RTP = 1,
    NICE_COMPONENT_TYPE_RTCP = 2
} n_comp_type_e;

/**
 * n_agent_recv_func:
 * @agent: The #n_agent_t Object
 * @stream_id: The id of the stream
 * @component_id: The id of the component of the stream
 *        which received the data
 * @len: The length of the data
 * @buf: The buffer containing the data received
 * @user_data: The user data set in n_agent_attach_recv()
 *
 * Callback function when data is received on a component
 *
 */
typedef void (*n_agent_recv_func)(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, uint32_t len, char * buf, void * user_data);

/**
 * n_agent_new:
 * @ctx: The Glib Mainloop Context to use for timers
 * @compat: The compatibility mode of the agent
 *
 * Create a new #n_agent_t.
 * The returned object must be freed with g_object_unref()
 *
 * Returns: The new agent GObject
 */
n_agent_t * n_agent_new();

/**
 * n_agent_add_local_addr:
 * @agent: The #n_agent_t Object
 * @addr: The address to listen to
 * If the port is 0, then a random port will be chosen by the system
 *
 * Add a local address from which to derive local host candidates for
 * candidate gathering.
 * <para>
 * Since 0.0.5, if this method is not called, libnice will automatically
 * discover the local addresses available
 * </para>
 *
 * See also: n_agent_gather_cands()
 * Returns: %TRUE on success, %FALSE on fatal (memory allocation) errors
 */
int n_agent_add_local_addr(n_agent_t * agent, n_addr_t * addr);

/**
 * n_agent_add_stream:
 * @agent: The #n_agent_t Object
 * @n_components: The number of components to add to the stream
 *
 * Adds a data stream to @agent containing @n_components components. The
 * returned stream ID is guaranteed to be positive on success.
 *
 * Returns: The ID of the new stream, 0 on failure
 **/
uint32_t n_agent_add_stream(n_agent_t * agent, uint32_t n_components);

/**
 * n_agent_remove_stream:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream to remove
 *
 * Remove and free a previously created data stream from @agent. If any I/O
 * streams have been created using nice_agent_get_io_stream(), they should be
 * closed completely using g_io_stream_close() before this is called, or they
 * will get broken pipe errors.
 *
 **/
void n_agent_remove_stream(n_agent_t * agent, uint32_t stream_id);


/**
 * n_agent_set_port_range:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @component_id: The ID of the component
 * @min_port: The minimum port to use
 * @max_port: The maximum port to use
 *
 * Sets a preferred port range for allocating host candidates.
 * <para>
 * If a local host candidate cannot be created on that port
 * range, then the n_agent_gather_cands() call will fail.
 * </para>
 * <para>
 * This MUST be called before n_agent_gather_cands()
 * </para>
 *
 */
void agent_set_port_range(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, uint32_t min_port, uint32_t max_port);

/**
 * n_agent_set_relay_info:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @component_id: The ID of the component
 * @server_ip: The IP address of the TURN server
 * @server_port: The port of the TURN server
 * @username: The TURN username to use for the allocate
 * @password: The TURN password to use for the allocate
 * @type: The type of relay to use
 *
 * Sets the settings for using a relay server during the candidate discovery.
 * This may be called multiple times to add multiple relay servers to the
 * discovery process; one TCP and one UDP, for example.
 *
 * Returns: %TRUE if the TURN settings were accepted.
 * %FALSE if the address was invalid.
 */
int n_agent_set_relay_info(n_agent_t * agent, uint32_t stream_id, uint32_t component_id,  const char * server_ip, uint32_t server_port,
                           const char * username, const char * password, n_relay_type_e type);

/**
 * n_agent_gather_cands:
 * @agent: The #n_agent_t object
 * @stream_id: The ID of the stream to start
 *
 * Allocate and start listening on local candidate ports and start the remote
 * candidate gathering process.
 * Once done, #n_agent_t::candidate-gathering-done is called for the stream.
 * As soon as this function is called, #n_agent_t::new-candidate signals may be
 * emitted, even before this function returns.
 *
 * n_agent_get_local_cands() will only return non-empty results after
 * calling this function.
 *
 * <para>See also: n_agent_add_local_addr()</para>
 * <para>See also: n_agent_set_port_range()</para>
 *
 * Returns: %FALSE if the stream ID is invalid or if a host candidate couldn't
 * be allocated on the requested interfaces/ports; %TRUE otherwise
 *
 */
int n_agent_gather_cands(n_agent_t * agent, uint32_t stream_id);

/**
 * n_agent_set_remote_credentials:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @ufrag: nul-terminated string containing an ICE username fragment
 *    (length must be between 22 and 256 chars)
 * @pwd: nul-terminated string containing an ICE password
 *    (length must be between 4 and 256 chars)
 *
 * Sets the remote credentials for stream @stream_id.
 *
 <note>
   <para>
     n_stream_t credentials do not override per-candidate credentials if set
   </para>
   <para>
     Due to the native of peer-reflexive candidates, any agent using a per-stream
     credentials (RFC5245, WLM2009, OC2007R2 and DRAFT19) instead of
     per-candidate credentials (GOOGLE, MSN, OC2007), must
     use the n_agent_set_remote_credentials() API instead of setting the
     username and password on the candidates.
   </para>
 </note>
 *
 * Returns: %TRUE on success, %FALSE on error.
 */
int n_agent_set_remote_credentials(n_agent_t * agent, uint32_t stream_id, const char * ufrag, const char * pwd);


/**
 * n_agent_set_local_credentials:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @ufrag: nul-terminated string containing an ICE username fragment
 *    (length must be between 22 and 256 chars)
 * @pwd: nul-terminated string containing an ICE password
 *    (length must be between 4 and 256 chars)
 *
 * Sets the local credentials for stream @stream_id.
 *
 * Returns: %TRUE on success, %FALSE on error.
 */
int n_agent_set_local_credentials(n_agent_t * agent, uint32_t stream_id, const char * ufrag, const char * pwd);

/**
 * n_agent_get_local_credentials:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @ufrag: (out callee-allocates): return location for a nul-terminated string
 * containing an ICE username fragment; must be freed with n_free()
 * @pwd: (out callee-allocates): return location for a nul-terminated string
 * containing an ICE password; must be freed with n_free()
 *
 * Gets the local credentials for stream @stream_id. This may be called any time
 * after creating a stream using n_agent_add_stream().
 *
 * An error will be returned if this is called for a non-existent stream, or if
 * either of @ufrag or @pwd are %NULL.
 *
 * Returns: %TRUE on success, %FALSE on error.
 */
int n_agent_get_local_credentials(n_agent_t * agent, uint32_t stream_id, char ** ufrag, char ** pwd);

/**
 * n_agent_set_remote_cands:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream the candidates are for
 * @component_id: The ID of the component the candidates are for
 * @candidates: (element-type n_cand_t) (transfer none): a #n_slist_t  of
 * #n_cand_t items describing each candidate to add
 *
 * Sets, adds or updates the remote candidates for a component of a stream.
 *
 <note>
   <para>
    NICE_AGENT_MAX_REMOTE_CANDIDATES is the absolute maximum limit
    for remote candidates.
   </para>
   <para>
   You must first call n_agent_gather_cands() and wait for the
   #n_agent_t::candidate-gathering-done signale before
   calling n_agent_set_remote_cands()
   </para>
   <para>
    Since 0.1.3, there is no need to wait for the candidate-gathering-done signal.
    Remote candidates can be set even while gathering local candidates.
    Newly discovered local candidates will automatically be paired with
    existing remote candidates.
   </para>
 </note>
 *
 * Returns: The number of candidates added, negative on errors (memory
 * allocation error or invalid component)
 **/
int n_agent_set_remote_cands(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, const n_slist_t  * candidates);

/**
 * n_agent_send:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream to send to
 * @component_id: The ID of the component to send to
 * @len: The length of the buffer to send
 * @buf: The buffer of data to send
 *
 * Sends a data payload over a stream's component.
 *
 <note>
   <para>
     n_comp_t state MUST be NICE_COMP_STATE_READY, or as a special case,
     in any state if component was in READY state before and was then restarted
   </para>
   <para>
   In reliable mode, the -1 error value means either that you are not yet
   connected or that the send buffer is full (equivalent to EWOULDBLOCK).
   In both cases, you simply need to wait for the
   #n_agent_t::reliable-transport-writable signal to be fired before resending
   the data.
   </para>
   <para>
   In non-reliable mode, it will virtually never happen with UDP sockets, but
   it might happen if the active candidate is a TURN-TCP connection that got
   disconnected.
   </para>
   <para>
   In both reliable and non-reliable mode, a -1 error code could also mean that
   the stream_id and/or component_id are invalid.
   </para>
</note>
 *
 * Returns: The number of bytes sent, or negative error code
 */
int32_t n_agent_send(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, uint32_t len, const char * buf);


/**
 * n_agent_get_local_cands:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @component_id: The ID of the component
 *
 * Retrieve from the agent the list of all local candidates
 * for a stream's component
 *
 <note>
   <para>
     The caller owns the returned n_slist_t  as well as the candidates contained
     within it.
     To get full results, the client should wait for the
     #n_agent_t::candidate-gathering-done signal.
   </para>
 </note>
 *
 * Returns: (element-type n_cand_t) (transfer full): a #n_slist_t  of
 * #n_cand_t objects representing the local candidates of @agent
 **/
n_slist_t * n_agent_get_local_cands(n_agent_t * agent, uint32_t stream_id, uint32_t component_id);

/**
 * n_agent_get_remote_cands:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @component_id: The ID of the component
 *
 * Get a list of the remote candidates set on a stream's component
 *
 <note>
   <para>
     The caller owns the returned n_slist_t  as well as the candidates contained
     within it.
   </para>
   <para>
     The list of remote candidates can change during processing.
     The client should register for the #n_agent_t::new-remote-candidate signal
     to get notified of new remote candidates.
   </para>
 </note>
 *
 * Returns: (element-type n_cand_t) (transfer full): a #n_slist_t  of
 * #NiceCandidates objects representing the remote candidates set on the @agent
 **/
n_slist_t  * n_agent_get_remote_cands(n_agent_t * agent, uint32_t stream_id, uint32_t component_id);

/**
 * n_agent_restart:
 * @agent: The #n_agent_t Object
 *
 * Restarts the session as defined in ICE draft 19. This function
 * needs to be called both when initiating (ICE spec section 9.1.1.1.
 * "ICE Restarts"), as well as when reacting (spec section 9.2.1.1.
 * "Detecting ICE Restart") to a restart.
 *
 * Returns: %TRUE on success %FALSE on error
 **/
int n_agent_restart(n_agent_t * agent);

/**
 * n_agent_restart_stream:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 *
 * Restarts a single stream as defined in RFC 5245. This function
 * needs to be called both when initiating (ICE spec section 9.1.1.1.
 * "ICE Restarts"), as well as when reacting (spec section 9.2.1.1.
 * "Detecting ICE Restart") to a restart.
 *
 * Unlike n_agent_restart(), this applies to a single stream. It also
 * does not generate a new tie breaker.
 *
 * Returns: %TRUE on success %FALSE on error
 **/
int n_agent_restart_stream(n_agent_t * agent, uint32_t stream_id);

/**
 * n_agent_attach_recv: (skip)
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of stream
 * @component_id: The ID of the component
 * @ctx: The Glib Mainloop Context to use for listening on the component
 * @func: The callback function to be called when data is received on
 * the stream's component (will not be called for STUN messages that
 * should be handled by #n_agent_t itself)
 * @data: user data associated with the callback
 *
 * Attaches the stream's component's sockets to the Glib Mainloop Context in
 * order to be notified whenever data becomes available for a component,
 * and to enable #n_agent_t to receive STUN messages (during the
 * establishment of ICE connectivity).
 *
 * This must not be used in combination with nice_agent_recv_messages() (or
 * #NiceIOStream or #NiceInputStream) on the same stream/component pair.
 *
 * Calling n_agent_attach_recv() with a %NULL @func will detach any existing
 * callback and cause reception to be paused for the given stream/component
 * pair. You must iterate the previously specified #GMainContext sufficiently to
 * ensure all pending I/O callbacks have been received before calling this
 * function to unset @func, otherwise data loss of received packets may occur.
 *
 * Returns: %TRUE on success, %FALSE if the stream or component IDs are invalid.
 */
int n_agent_attach_recv(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id,
                        n_agent_recv_func func,  void * data);

/**
 * n_agent_set_selected_pair:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @component_id: The ID of the component
 * @lfoundation: The local foundation of the candidate to use
 * @rfoundation: The remote foundation of the candidate to use
 *
 * Sets the selected candidate pair for media transmission
 * for a given stream's component. Calling this function will
 * disable all further ICE processing (connection check,
 * state machine updates, etc). Note that keepalives will
 * continue to be sent.
 *
 * Returns: %TRUE on success, %FALSE if the candidate pair cannot be found
 */
int n_agent_set_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, const char * lfoundation, const char * rfoundation);

/**
 * n_agent_get_selected_pair:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @component_id: The ID of the component
 * @local: The local selected candidate
 * @remote: The remote selected candidate
 *
 * Retreive the selected candidate pair for media transmission
 * for a given stream's component.
 *
 * Returns: %TRUE on success, %FALSE if there is no selected candidate pair
 */
int n_agent_get_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t ** local, n_cand_t ** remote);


/**
 * n_agent_set_selected_rcand:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @component_id: The ID of the component
 * @candidate: The #n_cand_t to select
 *
 * Sets the selected remote candidate for media transmission
 * for a given stream's component. This is used to force the selection of
 * a specific remote candidate even when connectivity checks are failing
 * (e.g. non-ICE compatible candidates).
 * Calling this function will disable all further ICE processing
 * (connection check, state machine updates, etc). Note that keepalives will
 * continue to be sent.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
int n_agent_set_selected_rcand(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, n_cand_t * candidate);


/**
 * n_agent_set_stream_tos:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @tos: The ToS to set
 *
 * Sets the IP_TOS and/or IPV6_TCLASS field on the stream's sockets' options
 *
 * Since: 0.0.9
 */
void n_agent_set_stream_tos(n_agent_t * agent, uint32_t stream_id, int32_t tos);

/**
 * n_agent_set_stream_name:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream to change
 * @name: The new name of the stream or %NULL
 *
 * This function will assign a media type to a stream. The only values
 * that can be used to produce a valid SDP are: "audio", "video",
 * "text", "application", "image" and "message".
 *
 * This is only useful when parsing and generating an SDP of the
 * candidates.
 *
 * <para>See also: nice_agent_generate_local_sdp()</para>
 * <para>See also: nice_agent_parse_remote_sdp()</para>
 * <para>See also: n_agent_get_stream_name()</para>
 *
 * Returns: %TRUE if the name has been set. %FALSE in case of error
 * (invalid stream or duplicate name).
 */
int n_agent_set_stream_name(n_agent_t * agent, uint32_t stream_id, const char * name);

/**
 * n_agent_get_stream_name:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream to change
 *
 * This function will return the name assigned to a stream.

 * <para>See also: n_agent_set_stream_name()</para>
 *
 * Returns: The name of the stream. The name is only valid while the stream
 * exists or until it changes through a call to n_agent_set_stream_name().
 */
const char * n_agent_get_stream_name(n_agent_t * agent, uint32_t stream_id);


/**
 * n_comp_state_to_str:
 * @state: a #n_comp_state_e
 *
 * Returns a string representation of the state, generally to use in debug
 * messages.
 *
 * Returns: (transfer none): a string representation of @state
 * Since: 0.1.6
 */
const char * n_comp_state_to_str(n_comp_state_e state);

/**
 * n_agent_forget_relays:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @component_id: The ID of the component
 *
 * Forget all the relay servers previously added using
 * n_agent_set_relay_info(). Currently connected streams will keep
 * using the relay as long as they have not been restarted and haven't
 * succesfully negotiated a different path.
 *
 * Returns: %FALSE if the component could not be found, %TRUE otherwise
 */
int n_agent_forget_relays(n_agent_t * agent, uint32_t stream_id, uint32_t component_id);

/**
 * n_agent_get_comp_state:
 * @agent: The #n_agent_t Object
 * @stream_id: The ID of the stream
 * @component_id: The ID of the component
 *
 * Retrieves the current state of a component.
 *
 * Returns: the #n_comp_state_e of the component and
 * %NICE_COMP_STATE_FAILED if the component was invalid.
 */
n_comp_state_e n_agent_get_comp_state(n_agent_t * agent, uint32_t stream_id,  uint32_t component_id);

void nice_print_cand(n_agent_t * agent, n_cand_t * l_cand, n_cand_t * r_cand);

int32_t n_agent_dispatcher(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id);

void n_networking_init(void);

#endif /* __LIBNICE_AGENT_H__ */
