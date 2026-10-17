#!/usr/bin/env python3
"""Extract and test real comm.c output functions; no server/full build needed."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "dbzc/src/comm.c").read_text()


def block(text, start):
    depth = 0
    for token in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
                             r"'(?:\\.|[^'\\])*'|[{}]",
                             text[start:], re.S):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start:start + token.end()]
    raise RuntimeError("Unterminated source block")


def extract(name):
    start = re.search(r"^(?:static )?(?:void|bool|int) " + name +
                      r"\([^;]*?\)\s*\{", source, re.M)
    if not start:
        raise RuntimeError(f"Cannot find {name}")
    return block(source, start.start())


names = ("queue_output", "prepare_output", "send_output", "drain_output",
         "finish_output_close", "drain_closed_output", "flush_buffer",
         "write_to_buffer", "write_to_descriptor", "pager_output",
         "free_desc", "close_socket", "copyover_output_ready")
copyover = (root / "dbzc/src/copyover.c").read_text()
copyover = copyover[copyover.index("void do_copyover("):]
guard_start = copyover.index("    if ( !copyover_output_ready() )")
guard = block(copyover, guard_start)
# Exercise the actual guard below, and check its integration boundary here.
assert copyover.index("save_timers();") < guard_start
assert guard_start + len(guard) < copyover.index("FCLOSE( fpReserve )")
assert guard_start < copyover.index("dlclose(") < copyover.index("execl(")
transition = copyover[guard_start + len(guard):copyover.index("execl(")]
assert not re.search(r"\b(?:write_to_descriptor|write_to_buffer|send_to_char)\s*\(",
                     transition), "Output appended after copyover gate"
constants = source[source.index("#define OUTPUT_LIMIT"):
                   source.index("static bool queue_output")]
harness = (root / "tests/output_queue_harness.c").read_text()
generated = harness.replace("/* OUTPUT_CONSTANTS */", constants).replace(
    "/* REAL_FUNCTIONS */", "\n\n".join(extract(n) for n in names)).replace(
    "/* COPYOVER_GUARD */", guard)
flags = shlex.split(os.environ.get(
    "OUTPUT_TEST_CFLAGS", "-std=gnu11 -O1 -g -Wall -Wextra -Werror "
    "-fsanitize=address,undefined -fno-omit-frame-pointer"))
with tempfile.TemporaryDirectory(prefix="output-test-") as tmp:
    cfile, exe = Path(tmp) / "output.c", Path(tmp) / "output"
    cfile.write_text(generated)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                   flags + [str(cfile), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15,
                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1"})
print("PASS normal copyover guard precedes resource release/exec with no later output")
