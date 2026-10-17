#ifndef PASSWORD_AUTH_H
#define PASSWORD_AUTH_H
#ifdef NOCRYPT
#error "NOCRYPT is insecure and unsupported"
#endif
#define PASSWORD_HASH_SIZE 256
#define PASSWORD_LEGACY_MAX 4023
#include <stddef.h>
int password_new_valid(const char *password);
int password_hash(const char *password, char out[PASSWORD_HASH_SIZE]);
/* 0 invalid, 1 modern, 2 migratable legacy, 3 ambiguous DES, 4 long legacy.
 * Existing credentials up to the historical input limit remain usable; long
 * credentials require an explicit change rather than silent truncation. */
int password_verify(const char *password, const char *stored);
/* One case-preserving, optionally quoted token; rejects overflow/unclosed quotes. */
int password_token(const char **input, char *out, size_t capacity);
#endif
