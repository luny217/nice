/* This file is part of the Nice GLib ICE library. */

#include <config.h>
#include <string.h>

#include "agent.h"
#include "component.h"

//G_DEFINE_BOXED_TYPE(n_cand_t, nice_candidate, nice_candidate_copy, n_cand_free);

GType nice_candidate_get_type(void)
{
    static volatile gsize g_define_type_id__volatile = 0;
    if ((g_once_init_enter((&g_define_type_id__volatile))))
    {
        GType g_define_type_id = g_boxed_type_register_static(g_intern_static_string("n_cand_t"), (GBoxedCopyFunc)nice_candidate_copy, (GBoxedFreeFunc)n_cand_free);
        {
            {
                {
                };
            }
        }(g_once_init_leave((&g_define_type_id__volatile), (gsize)(g_define_type_id)));
    }
    return g_define_type_id__volatile;
};

/* (ICE 4.1.1 "Gathering Candidates") ""Every candidate is a transport
 * address. It also has a type and a base. Three types are defined and
 * gathered by this specification - host candidates, server reflexive
 * candidates, and relayed candidates."" (ID-19) */

n_cand_t * n_cand_new(n_cand_type_e type)
{
    n_cand_t * candidate;

    candidate = g_slice_new0(n_cand_t);
    candidate->type = type;
    return candidate;
}

void n_cand_free(n_cand_t * candidate)
{
    /* better way of checking if socket is allocated? */

    if (candidate->username)
        n_free(candidate->username);

    if (candidate->password)
        n_free(candidate->password);

    if (candidate->turn)
        turn_server_unref(candidate->turn);

    g_slice_free(n_cand_t, candidate);
}

/*
 * ICE 4.1.2.1. "Recommended Formula" (ID-19):
 * returns number between 1 and 0x7effffff
 */
uint32_t nice_candidate_ice_priority_full(
    // must be ? (0, 126) (max 2^7 - 2)
    uint32_t type_preference,
    // must be ? (0, 65535) (max 2^16 - 1)
    uint32_t local_preference,
    // must be ? (0, 255) (max 2 ^ 8 - 1)
    uint32_t component_id)
{
    return (0x1000000 * type_preference + 0x100 * local_preference + (0x100 - component_id));
}

static uint32_t nice_candidate_ice_local_preference_full(uint32_t direction_preference, uint32_t other_preference)
{
    return (0x2000 * direction_preference + other_preference);
}

static uint16_t nice_candidate_ice_local_preference(const n_cand_t * candidate)
{
    uint32_t direction_preference;

    switch (candidate->transport)
    {
	case CANDIDATE_TRANSPORT_TCP_ACTIVE:
		if (candidate->type == CAND_TYPE_SERVER ||
			candidate->type == CAND_TYPE_PREF_NAT)
			direction_preference = 4;
		else
			direction_preference = 6;
		break;
	case CANDIDATE_TRANSPORT_TCP_PASSIVE:
		if (candidate->type == CAND_TYPE_SERVER ||
			candidate->type == CAND_TYPE_PREF_NAT)
			direction_preference = 2;
		else
			direction_preference = 4;
		break;
	case CANDIDATE_TRANSPORT_TCP_SO:
		if (candidate->type == CAND_TYPE_SERVER ||
			candidate->type == CAND_TYPE_PREF_NAT)
			direction_preference = 6;
		else
			direction_preference = 2;
		break;
        case CANDIDATE_TRANSPORT_UDP:
        default:
            return 1;
            break;
    }

    return nice_candidate_ice_local_preference_full(direction_preference, 1);
}

static uint8_t nice_candidate_ice_type_preference(const n_cand_t * candidate)
{
    uint8_t type_preference;

    switch (candidate->type)
    {
        case CAND_TYPE_HOST:
            type_preference = CAND_TYPE_PREF_HOST;
            break;
        case CAND_TYPE_PEER:
            type_preference = CAND_TYPE_PREF_PEER;
            break;
        case CAND_TYPE_SERVER:
            type_preference = CAND_TYPE_PREF_SERVER;
            break;
        case CAND_TYPE_RELAYED:
            type_preference = CAND_TYPE_PREF_RELAYED;
            break;
        default:
            type_preference = 0;
            break;
    }

    if (candidate->transport == CANDIDATE_TRANSPORT_UDP)
    {
        type_preference = type_preference / 2;
    }

    return type_preference;
}

uint32_t n_cand_ice_priority(const n_cand_t * candidate)
{
    uint8_t type_preference;
    uint16_t local_preference;

    type_preference = nice_candidate_ice_type_preference(candidate);
    local_preference = nice_candidate_ice_local_preference(candidate);

    return nice_candidate_ice_priority_full(type_preference, local_preference, candidate->component_id);
}

/*
 * Calculates the pair priority as specified in ICE
 * sect 5.7.2. "Computing Pair Priority and Ordering Pairs" (ID-19).
 */
uint64_t nice_candidate_pair_priority(uint32_t o_prio, uint32_t a_prio)
{
    uint32_t max = o_prio > a_prio ? o_prio : a_prio;
    uint32_t min = o_prio < a_prio ? o_prio : a_prio;
    /* These two constants are here explictly to make some version of GCC happy */
    const uint64_t one = 1;
    const uint64_t thirtytwo = 32;

    return ((one << thirtytwo) * min + 2 * max + (o_prio > a_prio ? 1 : 0));
}

/*
 * Copies a candidate
 */
n_cand_t * nice_candidate_copy(const n_cand_t * candidate)
{
    n_cand_t * copy;

    g_return_val_if_fail(candidate != NULL, NULL);

    copy = n_cand_new(candidate->type);
    memcpy(copy, candidate, sizeof(n_cand_t));

    copy->turn = NULL;
    copy->username = g_strdup(copy->username);
    copy->password = g_strdup(copy->password);

    return copy;
}
