/* Test dependency stubs only; implementations come from the real comm.c. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TRUE true
#define FALSE false
#define MAX_INPUT_LENGTH 1024
#define MAX_STRING_LENGTH 4096
#define CON_PLAYING 0
#define CON_EDITING 2
#define CON_NOTE_TO 3
#define CON_NOTE_FINISH 8
#define CON_COPYOVER_RECOVER 9
#define LOG_COMM 0
#define AT_ACTION 0
#define TO_CANSEE 0
#define PLR_BOOST 1
#define PLR_BLANK 2
#define PLR_PROMPT 4
#define PLR_TELNET_GA 8
#define PLR_ANSI 16
#define ANSI_LBLUE "<blue>"
#define LOWER(c) tolower((unsigned char)(c))
#define UMIN(a,b) ((a) < (b) ? (a) : (b))
#define UMAX(a,b) ((a) > (b) ? (a) : (b))
#define xIS_SET(a,b) ((a) & (b))
#define xREMOVE_BIT(a,b) ((a) &= ~(b))
#define DISPOSE(p) do { free(p); (p) = NULL; } while (0)
#define STRFREE(p) DISPOSE(p)
#define RECREATE(p,t,n) do { (p) = realloc((p), sizeof(t)*(n)); assert(p); } while (0)
#define UNLINK(d,f,l,n,p) do { \
    if ((d)->p) (d)->p->n = (d)->n; else (f) = (d)->n; \
    if ((d)->n) (d)->n->p = (d)->p; else (l) = (d)->p; } while (0)
typedef struct descriptor DESCRIPTOR_DATA;
typedef struct character {
    char *name;
    struct { char *filename; int pagerlen; bool copyover; } *pcdata;
    DESCRIPTOR_DATA *desc;
    int level, powerup, act;
} CHAR_DATA;
struct descriptor {
    DESCRIPTOR_DATA *next, *prev, *snoop_by, *fused_with;
    CHAR_DATA *character, *original;
    int descriptor, connected, idle, outtop;
    unsigned long outsize, pagesize;
    char *host, *outbuf, *pagebuf, *client, *pagepoint;
    int pagetop, pagecmd, pagecolor;
    bool fcommand;
    int out_pending;
    bool out_error, out_prompted;
    unsigned long out_epoch;
    int out_budget, out_calls;
};
static DESCRIPTOR_DATA *first_descriptor, *last_descriptor, *d_next;
static int num_descriptors, maxdesc;
static struct { int log_level; } sysdata;
static char log_buf[4096];
bool mud_down;
static char go_ahead_str[] = "GA";
/* OUTPUT_CONSTANTS */
void free_desc(DESCRIPTOR_DATA *);
void close_socket(DESCRIPTOR_DATA *, bool);
void write_to_buffer(DESCRIPTOR_DATA *, const char *, int);
bool flush_buffer(DESCRIPTOR_DATA *, bool);
static bool queue_output(DESCRIPTOR_DATA *, const char *, int);
static bool prepare_output(DESCRIPTOR_DATA *);
static bool drain_output(DESCRIPTOR_DATA *);
static int send_output(int, const char *, int);
static void finish_output_close(DESCRIPTOR_DATA *, bool);

static void bug(const char *s, ...) { (void)s; }
static void log_string_plus(const char *s, int a, int b)
{ (void)s; (void)a; (void)b; }
static void do_return(CHAR_DATA *ch, const char *s) { (void)ch; (void)s; }
static void act(int a, const char *s, CHAR_DATA *ch, void *o, void *v, int b)
{ (void)a; (void)s; (void)ch; (void)o; (void)v; (void)b; }
static void do_drop(CHAR_DATA *ch, const char *s) { (void)ch; (void)s; }
static void do_defuse(CHAR_DATA *ch, const char *s) { (void)ch; (void)s; }
static void check_boost(void) {}
static void free_char(CHAR_DATA *ch) { free(ch); }
static const char *color_str(int color, CHAR_DATA *ch)
{ (void)color; (void)ch; return "<reset>"; }
static int prompt_count;
static void display_prompt(DESCRIPTOR_DATA *d)
{ ++prompt_count; write_to_buffer(d, "<P>", 3); }

static bool real_io;
static int outcomes[32], outcome_count, outcome_next, send_calls, closed;
static int blocked_fd = -1;
static char captured[4][150000];
static size_t capture_len[4];
static int test_close(int fd)
{
    ++closed;
    return real_io ? close(fd) : 0;
}
static ssize_t test_send(int fd, const void *txt, size_t length, int flags)
{
    ++send_calls;
    if (real_io) return send(fd, txt, length, flags);
    int n = outcome_next < outcome_count ? outcomes[outcome_next++] : (int)length;
    if (fd == blocked_fd) n = -EAGAIN;
    if (n < 0) { errno = -n; return -1; }
    if ((size_t)n > length) n = length;
    assert(fd >= 0);
    int slot = (unsigned)fd % 4;
    assert(capture_len[slot] + n < sizeof(captured[slot]));
    memcpy(captured[slot] + capture_len[slot], txt, n);
    capture_len[slot] += n;
    captured[slot][capture_len[slot]] = '\0';
    return n;
}
#define send test_send
#define closesocket test_close
/* REAL_FUNCTIONS */
#undef send

static char recovery_path[] = "/tmp/output-recovery-XXXXXX";
#define COPYOVER_FILE recovery_path
#define CH(d) ((d)->original ? (d)->original : (d)->character)
static int irreversible_calls, admin_messages;
static void log_string(const char *s) { (void)s; }
static void send_to_char(const char *s, CHAR_DATA *ch)
{
    (void)ch;
    assert(strstr(s, "Retry after clients drain"));
    ++admin_messages;
}
static void test_copyover_transition(CHAR_DATA *ch)
{
    DESCRIPTOR_DATA *d;
    /* COPYOVER_GUARD */
    ++irreversible_calls; /* Stand-in for the first resource close and exec. */
}
static void recovery_file(void)
{
    strcpy(recovery_path, "/tmp/output-recovery-XXXXXX");
    int fd = mkstemp(recovery_path);
    assert(fd >= 0);
    assert(close(fd) == 0);
}

static DESCRIPTOR_DATA *make_desc(int fd)
{
    DESCRIPTOR_DATA *d = calloc(1, sizeof(*d));
    assert(d);
    d->descriptor = fd;
    d->connected = CON_PLAYING;
    d->fcommand = true;
    d->outsize = 2000;
    d->outbuf = calloc(1, d->outsize);
    assert(d->outbuf);
    d->prev = last_descriptor;
    if (last_descriptor) last_descriptor->next = d;
    else first_descriptor = d;
    last_descriptor = d;
    ++num_descriptors;
    return d;
}
static void reset(void)
{
    assert(!first_descriptor && !last_descriptor && !closing_output);
    assert(num_descriptors == 0);
    ++output_epoch;
    real_io = false;
    blocked_fd = -1;
    outcome_count = outcome_next = send_calls = closed = prompt_count = 0;
    memset(capture_len, 0, sizeof(capture_len));
    memset(captured, 0, sizeof(captured));
}
static void script(const int *values, int n)
{
    memcpy(outcomes, values, n * sizeof(*values));
    outcome_count = n;
    outcome_next = 0;
}
static void tick(DESCRIPTOR_DATA *d)
{
    ++output_epoch;
    d->fcommand = d->out_prompted = false;
}
static void expect(int fd, const char *s)
{
    assert(capture_len[fd % 4] == strlen(s));
    assert(!memcmp(captured[fd % 4], s, strlen(s)));
}

int main(void)
{
    alarm(10); /* A stalled nonblocking peer must never wedge the test runner. */
    reset();
    DESCRIPTOR_DATA *d = make_desc(1);
    const int partial[] = {2, -EINTR, -EAGAIN, -EAGAIN};
    script(partial, 4);
    write_to_buffer(d, "abcdef", 6);
    assert(flush_buffer(d, false) && d->outtop == 4 && d->out_pending == 4);
    assert(!memcmp(d->outbuf, "cdef", 4));
    assert(write_to_descriptor(1, "XYZ", 3) && d->outtop == 7);
    write_to_buffer(d, "!", 1);
    assert(d->out_pending == 7 && d->outtop == 8);
    tick(d);
    assert(flush_buffer(d, false) && !d->outtop && !d->out_pending);
    expect(1, "abcdefXYZ!");
    close_socket(d, true);
    puts("PASS partial/EINTR/EAGAIN suffix retention and staged/direct ordering");

    reset();
    d = make_desc(1);
    DESCRIPTOR_DATA *snoop = make_desc(2), *fused = make_desc(3);
    CHAR_DATA ch = {.name = "Hero", .act = PLR_BLANK|PLR_PROMPT|PLR_TELNET_GA};
    d->character = &ch;
    d->snoop_by = snoop; d->fused_with = fused;
    blocked_fd = 1;
    write_to_buffer(d, "msg", 3);
    assert(flush_buffer(d, true));
    int queued = d->outtop, mirrored = snoop->outtop;
    for (int i = 0; i < 5; ++i) {
        tick(d);
        assert(flush_buffer(d, true));
        assert(d->outtop == queued && snoop->outtop == mirrored);
    }
    assert(prompt_count == 1 && fused->outtop == mirrored);
    blocked_fd = -1; tick(d);
    assert(flush_buffer(d, true) && flush_buffer(snoop, false) && flush_buffer(fused, false));
    expect(1, "msg\n\r<P>GA");
    expect(2, "Hero% msg\n\r<P>GA");
    expect(3, "Hero% msg\n\r<P>GA");
    d->character = NULL;
    close_socket(d, true); close_socket(snoop, true); close_socket(fused, true);
    puts("PASS stalled retries append one prompt and one snoop/fused copy");

    reset();
    d = make_desc(1);
    DESCRIPTOR_DATA *other = make_desc(2);
    char large[40000]; memset(large, 'x', sizeof(large));
    assert(write_to_descriptor(1, large, sizeof(large)));
    assert(capture_len[1] == OUTPUT_BYTES && send_calls == OUTPUT_CALLS);
    assert(flush_buffer(d, false) && send_calls == OUTPUT_CALLS);
    assert(!copyover_output_ready() && send_calls == OUTPUT_CALLS);
    assert(write_to_descriptor(2, "fair", 4));
    expect(2, "fair");
    close_socket(d, true); close_socket(other, true);
    puts("PASS shared per-pulse byte/syscall budgets and other-client fairness");

    reset();
    d = make_desc(1);
    const int interrupted[] = {-EINTR, -EINTR, -EINTR, -EINTR};
    script(interrupted, 4);
    assert(write_to_descriptor(1, "retry", 5));
    assert(send_calls == OUTPUT_CALLS && d->outtop == 5);
    tick(d); assert(flush_buffer(d, false)); expect(1, "retry");
    close_socket(d, true);
    for (int error = 0; error < 2; ++error) {
        reset(); d = make_desc(1);
        const int failures[] = {2, error ? -EPIPE : 0};
        script(failures, 2);
        assert(!write_to_descriptor(1, "fatal", 5));
        assert(d->out_error && d->outtop == 3 && d->out_pending == 3 && send_calls == 2);
        assert(!flush_buffer(d, true) && send_calls == 2);
        close_socket(d, true); assert(closed == 1);
    }
    puts("PASS bounded repeated EINTR; zero/fatal send latch without retry/duplication");

    reset();
    d = make_desc(1);
    char *full = malloc(OUTPUT_LIMIT); assert(full);
    memset(full, 'b', OUTPUT_LIMIT);
    assert(queue_output(d, full, OUTPUT_LIMIT));
    assert(d->outsize <= OUTPUT_LIMIT + 1);
    write_to_buffer(d, "overflow", 8);
    assert(d->out_error && d->outtop == OUTPUT_LIMIT && !closed);
    write_to_buffer(d, "still alive", 11);
    assert(d->outtop == OUTPUT_LIMIT && !closed);
    close_socket(d, false); assert(closed == 1 && !closing_output);
    free(full);
    puts("PASS bounded queue overflow is atomic and never frees inside output caller");

    reset();
    d = make_desc(1);
    CHAR_DATA closing_ch = {.name = "Closing", .desc = d};
    typeof(*closing_ch.pcdata) closing_pc = {.filename = "Closing"};
    closing_ch.pcdata = &closing_pc;
    d->character = &closing_ch;
    blocked_fd = 1;
    assert(write_to_descriptor(1, "bye", 3));
    close_socket(d, false);
    assert(!first_descriptor && num_descriptors == 0 && closing_output && !closed);
    assert(!closing_ch.desc && !closing_output->character);
    blocked_fd = -1; ++output_epoch; drain_closed_output();
    assert(!closing_output && closed == 1); expect(1, "bye");
    reset();
    d = make_desc(1); blocked_fd = 1;
    assert(write_to_descriptor(1, "deadline", 8));
    close_socket(d, false);
    for (int i = 0; i < OUTPUT_CLOSE_PULSES; ++i) {
        ++output_epoch; drain_closed_output();
    }
    assert(!closing_output && closed == 1 && send_calls <= 2 + OUTPUT_CLOSE_PULSES);
    puts("PASS logical close unlinks immediately; transport drains or meets deadline");

    reset();
    d = make_desc(1);
    CHAR_DATA pager = {.act = PLR_ANSI};
    typeof(*pager.pcdata) pc = {.filename = "Hero", .pagerlen = 5};
    pager.pcdata = &pc; d->character = &pager;
    d->pagebuf = strdup("1\n\r2\n\r3\n\r4\n\r5\n\r");
    d->pagepoint = d->pagebuf; d->pagecmd = 0;
    blocked_fd = 1;
    assert(pager_output(d));
    assert(d->pagecmd == -1 && !strcmp(d->pagepoint, "5\n\r"));
    queued = d->outtop;
    for (int i = 0; i < 3; ++i) {
        tick(d); assert(pager_output(d) && d->outtop == queued);
    }
    blocked_fd = -1; tick(d); assert(pager_output(d));
    d->pagecmd = 'c'; assert(pager_output(d));
    assert(!d->pagepoint && !d->pagebuf);
    expect(1, "1\n\r2\n\r3\n\r4\n\r<blue>"
              "(C)ontinue, (N)on-stop, (R)efresh, (B)ack, (Q)uit: [C] <reset>5\n\r");
    d->character = NULL; close_socket(d, true);
    puts("PASS pager advances on queue acceptance and retries drain without redisplay");

    reset();
    d = make_desc(1);
    pager.act = 0; d->character = &pager;
    d->pagebuf = strdup("start\n\rmiddle\n\r");
    d->pagepoint = d->pagebuf; d->pagecmd = 'b';
    assert(pager_output(d)); /* Back at the beginning must not read before pagebuf. */
    assert(!d->pagebuf && !d->pagepoint);
    expect(1, "start\n\rmiddle\n\r");
    d->pagebuf = strdup("cancel\n\r");
    d->pagepoint = d->pagebuf; d->pagecmd = 'q';
    assert(pager_output(d) && !d->pagebuf && !d->pagepoint);
    expect(1, "start\n\rmiddle\n\r");
    d->character = NULL; close_socket(d, true);
    puts("PASS pager back-at-start and quit preserve ownership/bounds");

    reset();
    const int bare[] = {2, -EAGAIN};
    script(bare, 2);
    assert(!write_to_descriptor(1, "bare", 4));
    expect(1, "ba");
    assert(send_calls == 2);
    outcome_count = outcome_next = 0;
    assert(write_to_descriptor(1, "", 0));
    puts("PASS documented bare-fd partial-failure and empty-probe contract");

    reset();
    assert(copyover_output_ready() && !send_calls);
    d = make_desc(1);
    assert(copyover_output_ready() && !send_calls && !prompt_count);
    blocked_fd = 1;
    write_to_buffer(d, "pending", 7);
    assert(!copyover_output_ready() && d->outtop == 7 && !d->out_error);
    int calls = send_calls;
    assert(!copyover_output_ready() && send_calls <= calls + OUTPUT_CALLS);
    blocked_fd = -1; tick(d);
    assert(copyover_output_ready() && !d->outtop);
    expect(1, "pending");
    d->out_error = true;
    assert(!copyover_output_ready());
    close_socket(d, true);
    puts("PASS copyover gate empty, active pending, successful drain and active error");

    reset();
    d = make_desc(1); blocked_fd = 1;
    assert(write_to_descriptor(1, "closing", 7));
    close_socket(d, false);
    int remaining_pulses = closing_output->idle;
    assert(!copyover_output_ready() && closing_output->idle == remaining_pulses);
    assert(!closed && closing_output->outtop == 7);
    blocked_fd = -1; ++output_epoch;
    assert(copyover_output_ready() && !closing_output && closed == 1);
    expect(1, "closing");
    reset();
    d = make_desc(1); blocked_fd = 1;
    assert(write_to_descriptor(1, "failed", 6));
    close_socket(d, false);
    closing_output->out_error = true;
    assert(!copyover_output_ready() && !closed && closing_output);
    ++output_epoch; drain_closed_output();
    assert(!closing_output && closed == 1);
    puts("PASS copyover gate never forces/ages detached pending or failed queues");

    reset();
    snoop = make_desc(2); d = make_desc(1);
    d->snoop_by = snoop; /* Observer is traversed before its new snoop data exists. */
    write_to_buffer(d, "snoop", 5);
    assert(!copyover_output_ready() && snoop->outtop);
    ++output_epoch;
    assert(copyover_output_ready());
    expect(1, "snoop"); expect(2, "% snoop");
    close_socket(d, true); close_socket(snoop, true);
    puts("PASS copyover gate rechecks earlier snoopers after preparation");

    reset();
    recovery_file();
    test_copyover_transition(NULL);
    assert(irreversible_calls == 1 && admin_messages == 0);
    assert(access(COPYOVER_FILE, F_OK) == 0);
    assert(unlink(COPYOVER_FILE) == 0);
    d = make_desc(1); blocked_fd = 1;
    CHAR_DATA gate_ch = {.name = "Admin", .desc = d};
    typeof(*gate_ch.pcdata) gate_pc = {.filename = "Admin", .copyover = true};
    gate_ch.pcdata = &gate_pc; d->character = &gate_ch;
    write_to_buffer(d, "stalled", 7);
    recovery_file();
    test_copyover_transition(&gate_ch);
    assert(irreversible_calls == 1 && admin_messages == 1);
    assert(access(COPYOVER_FILE, F_OK) < 0 && errno == ENOENT);
    assert(!gate_pc.copyover && !d->out_error && d->outtop == 7 && !closed);
    d->character = NULL;
    close_socket(d, true);
    puts("PASS real copyover abort guard removes stale recovery file and notifies admin");

    reset();
    int pair[2], sndbuf = 4096;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(fcntl(pair[0], F_SETFL, O_NONBLOCK) == 0);
    assert(fcntl(pair[1], F_SETFL, O_NONBLOCK) == 0);
    assert(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0);
    real_io = true;
    d = make_desc(pair[0]);
    size_t total = OUTPUT_LIMIT - 64;
    char *payload = malloc(total + 6), *received = malloc(total + 6);
    assert(payload && received);
    for (size_t i = 0; i < total; ++i) payload[i] = 'a' + i % 26;
    memcpy(payload + total, "+tail+", 6);
    assert(write_to_descriptor(pair[0], payload, total));
    assert(d->outtop > 0); /* real peer is stalled */
    assert(write_to_descriptor(pair[0], "+tail+", 6));
    for (int i = 0; i < 20; ++i) {
        tick(d); int before = send_calls;
        assert(flush_buffer(d, false));
        assert(send_calls - before <= OUTPUT_CALLS);
    }
    size_t got = 0;
    for (int i = 0; i < 1000 && (d->outtop || got < total + 6); ++i) {
        ssize_t n;
        while (got < total + 6 &&
               (n = recv(pair[1], received + got, total + 6 - got, 0)) > 0)
            got += n;
        tick(d); assert(flush_buffer(d, false));
    }
    assert(got == total + 6 && !d->outtop && !d->out_pending);
    assert(!memcmp(payload, received, got));
    close_socket(d, false); close(pair[1]);
    free(payload); free(received);
    assert(closed == 1);
    puts("PASS real stalled socketpair returns promptly and resumes exact byte order");
    alarm(0);
    return 0;
}
