#define _GNU_SOURCE
#include "password_auth.h"
#include "auth_rate.h"
#include <assert.h>
#include <crypt.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>

/* Failure diagnostics deliberately never render credential expressions. */
#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "auth helper assertion failed at line %d\n", __LINE__); \
    abort(); } } while (0)

static int entropy_failure, crypt_failure, interrupted;
ssize_t __real_getrandom(void *, size_t, unsigned);
ssize_t __wrap_getrandom(void *p, size_t n, unsigned flags)
{
    if (entropy_failure) { errno = EIO; return -1; }
    if (interrupted) { interrupted = 0; errno = EINTR; return -1; }
    return __real_getrandom(p, n > 3 ? 3 : n, flags);
}
char *__real_crypt(const char *, const char *);
char *__wrap_crypt(const char *p, const char *s)
{
    if (crypt_failure == 1) return NULL;
    if (crypt_failure == 2) return "*0";
    return __real_crypt(p, s);
}
static void legacy(const char *p, const char *salt, int expected)
{
    char stored[PASSWORD_HASH_SIZE];
    char *result = crypt(p, salt);
    assert(result && result[0] != '*');
    snprintf(stored, sizeof(stored), "%s", result);
    assert(password_verify(p, stored) == expected);
    assert(!password_verify("deliberately incorrect fixture", stored));
    crypt_failure = 1;
    assert(!password_verify(p, stored));
    crypt_failure = 2;
    assert(!password_verify(p, stored));
    crypt_failure = 0;
}
int main(void)
{
    char hash[PASSWORD_HASH_SIZE], second[PASSWORD_HASH_SIZE], p[4025], token[16];
    const char *input;
    int i;
    memset(p, 'Q', sizeof(p)); p[12] = 0;
    assert(password_new_valid(p));
    interrupted = 1;
    assert(password_hash(p, hash));
    assert(strstr(hash, "$argon2id$v=19$m=19456,t=2,p=1$") == hash);
    assert(password_hash(p, second) && strcmp(hash, second));
    assert(password_verify(p, hash) == 1);
    assert(!password_verify("incorrect fixture", hash));
    entropy_failure = 1;
    assert(!password_hash(p, second) && !second[0]);
    entropy_failure = 0;
    assert(!password_verify(NULL, hash) && !password_verify(p, NULL));
    assert(!password_verify(p, "*0") && !password_verify(p, "!"));
    assert(!password_verify(p, "$argon2id$v=19$m=999999999,t=2,p=1$a$b"));
    assert(!password_verify(p, "$6$rounds=10001$salt$invalid"));
    assert(!password_verify(p, "$2b$31$invalid"));
    assert(!password_new_valid("short"));
    memset(p, 'Q', sizeof(p)); p[128] = 0;
    assert(password_new_valid(p) && password_hash(p, second));
    p[128] = 'Q'; p[129] = 0;
    assert(!password_new_valid(p) && !password_hash(p, second));
    legacy("Short", "ab", 2);
    legacy("MixedCaseSuffix", "ab", 3);
    legacy("Short", "$1$fixture$", 2);
    legacy("Short", "$5$rounds=1000$fixture$", 2);
    legacy("Short", "$6$fixture$", 2);
    legacy(p, "$6$rounds=1000$fixture$", 4);
    p[129] = 'Q'; p[4024] = 0;
    assert(!password_verify(p, hash));
    input = "  \"MiXeD Space\" next";
    assert(password_token(&input, token, sizeof(token)));
    assert(!strcmp(token, "MiXeD Space") && !strcmp(input, "next"));
    input = "'unterminated";
    assert(!password_token(&input, token, sizeof(token)));
    input = "\"quoted\"junk";
    assert(!password_token(&input, token, sizeof(token)));
    input = "012345678901234567890";
    assert(!password_token(&input, token, sizeof(token)));
    input = "MiXeD\tNext";
    assert(password_token(&input, token, sizeof(token)) && !strcmp(token, "MiXeD"));
    for (i = 0; i < 4; ++i) assert(auth_rate_allow("192.0.2.1", "Account", 100));
    assert(!auth_rate_allow("192.0.2.2", "Other", 100)); /* global */
    assert(auth_rate_allow("192.0.2.2", "ACCOUNT", 101));
    assert(auth_rate_allow("192.0.2.3", "account", 101));
    assert(!auth_rate_allow("192.0.2.4", "Account", 102)); /* account */
    for (i = 0; i < 12; ++i) {
        char account[32];
        snprintf(account, sizeof(account), "fixture%d", i);
        assert(auth_rate_allow("192.0.2.9", account, 110 + (unsigned)i));
    }
    assert(!auth_rate_allow("192.0.2.9", "another", 123)); /* IP */
    assert(auth_rate_allow("192.0.2.9", "another", 171));
    assert(!auth_attempt(-1, "fixture"));
    /* Actual numeric peer survives socket reconnects (not descriptor identity). */
    {
        int listener = socket(AF_INET, SOCK_STREAM, 0), client, server;
        struct sockaddr_in addr = {0};
        socklen_t len = sizeof(addr);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(listener >= 0 && !bind(listener, (void *)&addr, sizeof(addr)));
        assert(!getsockname(listener, (void *)&addr, &len) && !listen(listener, 8));
        for (i = 0; i < 8; ++i) {
            client = socket(AF_INET, SOCK_STREAM, 0);
            assert(client >= 0 && !connect(client, (void *)&addr, len));
            server = accept(listener, NULL, NULL);
            assert(server >= 0);
            if (i >= 6) assert(!auth_attempt(server, "ReconnectFixture"));
            else (void)auth_attempt(server, "ReconnectFixture");
            close(server); close(client);
        }
        close(listener);
    }
    puts("auth helper regressions passed");
    return 0;
}
