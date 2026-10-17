#ifndef AUTH_RATE_H
#define AUTH_RATE_H
#include <stdint.h>
int auth_rate_allow(const char *ip, const char *account, uint64_t now);
/* Resolves the actual socket peer, never the reverse-DNS hostname.
 * Charge BEFORE EACH verification/hash, not once per login: a migrating login
 * or password change costs two tokens. Limits: 4 KDFs per monotonic second
 * globally, 12 per IP and 6 per normalized account per 60-second window.
 * Fixed windows allow bursts at a boundary; buckets survive reconnects.
 * Tables are bounded and fail closed when full; process restart clears them. */
int auth_attempt(int fd, const char *account);
#endif
