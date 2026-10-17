#include "auth_rate.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/stat.h>

/* Bind a validated identity to a particular socket, not merely its fd number. */
static struct { char ip[INET6_ADDRSTRLEN]; dev_t dev; ino_t ino; } trusted[FD_SETSIZE];

int auth_proxy_mode(void)
{
    const char *value = getenv("DBZC_PROXY_PROTOCOL");
    return value && !strcmp(value, "1");
}

int auth_proxy_loopback(int fd, int peer)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    memset(&address, 0, sizeof(address));
    if ((peer ? getpeername(fd, (struct sockaddr *)&address, &length)
              : getsockname(fd, (struct sockaddr *)&address, &length)) != 0)
        return 0;
    return length == sizeof(address) && address.sin_family == AF_INET
        && (ntohl(address.sin_addr.s_addr) >> 24) == 127;
}

void auth_proxy_forget(int fd)
{
    if (fd >= 0 && fd < FD_SETSIZE) memset(&trusted[fd], 0, sizeof(trusted[fd]));
}

int auth_proxy_register(int fd, const char *ip)
{
    unsigned char address[16];
    char canonical[INET6_ADDRSTRLEN];
    struct stat st;
    int family;
    auth_proxy_forget(fd);
    if (!auth_proxy_mode() || fd < 0 || fd >= FD_SETSIZE || !ip
        || !auth_proxy_loopback(fd, 0) || !auth_proxy_loopback(fd, 1)
        || fstat(fd, &st)) return 0;
    family = strchr(ip, ':') ? AF_INET6 : AF_INET;
    if (inet_pton(family, ip, address) != 1
        || !inet_ntop(family, address, canonical, sizeof(canonical))
        || strcmp(ip, canonical)) return 0;
    strcpy(trusted[fd].ip, canonical);
    trusted[fd].dev = st.st_dev;
    trusted[fd].ino = st.st_ino;
    return 1;
}

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
    if (auth_proxy_mode()) {
        struct stat st;
        if (fd < 0 || fd >= FD_SETSIZE || !trusted[fd].ip[0]
            || !auth_proxy_loopback(fd, 0) || !auth_proxy_loopback(fd, 1)
            || fstat(fd, &st) || st.st_dev != trusted[fd].dev
            || st.st_ino != trusted[fd].ino) {
            auth_proxy_forget(fd);
            return 0;
        }
        return auth_rate_allow(trusted[fd].ip, account, (uint64_t)now.tv_sec);
    }
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
