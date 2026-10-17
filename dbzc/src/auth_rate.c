#include "auth_rate.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

#define CAPACITY 1024
struct bucket { char key[128]; uint64_t start; unsigned count; };
static struct bucket ips[CAPACITY], accounts[CAPACITY], global;

static struct bucket *lookup(struct bucket *table, const char *key, uint64_t now)
{
    unsigned i;
    struct bucket *empty = 0;
    for (i = 0; i < CAPACITY; ++i) {
        if (table[i].key[0] && now >= table[i].start && now - table[i].start >= 60)
            memset(&table[i], 0, sizeof(table[i]));
        if (!strcmp(table[i].key, key)) return &table[i];
        if (!table[i].key[0] && !empty) empty = &table[i];
    }
    if (!empty) return 0; /* No eviction that lets attackers reset a victim. */
    strcpy(empty->key, key);
    empty->start = now;
    return empty;
}

int auth_rate_allow(const char *ip, const char *account, uint64_t now)
{
    struct bucket *a, *b;
    char normalized[128];
    size_t i, n;
    if (!ip || !*ip || strlen(ip) >= 128 || !account || !*account) return 0;
    n = strlen(account);
    if (n >= sizeof(normalized)) return 0;
    for (i = 0; i <= n; ++i) normalized[i] = (char)tolower((unsigned char)account[i]);
    /* At most four expensive operations each second, across all connections. */
    if (now > global.start) { global.start = now; global.count = 0; }
    if (global.count >= 4) return 0;
    a = lookup(ips, ip, now);
    b = lookup(accounts, normalized, now);
    if (!a || !b || a->count >= 12 || b->count >= 6) return 0;
    ++a->count; ++b->count; ++global.count;
    return 1;
}

int auth_attempt(int fd, const char *account)
{
    struct sockaddr_storage peer;
    socklen_t len = sizeof(peer);
    char ip[INET6_ADDRSTRLEN];
    struct timespec now;
    const void *addr;
    if (getpeername(fd, (struct sockaddr *)&peer, &len) ||
        clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    if (peer.ss_family == AF_INET)
        addr = &((struct sockaddr_in *)&peer)->sin_addr;
    else if (peer.ss_family == AF_INET6) {
        struct in6_addr *v6 = &((struct sockaddr_in6 *)&peer)->sin6_addr;
        if (IN6_IS_ADDR_V4MAPPED(v6)) {
            addr = &v6->s6_addr[12];
            peer.ss_family = AF_INET;
        } else addr = v6;
    } else return 0;
    if (!inet_ntop(peer.ss_family, addr, ip, sizeof(ip))) return 0;
    return auth_rate_allow(ip, account, (uint64_t)now.tv_sec);
}
