# Maintenance batch 1

This branch is a development baseline, **not a production security clearance**.

## Build

Deployment target: the latest stable Ubuntu or Debian release. Prefer a
supported Ubuntu LTS release when choosing Ubuntu for a long-lived server.
The current verification environment is not an Ubuntu/Debian installation;
distribution-specific build and isolated startup checks remain required.

Requirements: GCC, GNU Make, and libcrypt/libxcrypt development files.
On Debian/Ubuntu the packages are `build-essential` and `libcrypt-dev`.

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential libcrypt-dev python3 git
make -C dbzc/src -j4
```

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
git diff --check
```

The connection harness extracts the actual production functions and the relevant
login branch, stubbing unrelated game and socket dependencies. It covers signed
accept errors, descriptor boundaries, listener and setup failures, bans, counting,
copyover recovery accounting, client cleanup, and disabled signup.

The web tests compile the actual converter with an allocator test double. They
cover literal printf directives, every supported color, HTML escaping, empty and
large input, repeated conversion, allocation failures, and output errors.

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

## Verification and remaining work

The full executable compiled and linked with GCC 15.3.0 and libxcrypt 4.4.38.
Both focused regression suites passed with sanitizers. Legacy compiler warnings
remain. No public listener was started, no game-world boot/login test was run,
and no whole-server memory profiling was performed.

Still pending from the review:

- Nonblocking output/backpressure: `write_to_descriptor` still retries
  EWOULDBLOCK without returning to the event loop.
- Rank-string allocator ownership and remaining unbounded string operations.
- Copyover client restoration and inconsistent emergency copyover records.
- Password-hash/transport modernization and legacy committed credential cleanup.
- Replacing the bundled allocator, removing `-fcommon`, and adding CI.

Keep the server isolated until these issues and full runtime verification have
been addressed. Do not merge or deploy this batch solely because it builds.
