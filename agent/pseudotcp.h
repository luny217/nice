/* This file is part of the Nice GLib ICE library. */

#ifndef __LIBNICE_PSEUDOTCP_H__
#define __LIBNICE_PSEUDOTCP_H__

/**
 * SECTION:pseudotcp
 * @short_description: Pseudo TCP implementation
 * @include: pseudotcp.h
 * @stability: Stable
 *
 * The #pst_socket_t is an object implementing a Pseudo Tcp Socket for use
 * over UDP.
 * The socket will implement a subset of the TCP stack to allow for a reliable
 * transport over non-reliable sockets (such as UDP).
 *
 * See the file tests/test-pseudotcp.c in the source package for an example
 * of how to use the object.
 *
 * Since: 0.0.11
 */



#include <glib-object.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
//#  define ECONNABORTED WSAECONNABORTED
//#  define ENOTCONN WSAENOTCONN
//#  define EWOULDBLOCK WSAEWOULDBLOCK
//#  define ECONNRESET WSAECONNRESET
#endif

#include "agent.h"

/**
 * pst_socket_t:
 *
 * The #pst_socket_t is the GObject implementing the Pseudo TCP Socket
 *
 * Since: 0.0.11
 */
typedef struct _PseudoTcpSocket pst_socket_t; 

typedef struct _PseudoTcpSocketClass pst_socket_tClass;

GType pst_get_type(void);

/* TYPE MACROS */
#define PSEUDO_TCP_SOCKET_TYPE \
  (pst_get_type ())
#define PSEUDO_TCP_SOCKET(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), PSEUDO_TCP_SOCKET_TYPE, \
                              pst_socket_t))
#define PSEUDO_TCP_SOCKET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), PSEUDO_TCP_SOCKET_TYPE, \
                           PseudoTcpSocketClass))
#define IS_PSEUDO_TCP_SOCKET(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), PSEUDO_TCP_SOCKET_TYPE))
#define IS_PSEUDO_TCP_SOCKET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), PSEUDO_TCP_SOCKET_TYPE))
#define PSEUDOTCP_SOCKET_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), PSEUDO_TCP_SOCKET_TYPE, \
                              PseudoTcpSocketClass))

struct _PseudoTcpSocketClass
{
    GObjectClass parent_class;
};

typedef struct _PseudoTcpSocketPrivate PseudoTcpSocketPrivate;

struct _PseudoTcpSocket
{
    GObject parent;
    PseudoTcpSocketPrivate * priv;
};

/**
 * PseudoTcpDebugLevel:
 * @PSEUDO_TCP_DEBUG_NONE: Disable debug messages
 * @PSEUDO_TCP_DEBUG_NORMAL: Enable basic debug messages
 * @PSEUDO_TCP_DEBUG_VERBOSE: Enable verbose debug messages
 *
 * Valid values of debug levels to be set.
 *
 * Since: 0.0.11
 */
typedef enum
{
    PSEUDO_TCP_DEBUG_NONE = 0,
    PSEUDO_TCP_DEBUG_NORMAL,
    PSEUDO_TCP_DEBUG_VERBOSE,
} PseudoTcpDebugLevel;

/**
 * PseudoTcpState:
 * @TCP_LISTEN: The socket's initial state. The socket isn't connected and is
 * listening for an incoming connection
 * @TCP_SYN_SENT: The socket has sent a connection request (SYN) packet and is
 * waiting for an answer
 * @TCP_SYN_RECEIVED: The socket has received a connection request (SYN) packet.
 * @TCP_ESTABLISHED: The socket is connected
 * @TCP_CLOSED: The socket has been closed
 * @TCP_FIN_WAIT_1: The socket has been closed locally but not remotely
 * (Since: 0.1.8)
 * @TCP_FIN_WAIT_2: The socket has been closed locally but not remotely
 * (Since: 0.1.8)
 * @TCP_CLOSING: The socket has been closed locally and remotely
 * (Since: 0.1.8)
 * @TCP_TIME_WAIT: The socket has been closed locally and remotely
 * (Since: 0.1.8)
 * @TCP_CLOSE_WAIT: The socket has been closed remotely but not locally
 * (Since: 0.1.8)
 * @TCP_LAST_ACK: The socket has been closed locally and remotely
 * (Since: 0.1.8)
 *
 * An enum representing the state of the #pst_socket_t. These states
 * correspond to the TCP states in RFC 793.
 * <para> See also: #pst_socket_t:state </para>
 *
 * Since: 0.0.11
 */
typedef enum
{
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_CLOSED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSING,
    TCP_TIME_WAIT,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
} PseudoTcpState;

/**
 * pst_wret_e:
 * @WR_SUCCESS: The write operation was successful
 * @WR_TOO_LARGE: The socket type requires that message be sent atomically
 * and the size of the message to be sent made this impossible.
 * @WR_FAIL: There was an error sending the message
 *
 * An enum representing the result value of the write operation requested by
 * the #pst_socket_t.
 * <para> See also: %pst_callback_t:WritePacket </para>
 *
 * Since: 0.0.11
 */
typedef enum
{
    WR_SUCCESS,
    WR_TOO_LARGE,
    WR_FAIL
} pst_wret_e; 

/**
 * PseudoTcpShutdown:
 * @PSEUDO_TCP_SHUTDOWN_RD: Shut down the local reader only
 * @PSEUDO_TCP_SHUTDOWN_WR: Shut down the local writer only
 * @PSEUDO_TCP_SHUTDOWN_RDWR: Shut down both reading and writing
 *
 * Options for which parts of a connection to shut down when calling
 * pst_shutdown(). These correspond to the values passed to POSIX
 * shutdown().
 *
 * Since: 0.1.8
 */
typedef enum
{
    PSEUDO_TCP_SHUTDOWN_RD,
    PSEUDO_TCP_SHUTDOWN_WR,
    PSEUDO_TCP_SHUTDOWN_RDWR,
} PseudoTcpShutdown;

/**
 * pst_callback_t:
 * @user_data: A user defined pointer to be passed to the callbacks
 * @PseudoTcpOpened: The #pst_socket_t is now connected
 * @PseudoTcpReadable: The socket is readable
 * @PseudoTcpWritable: The socket is writable
 * @PseudoTcpClosed: The socket was closed (both sides)
 * @WritePacket: This callback is called when the socket needs to send data.
 *
 * A structure containing callbacks functions that will be called by the
 * #pst_socket_t when some events happen.
 * <para> See also: #pst_wret_e </para>
 *
 * Since: 0.0.11
 */
typedef struct
{
	void * user_data;
    void (*PseudoTcpOpened)(pst_socket_t * tcp, void * data);
    void (*PseudoTcpReadable)(pst_socket_t * tcp, void * data);
    void (*PseudoTcpWritable)(pst_socket_t * tcp, void * data);
    void (*PseudoTcpClosed)(pst_socket_t * tcp, guint32 error, void * data);
    pst_wret_e(*WritePacket)(pst_socket_t * tcp, const char * buffer, uint32_t len, void * data);
} pst_callback_t;

/**
 * pst_new:
 * @conversation: The conversation id for the socket.
 * @callbacks: A pointer to the #pst_callback_t structure for getting
 * notified of the #pst_socket_t events.
 *
 * Creates a new #pst_socket_t for the specified conversation
 *
 <note>
   <para>
     The @callbacks must be non-NULL, in order to get notified of packets the
     socket needs to send.
   </para>
   <para>
     If the @callbacks structure was dynamicly allocated, it can be freed
     after the call @pst_new
   </para>
 </note>
 *
 * Returns: The new #pst_socket_t object, %NULL on error
 *
 * Since: 0.0.11
 */
pst_socket_t * pst_new(guint32 conversation,
                                        pst_callback_t * callbacks);


/**
 * pst_connect:
 * @self: The #pst_socket_t object.
 *
 * Connects the #pst_socket_t to the peer with the same conversation id.
 * The connection will only be successful after the
 * %pst_callback_t:PseudoTcpOpened callback is called
 *
 * Returns: %TRUE on success, %FALSE on failure (not in %TCP_LISTEN state)
 * <para> See also: pst_get_error() </para>
 *
 * Since: 0.0.11
 */
gboolean pst_connect(pst_socket_t * self);


/**
 * pst_recv:
 * @self: The #pst_socket_t object.
 * @buffer: The buffer to fill with received data
 * @len: The length of @buffer
 *
 * Receive data from the socket.
 *
 <note>
   <para>
     Only call this on the %pst_callback_t:PseudoTcpReadable callback.
   </para>
   <para>
     This function should be called in a loop. If this function does not
     return -1 with EWOULDBLOCK as the error, the
     %pst_callback_t:PseudoTcpReadable callback will not be called again.
   </para>
 </note>
 *
 * Returns: The number of bytes received or -1 in case of error
 * <para> See also: pst_get_error() </para>
 *
 * Since: 0.0.11
 */
int32_t  pst_recv(pst_socket_t * self, char * buffer, size_t len);


/**
 * pst_send:
 * @self: The #pst_socket_t object.
 * @buffer: The buffer with data to send
 * @len: The length of @buffer
 *
 * Send data on the socket.
 *
 <note>
   <para>
     If this function return -1 with EWOULDBLOCK as the error, or if the return
     value is lower than @len, then the %pst_callback_t:PseudoTcpWritable
     callback will be called when the socket will become writable.
   </para>
 </note>
 *
 * Returns: The number of bytes sent or -1 in case of error
 * <para> See also: pst_get_error() </para>
 *
 * Since: 0.0.11
 */
int32_t pst_send(pst_socket_t * self, const char * buffer,
                            guint32 len);


/**
 * pst_close:
 * @self: The #pst_socket_t object.
 * @force: %TRUE to close the socket forcefully, %FALSE to close it gracefully
 *
 * Close the socket for sending. If @force is set to %FALSE, the socket will
 * finish sending pending data before closing. If it is set to %TRUE, the socket
 * will discard pending data and close the connection immediately (sending a TCP
 * RST segment).
 *
 * The socket will be closed in both directions ?C sending and receiving ?C and
 * any pending received data must be read before calling this function, by
 * calling pst_recv() until it blocks. If any pending data is in
 * the receive buffer when pst_close() is called, a TCP RST
 * segment will be sent to the peer to notify it of the data loss.
 *
 <note>
   <para>
     The %pst_callback_t:PseudoTcpClosed callback will not be called once
     the socket gets closed. It is only used for aborted connection.
     Instead, the socket gets closed when the pst_get_next_clock()
     function returns FALSE.
   </para>
 </note>
 *
 * <para> See also: pst_get_next_clock() </para>
 *
 * Since: 0.0.11
 */
void pst_close(pst_socket_t * self, gboolean force);

/**
 * pst_shutdown:
 * @self: The #pst_socket_t object.
 * @how: The directions of the connection to shut down.
 *
 * Shut down sending, receiving, or both on the socket, depending on the value
 * of @how. The behaviour of pst_send() and
 * pst_recv() will immediately change after this function returns
 * (depending on the value of @how), though the socket may continue to process
 * network traffic in the background even if sending or receiving data is
 * forbidden.
 *
 * This is equivalent to the POSIX shutdown() function. Setting @how to
 * %PSEUDO_TCP_SHUTDOWN_RDWR is equivalent to calling pst_close().
 *
 * Since: 0.1.8
 */
void pst_shutdown(pst_socket_t * self, PseudoTcpShutdown how);

/**
 * pst_get_error:
 * @self: The #pst_socket_t object.
 *
 * Return the last encountered error.
 *
 <note>
   <para>
     The return value can be :
     <para>
       EINVAL (for pst_connect()).
     </para>
     <para>
       EWOULDBLOCK or ENOTCONN (for pst_recv() and
       pst_send()).
     </para>
   </para>
 </note>
 *
 * Returns: The error code
 * <para> See also: pst_connect() </para>
 * <para> See also: pst_recv() </para>
 * <para> See also: pst_send() </para>
 *
 * Since: 0.0.11
 */
int pst_get_error(pst_socket_t * self);


/**
 * pst_get_next_clock:
 * @self: The #pst_socket_t object.
 * @timeout: A pointer to be filled with the new timeout.
 *
 * Call this to determine the timeout needed before the next time call
 * to pst_notify_clock() should be made.
 *
 * Returns: %TRUE if @timeout was filled, %FALSE if the socket is closed and
 * ready to be destroyed.
 *
 * <para> See also: pst_notify_clock() </para>
 *
 * Since: 0.0.11
 */
gboolean pst_get_next_clock(pst_socket_t * self,
        guint64 * timeout);


/**
 * pst_notify_clock:
 * @self: The #pst_socket_t object.
 *
 * Start the processing of receiving data, pending data or syn/acks.
 * Call this based on timeout value returned by
 * pst_get_next_clock().
 * It's ok to call this too frequently.
 *
 * <para> See also: pst_get_next_clock() </para>
 *
 * Since: 0.0.11
 */
void pst_notify_clock(pst_socket_t * self);


/**
 * pst_notify_mtu:
 * @self: The #pst_socket_t object.
 * @mtu: The new MTU of the socket
 *
 * Set the MTU of the socket
 *
 * Since: 0.0.11
 */
void pst_notify_mtu(pst_socket_t * self, guint16 mtu);


/**
 * pst_notify_packet:
 * @self: The #pst_socket_t object.
 * @buffer: The buffer containing the received data
 * @len: The length of @buffer
 *
 * Notify the #pst_socket_t when a new packet arrives
 *
 * Returns: %TRUE if the packet was processed successfully, %FALSE otherwise
 *
 * Since: 0.0.11
 */
gboolean pst_notify_packet(pst_socket_t * self,
        const gchar * buffer, guint32 len);


/**
 * pst_notify_message:
 * @self: The #pst_socket_t object.
 * @message: A #n_input_msg_t containing the received data.
 *
 * Notify the #pst_socket_t that a new message has arrived, and enqueue the
 * data in its buffers to the #pst_socket_t??s receive buffer.
 *
 * Returns: %TRUE if the packet was processed successfully, %FALSE otherwise
 *
 * Since: 0.1.5
 */
gboolean pst_notify_message(pst_socket_t * self,
        n_input_msg_t * message);


/**
 * pseudo_tcp_set_debug_level:
 * @level: The level of debug to set
 *
 * Sets the debug level to enable/disable normal/verbose debug messages.
 *
 * Since: 0.0.11
 */
void pseudo_tcp_set_debug_level(PseudoTcpDebugLevel level);


/**
 * pst_get_available_bytes:
 * @self: The #pst_socket_t object.
 *
 * Gets the number of bytes of data in the buffer that can be read without
 * receiving more packets from the network.
 *
 * Returns: The number of bytes or -1 if the connection is not established
 *
 * Since: 0.1.5
 */

int32_t pst_get_available_bytes(pst_socket_t * self);

/**
 * pst_can_send:
 * @self: The #pst_socket_t object.
 *
 * Returns if there is space in the send buffer to send any data.
 *
 * Returns: %TRUE if data can be sent, %FALSE otherwise
 *
 * Since: 0.1.5
 */

gboolean pst_can_send(pst_socket_t * self);

/**
 * pst_get_available_send_space:
 * @self: The #pst_socket_t object.
 *
 * Gets the number of bytes of space available in the transmission buffer.
 *
 * Returns: The number of bytes, or 0 if the connection is not established.
 *
 * Since: 0.1.5
 */
gsize pst_get_available_send_space(pst_socket_t * self);

/**
 * pst_set_time:
 * @self: The #pst_socket_t object.
 * @current_time: Current monotonic time, in milliseconds; or zero to use the
 * system monotonic clock.
 *
 * Sets the current monotonic time to be used by the TCP socket when calculating
 * timeouts and expiry times. If this function is not called, or is called with
 * @current_time as zero, g_get_monotonic_time() will be used. Otherwise, the
 * specified @current_time will be used until it is updated by calling this
 * function again.
 *
 * This function is intended for testing only, and should not be used in
 * production code.
 *
 * Since: 0.1.8
 */
void pst_set_time(pst_socket_t * self, guint32 current_time);

/**
 * pst_is_closed:
 * @self: The #pst_socket_t object.
 *
 * Gets whether the socket is closed, with the shutdown handshake completed,
 * and both peers no longer able to read or write data to the connection.
 *
 * Returns: %TRUE if the socket is closed in both directions, %FALSE otherwise
 * Since: 0.1.8
 */
gboolean pst_is_closed(pst_socket_t * self);

/**
 * pst_is_closed_remotely:
 * @self: The #pst_socket_t object.
 *
 * Gets whether the socket has been closed on the remote peer??s side of the
 * connection (i.e. whether pst_close() has been called there).
 * This is guaranteed to return %TRUE if pst_is_closed() returns
 * %TRUE. It will not return %TRUE after pst_close() is called
 * until a FIN segment is received from the remote peer.
 *
 * Returns: %TRUE if the remote peer has closed its side of the connection,
 * %FALSE otherwise
 * Since: 0.1.8
 */
gboolean pst_is_closed_remotely(pst_socket_t * self);

#endif /* __LIBNICE_PSEUDOTCP_H__ */

