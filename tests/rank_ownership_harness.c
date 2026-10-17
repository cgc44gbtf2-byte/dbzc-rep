/* Driver only: all rank mutation/ownership code is extracted from production. */
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HASHSTR
#define TRUE 1
#define FALSE 0
#define MAX_STRING_LENGTH 4096
#define MSL MAX_STRING_LENGTH
#define AT_IMMORT 0
#define LEVEL_GOD 60
#define IS_NPC(ch) ((ch)->npc)
#define IS_IMMORTAL(ch) ((ch)->level >= 49)
#define str_cmp strcasecmp

typedef struct { char *rank; } PC_DATA;
typedef struct { PC_DATA *pcdata; int npc, race, level; } CHAR_DATA;
static int fBootDb;
static char *npc_race[] = { "human" };

static void bug(const char *format, ...)
{
    (void)format;
    /* A bad STRFREE or malformed test input must fail, not silently continue. */
    abort();
}
static void set_char_color(int color, CHAR_DATA *ch) {}
static void send_to_char(char *text, CHAR_DATA *ch) {}
static int get_trust(CHAR_DATA *ch) { return ch->level; }
static char *capitalize(char *text)
{
    static char buf[MAX_STRING_LENGTH];
    assert(strlen(text) < sizeof(buf));
    strcpy(buf, text);
    buf[0] = toupper((unsigned char)buf[0]);
    return buf;
}
static void smash_tilde(char *text)
{
    for (; *text; ++text)
        if (*text == '~')
            *text = '-';
}

/* PRODUCTION_CODE */

static void empty_table(void)
{
    int i;
    for (i = 0; i < STR_HASH_SIZE; ++i)
        assert(string_hash[i] == NULL);
}

static unsigned int links(char *text)
{
    struct hashstr_data *entry =
        (struct hashstr_data *)(text - sizeof(struct hashstr_data));
    return entry->links;
}

static void read_rank(CHAR_DATA *ch, const char *text)
{
    FILE *fp = tmpfile();
    assert(fp);
    assert(fputs(text, fp) >= 0);
    rewind(fp);
    assert(rank_read(ch, fp, "Rank"));
    assert(fclose(fp) == 0);
}

static void test_load_and_free(void)
{
    PC_DATA a = { NULL }, b = { NULL };
    CHAR_DATA ch = { &a, 0, 0, 10 }, other = { &b, 0, 0, 10 };
    char *saved;

    /* New-player default and existing-player missing Rank fallback. */
    rank_initial(&ch);
    rank_fallback(&other);
    assert(!strcmp(a.rank, ""));
    assert(a.rank == b.rank && links(a.rank) == 2);
    saved = a.rank;
    rank_fallback(&ch);
    assert(a.rank == saved && links(saved) == 2);
    rank_free(&ch);
    rank_free(&other);
    empty_table();

    /* First loaded field may replace NULL, or an allocated default. */
    read_rank(&ch, "Captain~");
    rank_fallback(&ch);
    assert(!strcmp(a.rank, "Captain") && links(a.rank) == 1);
    rank_initial(&other);
    read_rank(&other, "Captain~");
    assert(a.rank == b.rank && links(a.rank) == 2);
    read_rank(&ch, "Captain~");
    assert(a.rank == b.rank && links(a.rank) == 2);
    read_rank(&ch, "Commander~");
    assert(!strcmp(a.rank, "Commander") && links(b.rank) == 1);
    read_rank(&ch, "~");
    assert(!strcmp(a.rank, ""));
    read_rank(&ch, "Captain~");
    assert(a.rank == b.rank && links(a.rank) == 2);
    rank_free(&ch);
    assert(!a.rank && links(b.rank) == 1);
    rank_free(&other);
    rank_free(&other);
    empty_table();
}

static void test_commands(void)
{
    PC_DATA a = { NULL }, b = { NULL };
    CHAR_DATA ch = { &a, 0, 0, 10 }, other = { &b, 0, 0, LEVEL_GOD };
    char expected[MAX_STRING_LENGTH], *saved;
    char title[] = "Keeper";
    int level;

    rank_initial(&ch);
    rank_initial(&other);
    do_rank(&ch, "Champion");
    do_rank(&other, "Champion");
    assert(a.rank == b.rank && links(a.rank) == 2);
    /* Allocation must precede release even for aliased input. */
    do_rank(&ch, a.rank);
    assert(a.rank == b.rank && links(a.rank) == 2);
    do_rank(&ch, a.rank + 1);
    assert(!strcmp(a.rank, "hampion") && links(b.rank) == 1);
    saved = a.rank;
    do_rank(&ch, "");
    do_rank(&ch, NULL);
    assert(a.rank == saved);
    do_rank(&ch, "none");
    sprintf(expected, "&w&W%-12.12s&Y", "Human");
    assert(!strcmp(a.rank, expected));

    /* Exercises the formerly uninitialized buf2/overlapping sprintf path. */
    for (level = 49; level <= 64; ++level)
    {
        ch.level = level;
        do_rank(&ch, "none");
        assert(!strncmp(a.rank, "&w&W&W", 6));
        assert(strlen(a.rank) == 18);
    }
    ch.level = 50;
    do_rank(&ch, "none");
    sprintf(expected, "&w&W%-12.12s&Y", "&WPlayer Tester");
    assert(!strcmp(a.rank, expected));
    ch.level = 65;
    do_rank(&ch, "none");
    sprintf(expected, "&w&W%-12.12s&Y", "");
    assert(!strcmp(a.rank, expected));

    rank_mset(&other, &ch, title);
    assert(!strcmp(a.rank, "Keeper"));
    rank_mset(&other, &ch, a.rank);
    assert(!strcmp(a.rank, "Keeper") && links(a.rank) == 1);
    rank_mset(&other, &ch, a.rank + 1);
    assert(!strcmp(a.rank, "eeper"));
    rank_mset(&other, &ch, "none");
    assert(!strcmp(a.rank, ""));
    rank_mset(&other, &ch, title);
    rank_mset(&other, &ch, "");
    assert(!strcmp(a.rank, ""));
    other.level = 10;
    rank_mset(&other, &ch, title);
    assert(!strcmp(a.rank, ""));

    do_rank(&ch, "Former immortal");
    rank_demote(&ch);
    assert(!strcmp(a.rank, ""));
    rank_demote(&ch);
    assert(!strcmp(a.rank, "") && links(a.rank) == 1);
    do_rank(&ch, "Race title");
    rank_race(&ch);
    assert(!strcmp(a.rank, ""));
    do_rank(&ch, "Clear title");
    do_clear(&ch, "");
    do_clear(&ch, "");
    assert(!strcmp(a.rank, "") && links(a.rank) == 1);

    /* NPC early-outs must not dereference absent pcdata. */
    ch.npc = 1;
    ch.pcdata = NULL;
    do_rank(&ch, title);
    do_clear(&ch, "");
    rank_race(&ch);
    other.level = LEVEL_GOD;
    rank_mset(&other, &ch, title);
    ch.pcdata = &a;
    rank_free(&ch);
    rank_free(&other);
    empty_table();
}

int main(void)
{
    int i;
    for (i = 0; i < 100; ++i)
    {
        test_load_and_free();
        test_commands();
    }
    puts("PASS rank ownership: load/default/fallback/repeated Rank, commands,"
         " aliasing, free, demotion, race, clear (ASan/UBSan/leaks)");
    return 0;
}
