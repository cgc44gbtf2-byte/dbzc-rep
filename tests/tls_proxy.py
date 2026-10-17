#!/usr/bin/env python3
"""Sanitize the real parser, pending lifecycle, registry and extracted listener."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / "dbzc/src"
comm = (src / "comm.c").read_text()
start = comm.index("int init_socket( int port )\n{")
end = comm.index("\n}\n", start) + 3
listener = comm[start:end]
harness = (root / "tests/tls_proxy_harness.c").read_text().replace(
    "/* INIT_SOCKET */", listener)
# Guard production wiring as well as executing its transport implementation.
accept = comm[comm.index("void new_descriptor( int new_desc )\n{"):
              comm.index("void initialize_descriptor(int desc", comm.index("void new_descriptor( int new_desc )\n{"))]
assert accept.index("proxy_pending_add(") < accept.index("initialize_descriptor(")
assert "return;" in accept[accept.index("proxy_pending_add("):accept.index("initialize_descriptor(")]
assert "proxy_pending_select(&in_set, &maxdesc)" in comm
assert "proxy_pending_poll(initialize_descriptor)" in comm
assert "auth_proxy_mode() || sysdata.NO_NAME_RESOLVING" in comm
assert "proxy_listener_valid(control)" in comm
recovery = (src / "copyover.c").read_text()
recovery = recovery[recovery.index("void copyover_recover( void )"):]
assert recovery.index("auth_proxy_register(") < recovery.index("load_char_obj(")
assert "proxy_listener_valid(control)" in recovery
assert "controlled restart" in recovery
guard_start = recovery.index("    if (auth_proxy_mode())")
guard_end = recovery.index("    memset(keep", guard_start)
harness = harness.replace("/* RECOVERY_GUARD */",
    "static void recover_identity(int fd, const char *host) {\n"
    "struct { int desc; const char *host; } records[] = {{fd, host}};\n"
    "size_t record_index, record_count = 1;\n" +
    recovery[guard_start:guard_end] + "\n}\n")
assert re.search(r"close_registered_socket.*?auth_proxy_forget", comm, re.S)
assert "#define closesocket close_registered_socket" in comm
with tempfile.TemporaryDirectory(prefix="tls-proxy-") as tmp:
    cfile, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    cfile.write_text(harness)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                   shlex.split(os.environ.get("NIX_CFLAGS_COMPILE", "")) +
                   ["-std=gnu11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I" + str(src), str(cfile), str(src / "proxy_protocol.c"),
                    str(src / "auth_rate.c")] +
                   shlex.split(os.environ.get("NIX_LDFLAGS", "")) +
                   ["-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True,
                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1"})
