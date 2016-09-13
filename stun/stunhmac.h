/* This file is part of the Nice GLib ICE library */

#ifndef _STUN_HMAC_H
#define _STUN_HMAC_H

#include "stunmessage.h"

/*
 * Computes the MESSAGE-INTEGRITY hash of a STUN message.
 * @param msg pointer to the STUN message
 * @param len size of the message from header (inclusive) and up to
 *            MESSAGE-INTEGRITY attribute (inclusive)
 * @param sha output buffer for SHA1 hash (20 bytes)
 * @param key HMAC key
 * @param keylen HMAC key bytes length
 *
 * @return fingerprint value in <b>host</b> byte order.
 */
void stun_sha1(const uint8_t * msg, size_t len, size_t msg_len,
               uint8_t * sha, const void * key, size_t keylen, int padding);

/*
 * SIP H(A1) computation
 */

void stun_hash_creds(const uint8_t * realm, size_t realm_len,
                     const uint8_t * username, size_t username_len,
                     const uint8_t * password, size_t password_len,
                     unsigned char md5[16]);
/*
 * Generates a pseudo-random secure STUN transaction ID.
 */
void stun_make_transid(stun_trans_id id);


#endif /* _STUN_HMAC_H */
