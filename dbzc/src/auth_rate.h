#ifndef AUTH_RATE_H
#define AUTH_RATE_H
#include <stdint.h>
int auth_rate_allow(const char *ip, const char *account, uint64_t now);
/* Uses the actual socket peer in direct mode, or a validated numeric PROXY
 * identity bound to this socket in explicit proxy mode; never a DNS hostname.
 * Charge BEFORE EACH verification/hash, not once per login: a migrating login
 * or password change costs two tokens. Limits: 4 KDFs per monotonic second
 * globally, 12 per IP and 6 per normalized account per 60-second window.
 * Fixed windows allow bursts at a boundary; buckets survive reconnects.
 * Tables are bounded and fail closed when full; process restart clears them. */
int auth_attempt(int fd, const char *account);
int auth_proxy_mode(void);
/* Registration is only for the validated header/recovery paths. Both socket
 * endpoints must be IPv4 loopback; callers also validate the actual listener.
 * Local processes are inside this trust boundary. Socket inode binding prevents
 * a missed cleanup from transferring identity to a reused descriptor number. */
int auth_proxy_register(int fd, const char *ip);
void auth_proxy_forget(int fd);
int auth_proxy_loopback(int fd, int peer);
#endif
