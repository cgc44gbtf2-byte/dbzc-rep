/* Stubs only: tested implementations are extracted from comm.c by the runner. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define TRUE true
#define FALSE false
#define MAX_STRING_LENGTH 4096
#define CON_GET_NAME 1
#define CON_PLAYING 0
#define CON_EDITING 2
#define CON_NOTE_TO 3
#define CON_NOTE_FINISH 8
#define CON_COPYOVER_RECOVER 9
#define LOG_COMM 0
#define CHANNEL_MONITOR 0
#define LEVEL_IMMORTAL 50
#define AT_ACTION 0
#define TO_CANSEE 0
#define PLR_BOOST 1
#define UMAX(a,b) ((a) > (b) ? (a) : (b))
#define UPPER(c) toupper((unsigned char)(c))
#define CREATE(p,t,n) do { (p) = calloc((n), sizeof(t)); assert(p); } while (0)
#define DISPOSE(p) do { free(p); (p) = NULL; } while (0)
#define STRALLOC(p) strdup(p)
static int strings_freed;
#define STRFREE(p) do { assert(p); ++strings_freed; DISPOSE(p); } while (0)
#define LINK(d,f,l,n,p) do { (d)->p = (l); (d)->n = NULL; \
    if (l) (l)->n = (d); else (f) = (d); (l) = (d); } while (0)
#define UNLINK(d,f,l,n,p) do { \
    if ((d)->p) (d)->p->n = (d)->n; else (f) = (d)->n; \
    if ((d)->n) (d)->n->p = (d)->p; else (l) = (d)->p; } while (0)
#define xIS_SET(bits,bit) ((bits) & (bit))
#define xREMOVE_BIT(bits,bit) ((bits) &= ~(bit))

typedef struct descriptor DESCRIPTOR_DATA;
typedef struct character {
    char *name;
    struct { char *filename; } *pcdata;
    DESCRIPTOR_DATA *desc;
    int level, powerup;
} CHAR_DATA;
struct descriptor {
    DESCRIPTOR_DATA *next, *prev, *snoop_by, *fused_with;
    CHAR_DATA *character, *original;
    int descriptor, connected, ansi, outsize, idle, lines, scrlen, port;
    int newstate, prevcolor, outtop;
    char *host, *outbuf, *pagebuf, *client;
};
typedef struct {
    int maxplayers, alltimemax, log_level, NO_NAME_RESOLVING, DENY_NEW_PLAYERS;
    char *time_of_max;
} SYSTEM_DATA;
static SYSTEM_DATA sysdata;
static DESCRIPTOR_DATA *first_descriptor, *last_descriptor, *d_next;
static int num_descriptors, maxdesc, newdesc;
static fd_set in_set, out_set, exc_set;
static char *alarm_section;
static char log_buf[MAX_STRING_LENGTH * 2];
static time_t current_time;
char *help_greeting = "greeting", *help_greeting2 = "greeting2";
char *help_greeting3 = "greeting3", *help_greeting4 = "greeting4";
static int alarm_value, accepted, accept_calls, nonblock_calls, closed_fd;
static int close_calls, bad_calls, bad_at, nonblock_fail, ban, messages, polls;
static char *peer_ip = "127.0.0.1";

void free_desc(DESCRIPTOR_DATA *);
/* Transport draining is covered by output tests; lifecycle uses immediate close. */
static void finish_output_close(DESCRIPTOR_DATA *d, bool force)
{ (void)force; free_desc(d); }
void close_socket(DESCRIPTOR_DATA *, bool);
void new_descriptor(int);
static void bug(const char *s, ...) { (void)s; }
static void log_string_plus(const char *s, int a, int b)
{ (void)s; (void)a; (void)b; }
static void set_alarm(int n) { alarm_value = n; }
static bool check_bad_desc(int fd)
{ assert(fd == 3); return ++bad_calls == bad_at; }
/* Exact POSIX prototype makes a wrong accept address-length type a build error. */
static int test_accept(int fd, struct sockaddr *addr, socklen_t *length)
{
    assert(fd == 3);
    assert(*length == sizeof(struct sockaddr_in));
    ++accept_calls;
    if (accepted < 0) { errno = EAGAIN; return -1; }
    struct sockaddr_in *sin = (struct sockaddr_in *)addr;
    sin->sin_family = AF_INET;
    sin->sin_port = htons(1234);
    assert(inet_pton(AF_INET, peer_ip, &sin->sin_addr) == 1);
    return accepted;
}
static int test_fcntl(int fd, int command, int flags)
{
    assert(fd == accepted && command == F_SETFL);
    (void)flags;
    ++nonblock_calls;
    if (nonblock_fail) { errno = EIO; return -1; }
    return 0;
}
static int test_close(int fd)
{ closed_fd = fd; ++close_calls; return 0; }
static int test_select(int nfds, fd_set *in, fd_set *out, fd_set *exc,
                       struct timeval *timeout)
{
    assert(nfds <= FD_SETSIZE);
    assert(FD_ISSET(3, in));
    (void)timeout;
    ++polls;
    FD_ZERO(in); FD_ZERO(out); FD_ZERO(exc);
    return 0;
}
#define accept test_accept
#define fcntl test_fcntl
#define closesocket test_close
#define select test_select
static bool check_total_bans(DESCRIPTOR_DATA *d) { (void)d; return ban; }
static bool flush_buffer(DESCRIPTOR_DATA *d, bool prompt)
{ (void)d; (void)prompt; return true; }
static void write_to_buffer(DESCRIPTOR_DATA *d, const char *s, int n)
{ (void)d; (void)s; (void)n; }
static bool write_to_descriptor(int fd, const char *s, int n)
{ (void)fd; (void)s; (void)n; return true; }
static void send_to_desc_color(const char *s, DESCRIPTOR_DATA *d)
{ assert(d->connected == CON_GET_NAME); (void)s; ++messages; }
static void do_return(CHAR_DATA *ch, const char *s) { (void)ch; (void)s; }
static void act(int a, const char *s, CHAR_DATA *ch, void *o, void *v, int b)
{ (void)a; (void)s; (void)ch; (void)o; (void)v; (void)b; }
static void do_drop(CHAR_DATA *ch, const char *s) { (void)ch; (void)s; }
static void do_defuse(CHAR_DATA *ch, const char *s) { (void)ch; (void)s; }
static void check_boost(void) {}
static void free_char(CHAR_DATA *ch) { free(ch); }
static int number_range(int a, int b) { (void)b; return a; }
static char *str_dup(const char *s) { return strdup(s); }
static int str_cmp(const char *a, const char *b) { return strcasecmp(a, b); }
static char *strlower(char *s)
{ for (char *p = s; *p; ++p) *p = tolower((unsigned char)*p); return s; }
static bool check_parse_name(char *s, bool fresh)
{ (void)fresh; return strcmp(s, "Illegal") != 0; }
static void to_channel(char *s, int a, const char *b, int c)
{ (void)s; (void)a; (void)b; (void)c; }
static void save_sysdata(SYSTEM_DATA s) { (void)s; }

/* EXTRACTED_FUNCTIONS */

static void reset(void)
{
    assert(first_descriptor == NULL && last_descriptor == NULL);
    memset(&sysdata, 0, sizeof(sysdata));
    sysdata.NO_NAME_RESOLVING = 1;
    sysdata.alltimemax = 100;
    num_descriptors = 0;
    d_next = NULL;
    accepted = 10;
    bad_at = -1;
    accept_calls = nonblock_calls = close_calls = bad_calls = 0;
    strings_freed = messages = polls = nonblock_fail = ban = 0;
    alarm_value = -1;
    closed_fd = -1;
    peer_ip = "127.0.0.1";
}

static DESCRIPTOR_DATA *linked(int fd, int state)
{
    DESCRIPTOR_DATA *d = calloc(1, sizeof(*d));
    assert(d);
    d->descriptor = fd;
    d->connected = state;
    d->host = strdup("localhost");
    LINK(d, first_descriptor, last_descriptor, next, prev);
    return d;
}

int main(void)
{
    reset();
    accepted = -1;
    new_descriptor(3);
    assert(accept_calls == 1 && nonblock_calls == 0 && close_calls == 0);
    assert(!first_descriptor && num_descriptors == 0 && alarm_value == 0);
    puts("PASS accept failure stays signed and does not close an unowned fd");

    const int high_fds[] = { FD_SETSIZE, FD_SETSIZE + 7 };
    for (unsigned i = 0; i < sizeof(high_fds)/sizeof(*high_fds); ++i) {
        reset();
        accepted = high_fds[i];
        new_descriptor(3);
        assert(close_calls == 1 && closed_fd == accepted);
        assert(!first_descriptor && nonblock_calls == 0 && alarm_value == 0);
    }
    puts("PASS FD_SETSIZE and larger accepted sockets rejected before setup");

    for (int fail = 1; fail <= 4; ++fail) {
        reset();
        if (fail < 4) bad_at = fail;
        else nonblock_fail = 1;
        new_descriptor(3);
        assert(!first_descriptor && num_descriptors == 0 && alarm_value == 0);
        assert(close_calls == (fail == 1 ? 0 : 1));
        if (close_calls) assert(closed_fd == accepted);
    }
    puts("PASS listener exception and nonblocking setup failure cleanups");

    for (int rejection = 0; rejection < 2; ++rejection) {
        reset();
        if (rejection) ban = 1;
        else peer_ip = "63.78.203.210";
        num_descriptors = 2; /* Other connected users must remain counted. */
        new_descriptor(3);
        assert(!first_descriptor && num_descriptors == 2);
        assert(close_calls == 1 && closed_fd == accepted && alarm_value == 0);
        assert(strings_freed == (rejection ? 2 : 1));
    }
    puts("PASS hard/site bans release client and leave active count unchanged");

    reset();
    accepted = FD_SETSIZE - 1;
    new_descriptor(3);
    assert(first_descriptor && first_descriptor == last_descriptor);
    assert(num_descriptors == 1 && messages == 1 && alarm_value == 0);
    d_next = first_descriptor;
    close_socket(first_descriptor, false);
    assert(!first_descriptor && !last_descriptor && !d_next);
    assert(num_descriptors == 0 && close_calls == 1 && strings_freed == 2);
    puts("PASS highest selectable fd connects; close decrements exactly once");

    reset();
    DESCRIPTOR_DATA *d = linked(12, CON_COPYOVER_RECOVER);
    num_descriptors = 2; /* Failed recovery was not incremented by copyover.c. */
    close_socket(d, true);
    assert(num_descriptors == 2 && strings_freed == 1);
    d = linked(13, CON_PLAYING); /* Successful recovery was counted. */
    close_socket(d, true);
    assert(num_descriptors == 1);
    puts("PASS failed/successful recovery accounting; null client is safe");

    reset();
    linked(FD_SETSIZE, CON_PLAYING);
    linked(14, CON_PLAYING);
    linked(-2, CON_PLAYING);
    num_descriptors = 3;
    accept_new(3);
    assert(close_calls == 2 && num_descriptors == 1 && polls == 1);
    assert(first_descriptor == last_descriptor && first_descriptor->descriptor == 14);
    close_socket(first_descriptor, true);
    accept_new(FD_SETSIZE);
    accept_new(-1);
    assert(polls == 1);
    puts("PASS inherited out-of-range descriptors removed before FD_SET");

    reset();
    d = linked(10, CON_GET_NAME);
    d->client = strdup("detected-client");
    num_descriptors = 1;
    sysdata.DENY_NEW_PLAYERS = true;
    char denied[] = "new";
    test_get_name(d, denied); /* ASan catches any access after actual close. */
    assert(!first_descriptor && num_descriptors == 0 && messages == 1);
    assert(close_calls == 1 && strings_freed == 2);
    puts("PASS denied New branch returns immediately after freeing descriptor");

    reset();
    d = linked(10, CON_GET_NAME);
    num_descriptors = 1;
    char allowed[] = "new", repeated[] = "New", illegal[] = "illegal", empty[] = "";
    test_get_name(d, allowed);
    assert(d->newstate == 1 && messages == 1 && close_calls == 0);
    test_get_name(d, repeated);
    test_get_name(d, illegal);
    assert(d->newstate == 1 && messages == 3 && close_calls == 0);
    test_get_name(d, empty);
    assert(!first_descriptor && num_descriptors == 0 && close_calls == 1);
    puts("PASS allowed/repeated/illegal/empty name paths retain their behavior");
    return 0;
}
