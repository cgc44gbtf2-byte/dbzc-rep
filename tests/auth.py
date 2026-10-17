#!/usr/bin/env python3
"""Compile real auth helpers and extracted production flows; no server/pfiles."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / "dbzc/src"


def function(text, name):
    match = re.search(r"^(?:static )?(?:void|int)\s+" + name +
                      r"\s*\([^;]*?\)\s*\{", text, re.M)
    assert match, name
    depth = 0
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]'
    for token in re.finditer(pattern, text[match.start():], re.S):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():match.start() + token.end()]
    raise AssertionError(name)


comm = (src / "comm.c").read_text()
info = (src / "act_info.c").read_text()
interp = (src / "interp.c").read_text()
wiz = (src / "act_wiz.c").read_text()
build = (src / "build.c").read_text()
# Check actual call sites, including both separately charged KDFs per migration.
old = comm[comm.index("case CON_GET_OLD_PASSWORD:"):
           comm.index("case CON_CONFIRM_NEW_NAME:")]
assert old.index("auth_attempt(") < old.index("password_verify(")
assert old.count("auth_attempt(") == 2
assert old.count("auth_migrate(") == 3
assert old.index("load_char_obj(") < old.rindex("auth_migrate(")
assert "if (fOld) auth_migrate" in old
for name in ("CON_GET_NEW_PASSWORD", "CON_CONFIRM_NEW_PASSWORD"):
    block = comm[comm.index("case " + name + ":"):]
    assert block.index("auth_attempt(") < min(
        (block.index(s) for s in ("password_hash(", "password_verify(") if s in block))
for name, source in (("do_password", info), ("do_delete", wiz)):
    body = function(source, name)
    assert body.index("auth_attempt(") < body.index("password_verify(")
    assert "password_token(" in body
assert build.index("auth_attempt(") < build.index("password_hash(")
assert "crypt(" not in function(wiz, "do_form_password")
# Strip comments so disabled redaction can never pass the ordering regression.
clean = re.sub(r'/\*.*?\*/|//[^\n]*', '', interp, flags=re.S)
policy = clean.index("sensitive = found")
assert policy < clean.index("interpret( ch->fused_with, arg)")
assert policy < clean.index('sprintf( lastplayercmd,')
assert policy < clean.index("write_watch_files(ch,")
assert policy < clean.index("ch->desc->snoop_by", policy)
assert 'sensitive || cmd->log == LOG_NEVER ? "XXX" : argument' in clean
assert "cmd->log = LOG_NEVER" not in clean
for handler in ("do_password", "do_delete", "do_form_password", "do_mset"):
    assert handler in clean[policy:policy + 400]

start = old.index("        if (!auth_attempt")
end = old.index("        if ( ch->position")
login = old[start:end]
production = (function(info, "do_password") + "\n" +
              function(comm, "auth_migrate") + "\n" +
              "static void login_fixture(DESCRIPTOR_DATA *d, char *argument) {\n"
              "CHAR_DATA *ch=d->character; int auth_result, chk, fOld;\n"
              "char auth_old[PASSWORD_HASH_SIZE], auth_new[PASSWORD_HASH_SIZE], buf[4096];\n"
              + login + "\n}\n")
flags = shlex.split(os.environ.get(
    "AUTH_TEST_CFLAGS", "-std=gnu11 -g -O1 -Wall -Wextra -Werror "
    "-fsanitize=address,undefined -fno-omit-frame-pointer"))
with tempfile.TemporaryDirectory(prefix="auth-tests-") as tmp:
    generated = Path(tmp) / "integration.c"
    generated.write_text((root / "tests/auth_integration_harness.c").read_text()
                         .replace("/* PRODUCTION_FUNCTIONS */", production))
    for name, harness, wrappers in (
        ("helper", root / "tests/auth_harness.c",
         ["-Wl,--wrap=getrandom", "-Wl,--wrap=crypt"]),
        ("integration", generated, []),
    ):
        exe = Path(tmp) / name
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + flags +
                       ["-I", str(src), str(harness), str(src / "password_auth.c")] +
                       ([str(src / "auth_rate.c")] if name == "helper" else []) +
                       wrappers + ["-largon2", "-lcrypt", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=60, env={
            **os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
            "UBSAN_OPTIONS": "halt_on_error=1"})
    # Plaintext builds must fail at compile time.
    rejected = subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                              ["-DNOCRYPT", "-I", str(src), "-x", "c", "-c",
                               "-o", str(Path(tmp) / "nocrypt.o"), "-"],
                              input='#include "password_auth.h"\n', text=True,
                              capture_output=True)
    assert rejected.returncode != 0
print("auth production integration and policy regressions passed")
