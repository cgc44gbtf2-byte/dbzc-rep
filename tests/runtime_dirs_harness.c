#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_STRING_LENGTH 4096
#define GOD_DIR "../gods/"
#define PLAYER_DIR "../player/"
#define COPYOVER_DIR "../copyover/"
#define FALSE 0
typedef int bool;
typedef short sh_int;
typedef struct clan_data {
    struct clan_data *next;
    int members;
} CLAN_DATA;
static CLAN_DATA test_clan, *first_clan = &test_clan;
static struct { int pfile_count; } sysdata;
static void *first_wiz, *last_wiz;
static int deleted, num_pfiles, bugs, errors, reads, saves, reached;
static int entries_read, dirs_closed, incomplete, completed, clear_calls;
static time_t now_time;
static char log_buf[MAX_STRING_LENGTH], last_bug[MAX_STRING_LENGTH];
static const char *deny_dir;

static void bug(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(last_bug, sizeof(last_bug), format, args);
    va_end(args);
    assert(strstr(last_bug, "preflight.sh"));
    bugs++;
}
static void test_perror(const char *path)
{
    assert(path && (errno == ENOENT || errno == ENOTDIR || errno == EACCES));
    errors++;
}
static DIR *test_opendir(const char *path)
{
    if (deny_dir && !strcmp(path, deny_dir)) {
        errno = EACCES;
        return NULL;
    }
    return opendir(path);
}
static struct dirent *test_readdir(DIR *dp)
{
    assert(dp != NULL);
    entries_read++;
    return readdir(dp);
}
static int test_closedir(DIR *dp)
{
    assert(dp != NULL);
    dirs_closed++;
    return closedir(dp);
}
static void log_string(const char *s)
{
    if (strstr(s, "incomplete:")) incomplete++;
    if (strstr(s, "completed.")) completed++;
}
static void clear_race_count(void) { clear_calls++; }
static void clear_sex_count(void) { clear_calls++; }
static int test_nice(int n) { return n; }
static int str_cmp(const char *a, const char *b) { return strcmp(a, b); }
static void read_pfile(char *dir, char *name, bool count)
{
    assert(!count && !strcmp(dir, "../player/a"));
    assert(!strcmp(name, "Alice") || !strcmp(name, "Another"));
    reads++;
}
static void save_clan(CLAN_DATA *clan) { assert(clan == &test_clan); saves++; }
/* Production takes SYSTEM_DATA by value; the small harness needs only its count. */
#define save_sysdata(data) ((void)(data).pfile_count)
#define opendir test_opendir
#define readdir test_readdir
#define closedir test_closedir
#define perror test_perror
#define nice test_nice

/* PRODUCTION_CODE */

static void touch_file(const char *path)
{
    FILE *fp = fopen(path, "w");
    assert(fp);
    assert(fputs("retained\n", fp) >= 0);
    assert(fclose(fp) == 0);
}

static void reset_counts(void)
{
    bugs = errors = reads = saves = reached = 0;
    entries_read = dirs_closed = incomplete = completed = clear_calls = 0;
}

int main(void)
{
    int i;
    char path[80];
    assert(mkdir("area", 0700) == 0);
    assert(chdir("area") == 0);
    /* A failed wiz scan must neither clear the current list nor its output. */
    touch_file("WIZLIST");
    first_wiz = last_wiz = &test_clan;
    guard_make_wizlist();
    guard_load_buildlist();
    assert(bugs == 2 && errors == 2 && reached == 0 && entries_read == 0);
    assert(first_wiz == &test_clan && last_wiz == &test_clan);
    assert(access("WIZLIST", F_OK) == 0);

    /* ENOTDIR and EACCES take exactly the same safe return path. */
    touch_file("../gods");
    reset_counts();
    guard_make_wizlist();
    guard_load_buildlist();
    assert(bugs == 2 && errors == 2 && entries_read == 0);
    assert(unlink("../gods") == 0);
    assert(mkdir("../gods", 0700) == 0);
    deny_dir = GOD_DIR;
    reset_counts();
    guard_make_wizlist();
    guard_load_buildlist();
    assert(bugs == 2 && errors == 2 && entries_read == 0);
    deny_dir = NULL;
    reset_counts();
    guard_make_wizlist();
    guard_load_buildlist();
    assert(bugs == 0 && reached == 2 && dirs_closed == 2 && entries_read == 2);

    /* The optional copyover object directory must also guard the real scan. */
    reset_counts();
    guard_load_obj_files();
    assert(bugs == 1 && errors == 1 && reached == 0 && entries_read == 0);
    assert(strstr(last_bug, COPYOVER_DIR));
    assert(strstr(last_bug, "optional world object directory"));
    assert(strstr(last_bug, "skipping object restore"));
    touch_file("../copyover");
    reset_counts();
    guard_load_obj_files();
    assert(bugs == 1 && errors == 1 && reached == 0 && entries_read == 0);
    assert(unlink("../copyover") == 0);
    assert(mkdir("../copyover", 0700) == 0);
    deny_dir = COPYOVER_DIR;
    reset_counts();
    guard_load_obj_files();
    assert(bugs == 1 && errors == 1 && reached == 0 && entries_read == 0);
    deny_dir = NULL;
    reset_counts();
    guard_load_obj_files();
    assert(bugs == 0 && reached == 1 && dirs_closed == 1 && entries_read == 1);

    /* Execute the entire production scan, not a copied guard. */
    reset_counts();
    pfile_scan(1);
    assert(bugs == 26 && errors == 26 && entries_read == 0);
    assert(incomplete == 1 && completed == 0 && clear_calls == 2);
    assert(num_pfiles == 0 && sysdata.pfile_count == 0);
    assert(mkdir("../player", 0700) == 0);
    assert(mkdir("../player/a", 0700) == 0);
    touch_file("../player/a/Alice");
    touch_file("../player/a/Another");
    touch_file("../player/a/.hidden");
    touch_file("../player/a/CVS");
    touch_file("../player/b");
    reset_counts();
    pfile_scan(0);
    assert(bugs == 25 && errors == 25 && dirs_closed == 1);
    assert(reads == 2 && saves == 1 && num_pfiles == 2);
    assert(incomplete == 1 && completed == 0);
    assert(unlink("../player/b") == 0);
    for (i = 1; i < 26; i++) {
        snprintf(path, sizeof(path), "../player/%c", 'a' + i);
        assert(mkdir(path, 0700) == 0);
    }
    deny_dir = "../player/c";
    reset_counts();
    pfile_scan(1);
    assert(bugs == 1 && errors == 1 && dirs_closed == 25);
    assert(incomplete == 1 && completed == 0 && num_pfiles == 2);
    deny_dir = NULL;
    reset_counts();
    pfile_scan(1);
    assert(bugs == 0 && errors == 0 && dirs_closed == 26 && reads == 0);
    assert(incomplete == 0 && completed == 1 && num_pfiles == 2);
    puts("runtime directory production guards: passed");
    return 0;
}
