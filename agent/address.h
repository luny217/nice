/* This file is part of the Nice GLib ICE library. */

#ifndef __LIBNICE_ADDRESS_H__
#define __LIBNICE_ADDRESS_H__

/**
 * SECTION:address
 * @short_description: IP address convenience library
 * @stability: Stable
 *
 * The #n_addr_t structure will allow you to easily set/get and modify an IPv4
 * or IPv6 address in order to communicate with the #n_agent_t.
 */


#include <glib.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif


/**
 * n_addr_t:
 *
 * The #n_addr_t structure that represents an IPv4 or IPv6 address.
 */
struct _addr_st
{
    union
    {
        struct sockaddr     addr;
        struct sockaddr_in  ip4;
        struct sockaddr_in6 ip6;
    } s;
};


/**
 * NICE_ADDRESS_STRING_LEN:
 *
 * The maximum string length representation of an address.
 * When using nice_address_to_string() make sure the string has a size of
 * at least %NICE_ADDRESS_STRING_LEN
 */
#define NICE_ADDRESS_STRING_LEN INET6_ADDRSTRLEN

typedef struct _addr_st n_addr_t;


/**
 * nice_address_init:
 * @addr: The #n_addr_t to init
 *
 * Initialize a #n_addr_t into an undefined address
 */
void nice_address_init(n_addr_t * addr);

/**
 * nice_address_new:
 *
 * Create a new #n_addr_t with undefined address
 * You must free it with nice_address_free()
 *
 * Returns: The new #n_addr_t
 */
n_addr_t * nice_address_new(void);

/**
 * nice_address_free:
 * @addr: The #n_addr_t to free
 *
 * Frees a #n_addr_t created with nice_address_new() or nice_address_dup()
 */
void nice_address_free(n_addr_t * addr);

/**
 * nice_address_dup:
 * @addr: The #n_addr_t to dup
 *
 * Creates a new #n_addr_t with the same address as @addr
 *
 * Returns: The new #n_addr_t
 */
n_addr_t * nice_address_dup(const n_addr_t * addr);


/**
 * nice_address_set_ipv4:
 * @addr: The #n_addr_t to modify
 * @addr_ipv4: The IPv4 address
 *
 * Set @addr to an IPv4 address using the data from @addr_ipv4
 *
 <note>
  <para>
   This function will reset the port to 0, so make sure you call it before
   nice_address_set_port()
  </para>
 </note>
 */
void nice_address_set_ipv4(n_addr_t * addr, guint32 addr_ipv4);


/**
 * nice_address_set_ipv6:
 * @addr: The #n_addr_t to modify
 * @addr_ipv6: The IPv6 address
 *
 * Set @addr to an IPv6 address using the data from @addr_ipv6
 *
 <note>
  <para>
   This function will reset the port to 0, so make sure you call it before
   nice_address_set_port()
  </para>
 </note>
 */
void nice_address_set_ipv6(n_addr_t * addr, const guchar * addr_ipv6);


/**
 * nice_address_set_port:
 * @addr: The #n_addr_t to modify
 * @port: The port to set
 *
 * Set the port of @addr to @port
 */
void nice_address_set_port(n_addr_t * addr, guint port);

/**
 * nice_address_get_port:
 * @addr: The #n_addr_t to query
 *
 * Retreive the port of @addr
 *
 * Returns: The port of @addr
 */
uint32_t nice_address_get_port(const n_addr_t * addr);

/**
 * nice_address_set_from_string:
 * @addr: The #n_addr_t to modify
 * @str: The string to set
 *
 * Sets an IPv4 or IPv6 address from the string @str
 *
 * Returns: %TRUE if success, %FALSE on error
 */
int nice_address_set_from_string(n_addr_t * addr, const gchar * str);

/**
 * n_addr_set_from_sock:
 * @addr: The #n_addr_t to modify
 * @sin: The sockaddr to set
 *
 * Sets an IPv4 or IPv6 address from the sockaddr structure @sin
 *
 */
void n_addr_set_from_sock(n_addr_t * addr, const struct sockaddr * sin);


/**
 * nice_address_copy_to_sockaddr:
 * @addr: The #n_addr_t to query
 * @sin: The sockaddr to fill
 *
 * Fills the sockaddr structure @sin with the address contained in @addr
 *
 */
void nice_address_copy_to_sockaddr(const n_addr_t * addr, struct sockaddr * sin);

/**
 * nice_address_equal:
 * @a: First #n_addr_t to compare
 * @b: Second #n_addr_t to compare
 *
 * Compares two #n_addr_t structures to see if they contain the same address
 * and the same port.
 *
 * Returns: %TRUE if @a and @b are the same address, %FALSE if they are different
 */
int nice_address_equal(const n_addr_t * a, const n_addr_t * b);

/**
 * nice_address_equal_no_port:
 * @a: First #n_addr_t to compare
 * @b: Second #n_addr_t to compare
 *
 * Compares two #n_addr_t structures to see if they contain the same address,
 * ignoring the port.
 *
 * Returns: %TRUE if @a and @b are the same address, %FALSE if they
 * are different
 *
 * Since: 0.1.8
 */
int nice_address_equal_no_port(const n_addr_t * a, const n_addr_t * b);

/**
 * nice_address_to_string:
 * @addr: The #n_addr_t to query
 * @dst: The string to fill
 *
 * Transforms the address @addr into a human readable string
 *
 */
void nice_address_to_string(const n_addr_t * addr, gchar * dst);

/**
 * nice_address_is_private:
 * @addr: The #n_addr_t to query
 *
 * Verifies if the address in @addr is a private address or not
 *
 * Returns: %TRUE if @addr is a private address, %FALSE otherwise
 */
int nice_address_is_private(const n_addr_t * addr);

/**
 * n_addr_is_valid:
 * @addr: The #n_addr_t to query
 *
 * Validate whether the #n_addr_t @addr is a valid IPv4 or IPv6 address
 *
 * Returns: %TRUE if @addr is valid, %FALSE otherwise
 */

int n_addr_is_valid(const n_addr_t * addr);

/**
 * nice_address_ip_version:
 * @addr: The #n_addr_t to query
 *
 * Returns the IP version of the address
 *
 * Returns: 4 for IPv4, 6 for IPv6 and 0 for undefined address
 */

int nice_address_ip_version(const n_addr_t * addr);


#endif /* __LIBNICE_ADDRESS_H__ */

