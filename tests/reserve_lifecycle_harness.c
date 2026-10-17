/* Driver only; production functions and SYSTEM_DATA are extracted at runtime. */
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef short sh_int;
typedef unsigned char bool;
typedef struct system_data SYSTEM_DATA;
typedef struct { int unused; } CHAR_DATA;
#define MAX_INPUT_LENGTH 1024
#define MAX_STRING_LENGTH 4096
#define SYSTEM_DIR "./"
#define NULL_FILE reserve_path
static const char *reserve_path = "/dev/null";
static FILE *fpReserve;
/* SYSTEM_DECLARATIONS */
static SYSTEM_DATA sysdata;
static int errors, pages, fatal_errors;

static void bug(const char *format, ...)
{
    assert(strstr(format, "save_sysdata:") || strstr(format, "show_file:"));
    ++errors;
    if (strstr(format, "FATAL:"))
        ++fatal_errors;
}

static void send_to_pager_color(char *text, CHAR_DATA *ch)
{
    assert(ch && text[0]);
    ++pages;
}

/* PRODUCTION_CODE */

static int open_files(void)
{
    DIR *dir = opendir("/proc/self/fd");
    struct dirent *entry;
    int count = 0;
    assert(dir);
    while ((entry = readdir(dir)) != NULL)
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
            ++count;
    assert(closedir(dir) == 0);
    return count;
}

static void check_reserve(void)
{
    assert(fpReserve);
    assert(fcntl(fileno(fpReserve), F_GETFD) >= 0);
    assert(fgetc(fpReserve) == EOF);
    assert(!ferror(fpReserve));
}

static void check_saved(void)
{
    FILE *fp = fopen("sysdata.dat", "r");
    char data[8192];
    size_t bytes;
    assert(fp);
    bytes = fread(data, 1, sizeof(data) - 1, fp);
    data[bytes] = '\0';
    assert(!ferror(fp));
    assert(fclose(fp) == 0);
    assert(strstr(data, "#SYSTEM\nMudName"));
    assert(strstr(data, "Sex             0 0\n"));
    assert(strstr(data, "Race            0 0\n"));
    assert(strstr(data, "End\n\n#END\n"));
}

static void check_fatal_exit(void)
{
    assert(fpReserve == NULL);
    assert(fatal_errors == 1);
}

static void expect_fatal_reopen(int show, char *filename, int start_null)
{
    pid_t child = fork();
    int status;
    assert(child >= 0);
    if (child == 0)
    {
        CHAR_DATA ch = { 0 };
        assert(atexit(check_fatal_exit) == 0);
        if (start_null)
        {
            assert(fclose(fpReserve) == 0);
            fpReserve = NULL;
        }
        reserve_path = "./missing/reserve";
        if (show)
            show_file(&ch, filename);
        else
            save_sysdata(sysdata);
        /* Returning would expose NULL to legacy callers' unconditional close. */
        _exit(99);
    }
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == EXIT_FAILURE);
    assert(fatal_errors == 0);
    check_reserve();
}

int main(void)
{
    int i, baseline;
    CHAR_DATA ch = { 0 };
    FILE *sentinel = tmpfile();
    assert(sentinel);
    sysdata.mud_name = "Reserve regression";
    sysdata.time_of_max = "not recorded";
    fpReserve = fopen(NULL_FILE, "r");
    check_reserve();
    baseline = open_files();

    /* The real cleanup sequence previously closed the same stale FILE twice. */
    for (i = 0; i < 100; ++i)
    {
        sysdata.race_count[0] = 7;
        sysdata.sex_count[0] = 9;
        clear_race_count();
        check_reserve();
        clear_sex_count();
        check_reserve();
        check_saved();
        assert(open_files() == baseline);
    }
    assert(errors == 0);

    /* Real target fopen failure must still restore the reserve. */
    assert(unlink("sysdata.dat") == 0);
    assert(mkdir("sysdata.dat", 0700) == 0);
    clear_race_count();
    clear_sex_count();
    check_reserve();
    assert(errors == 2 && open_files() == baseline);
    /* Both target-open failure and reserve-open failure must terminate cleanly. */
    expect_fatal_reopen(0, NULL, 0);
    assert(rmdir("sysdata.dat") == 0);

    /* Real reopen failure is fatal; a fresh independent operation still works. */
    expect_fatal_reopen(0, NULL, 0);
    save_sysdata(sysdata);
    check_saved();
    expect_fatal_reopen(0, NULL, 1);
    save_sysdata(sysdata);
    check_reserve();
    assert(open_files() == baseline);

    /* Buffered write/close failure must also restore the reserve. */
    assert(unlink("sysdata.dat") == 0);
    assert(symlink("/dev/full", "sysdata.dat") == 0);
    save_sysdata(sysdata);
    check_reserve();
    assert(errors == 3 && open_files() == baseline);
    assert(unlink("sysdata.dat") == 0);
    save_sysdata(sysdata);
    check_saved();

    show_file(&ch, "sysdata.dat");
    assert(pages > 0);
    check_reserve();
    show_file(&ch, "missing-file");
    check_reserve();
    expect_fatal_reopen(1, "sysdata.dat", 0);
    show_file(&ch, "sysdata.dat");
    check_reserve();
    expect_fatal_reopen(1, "missing-file", 0);
    show_file(&ch, "missing-file");
    check_reserve();
    expect_fatal_reopen(1, "missing-file", 1);
    show_file(&ch, "missing-file");
    check_reserve();
    assert(open_files() == baseline);

    /* Reserve maintenance must not close a separate, live FILE. */
    assert(fputs("still open", sentinel) >= 0);
    assert(fflush(sentinel) == 0);
    assert(fclose(sentinel) == 0);
    assert(fclose(fpReserve) == 0);
    fpReserve = NULL;
    assert(unlink("sysdata.dat") == 0);
    puts("reserve lifecycle: repeated saves and real fopen/close failures passed");
    return 0;
}
