#!/bin/sh
# Run as the unprivileged game account, before starting from dbzc/area.
# Usage: sh scripts/preflight.sh [--check] [path/to/dbzc]
# Uses only the POSIX shell and standard Ubuntu/Debian utilities. Does not
# install packages, generate world data, change existing modes, or start a server.
set -eu
set -f
umask 027

fail()
{
    printf 'preflight: %s\n' "$*" >&2
    exit 1
}

check_only=false
if [ "${1-}" = "--check" ]; then
    check_only=true
    shift
fi
if [ "$#" -gt 1 ] || [ "${1-}" = "--help" ]; then
    printf 'Usage: sh scripts/preflight.sh [--check] [path/to/dbzc]\n' >&2
    exit 2
fi
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=${1:-"$script_dir/../dbzc"}
root=$(CDPATH= cd -- "$root" && pwd) ||
    fail "deployment directory does not exist: $root"

require_file()
{
    [ -f "$1" ] && [ -r "$1" ] && [ -s "$1" ] ||
        fail "required data file missing, empty, or unreadable: $1. Restore real deployment data; no replacement will be generated."
}

# These loaders are mandatory at boot. Optional state/configuration is not
# synthesized. Lists contain whitespace-delimited fread_word entries ending $.
require_list()
{
    require_file "$1"
    entries=$(awk '
        { for (i = 1; i <= NF; i++) {
            if (substr($i, 1, 1) == "$") { ended = 1; exit }
            print $i
            count++
        } }
        END { if (!ended || !count) exit 1 }
    ' "$1") || fail "required list is empty or lacks its dollar terminator: $1"
    for entry in $entries; do
        # All paths are interpreted from the server working directory (area),
        # including ../classes and ../races entries in the bundled lists.
        case "$entry" in
            /*) require_file "$entry" ;;
            *) require_file "$root/area/$entry" ;;
        esac
    done
}

require_list "$root/area/area.lst"
require_list "$root/classes/class.lst"
require_list "$root/races/race.lst"
for file in commands.dat specfuns.dat socials.dat askills.dat sskills.dat; do
    require_file "$root/system/$file"
done
# Keep in step with map_filenames in src/overland.c.
for file in earth.raw aboveearth.raw belowearth.raw namek.raw \
            abovenamek.raw belownamek.raw battlestar.raw legendaria.raw; do
    require_file "$root/maps/$file"
done

ensure_dir()
{
    if [ ! -d "$1" ]; then
        if "$check_only"; then
            fail "required runtime directory missing: $1. Run preflight without --check to provision it."
        fi
        # Deliberately no chmod/chown and no mkdir -p: existing objects must
        # remain untouched, and a file blocking a directory must fail visibly.
        mkdir -m 0750 -- "$1" ||
            fail "cannot create runtime directory: $1"
        printf 'preflight: created %s (0750)\n' "$1"
    fi
    [ -r "$1" ] && [ -w "$1" ] && [ -x "$1" ] ||
        fail "runtime directory is not readable/writable/searchable by the game account: $1"
}

for dir in area player gods building corpses log system boards clans classes \
           races maps home backup watch ships copyover gnotes html; do
    ensure_dir "$root/$dir"
done
for letter in a b c d e f g h i j k l m n o p q r s t u v w x y z; do
    ensure_dir "$root/player/$letter"
done
ensure_dir "$root/area/mudprogs"
# WEB* outputs are relative to area as ../../public_html.
ensure_dir "$root/../public_html"

printf 'preflight: required data and runtime directories ready at %s\n' "$root"
printf 'preflight: start the built server with working directory %s/area\n' "$root"
