/* Dependency stubs; all format/recovery/socket implementations are extracted. */
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "copyover_record.h"

#define ROOM_VNUM_TEMPLE 3001
#define CON_GET_NAME -99
#define CON_COPYOVER_RECOVER -70
#define TRUE 1
#define CREATE(p,t,n) do { (p)=calloc((n),sizeof(t)); assert(p); } while (0)
#define DISPOSE(p) do { free(p); (p)=NULL; } while (0)
static int strings_freed, closed_count;
#define STRALLOC(p) strdup(p)
#define STRFREE(p) do { ++strings_freed; DISPOSE(p); } while (0)
#define FCLOSE(fp) do { fclose(fp); (fp)=NULL; } while (0)
#define LINK(d,f,l,n,p) do { (d)->p=(l); (d)->n=NULL; \
    if (l) (l)->n=(d); else (f)=(d); (l)=(d); } while (0)
typedef struct descriptor DESCRIPTOR_DATA;
typedef struct room { int vnum; } ROOM;
typedef struct character { char *name; ROOM *in_room, *was_in_room; } CHAR_DATA;
struct descriptor {
    DESCRIPTOR_DATA *next, *prev;
    CHAR_DATA *character;
    char *host, *client, *outbuf, *pagebuf;
    int descriptor, connected, outsize, idle, lines, scrlen, newstate, prevcolor, port;
};
static DESCRIPTOR_DATA *first_descriptor, *last_descriptor;
static void bug(const char *s, ...) { (void)s; }
static int test_close(int fd) { (void)fd; ++closed_count; return 0; }
#define closesocket test_close
/* PRODUCTION_FUNCTIONS */

static FILE *fixture(const void *text, size_t size)
{
    FILE *fp = tmpfile();
    assert(fp && fwrite(text, 1, size, fp) == size);
    rewind(fp);
    return fp;
}
static void reject(const void *text, size_t size)
{
    FILE *fp = fixture(text, size);
    COPYOVER_RECORD *records = (COPYOVER_RECORD *)1;
    size_t count = 123;
    int maxp = -1;
    assert(!copyover_read_records(fp, &records, &count, &maxp));
    assert(!records && !count);
    fclose(fp);
}
static void one(const char *text, const char *client, int emergency, int max)
{
    FILE *fp = fixture(text, strlen(text));
    COPYOVER_RECORD *records;
    size_t count;
    int maxp;
    assert(copyover_read_records(fp, &records, &count, &maxp));
    assert(count == 1 && maxp == max);
    assert(records[0].desc == 7 && !strcmp(records[0].name, "Hero"));
    assert(!strcmp(records[0].host, "host.example"));
    assert(!strcmp(records[0].client, client));
    assert(records[0].legacy_emergency == emergency);
    free(records); fclose(fp);
}
static void wait_ok(pid_t pid, int status)
{
    int result;
    assert(waitpid(pid, &result, 0) == pid);
    assert(WIFEXITED(result) && WEXITSTATUS(result) == status);
}

int main(void)
{
    alarm(20);
    ROOM room = {123}, oldroom = {456};
    CHAR_DATA ch = {"Hero", &room, &oldroom};
    DESCRIPTOR_DATA source = {.character=&ch, .descriptor=7, .port=5555, .idle=23,
                              .host="host.example", .client="Mud Client 2\tANSI\nv3 "};
    FILE *normal = tmpfile(), *emergency = tmpfile();
    assert(normal && emergency && actual_normal(normal, &source));
    assert(actual_emergency(emergency, &source));
    assert(fputs("MAXP 99\n-1\n", normal) >= 0);
    assert(fputs("MAXP 99\n-1\n", emergency) >= 0);
    rewind(normal); rewind(emergency);
    int a, b;
    do { a = fgetc(normal); b = fgetc(emergency); assert(a == b); } while (a != EOF);
    rewind(normal);
    COPYOVER_RECORD *records;
    size_t count;
    int maxp;
    assert(copyover_read_records(normal, &records, &count, &maxp));
    assert(count == 1 && maxp == 99 && records[0].desc == 7);
    assert(records[0].room == 123 && records[0].lastroom == 456);
    assert(records[0].port == 5555 && records[0].idle == 23);
    assert(!strcmp(records[0].client, source.client));
    DESCRIPTOR_DATA *restored = actual_restore(&records[0]);
    assert(first_descriptor == restored && last_descriptor == restored);
    assert(restored->client != records[0].client);
    assert(!strcmp(restored->client, source.client));
    free(records);
    assert(!strcmp(restored->client, source.client)); /* Independently owned. */
    STRFREE(restored->client); restored->client = STRALLOC("replacement");
    first_descriptor = last_descriptor = NULL;
    free_desc(restored);
    assert(strings_freed == 3 && closed_count == 1);
    fclose(normal); fclose(emergency);
    puts("PASS extracted normal/emergency writers roundtrip identically; restored client owned/freed");

    normal = tmpfile();
    source.host = source.client = NULL;
    ch.in_room = ch.was_in_room = NULL;
    assert(actual_normal(normal, &source));
    fputs("-1\n", normal); rewind(normal);
    assert(copyover_read_records(normal, &records, &count, &maxp));
    assert(!strcmp(records[0].host, "unknown"));
    assert(!strcmp(records[0].client, "Unidentified"));
    assert(records[0].room == ROOM_VNUM_TEMPLE && records[0].lastroom == ROOM_VNUM_TEMPLE);
    free(records); fclose(normal);
    normal = tmpfile();
    source.client = "(null)"; /* A literal new-format value is not a NULL pointer. */
    assert(actual_normal(normal, &source));
    fputs("-1\n", normal); rewind(normal);
    assert(copyover_read_records(normal, &records, &count, &maxp));
    assert(!strcmp(records[0].client, "(null)"));
    free(records); fclose(normal);
    one("7 0 123 456 5555 23 Hero host.example MUD Client with spaces\n"
        "0 0 0 0 0 99 maxp maxp\n-1\n", "MUD Client with spaces", 0, 99);
    one("7 Hero host.example\n-1\n", "Unidentified", 1, 0);
    one("7 0 123 456 5555 23 Hero host.example (null)\r\n-1\r\n",
        "Unidentified", 0, 0);
    puts("PASS nullable defaults, legacy normal/maxp, spaced clients and legacy emergency");

    const char *bad[] = {
        "", "7 Hero host.example\n", "-1 extra\n", "MAXP junk\n-1\n",
        "MAXP 1\nMAXP 2\n-1\n",
        "7 0 1 2 3 4 Hero host.example\n-1\n", "7 Hero\n-1\n",
        "7 Hero host extra\n-1\n", "7 ../Hero host\n-1\n",
        "999999999999999999999999 Hero host\n-1\n", "-2 Hero host\n-1\n",
        "0 Hero host\n-1\n", "7 1 1 2 3 4 Hero host c\n-1\n",
        "7 0 -1 2 3 4 Hero host c\n-1\n", "7 0 1 2 65536 4 Hero host c\n-1\n",
        "7 0 1 2 3 32768 Hero host c\n-1\n",
        "7 Hero host.example\n7 Other host\n-1\n",
        "-1\n7 Hero host.example\n",
        "DBZC2 7 1 2 3 4 4865726f 686f7374 00\n-1\n",
        "DBZC2 7 1 2 3 4 4865726f 686f7374 abc\n-1\n",
        "DBZC2 7 1 2 3 4 4865726f 686f7374 zz\n-1\n",
        "DBZC2 7 1 2 3 4 4865726f 686f7374 41 extra\n-1\n",
        "DBZC3 7 1 2 3 4 Hero host c\n-1\n"
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) reject(bad[i], strlen(bad[i]));
    char boundary[100];
    snprintf(boundary, sizeof(boundary), "%d Hero host\n-1\n", FD_SETSIZE);
    reject(boundary, strlen(boundary));
    const char nul[] = "7 Hero\0 host\n-1\n";
    reject(nul, sizeof(nul)-1);
    char oversized[COPYOVER_LINE_SIZE + 20];
    memset(oversized, 'x', sizeof(oversized));
    reject(oversized, sizeof(oversized));
    const char valid[] = "7 0 1 2 3 4 Hero host client\n-1\n";
    for (size_t i = 0; i < sizeof(valid)-3; ++i) reject(valid, i);
    puts("PASS malformed/truncated/overlong/NUL/duplicate/overflow/out-of-range records fail closed");

    normal = tmpfile();
    char too_long[COPYOVER_CLIENT_SIZE + 1];
    memset(too_long, 'x', sizeof(too_long)-1); too_long[sizeof(too_long)-1] = 0;
    assert(!copyover_write_record(normal, 7, 1, 2, 3, 4, "Hero", "host", too_long));
    assert(!copyover_write_record(normal, FD_SETSIZE, 1, 2, 3, 4, "Hero", NULL, NULL));
    assert(!copyover_write_record(normal, 7, 1, 2, 3, 4, "../Hero", NULL, NULL));
    fclose(normal);
    FILE *full = fopen("/dev/full", "w");
    assert(full && setvbuf(full, NULL, _IONBF, 0) == 0);
    assert(!copyover_write_record(full, 7, 1, 2, 3, 4, "Hero", NULL, NULL));
    fclose(full);
    puts("PASS writer validates bounds/names and reports real write errors");

    const size_t sizes[] = {COPYOVER_NAME_SIZE, COPYOVER_HOST_SIZE, COPYOVER_CLIENT_SIZE};
    for (int field = 0; field < 3; ++field) {
        char value[COPYOVER_HOST_SIZE + 1], hex[2 * (COPYOVER_HOST_SIZE + 1)];
        memset(value, 'a', sizes[field]); value[sizes[field]] = 0;
        copyover_hex(hex, value);
        normal = tmpfile(); assert(normal);
        fprintf(normal, "DBZC2 7 1 2 3 4 %s %s %s\n-1\n",
                field == 0 ? hex : "4865726f",
                field == 1 ? hex : "686f7374",
                field == 2 ? hex : "41");
        rewind(normal);
        assert(!copyover_read_records(normal, &records, &count, &maxp) && !records);
        fclose(normal);
    }
    normal = tmpfile(); assert(normal);
    fputs("-1\n", normal);
    for (int i = 0; i < FD_SETSIZE + 3; ++i) fputc('\n', normal);
    rewind(normal);
    assert(!copyover_read_records(normal, &records, &count, &maxp) && !records);
    fclose(normal);
    puts("PASS decoded-field capacities and total line-count bounds enforced");

    unsigned int seed = 0x13579;
    for (int iteration = 0; iteration < 10000; ++iteration) {
        char fuzz[200];
        for (size_t i = 0; i < sizeof(fuzz)-1; ++i) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            fuzz[i] = 1 + seed % 127;
        }
        fuzz[sizeof(fuzz)-1] = 0;
        COPYOVER_RECORD parsed;
        copyover_parse_line(fuzz, &parsed, &maxp);
    }
    puts("PASS deterministic malformed-line fuzz runs within sanitizer bounds");

    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    FILE *broken = fixture("7 Hero\n", 7);
    fflush(NULL);
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) {
        close(pair[1]);
        actual_read_or_exit(broken); /* Actual recovery error guard exits(1). */
        exit(42);
    }
    close(pair[0]); wait_ok(pid, 1);
    char byte;
    assert(recv(pair[1], &byte, 1, 0) == 0);
    close(pair[1]); fclose(broken);
    puts("PASS actual fatal recovery guard exits and releases inherited client fd");

    int kept[2], orphan[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, kept) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, orphan) == 0);
    fflush(NULL);
    pid = fork(); assert(pid >= 0);
    if (!pid) {
        close(kept[1]); close(orphan[1]);
        int listener = socket(AF_INET, SOCK_STREAM, 0);
        assert(listener >= 0);
        struct sockaddr_in addr = {.sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
        assert(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0);
        assert(listen(listener, 1) == 0);
        FILE *regular = tmpfile(); assert(regular);
        int high = fcntl(orphan[0], F_DUPFD, FD_SETSIZE + 7);
        assert(high >= FD_SETSIZE);
        assert(copyover_prepare_socket(kept[0]));
        assert(fcntl(kept[0], F_GETFL) & O_NONBLOCK);
        assert(!copyover_prepare_socket(listener));
        assert(!copyover_prepare_socket(fileno(regular)));
        unsigned char keep[FD_SETSIZE] = {0};
        keep[kept[0]] = 1;
        copyover_close_orphans(keep);
        assert(fcntl(orphan[0], F_GETFL) < 0 && errno == EBADF);
        assert(fcntl(high, F_GETFL) < 0 && errno == EBADF);
        assert(fcntl(listener, F_GETFL) >= 0 && fcntl(fileno(regular), F_GETFL) >= 0);
        assert(send(kept[0], "ok", 2, 0) == 2);
        close(kept[0]); close(listener); fclose(regular);
        exit(0);
    }
    close(kept[0]); close(orphan[0]);
    wait_ok(pid, 0);
    char message[2];
    assert(recv(kept[1], message, 2, MSG_WAITALL) == 2 && !memcmp(message, "ok", 2));
    assert(recv(orphan[1], &byte, 1, 0) == 0);
    close(kept[1]); close(orphan[1]);
    puts("PASS real inherited sockets validate/nonblock; orphan/high fds close; listeners/files remain");
    alarm(0);
    return 0;
}
