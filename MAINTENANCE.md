# Maintenance batches 1–5

This branch is a development baseline, **not a production security clearance**.

## Build

Deployment target: the latest stable Ubuntu or Debian release. Prefer a
supported Ubuntu LTS release when choosing Ubuntu for a long-lived server.
GitHub Actions builds and runs the focused sanitizer tests in the official
`ubuntu:latest` and `debian:stable` containers, logging the resolved OS/compiler
versions. These tags follow upstream releases rather than pinning a deployment
image. Isolated game-world startup checks remain required.

Requirements: GCC, GNU Make, and libcrypt/libxcrypt development files.
On Debian/Ubuntu the packages are `build-essential` and `libcrypt-dev`.

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential libcrypt-dev libargon2-dev python3 git
make -C dbzc/src -j4
```

Before startup, run preflight **as the game service account**, not root:

```sh
sh scripts/preflight.sh
sh scripts/preflight.sh --check
# Start the server from dbzc/area, not the repository root.
```

Preflight validates mandatory world/list/table/map data, then creates missing
runtime directories with mode 0750. It preserves existing data and permissions.
It accepts an optional deployment path, including paths containing spaces.
It does not start the server, install packages, or invent missing world data.

The Makefile explicitly uses GNU89 and `-fcommon` for the legacy source's
tentative shared globals. These are compatibility settings, not a modernization
of those globals. The duplicate wear enum slot was renamed rather than removed
to preserve subsequent numeric values. The sector table declaration now follows
the complete structure definition, and object files depend on the headers.

If libcrypt is outside the compiler's search path, pass
`NEED_CRYPT='-L/path/to/lib -Wl,-rpath,/path/to/lib -lcrypt'`.
Do not disable password hashing to work around missing build dependencies.

## Regression tests

Requires Python 3 and a compiler with AddressSanitizer/UndefinedBehaviorSanitizer.

```sh
python3 tests/connection_lifecycle.py
sh tests/web_output_test.sh
python3 tests/output_queue.py
python3 tests/copyover_records.py
python3 tests/rank_ownership.py
python3 tests/reserve_lifecycle.py
python3 tests/runtime_dirs.py
python3 tests/auth.py
git diff --check
```

The connection harness extracts the actual production functions and the relevant
login branch, stubbing unrelated game and socket dependencies. It covers signed
accept errors, descriptor boundaries, listener and setup failures, bans, counting,
copyover recovery accounting, client cleanup, and disabled signup.

The web tests compile the actual converter with an allocator test double. They
cover literal printf directives, every supported color, HTML escaping, empty and
large input, repeated conversion, allocation failures, and output errors.

The output tests extract the production queue/transport functions. They include
a real nonblocking socketpair with a stalled peer and byte-exact resumed output,
plus partial writes, EINTR/EAGAIN, zero/fatal sends, fairness, ordering, prompt and
snoop retry behavior, overflow, graceful-close deadlines, and pager behavior.

Rank tests use the actual interned-string allocator and extracted production
load/mutation/free paths, checking reference counts, aliased input, repeated
loads, shared values, rank commands, demotion, race changes and clearing.
Copyover tests exercise the actual bounded parser/writers, restored string
ownership, legacy records, malformed input, socket cleanup and error handling.
Reserve tests use actual FILE streams and extracted save/show/cleanup code,
including repeated saves, open/close failures and fail-closed reserve recovery.
Directory tests exercise production guards and preflight against copied data,
including missing/inaccessible directories and nondestructive provisioning.

These focused tests run with ASan/UBSan and leak detection; they do not establish
that the whole server is free of leaks or memory corruption.

## Changes

- Return immediately after closing a denied new-player connection.
- Free descriptor client strings and count active descriptors only once.
- Check signed accept results, close sockets on setup failures, and reject
  descriptors outside `select`'s supported range, including inherited ones.
- Replace web exporters' dynamic printf formats with a shared literal-data
  writer, releasing its allocated conversion result.
- Replace repeated leaking string substitutions with an overflow-checked,
  one-pass HTML/color converter. Generated entities are not rescanned as colors.
- Retain unsent output in a bounded 64 KiB per-client queue. Each pulse allows
  at most 16 KiB and four send attempts per descriptor; EAGAIN yields immediately.
- Latch output errors/overflow for safe event-loop closure instead of freeing a
  descriptor inside an output caller. Graceful closes retain transport storage
  for at most eight pulses; forced/error/deadline closure aborts remaining output.
- Add a read-only-permission GitHub Actions matrix for Ubuntu and Debian builds
  and all focused sanitizer regressions. No deployment step is included.
- Use STRALLOC/STRFREE consistently for all player rank initialization,
  replacement and cleanup, including commands and repeated player-file fields.
- Share a bounded, versioned copyover writer between normal/emergency paths;
  restore owned client strings and close unclaimed inherited client sockets.
- Restore the reserve FILE after system saves on both success and failure;
  clear closed pointers and stop with a fatal diagnostic if reopening fails,
  rather than passing an invalid stream to legacy callers.
- Guard directory reads in wizlist, builder-list, player-file scan and copyover
  object restoration. Unavailable optional scans are diagnosed and skipped;
  player scans explicitly report incomplete totals.

### Copyover record compatibility

New files use DBZC2 records with hex-encoded string fields, preserving whitespace
without ambiguity. The new reader also accepts historical normal and emergency
records. Legacy emergency records lack room/client metadata; recovery uses
temple/Unidentified defaults for those missing fields.

Malformed, truncated, duplicate or unsupported records reject the entire
recovery file and terminate recovery before any player is loaded, closing
inherited sockets. Syntactically valid records for invalid/non-client sockets
are diagnosed and skipped. Normal and emergency writers abort on record/file
write failure before exec.

Old executables do not understand DBZC2. Do not live-copyover into an older
binary; use a controlled shutdown/restart for rollback. Emergency crash handling
remains best-effort, not a guarantee that a corrupted process can recover.

### Transport contract

For linked descriptors, `write_to_descriptor` returning TRUE now means accepted
in order, not necessarily delivered. Prompt/snoop copies are prepared once, and
retries drain the prepared prefix without duplicating it. Bare file descriptors
(pre-link rejection/recovery probes) have bounded best-effort writes; FALSE may
follow a sent prefix, so callers must not retry the whole message.

Queues are process-local. Normal copyover now nonblockingly drains/checks queues
before exec; pending/failed output aborts the copyover and removes its recovery
file so the admin can retry after clients drain. Emergency crash recovery and
process shutdown cannot promise delivery. Idle clients can still be disconnected
by the existing timeout policy.

## Password security and login throttling (batch 5)

New passwords use libargon2 Argon2id v19 (19 MiB, two iterations, one lane)
with a fresh 128-bit kernel-random salt. New passwords must be 12–128 bytes.
Plaintext `NOCRYPT` builds are rejected. Keep account files and backups private;
password hashes remain sensitive.

Successful legacy DES/MD5-crypt/SHA-crypt authentication upgrades the password
after the full player loads or reconnects, not during a partial character load.
Only bounded, recognized legacy hash formats are accepted (SHA-crypt rounds
1000–10000). Unsupported/manual hash formats require an administrator reset.
DES inputs longer than eight bytes are ambiguous and are not silently migrated.
Legacy passwords longer than the new limit remain verifiable up to the historical
input limit but require an explicit change. Failed or throttled upgrades leave
the original credential intact and report that the upgrade was deferred.

The password command now requires reauthentication:

```text
password <current> <new> <again>
```

Quoted tokens support spaces; matching and parsing preserve case. Deletion and
administrator password reset use the same verification/hashing helpers.
Sensitive commands are redacted before diagnostic/watch/snoop/fuse/log output,
independently of editable command-log settings. The hash-display command is
disabled. These protections do not encrypt network traffic: plain Telnet still
exposes credentials. Configure encrypted transport before public deployment.

Each expensive hash/verification consumes a token: at most four per monotonic
second globally, twelve per numeric peer IP per minute, and six per
case-normalized account per minute. A migration or change can cost two tokens.
Limits survive reconnects, use bounded non-evicting tables, and fail closed on
capacity exhaustion. Fixed windows permit bursts at boundaries; shared NATs
share an IP budget. Restart/copyover clears these process-local budgets.
These are short temporary limits, not persistent account locks. A distributed
attacker can still consume global capacity; upstream filtering remains needed.

Do not roll back to an old executable after accounts have migrated: old code
cannot verify Argon2id. Preserve protected backups and plan recovery explicitly.
The previous DBZC2 controlled-restart rollback restriction still applies.

Batch 5 adds sanitizer helper/production-flow tests for legacy migration,
bounded formats/costs, entropy failure, reconnect/save ordering, current-password
checks, throttling, and sensitive command redaction. All eight suites and a full
rebuild passed locally. A fresh-copy isolated live run additionally verified
SHA-crypt login migration persisted as Argon2id, repeated system saves and
same-connection copyover recovery. This does not cover every auth path under
live load. Consult the PR checks for Ubuntu/Debian CI status.

The local Nix environment needs `libargon2` and `libxcrypt-legacy` plus the C
toolchain. The non-legacy Nix crypt variant excludes some existing password
formats; the auth suite intentionally fails when that compatibility is absent.
The Makefile and auth tests consume NIX_CFLAGS_COMPILE/NIX_LDFLAGS when set.
Ubuntu/Debian use the development packages listed above.

## Verification and remaining work

The full executable compiled and linked with GCC 15.3.0 and libxcrypt 4.4.38.
All seven focused regression suites passed locally with sanitizers. Legacy
compiler warnings remain.

Batch 4 additionally passed two independent, fresh-copy live tests in an
isolated Linux user/network namespace. Each used the bundled world data and
preflight, retained automatic cleanup, logged in a synthetic administrator,
ran two explicit player scans (repeated system saves), executed real
`copyover now`, and verified `look` on the same TCP connection afterward.
The MUD name remained intact before and after exec. No debugger or reused
configuration was used in these two runs. All test processes were stopped.

Those live tests ran in the local Linux environment, not the Ubuntu/Debian CI
containers. CI runs full builds and the seven focused suites. No public
listener, production deployment or whole-server memory profile was performed.
Existing world-data reset warnings, a missing relax skill and absent clan
vault files remain. Multi-player/load/soak and emergency-crash testing remain
necessary before a production readiness claim.

Still pending from the review:

- Remaining unbounded string operations.
- Password-hash/transport modernization and legacy committed credential cleanup.
- Replacing the bundled allocator and removing `-fcommon`.

Keep the server isolated until these issues and full runtime verification have
been addressed. Do not merge or deploy this batch solely because it builds.
