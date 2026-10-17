#!/usr/bin/env python3
"""Exercise extracted real copyover readers/writers and recovery ownership."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
copyover = (root / "dbzc/src/copyover.c").read_text()
comm = (root / "dbzc/src/comm.c").read_text()


def balanced(text, start, left="{", right="}"):
    depth = 0
    pattern = (r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
               r"'(?:\\.|[^'\\])*'|[" + re.escape(left + right) + "]")
    for token in re.finditer(pattern, text[start:], re.S):
        if token.group() == left:
            depth += 1
        elif token.group() == right:
            depth -= 1
            if depth == 0:
                return text[start:start + token.end()]
    raise RuntimeError("Unterminated production source block")


def function(text, name):
    start = re.search(r"^(?:static )?(?:void|int|char \*)\s*" + name +
                      r"\([^;]*?\)\s*\{", text, re.M)
    if not start:
        raise RuntimeError(f"Cannot find {name}")
    return balanced(text, start.start())


helpers = ("copyover_number", "copyover_name_valid", "copyover_hex",
           "copyover_write_record", "copyover_unhex", "copyover_token",
           "copyover_parse_line", "copyover_read_records", "copyover_client_fd",
           "copyover_prepare_socket", "copyover_close_orphans")
implementations = "\n\n".join(function(copyover, n) for n in helpers)
implementations += "\n" + function(comm, "free_desc")
normal = function(copyover, "do_copyover")
emergency = function(comm, "emergancy_copyover")
wrappers = []
for name, text in (("normal", normal), ("emergency", emergency)):
    start = text.index("copyover_write_record(")
    call = balanced(text, start, "(", ")")
    wrappers.append("static int actual_" + name +
                    "(FILE *fp, DESCRIPTOR_DATA *d) {\n"
                    "CHAR_DATA *och = d->character;\nreturn " + call + ";\n}\n")

recovery = function(copyover, "copyover_recover")
start = recovery.index("    if (!copyover_read_records(")
guard = balanced(recovery, start)
wrappers.append("static void actual_read_or_exit(FILE *fp) {\n"
                "COPYOVER_RECORD *records; size_t record_count; int maxp;\n" +
                guard + "\nfree(records);\n}\n")
start = recovery.index("        CREATE( d, DESCRIPTOR_DATA, 1 );")
end = recovery.index("        /* Now, find the pfile */", start)
initializer = recovery[start:end]
wrappers.append("static DESCRIPTOR_DATA *actual_restore(COPYOVER_RECORD *record) {\n"
                "DESCRIPTOR_DATA *d; int desc=record->desc, dport=record->port, "
                "idle=record->idle;\n" + initializer + "\nreturn d;\n}\n")
# The full file must be validated before any descriptor/player is constructed.
assert recovery.index("copyover_read_records(") < recovery.index("CREATE( d,")
assert "fscanf(" not in recovery
harness = (root / "tests/copyover_records_harness.c").read_text()
generated = harness.replace("/* PRODUCTION_FUNCTIONS */",
                            implementations + "\n".join(wrappers))
flags = shlex.split(os.environ.get(
    "COPYOVER_TEST_CFLAGS", "-std=gnu11 -g -O1 -Wall -Wextra -Werror "
    "-fsanitize=address,undefined -fno-omit-frame-pointer"))
with tempfile.TemporaryDirectory(prefix="copyover-test-") as tmp:
    cfile, exe = Path(tmp) / "records.c", Path(tmp) / "records"
    cfile.write_text(generated)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + flags +
                   ["-I", str(root / "dbzc/src"), str(cfile), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True, timeout=30,
                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1"})
