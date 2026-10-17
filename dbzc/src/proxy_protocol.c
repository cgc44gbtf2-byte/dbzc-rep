#include "proxy_protocol.h"
#include "auth_rate.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static struct { int active, fd; struct proxy_header header; } pending[PROXY_PENDING_MAX];

static int monotonic_ms(uint64_t *now)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    *now = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return 1;
}

static int numeric(int family, const char *text)
{
    unsigned char address[16];
    char canonical[INET6_ADDRSTRLEN];
    return inet_pton(family, text, address) == 1
        && inet_ntop(family, address, canonical, sizeof(canonical))
        && !strcmp(text, canonical);
}

static int port_number(const char *s, unsigned short *out)
{
    unsigned value = 0;
    size_t n = strlen(s), i;
    if (!n || n > 5 || (n > 1 && *s == '0')) return 0;
    for (i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return 0;
        value = value * 10 + s[i] - '0';
    }
    if (value > 65535) return 0;
    *out = value;
    return 1;
}

int proxy_header_byte(struct proxy_header *h, unsigned char byte, uint64_t now)
{
    char *fields[6], *p;
    int i, family;
    unsigned short destination;
    if (now < h->started || now - h->started >= 5000
        || h->used >= PROXY_HEADER_MAX) return -1;
    if (byte != '\r' && byte != '\n' && (byte < 32 || byte > 126)) return -1;
    if (h->used && h->bytes[h->used - 1] == '\r' && byte != '\n') return -1;
    h->bytes[h->used++] = byte;
    if (byte != '\n') return h->used == PROXY_HEADER_MAX ? -1 : 0;
    if (h->used < 2 || h->bytes[h->used - 2] != '\r') return -1;
    h->bytes[h->used - 2] = 0;
    p = h->bytes;
    for (i = 0; i < 6; ++i) {
        fields[i] = p;
        p = strchr(p, ' ');
        if (i < 5) {
            if (!p || p == fields[i]) return -1;
            *p++ = 0;
        } else if (p || !*fields[i]) return -1;
    }
    if (strcmp(fields[0], "PROXY")) return -1;
    if (!strcmp(fields[1], "TCP4")) family = AF_INET;
    else if (!strcmp(fields[1], "TCP6")) family = AF_INET6;
    else return -1;
    if (!numeric(family, fields[2]) || !numeric(family, fields[3])
        || !port_number(fields[4], &h->port)
        || !port_number(fields[5], &destination)) return -1;
    strcpy(h->ip, fields[2]);
    return 1;
}

int proxy_listener_valid(int fd)
{
    int accepting = 0;
    socklen_t size = sizeof(accepting);
    return fd >= 0 && fd < FD_SETSIZE && auth_proxy_loopback(fd, 0)
        && !getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &size)
        && accepting;
}

int proxy_pending_add(int listener, int fd)
{
    unsigned i;
    uint64_t now;
    int flags;
    auth_proxy_forget(fd);
    if (!auth_proxy_mode() || !proxy_listener_valid(listener)
        || fd < 0 || fd >= FD_SETSIZE || !auth_proxy_loopback(fd, 0)
        || !auth_proxy_loopback(fd, 1) || !monotonic_ms(&now)) return 0;
    flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0
        || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return 0;
    for (i = 0; i < PROXY_PENDING_MAX; ++i) if (!pending[i].active) {
        memset(&pending[i], 0, sizeof(pending[i]));
        pending[i].active = 1;
        pending[i].fd = fd;
        pending[i].header.started = now;
        return 1;
    }
    return 0;
}

void proxy_pending_select(fd_set *input, int *maximum)
{
    unsigned i;
    for (i = 0; i < PROXY_PENDING_MAX; ++i) if (pending[i].active) {
        FD_SET(pending[i].fd, input);
        if (pending[i].fd > *maximum) *maximum = pending[i].fd;
    }
}

void proxy_pending_poll(void (*ready)(int, const char *, unsigned short))
{
    unsigned i;
    for (i = 0; i < PROXY_PENDING_MAX; ++i) if (pending[i].active) {
        int status = 0, fd = pending[i].fd;
        uint64_t now;
        struct proxy_header *h = &pending[i].header;
        /* At most 107 nonblocking reads per socket per poll. No input beyond
         * CRLF is consumed, even if the first command arrived in the same packet. */
        while (!status) {
            unsigned char byte;
            ssize_t n;
            if (!monotonic_ms(&now) || now < h->started || now - h->started >= 5000) {
                status = -1;
                break;
            }
            n = recv(fd, &byte, 1, MSG_DONTWAIT);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
            if (n != 1) { status = -1; break; }
            status = proxy_header_byte(h, byte, now);
        }
        if (!status) continue;
        pending[i].active = 0;
        if (status == 1 && auth_proxy_register(fd, h->ip)
            && fcntl(fd, F_SETFD, 0) == 0) {
            ready(fd, h->ip, h->port);
        } else {
            auth_proxy_forget(fd);
            close(fd);
        }
    }
}

void proxy_pending_close(void)
{
    unsigned i;
    for (i = 0; i < PROXY_PENDING_MAX; ++i) if (pending[i].active) {
        auth_proxy_forget(pending[i].fd);
        close(pending[i].fd);
        pending[i].active = 0;
    }
}
