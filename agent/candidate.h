/* This file is part of the Nice GLib ICE library. */

#ifndef __LIBNICE_CANDIDATE_H__
#define __LIBNICE_CANDIDATE_H__

#include <glib.h>
#include <glib-object.h>


/**
 * SECTION:candidate
 * @short_description: ICE candidate representation
 * @see_also: #NiceAddress
 * @stability: Stable
 *
 * A representation of an ICE candidate. Make sure you read the ICE drafts[1] to
 * understand correctly the concept of ICE candidates.
 *
 * [1] http://tools.ietf.org/wg/mmusic/draft-ietf-mmusic-ice/
 */

/* Constants for determining candidate priorities */
#define CANDIDATE_TYPE_PREF_HOST                 120
#define CANDIDATE_TYPE_PREF_PEER_REFLEXIVE       110
#define CANDIDATE_TYPE_PREF_NAT_ASSISTED         105
#define CANDIDATE_TYPE_PREF_SERVER_REFLEXIVE     100
#define CANDIDATE_TYPE_PREF_UDP_TUNNELED          75
#define CANDIDATE_TYPE_PREF_RELAYED               10

#if 0
/* Priority preference constants for MS-ICE compatibility */
#define NICE_CANDIDATE_TRANSPORT_MS_PREF_UDP           15
#define NICE_CANDIDATE_TRANSPORT_MS_PREF_TCP            6
#define NICE_CANDIDATE_DIRECTION_MS_PREF_PASSIVE        2
#define NICE_CANDIDATE_DIRECTION_MS_PREF_ACTIVE         5
#endif

/* Max foundation size '1*32ice-char' plus terminating NULL, ICE ID-19  */
/**
 * NICE_CANDIDATE_MAX_FOUNDATION:
 *
 * The maximum size a candidate foundation can have.
 */
#define CANDIDATE_MAX_FOUNDATION                (32+1)


/**
 * NiceCandidateType:
 * @NICE_CANDIDATE_TYPE_HOST: A host candidate
 * @NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE: A server reflexive candidate
 * @NICE_CANDIDATE_TYPE_PEER_REFLEXIVE: A peer reflexive candidate
 * @NICE_CANDIDATE_TYPE_RELAYED: A relay candidate
 *
 * An enum represneting the type of a candidate
 */
typedef enum
{
    CANDIDATE_TYPE_HOST,
    CANDIDATE_TYPE_SERVER_REFLEXIVE,
    CANDIDATE_TYPE_PEER_REFLEXIVE,
    CANDIDATE_TYPE_RELAYED,
} NiceCandidateType;

/**
 * NiceCandidateTransport:
 * @NICE_CANDIDATE_TRANSPORT_UDP: UDP transport
 * @NICE_CANDIDATE_TRANSPORT_TCP_ACTIVE: TCP Active transport
 * @NICE_CANDIDATE_TRANSPORT_TCP_PASSIVE: TCP Passive transport
 * @NICE_CANDIDATE_TRANSPORT_TCP_SO: TCP Simultaneous-Open transport
 *
 * An enum representing the type of transport to use
 */
typedef enum
{
    CANDIDATE_TRANSPORT_UDP,
    CANDIDATE_TRANSPORT_TCP_ACTIVE,
    CANDIDATE_TRANSPORT_TCP_PASSIVE,
    CANDIDATE_TRANSPORT_TCP_SO,
} NiceCandidateTransport;

/**
 * NiceRelayType:
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
} NiceRelayType;


typedef struct _NiceCandidate NiceCandidate;

typedef struct _TurnServer TurnServer;

/**
 * TurnServer:
 * @ref_count: Reference count for the structure.
 * @server: The #NiceAddress of the TURN server
 * @username: The TURN username
 * @password: The TURN password
 * @type: The #NiceRelayType of the server
 *
 * A structure to store the TURN relay settings
 */
struct _TurnServer
{
    uint32_t ref_count;
    NiceAddress server;
    char * username;
    char * password;
    NiceRelayType type;
};

/**
 * NiceCandidate:
 * @type: The type of candidate
 * @transport: The transport being used for the candidate
 * @addr: The #NiceAddress of the candidate
 * @base_addr: The #NiceAddress of the base address used by the candidate
 * @priority: The priority of the candidate <emphasis> see note </emphasis>
 * @stream_id: The ID of the stream to which belongs the candidate
 * @component_id: The ID of the component to which belongs the candidate
 * @foundation: The foundation of the candidate
 * @username: The candidate-specific username to use (overrides the one set
 * by nice_agent_set_local_credentials() or nice_agent_set_remote_credentials())
 * @password: The candidate-specific password to use (overrides the one set
 * by nice_agent_set_local_credentials() or nice_agent_set_remote_credentials())
 * @turn: The #TurnServer settings if the candidate is
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
struct _NiceCandidate
{
    NiceCandidateType type;
    NiceCandidateTransport transport;
    NiceAddress addr;
    NiceAddress base_addr;
    uint32_t priority;
    uint32_t stream_id;
    uint32_t component_id;
    char foundation[CANDIDATE_MAX_FOUNDATION];
    char * username;       /* pointer to a nul-terminated username string */
    char * password;       /* pointer to a nul-terminated password string */
    TurnServer * turn;
    void * sockptr;
};

/**
 * nice_candidate_new:
 * @type: The #NiceCandidateType of the candidate to create
 *
 * Creates a new candidate. Must be freed with nice_candidate_free()
 *
 * Returns: A new #NiceCandidate
 */
NiceCandidate * nice_candidate_new(NiceCandidateType type);

/**
 * nice_candidate_free:
 * @candidate: The candidate to free
 *
 * Frees a #NiceCandidate
 */
void nice_candidate_free(NiceCandidate * candidate);

/**
 * nice_candidate_copy:
 * @candidate: The candidate to copy
 *
 * Makes a copy of a #NiceCandidate
 *
 * Returns: A new #NiceCandidate, a copy of @candidate
 */
NiceCandidate * nice_candidate_copy(const NiceCandidate * candidate);

GType nice_candidate_get_type(void);

/**
 * NICE_TYPE_CANDIDATE:
 *
 * A boxed type for a #NiceCandidate.
 */
#define NICE_TYPE_CANDIDATE nice_candidate_get_type ()

#endif /* __LIBNICE_CANDIDATE_H__ */

