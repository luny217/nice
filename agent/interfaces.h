/* This file is part of the Nice GLib ICE library. */

#ifndef __LIBNICE_INTERFACES_H__
#define __LIBNICE_INTERFACES_H__

/**
 * SECTION:interfaces
 * @short_description: Utility functions to discover local network interfaces
 * @include: interfaces.h
 * @stability: Stable
 *
 * These utility functions allow the discovery of local network interfaces
 * in a portable manner, they also allow finding the local ip addresses or
 * the address allocated to a network interface.
 */

#include <glib.h>
#include "nlist.h"

/**
 * nice_interfaces_get_ip_for_interface:
 * @interface_name: name of local interface
 *
 * Retrieves the IP address of an interface by its name. If this fails, %NULL
 * is returned.
 *
 * Returns: (nullable) (transfer full): a newly-allocated string with the IP
 * address
 */
char * nice_interfaces_get_ip_for_interface (char *interface_name);


/**
 * nice_interfaces_get_local_ips:
 * @include_loopback: Include any loopback devices
 *
 * Get a list of local ipv4 interface addresses
 *
 * Returns: (element-type utf8) (transfer full): a newly-allocated #n_dlist_t  of
 * strings. The caller must free it.
 */

n_dlist_t  * nice_interfaces_get_local_ips (gboolean include_loopback);


/**
 * nice_interfaces_get_local_interfaces:
 *
 * Get the list of local interfaces
 *
 * Returns: (element-type utf8) (transfer full): a newly-allocated #n_dlist_t  of
 * strings. The caller must free it.
 */
n_dlist_t  * nice_interfaces_get_local_interfaces (void);

#endif /* __LIBNICE_INTERFACES_H__ */
