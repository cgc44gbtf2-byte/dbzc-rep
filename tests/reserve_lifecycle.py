#!/usr/bin/env python3
"""Run extracted production reserve/save paths with real FILEs and sanitizers."""
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
    match = re.search(r"^void\s+" + name + r"\([^;]*?\)\s*\{", text, re.M)
    if not match:
        raise RuntimeError(f"Cannot find production function {name}")
    return block(text, match.start())


db, pfiles, mud = ((SRC / name).read_text()
                   for name in ("db.c", "pfiles.c", "mud.h"))
system = re.search(r"^struct\s+system_data\s*\{", mud, re.M)
assert system
declarations = (re.search(r"^#define MAX_NPC_RACE\s+\d+", mud, re.M).group()
                + "\n" + block(mud, system.start()) + ";\n")
production = "\n\n".join(
    [function(db, "save_sysdata"), function(db, "show_file"),
     function(pfiles, "clear_race_count"), function(pfiles, "clear_sex_count")])
harness = (ROOT / "tests/reserve_lifecycle_harness.c").read_text()
flags = shlex.split(os.environ.get(
    "RESERVE_TEST_CFLAGS", "-std=gnu89 -O1 -g -Wall -Wextra -Werror "
    "-fsanitize=address,undefined -fno-omit-frame-pointer"))
with tempfile.TemporaryDirectory(prefix="reserve-lifecycle-") as tmp:
    cfile, exe = Path(tmp) / "reserve.c", Path(tmp) / "reserve"
    cfile.write_text(harness.replace("/* SYSTEM_DECLARATIONS */", declarations)
                    .replace("/* PRODUCTION_CODE */", production))
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                   flags + [str(cfile), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=tmp, check=True, timeout=30,
                   env={**os.environ,
                        "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
