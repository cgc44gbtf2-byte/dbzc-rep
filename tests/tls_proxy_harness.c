#include "proxy_protocol.h"
#include "auth_rate.h"
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define closesocket close
/* INIT_SOCKET */
int control;
#define bug(...) fprintf(stderr, __VA_ARGS__)
/* RECOVERY_GUARD */

static int delivered, delivered_fd;
static char delivered_ip[46];
static unsigned short delivered_port;
static void ready(int fd, const char *ip, unsigned short port)
{
    ++delivered;
    delivered_fd = fd;
    strcpy(delivered_ip, ip);
    delivered_port = port;
}

static int parse(const char *text)
{
    struct proxy_header h = {0};
    int status = 0;
    size_t i;
    h.started = 100;
    for (i = 0; text[i] && status == 0; ++i)
        status = proxy_header_byte(&h, text[i], 101);
    return status;
}

static int connect_pair(int listener, int *client)
{
    struct sockaddr_in a;
    socklen_t length = sizeof(a);
    int fd;
    assert(!getsockname(listener, (struct sockaddr *)&a, &length));
    *client = socket(AF_INET, SOCK_STREAM, 0);
    assert(*client >= 0);
    assert(!connect(*client, (struct sockaddr *)&a, length));
    fd = accept(listener, NULL, NULL);
    assert(fd >= 0);
    return fd;
}

static void rejected_bind(const char *value)
{
    pid_t child = fork();
    int status;
    assert(child >= 0);
    if (!child) {
        setenv("DBZC_BIND_ADDRESS", value, 1);
        close(init_socket(0));
        _exit(0);
    }
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
}

int main(void)
{
    int listener, fd, client, original, other, maximum, sv[2];
    fd_set input;
    struct sockaddr_in a;
    socklen_t length = sizeof(a);
    struct proxy_header h = {0};
    const char *valid = "PROXY TCP4 198.51.100.7 127.0.0.1 32100 8112\r\n";
    char bytes[32];
    size_t i;
    assert(parse(valid) == 1);
    assert(parse("PROXY TCP6 2001:db8::1 ::1 123 8112\r\n") == 1);
    const char *bad[] = {
        "PROXY UNKNOWN\r\n", "PROXY TCP4 127.1 127.0.0.1 1 2\r\n",
        "PROXY TCP4 127.0.0.1 127.0.0.1 01 2\r\n",
        "PROXY TCP4 127.0.0.1 127.0.0.1 -1 2\r\n",
        "PROXY TCP4 127.0.0.1 127.0.0.1 65536 2\r\n",
        "PROXY TCP4 127.0.0.1 127.0.0.1 1 2 extra\r\n",
        "PROXY  TCP4 127.0.0.1 127.0.0.1 1 2\r\n",
        "PROXY TCP6 2001:DB8::1 ::1 1 2\r\n",
        "PROXY TCP4 1.2.3.4 ::1 1 2\r\n",
        "PROXY TCP4 1.2.3.4 127.0.0.1 1 2\n",
        "PROXY TCP4 1.2.3.4 127.0.0.1 1 2\rX"
    };
    for (i = 0; i < sizeof(bad) / sizeof(*bad); ++i) assert(parse(bad[i]) == -1);
    h.started = 10;
    assert(proxy_header_byte(&h, 'P', 5009) == 0);
    assert(proxy_header_byte(&h, 'R', 5010) == -1);
    memset(&h, 0, sizeof(h));
    for (i = 0; i < PROXY_HEADER_MAX - 1; ++i)
        assert(proxy_header_byte(&h, 'X', 1) == 0);
    assert(proxy_header_byte(&h, 'X', 1) == -1);
    puts("PASS strict canonical parser, malformed/oversize and non-extending deadline");

    unsetenv("DBZC_BIND_ADDRESS");
    setenv("DBZC_PROXY_PROTOCOL", "1", 1);
    listener = init_socket(0);
    control = listener;
    assert(proxy_listener_valid(listener));
    assert(!getsockname(listener, (struct sockaddr *)&a, &length));
    assert(ntohl(a.sin_addr.s_addr) == 0x7f000001);
    assert(fcntl(listener, F_GETFL) & O_NONBLOCK);
    rejected_bind("0.0.0.0");
    rejected_bind("localhost");
    rejected_bind("192.0.2.1");
    puts("PASS production init_socket loopback default and unsafe bind rejection");

    fd = connect_pair(listener, &client);
    assert(!auth_attempt(fd, "unvalidated"));
    assert(proxy_pending_add(listener, fd));
    FD_ZERO(&input); maximum = listener;
    proxy_pending_select(&input, &maximum);
    assert(FD_ISSET(fd, &input) && maximum >= fd);
    for (i = 0; i < strlen(valid) - 1; ++i) {
        assert(write(client, valid + i, 1) == 1);
        proxy_pending_poll(ready);
        assert(!delivered);
        assert(!auth_attempt(fd, "unvalidated"));
    }
    assert(write(client, "\nlook\r\n", 7) == 7);
    proxy_pending_poll(ready);
    assert(delivered == 1 && delivered_fd == fd);
    assert(!strcmp(delivered_ip, "198.51.100.7") && delivered_port == 32100);
    assert(!(fcntl(fd, F_GETFD) & FD_CLOEXEC));
    assert(read(fd, bytes, sizeof(bytes)) == 6 && !memcmp(bytes, "look\r\n", 6));
    assert(auth_attempt(fd, "validated"));
    auth_proxy_forget(fd);
    assert(!auth_attempt(fd, "forgotten"));
    assert(!auth_proxy_register(fd, "hostname.example"));
    {
        pid_t child = fork();
        int status;
        assert(child >= 0);
        if (!child) { recover_identity(fd, "legacy.hostname"); _exit(0); }
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    }
    recover_identity(fd, "2001:db8::1");
    assert(auth_attempt(fd, "recovered"));
    puts("PASS fragmented header gate, coalesced game input, canonical recovery identity");

    original = fd;
    close(fd); close(client); /* intentionally omit unregister: inode must protect reuse */
    fd = connect_pair(listener, &client);
    if (fd != original) { assert(dup2(fd, original) == original); close(fd); fd = original; }
    assert(!auth_attempt(fd, "reused"));
    assert(auth_proxy_register(fd, "203.0.113.2"));
    assert(auth_attempt(fd, "distinct"));
    close(fd); close(client);
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
    assert(!proxy_pending_add(listener, sv[0]));
    assert(!auth_proxy_register(sv[0], "198.51.100.1"));
    close(sv[0]); close(sv[1]);
    puts("PASS closed fd reuse and non-IPv4/non-loopback trust boundary");

    fd = connect_pair(listener, &client);
    assert(proxy_pending_add(listener, fd));
    assert(write(client, "PROXY UNKNOWN\r\n", 15) == 15);
    proxy_pending_poll(ready);
    assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    close(client);
    fd = connect_pair(listener, &client);
    assert(proxy_pending_add(listener, fd));
    struct timespec pause = {1, 100000000};
    for (i = 0; i < 5; ++i) {
        assert(write(client, "P", 1) == 1);
        nanosleep(&pause, NULL);
        proxy_pending_poll(ready);
    }
    assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    assert(delivered == 1);
    close(client);
    puts("PASS real nonblocking lifecycle malformed and total five-second trickle timeout");

    int clients[PROXY_PENDING_MAX], sockets[PROXY_PENDING_MAX];
    for (i = 0; i < PROXY_PENDING_MAX; ++i) {
        sockets[i] = connect_pair(listener, &clients[i]);
        assert(proxy_pending_add(listener, sockets[i]));
    }
    other = connect_pair(listener, &client);
    assert(!proxy_pending_add(listener, other));
    close(other); close(client);
    proxy_pending_close();
    for (i = 0; i < PROXY_PENDING_MAX; ++i) {
        assert(fcntl(sockets[i], F_GETFD) == -1 && errno == EBADF);
        close(clients[i]);
    }
    close(listener);
    setenv("DBZC_PROXY_PROTOCOL", "0", 1);
    setenv("DBZC_BIND_ADDRESS", "0.0.0.0", 1);
    listener = init_socket(0);
    assert(!proxy_listener_valid(listener));
    close(listener);
    puts("PASS bounded pending capacity/cleanup and explicit plaintext wildcard opt-in");
    return 0;
}
