#ifndef DBZC_PROXY_PROTOCOL_H
#define DBZC_PROXY_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#define PROXY_HEADER_MAX 107
#define PROXY_PENDING_MAX 128
struct proxy_header {
    char bytes[PROXY_HEADER_MAX + 1];
    size_t used;
    uint64_t started;
    char ip[46];
    unsigned short port;
};
/* Returns -1 rejected, 0 incomplete, 1 complete. Deadline is total elapsed time. */
int proxy_header_byte(struct proxy_header *h, unsigned char byte, uint64_t now);
int proxy_listener_valid(int fd);
int proxy_pending_add(int listener, int fd);
void proxy_pending_select(fd_set *input, int *maximum);
void proxy_pending_poll(void (*ready)(int, const char *, unsigned short));
void proxy_pending_close(void);
#endif
