#!/usr/bin/env python3
"""Run extracted production rank paths with ASan, UBSan and leak detection.

No server, player files or full build are needed. Uses the real hash allocator,
string reader, ownership macros, commands and rank-specific load/free/mset code.
CC and RANK_TEST_CFLAGS may override the compiler and compilation flags.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "dbzc/src"


def block(text, start):
    depth = 0
    for token in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
                             r"'(?:\\.|[^'\\])*'|[{}]", text[start:], re.S):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start:start + token.end()]
    raise RuntimeError("Unterminated production block")


def function(text, name):
    match = re.search(r"^(?:void|char\s*\*)\s*" + name +
                      r"\([^;]*?\)\s*\{", text, re.M)
    if not match:
        raise RuntimeError(f"Cannot find production function {name}")
    return block(text, match.start())


def one(pattern, text):
    matches = re.findall(pattern, text, re.M)
    if len(matches) != 1:
        raise RuntimeError(f"Expected one production match: {pattern!r}")
    return matches[0]


save, db, wiz, build, misc, mud, hashing = (
    (SRC / name).read_text() for name in
    ("save.c", "db.c", "act_wiz.c", "build.c", "dbzmisc.c", "mud.h", "hashstr.c"))

# Extract the original allocator declarations/table and alloc/link/free routines.
# The remaining hash debugging functions are unrelated and have legacy warnings.
allocator = hashing[hashing.index("#define STR_HASH_SIZE"):
                    hashing.index("void show_hash( int count )\n{")]
macros = mud[mud.index("#ifdef HASHSTR\n#define STRALLOC"):
             mud.index("/* double-linked list handling macros")]
initial = one(r"^\s*(ch->pcdata->rank\s*=\s*STRALLOC\( \"\" \);)$",
              save[:save.index("void fread_char(")])
fallback = one(r"(\s*if \(!ch->pcdata->rank\)\s*\n"
               r"\s*ch->pcdata->rank\s*=\s*STRALLOC\( \"\" \);)", save)
load = block(save, save.index('if ( !strcmp( word, "Rank" ) )'))
cleanup = one(r"^\s*(STRFREE\( ch->pcdata->rank \);)$", db)
mortalize = function(wiz, "do_mortalize")
demotion = block(mortalize, mortalize.index(
    "{", mortalize.index("victim->move     = victim->max_move;")))
race = block(build, build.index("if (!IS_NPC(victim))",
                               build.index("victim->race = value;")))
mset = block(build, build.index('if ( !str_cmp( arg2, "rank" ) )'))

pieces = [
    allocator, macros, function(db, "fread_string"),
    function(wiz, "do_rank"), function(misc, "do_clear"),
    "void rank_initial(CHAR_DATA *ch) { " + initial + " }",
    "void rank_fallback(CHAR_DATA *ch) { " + fallback + " }",
    "void rank_free(CHAR_DATA *ch) { " + cleanup + " }",
    "int rank_read(CHAR_DATA *ch, FILE *fp, char *word) {\n"
    "int fMatch = FALSE; do {\n" + load + "\n} while (0); return fMatch;\n}",
    "void rank_demote(CHAR_DATA *victim) " + demotion,
    "void rank_race(CHAR_DATA *victim) {\n" + race + "\n}",
    "void rank_mset(CHAR_DATA *ch, CHAR_DATA *victim, char *argument) {\n"
    'char *arg2 = "rank";\n' + mset + "\n}",
]

# Guard against reintroducing a non-interned assignment anywhere in production.
for path in SRC.glob("*.c"):
    text = path.read_text()
    assert not re.search(r"pcdata->rank\s*=\s*(?:str_dup\b|\")", text), path
    assert not re.search(r"DISPOSE\s*\(\s*\w+->pcdata->rank\s*\)", text), path

harness = (ROOT / "tests/rank_ownership_harness.c").read_text()
generated = harness.replace("/* PRODUCTION_CODE */", "\n\n".join(pieces))
flags = shlex.split(os.environ.get(
    "RANK_TEST_CFLAGS", "-std=gnu89 -O1 -g -Wall -Wextra -Werror "
    "-Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer"))
with tempfile.TemporaryDirectory(prefix="rank-ownership-") as tmp:
    cfile, exe = Path(tmp) / "rank.c", Path(tmp) / "rank"
    cfile.write_text(generated)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                   flags + [str(cfile), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30,
                   env={**os.environ,
                        "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
