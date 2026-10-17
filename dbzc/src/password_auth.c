#include "password_auth.h"
#include <argon2.h>
#include <crypt.h>
#include <sys/random.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

int password_token(const char **input, char *out, size_t capacity)
{
    const char *s = *input;
    size_t n = 0;
    char quote = 0;
    if (!capacity) return 0;
    while (isspace((unsigned char)*s)) ++s;
    if (*s == '\'' || *s == '"') quote = *s++;
    while (*s && (quote ? *s != quote : !isspace((unsigned char)*s))) {
        if (n + 1 >= capacity) { out[0] = '\0'; return 0; }
        out[n++] = *s++;
    }
    out[n] = '\0';
    if (quote) {
        if (*s != quote) return 0;
        ++s;
        if (*s && !isspace((unsigned char)*s)) return 0;
    }
    while (isspace((unsigned char)*s)) ++s;
    *input = s;
    return n != 0;
}

int password_new_valid(const char *p)
{
    size_t n = p ? strnlen(p, 129) : 0;
    return n >= 12 && n <= 128;
}

int password_hash(const char *p, char out[PASSWORD_HASH_SIZE])
{
    unsigned char salt[16];
    size_t used = 0;
    ssize_t n;
    if (!out) return 0;
    out[0] = '\0';
    if (!p || strnlen(p, 129) > 128 || !*p) return 0;
    while (used < sizeof(salt)) {
        n = getrandom(salt + used, sizeof(salt) - used, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        used += (size_t)n;
    }
    return argon2id_hash_encoded(2, 19456, 1, p, strlen(p),
        salt, sizeof(salt), 32, out, PASSWORD_HASH_SIZE) == ARGON2_OK;
}

static int alphabet(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (!strchr("./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", s[i]))
            return 0;
    return 1;
}

int password_verify(const char *p, const char *stored)
{
    size_t n, i;
    char *result;
    unsigned int diff = 0;
    const char *s, *end;
    unsigned long rounds;
    if (!p || !stored || !*p ||
        strnlen(p, PASSWORD_LEGACY_MAX + 1) > PASSWORD_LEGACY_MAX) return 0;
    n = strnlen(stored, PASSWORD_HASH_SIZE);
    if (!n || n >= PASSWORD_HASH_SIZE) return 0;
    if (!strncmp(stored, "$argon2id$", 10)) {
        /* Only our bounded policy is accepted; never execute file-supplied costs. */
        if (strncmp(stored, "$argon2id$v=19$m=19456,t=2,p=1$",
                    sizeof("$argon2id$v=19$m=19456,t=2,p=1$") - 1)) return 0;
        return argon2id_verify(stored, p, strlen(p)) == ARGON2_OK ? 1 : 0;
    }
    if (n == 13 && alphabet(stored, n)) {
        /* Traditional DES crypt. */
    } else if (!strncmp(stored, "$1$", 3)) {
        s = strchr(stored + 3, '$');
        if (!s || s == stored + 3 || s - (stored + 3) > 8 ||
            !alphabet(stored + 3, s - (stored + 3)) ||
            strlen(s + 1) != 22 || !alphabet(s + 1, 22)) return 0;
    } else if (!strncmp(stored, "$5$", 3) || !strncmp(stored, "$6$", 3)) {
        s = stored + 3;
        if (!strncmp(s, "rounds=", 7)) {
            s += 7;
            if (*s < '0' || *s > '9') return 0;
            errno = 0;
            rounds = strtoul(s, (char **)&end, 10);
            if (errno || *end != '$' || rounds < 1000 || rounds > 10000) return 0;
            s = end + 1;
        }
        end = strchr(s, '$');
        i = stored[1] == '5' ? 43 : 86;
        if (!end || end == s || end - s > 16 || !alphabet(s, end - s) ||
            strlen(end + 1) != i || !alphabet(end + 1, i)) return 0;
    } else return 0;
    result = crypt(p, stored);
    if (!result || result[0] == '*' || strlen(result) != n) return 0;
    for (i = 0; i < n; ++i) diff |= (unsigned char)result[i] ^ (unsigned char)stored[i];
    if (diff) return 0;
    if (n == 13 && strlen(p) > 8) return 3;
    return strlen(p) > 128 ? 4 : 2;
}
