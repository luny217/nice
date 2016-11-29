/* This file is part of the Nice GLib ICE library. */

#ifndef __LIBNICE_CANDIDATE_H__
#define __LIBNICE_CANDIDATE_H__

//#include <glib.h>
//#include <glib-object.h>

/* Constants for determining candidate priorities */
#define CAND_TYPE_PREF_HOST 120
#define CAND_TYPE_PREF_PEER 110
#define CAND_TYPE_PREF_NAT 105
#define CAND_TYPE_PREF_SERVER 100
#define CAND_TYPE_PREF_TUNNELED 75
#define CAND_TYPE_PREF_RELAYED 10

/* Max foundation size '1*32ice-char' plus terminating NULL, ICE ID-19  */
/**
 * NICE_CANDIDATE_MAX_FOUNDATION:
 *
 * The maximum size a candidate foundation can have.
 */
#define CAND_MAX_FOUNDATION (32+1)

/**
 * n_cand_type_e:
 * @NICE_CANDIDATE_TYPE_HOST: A host candidate
 * @NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE: A server reflexive candidate
 * @NICE_CANDIDATE_TYPE_PEER_REFLEXIVE: A peer reflexive candidate
 * @NICE_CANDIDATE_TYPE_RELAYED: A relay candidate
 *
 * An enum represneting the type of a candidate
 */
typedef enum
{
    CAND_TYPE_HOST,
    CAND_TYPE_SERVER,
    CAND_TYPE_PEER,
    CAND_TYPE_RELAYED,
} n_cand_type_e; 

/**
 * n_cand_trans_e:
 * @NICE_CAND_TRANS_UDP: UDP transport
 * @NICE_CAND_TRANS_TCP_ACTIVE: TCP Active transport
 * @NICE_CAND_TRANS_TCP_PASSIVE: TCP Passive transport
 * @NICE_CAND_TRANS_TCP_SO: TCP Simultaneous-Open transport
 *
 * An enum representing the type of transport to use
 */
typedef enum
{
    CAND_TRANS_UDP,
    CAND_TRANS_TCP_ACTIVE,
    CAND_TRANS_TCP_PASSIVE,
    CAND_TRANS_TCP_SO,
} n_cand_trans_e;

/**
 * n_relay_type_e:
 * @NICE_RELAY_TYPE_TURN_UDP: A TURN relay using UDP
 * @NICE_RELAY_TYPE_TURN_TCP: A TURN relay using TCP
 * @NICE_RELAY_TYPE_TURN_TLS: A TURN relay using TLS over TCP
 *
 * An enum representing the type of relay to use
 */
typedef enum
{
    RELAY_TYPE_TURN_UDP,
    RELAY_TYPE_TURN_TCP,
    RELAY_TYPE_TURN_TLS
} n_relay_type_e;


typedef struct _cand_st n_cand_t; 

typedef struct _turn_server_st turn_server_t;

/**
 * turn_server_t:
 * @ref_count: Reference count for the structure.
 * @server: The #n_addr_t of the TURN server
 * @username: The TURN username
 * @password: The TURN password
 * @type: The #n_relay_type_e of the server
 *
 * A structure to store the TURN relay settings
 */
struct _turn_server_st
{
    uint32_t ref_count;
    n_addr_t server;
    char * username;
    char * password;
    //n_relay_type_e type;
};

/**
 * n_cand_t:
 * @type: The type of candidate
 * @transport: The transport being used for the candidate
 * @addr: The #n_addr_t of the candidate
 * @base_addr: The #n_addr_t of the base address used by the candidate
 * @priority: The priority of the candidate <emphasis> see note </emphasis>
 * @stream_id: The ID of the stream to which belongs the candidate
 * @component_id: The ID of the component to which belongs the candidate
 * @foundation: The foundation of the candidate
 * @username: The candidate-specific username to use (overrides the one set
 * by n_agent_set_local_credentials() or n_agent_set_remote_credentials())
 * @password: The candidate-specific password to use (overrides the one set
 * by n_agent_set_local_credentials() or n_agent_set_remote_credentials())
 * @turn: The #turn_server_t settings if the candidate is
 * of type %NICE_CANDIDATE_TYPE_RELAYED
 * @sockptr: The underlying socket
 *
 * A structure to represent an ICE candidate
 <note>
   <para>
   The @priority is an integer as specified in the ICE draft 19. If you are
   using the MSN or the GOOGLE compatibility mode (which are based on ICE
   draft 6, which uses a floating point qvalue as priority), then the @priority
   value will represent the qvalue multiplied by 1000.
   </para>
 </note>
 */
struct _cand_st
{
    n_cand_type_e type;
    n_cand_trans_e transport;
    n_addr_t addr;
    n_addr_t base_addr;
    uint32_t priority;
    uint32_t stream_id;
    uint32_t component_id;
    char foundation[CAND_MAX_FOUNDATION];
    char * username;       /* pointer to a nul-terminated username string */
    char * password;       /* pointer to a nul-terminated password string */
    turn_server_t * turn;
    void * sockptr;
};

/**
 * n_cand_new:
 * @type: The #n_cand_type_e of the candidate to create
 *
 * Creates a new candidate. Must be freed with n_cand_free()
 *
 * Returns: A new #n_cand_t
 */
n_cand_t * n_cand_new(n_cand_type_e type);

/**
 * n_cand_free:
 * @candidate: The candidate to free
 *
 * Frees a #n_cand_t
 */
void n_cand_free(n_cand_t * candidate);

/**
 * nice_candidate_copy:
 * @candidate: The candidate to copy
 *
 * Makes a copy of a #n_cand_t
 *
 * Returns: A new #n_cand_t, a copy of @candidate
 */
n_cand_t * nice_candidate_copy(const n_cand_t * candidate);

//GType nice_candidate_get_type(void);

/**
 * NICE_TYPE_CANDIDATE:
 *
 * A boxed type for a #n_cand_t.
 */
//#define NICE_TYPE_CANDIDATE nice_candidate_get_type ()

#endif /* __LIBNICE_CANDIDATE_H__ */

