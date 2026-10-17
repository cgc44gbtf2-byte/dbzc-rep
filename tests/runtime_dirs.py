#!/usr/bin/env python3
"""Exercise production directory guards and isolated startup provisioning.

No server is started and the checkout's deployment directories are not changed.
CC and RUNTIME_DIRS_TEST_CFLAGS override compiler/default sanitizer flags.
"""
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(text, name):
    match = re.search(r"^void\s+" + name + r"\s*\([^;]*?\)\s*\{", text, re.M)
    assert match, name
    depth = 0
    for token in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
                             r"'(?:\\.|[^'\\])*'|[{}]", text[match.start():], re.S):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():match.start() + token.end()]
    raise AssertionError("Unterminated production function")


db = (ROOT / "dbzc/src/db.c").read_text()
pieces = []
for name in ("make_wizlist", "load_buildlist"):
    source = function(db, name)
    # Compile the actual opendir-to-first-readdir path. The rest of these large
    # functions depends on world parsers; it is deliberately not reimplemented.
    start = source.index("dp = opendir(")
    end = source.index("dentry = readdir(", start)
    pieces.append("static void guard_" + name + "(void) {\n"
                  "DIR *dp; int ilevel = 0;\n" + source[start:end] +
                  "(void)ilevel; (void)readdir(dp); closedir(dp); reached++;\n}")
source = function((ROOT / "dbzc/src/copyover.c").read_text(), "load_obj_files")
start = source.index('sprintf( directory_name,')
end = source.index("dentry = readdir(", start)
pieces.append("static void guard_load_obj_files(void) {\n"
              "DIR *dp; char directory_name[100];\n" + source[start:end] +
              "(void)readdir(dp); closedir(dp); reached++;\n}")
pieces.append(function((ROOT / "dbzc/src/pfiles.c").read_text(), "pfile_scan"))
harness = (ROOT / "tests/runtime_dirs_harness.c").read_text()
generated = harness.replace("/* PRODUCTION_CODE */", "\n\n".join(pieces))
flags = shlex.split(os.environ.get(
    "RUNTIME_DIRS_TEST_CFLAGS", "-std=gnu89 -O1 -g -Wall -Wextra -Werror "
    "-Wno-unused-parameter -Wno-misleading-indentation "
    "-fsanitize=address,undefined -fno-omit-frame-pointer"))
with tempfile.TemporaryDirectory(prefix="runtime-dirs-") as tmp:
    tmp = Path(tmp)
    cfile, exe = tmp / "runtime.c", tmp / "runtime"
    cfile.write_text(generated)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                   flags + [str(cfile), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, cwd=tmp, timeout=30,
                   env={**os.environ,
                        "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})


def preflight(deploy, ok=True, check=False):
    result = subprocess.run(["sh", str(ROOT / "scripts/preflight.sh")] +
                            (["--check"] if check else []) + [str(deploy)],
                            text=True, capture_output=True, timeout=30, cwd="/")
    assert (result.returncode == 0) == ok, result.stdout + result.stderr
    return result.stdout + result.stderr


with tempfile.TemporaryDirectory(prefix="runtime-preflight-") as tmp:
    deploy = Path(tmp) / "deployment with spaces" / "dbzc"
    deploy.mkdir(parents=True)
    # Use authentic bundled data, not fabricated replacement world files.
    assert "area.lst" in preflight(deploy, ok=False)
    for name in ("area", "classes", "races", "system", "maps"):
        shutil.copytree(ROOT / "dbzc" / name, deploy / name)
    original = {p.relative_to(deploy): p.read_bytes()
                for p in deploy.rglob("*") if p.is_file()}
    assert "runtime directory missing" in preflight(deploy, ok=False, check=True)
    preflight(deploy)
    for letter in "abcdefghijklmnopqrstuvwxyz":
        assert (deploy / "player" / letter).is_dir()
        assert stat.S_IMODE((deploy / "player" / letter).stat().st_mode) == 0o750
    for name in ("gods", "building", "corpses", "log", "copyover", "gnotes",
                 "home", "backup", "watch", "ships", "html"):
        assert (deploy / name).is_dir()
    assert (deploy.parent / "public_html").is_dir()
    (deploy / "gods").chmod(0o700)
    preflight(deploy)
    preflight(deploy, check=True)
    assert stat.S_IMODE((deploy / "gods").stat().st_mode) == 0o700
    current = {p.relative_to(deploy): p.read_bytes()
               for p in deploy.rglob("*") if p.is_file()}
    assert current == original, "provisioning changed or invented data files"

    (deploy / "player/z").rmdir()
    (deploy / "player/z").write_text("must not be overwritten\n")
    assert "cannot create runtime directory" in preflight(deploy, ok=False)
    assert (deploy / "player/z").read_text() == "must not be overwritten\n"
    (deploy / "player/z").unlink()
    preflight(deploy)
    area_list = deploy / "area/area.lst"
    saved = area_list.read_bytes()
    area_list.write_bytes(saved.replace(b"$", b""))
    assert "dollar terminator" in preflight(deploy, ok=False)
    area_list.write_bytes(saved)
    first_area = saved.split()[0].decode()
    area_file = deploy / "area" / first_area
    area_file.unlink()
    assert first_area in preflight(deploy, ok=False)
    assert not area_file.exists(), "preflight invented a missing world file"

# Catch drift between mandatory map loaders and provisioning's file list.
overland = (ROOT / "dbzc/src/overland.c").read_text()
maps = re.search(r"map_filenames\[\]\s*=\s*\{(.*?)\};", overland, re.S).group(1)
script = (ROOT / "scripts/preflight.sh").read_text()
for name in re.findall(r'"([^"]+)"', maps):
    assert name in script, "preflight missing mandatory map " + name
print("runtime preflight provisioning: passed")
