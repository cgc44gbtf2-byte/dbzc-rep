#define _GNU_SOURCE
#include "password_auth.h"
#include <assert.h>
#include <crypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "auth integration assertion failed at line %d\n", __LINE__); \
    abort(); } } while (0)

#define MAX_INPUT_LENGTH 4024
#define MAX_STRING_LENGTH 8192
#define TRUE 1
#define FALSE 0
#define BERR 2
#define CON_PLAYING 10
#define CON_EDITING 11
#define IS_NPC(ch) 0
#define DISPOSE(p) free(p)
#define str_dup strdup
typedef struct descriptor DESCRIPTOR_DATA;
typedef struct {
    char *pwd;
    char *filename;
} PC_DATA;
typedef struct {
    PC_DATA *pcdata;
    DESCRIPTOR_DATA *desc;
    char *name;
    int complete;
} CHAR_DATA;
struct descriptor {
    int descriptor, connected;
    char *host;
    CHAR_DATA *character;
};
static CHAR_DATA partial, full;
static PC_DATA partial_pc, full_pc;
static DESCRIPTOR_DATA descriptor;
static int saves, operations, budget, route, closed, warning;
static char persisted[PASSWORD_HASH_SIZE];
static int auth_attempt(int fd, const char *account)
{
    assert(fd == 7 && account && *account);
    ++operations;
    return operations <= budget;
}
static void send_to_char(const char *message, CHAR_DATA *ch)
{
    (void)ch;
    if (strstr(message, "eight bytes") || strstr(message, "128-byte limit"))
        ++warning;
}
static void send_to_desc_color(const char *message, DESCRIPTOR_DATA *d)
{
    send_to_char(message, d->character);
}
static void log_string(const char *message) { (void)message; }
static void save_char_obj(CHAR_DATA *ch)
{
    assert(ch->complete); /* Saving a preloaded/partially-created player is fatal. */
    ++saves;
    snprintf(persisted, sizeof(persisted), "%s", ch->pcdata->pwd);
}
static void free_char(CHAR_DATA *ch)
{
    assert(ch == &partial && !ch->complete);
}
static void attach(DESCRIPTOR_DATA *d)
{
    d->character = &full;
    full.desc = d;
    d->connected = CON_PLAYING;
}
static int check_playing(DESCRIPTOR_DATA *d, const char *name, int kick)
{
    (void)name; assert(kick);
    if (route == 1) { attach(d); return TRUE; }
    if (route == 3) return BERR;
    return FALSE;
}
static int check_reconnect(DESCRIPTOR_DATA *d, const char *name, int reconnect)
{
    (void)name; assert(reconnect);
    if (route == 2) { attach(d); return TRUE; }
    if (route == 4) return BERR;
    return FALSE;
}
static void close_socket(DESCRIPTOR_DATA *d, int force)
{
    (void)d; (void)force; ++closed;
}
static void check_multi(DESCRIPTOR_DATA *d, const char *name)
{
    (void)d; (void)name;
}
static int load_char_obj(DESCRIPTOR_DATA *d, const char *name, int preload, int copyover)
{
    (void)name; assert(!preload && !copyover && !saves);
    attach(d); return TRUE;
}

/* PRODUCTION_FUNCTIONS */

static void reset(const char *stored)
{
    free(partial_pc.pwd); free(full_pc.pwd);
    partial_pc.pwd = strdup(stored); full_pc.pwd = strdup(stored);
    partial_pc.filename = full_pc.filename = "Fixture";
    descriptor = (DESCRIPTOR_DATA){7, 0, "numeric-fixture", &partial};
    partial = (CHAR_DATA){&partial_pc, &descriptor, "Fixture", 0};
    full = (CHAR_DATA){&full_pc, NULL, "Fixture", 1};
    saves = operations = closed = warning = 0; budget = 20; persisted[0] = 0;
}
int main(void)
{
    char stored[PASSWORD_HASH_SIZE], modern[PASSWORD_HASH_SIZE], command[8192];
    char *result = crypt("Short", "$6$rounds=1000$fixture$");
    int i;
    assert(result && result[0] != '*');
    snprintf(stored, sizeof(stored), "%s", result);
    for (i = 0; i < 3; ++i) {
        reset(stored); route = i;
        login_fixture(&descriptor, "Short");
        assert(operations == 2 && saves == 1 && !closed);
        assert(password_verify("Short", persisted) == 1);
        assert(!strcmp(full.pcdata->pwd, persisted));
        snprintf(modern, sizeof(modern), "%s", persisted);
        reset(modern); route = i;
        login_fixture(&descriptor, "Short");
        assert(operations == 1 && !saves && !closed);
    }
    reset(stored); route = 0; budget = 1;
    login_fixture(&descriptor, "Short");
    assert(operations == 2 && !saves && !strcmp(full_pc.pwd, stored));
    reset(stored); budget = 0;
    login_fixture(&descriptor, "Short");
    assert(operations == 1 && !saves && descriptor.character == &partial);
    reset(stored);
    login_fixture(&descriptor, "wrong fixture");
    assert(operations == 1 && !saves && closed == 1);
    for (i = 3; i <= 4; ++i) {
        reset(stored); route = i;
        login_fixture(&descriptor, "Short");
        assert(!saves); /* Neither failed reconnect path saves the preload. */
    }
    result = crypt("EightChrArbitrarySuffix", "ab");
    assert(result && result[0] != '*');
    snprintf(stored, sizeof(stored), "%s", result);
    reset(stored); route = 0;
    login_fixture(&descriptor, "EightChrDifferentSuffix");
    assert(warning && !saves && operations == 1);
    /* Explicit change requires exact mixed-case current/new/confirmation parsing. */
    reset(modern); attach(&descriptor);
    snprintf(command, sizeof(command), "Short \"MiXeD New Fixture\" \"MiXeD New Fixture\"");
    do_password(&full, command);
    assert(saves == 1 && operations == 2);
    assert(password_verify("MiXeD New Fixture", persisted) == 1);
    reset(modern); attach(&descriptor);
    do_password(&full, "short \"MiXeD New Fixture\" \"MiXeD New Fixture\"");
    assert(!saves && operations == 1);
    reset(modern); attach(&descriptor);
    do_password(&full, "Short \"MiXeD New Fixture\" \"mismatch fixture\"");
    assert(!saves && operations == 1);
    reset(modern); attach(&descriptor);
    do_password(&full, "Short \"MiXeD New Fixture\" \"MiXeD New Fixture\" extra");
    assert(!saves && !operations);
    reset(modern); attach(&descriptor); budget = 1;
    do_password(&full, "Short \"MiXeD New Fixture\" \"MiXeD New Fixture\"");
    assert(!saves && operations == 2 && !strcmp(full_pc.pwd, modern));
    memset(command, 'x', sizeof(command) - 1); command[sizeof(command)-1] = 0;
    reset(modern); attach(&descriptor);
    do_password(&full, command);
    assert(!saves && !operations);
    free(partial_pc.pwd); free(full_pc.pwd);
    puts("auth production flow regressions passed");
    return 0;
}
