#!/usr/bin/env python3
"""Run: python3 tests/connection_lifecycle.py (requires Python 3 and a C compiler).

Extracts real comm.c functions; no server, game data or full build is needed.
CC and CONNECTION_TEST_CFLAGS can override the compiler/sanitizer flags.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "dbzc/src/comm.c").read_text()


def function(name):
    start = re.search(r"^(?:void|bool) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    if not start:
        raise RuntimeError(f"Cannot find real function {name}")
    tokens = re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
                         r"'(?:\\.|[^'\\])*'|[{}]", source[start.start():], re.S)
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start.start():start.start() + token.end()]
    raise RuntimeError(f"Unterminated function {name}")


# Full nanny has unrelated game dependencies. Test its actual pre-load name
# branch, with its original returns, instead of reimplementing the denial.
nanny = function("nanny")
name_branch = nanny[nanny.index("    case CON_GET_NAME:"):
                    nanny.index("if ( check_playing(")]
branch = ("void test_get_name(DESCRIPTOR_DATA *d, char *argument) {\n"
          "switch (d->connected) {\n" + name_branch + "\n} }\n")
pieces = [function(n) for n in
          ("free_desc", "close_socket", "new_descriptor", "accept_new")]
harness = (ROOT / "tests/connection_lifecycle_harness.c").read_text()
generated = harness.replace("/* EXTRACTED_FUNCTIONS */", "\n\n".join(pieces) + branch)
flags = shlex.split(os.environ.get(
    "CONNECTION_TEST_CFLAGS",
    "-std=gnu11 -g -O1 -Wall -Wextra -Werror "
    "-fsanitize=address,undefined -fno-omit-frame-pointer"))
with tempfile.TemporaryDirectory(prefix="connection-test-") as tmp:
    cfile, exe = Path(tmp) / "lifecycle.c", Path(tmp) / "lifecycle"
    cfile.write_text(generated)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                   flags + [str(cfile), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True,
                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1"})
