# TLS-only game ingress on Ubuntu/Debian

This template terminates TLS at the distribution HAProxy service: public TCP
**4001 -> TLS -> HAProxy -> PROXY v1 -> 127.0.0.1:4000**. The game must contain
the loopback bind and strict PROXY v1 support and run with the unit's explicit
environment. There is no public plaintext fallback, HTTP endpoint, stats listener,
or admin socket. This is deployment guidance, not a production security clearance.
Do not deploy an unreviewed world or bundled credentials.

## Trust boundary

Use a dedicated, trusted host. Loopback is plaintext: other local users/processes
(and root) can connect to 4000 and forge PROXY source addresses. These settings do
**not** protect against hostile multi-tenant users, a compromised local service,
or root. HAProxy alone is trusted to supply the real peer address used by game
bans, throttling and copyover. Do not expose the backend through containers,
port forwarding or another proxy. Never enable frontend `accept-proxy`.

Clients must use a TLS-capable MUD client with mandatory certificate-chain and
hostname validation for the advertised DNS name, not an insecure Telnet client,
an unverified TLS wrapper, or a “trust any certificate” setting. TLS authenticates
the server, not the player; account authentication remains necessary.

## Prepare a reviewed deployment

Run these commands only on the intended host, after reviewing paths and existing
service configuration. This repository does not request certificates, change
firewall rules or deploy anything automatically.

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends haproxy openssl
haproxy -vv
openssl version
# Build dependencies and world-data preparation: see ../../MAINTENANCE.md.
# Create once, unless an appropriate dedicated account already exists:
sudo useradd --system --user-group --home-dir /srv/dbzc --no-create-home \
  --shell /usr/sbin/nologin dbzc
```

Install the reviewed tree at `/srv/dbzc`, with the built executable at
`/srv/dbzc/dbzc/src/dbzc`. Keep `/srv/dbzc`, `/srv/dbzc/dbzc`, source/binary and
`scripts` root-owned and not group/other-writable. Do not build as the running
game account. Back up existing state before any ownership changes. Grant the
game account write access only to its actual data/runtime trees:

```sh
# Run against the populated deployment, not an empty replacement world.
for dir in area player gods building corpses log system boards clans classes \
           races maps home backup watch ships copyover gnotes html; do
    sudo install -d -m 0750 -o dbzc -g dbzc "/srv/dbzc/dbzc/$dir"
    sudo chown -R dbzc:dbzc "/srv/dbzc/dbzc/$dir"
done
sudo install -d -m 0750 -o dbzc -g dbzc /srv/dbzc/public_html
sudo chown -R dbzc:dbzc /srv/dbzc/public_html
sudo -u dbzc /bin/sh /srv/dbzc/scripts/preflight.sh /srv/dbzc/dbzc
sudo -u dbzc /bin/sh /srv/dbzc/scripts/preflight.sh --check /srv/dbzc/dbzc
```

The unit makes the game data tree and `public_html` writable under
`ProtectSystem=strict`, but mounts `src` and `scripts` read-only. This includes
all preflight runtime directories, area saves and inherited-socket copyover
files. Preprovision before starting: systemd's writable paths must exist.
The account can read/execute the root-owned binary; same-binary copyover exec is
allowed. Do not add a separate launcher or a sandbox that denies exec/inherited
sockets. Game service restart is disruptive; in-game copyover is not a restart.

## Certificate and validation

Have your ACME/certificate operator provision a valid certificate for the actual
DNS name. `/etc/haproxy/certs/dbzc.pem` must contain the PEM full chain followed
by its private key, without an interactive passphrase. No key/certificate is
shipped here. Keep the directory root-owned mode 0700 and PEM root-owned mode
0600; the distribution service starts HAProxy as root to load it before dropping
to `haproxy`. Do not give the game user access. Ensure the operator installs
renewals atomically with the same ownership/modes, validates and reloads HAProxy.
Do not print or paste the PEM, private key or passwords into diagnostics.

From the reviewed checkout, after certificate provisioning:

```sh
# This replaces the host's HAProxy configuration: preserve/review any existing
# services first. Do not accidentally retain a distribution plaintext listener.
sudo haproxy -c -f deploy/tls/haproxy.cfg
sudo install -o root -g root -m 0644 deploy/tls/haproxy.cfg /etc/haproxy/haproxy.cfg
sudo haproxy -c -f /etc/haproxy/haproxy.cfg
sudo install -o root -g root -m 0644 deploy/tls/dbzc.service /etc/systemd/system/dbzc.service
sudo systemd-analyze verify /etc/systemd/system/dbzc.service
sudo systemctl daemon-reload
# Only after validation and firewall review:
sudo systemctl enable --now dbzc.service
sudo systemctl enable --now haproxy.service
```

Use the distribution's `haproxy.service`, not a second custom proxy service.
This template requires HAProxy 3.0 or newer with OpenSSL support. The
[HAProxy 3.0 versioned manual](https://docs.haproxy.org/3.0/configuration.html#4-timeout%20client-hs)
documents `timeout client-hs` as the maximum TLS handshake duration, independently
of the established client timeout. This is a documentation compatibility check,
not a claim that the local test ran against 3.0. Older releases such as HAProxy
2.8 (including Ubuntu 24.04's original package) do not support this directive;
select a supported distribution release providing 3.0+ rather than removing
the handshake deadline. Always run `haproxy -c` with the actual host version.
The TLS config regression needs Python 3, HAProxy with OpenSSL, and `openssl`:

```sh
python3 tests/tls_config.py
```

It generates a disposable self-signed key/certificate in a private temporary
directory, checks the actual template with `haproxy -c`, and opens no listeners.
Only the certificate path and worker UID/GID are replaced for portability to
developer hosts without the distribution's `haproxy` account.
The self-signed fixture is never a production trust configuration.

## Exposure, limits and monitoring

At host **and** provider/network firewalls, allow public TCP 4001 only for game
traffic; never allow public 4000, including IPv6. Retain only separately reviewed
management access (for example restricted SSH); avoid locking yourself out.
The template binds IPv4 only. If adding IPv6, use a separate TLS-only bind,
equivalent source limits and firewall rules; the current IPv4 stick table must
also be adapted for IPv6 addresses. Inspect `ss -ltnp`: game 4000 must be exactly
`127.0.0.1:4000`, and there must be no wildcard plaintext game listener. Check
from an external host as well; local-only tests cannot establish firewall safety.

TLS 1.2 is the minimum (TLS 1.3 allowed), with authenticated ephemeral TLS 1.2
ciphers. A 10-second handshake deadline, 256 total/SSL/backend connections, eight
connections per source and 20 new connections per 10 seconds limit slow-handshake
and connection floods. Shared NATs share limits. The 100k-entry bounded table and
global cap are not DDoS protection; use upstream filtering and monitor saturation.
Queue/connect/half-close timeouts are short, but established game connections
have 12-hour **inactivity** timeouts. Active games can remain connected longer;
the game's own idle policy still applies. Adjust caps only after reviewing the
game's `select`/file-descriptor limits and measuring resource use.

Monitor service health/restarts, HAProxy TCP connection/error logs through the
distribution's syslog/journal setup, certificate expiry, resource use, rejection
rates and backend failures. No game payload/password logging is enabled here.
Do not expose unauthenticated stats or admin sockets publicly. Verify with a
trusted CA store and the actual name, from outside the host:

```sh
openssl s_client -connect game.example.org:4001 -servername game.example.org \
  -verify_hostname game.example.org -verify_return_error -CApath /etc/ssl/certs
```

Require successful certificate verification and a TLS 1.2/1.3 session before
sending credentials. Verify plaintext clients and TLS <1.2 cannot reach a game
prompt, public 4000 is unreachable, and a TLS-connected player retains the real
IP for bans/throttles. Before opening service, test same-connection in-game
copyover on isolated authentic world data. Config validation alone does not
establish live game/copyover correctness.

## Renewal, copyover and rollback

After an atomic certificate/config update:

```sh
sudo haproxy -c -f /etc/haproxy/haproxy.cfg &&
  sudo systemctl reload haproxy.service
```

A distribution graceful HAProxy reload sends new connections to the new worker;
existing TLS sessions remain with the old worker until disconnect/timeout.
No `hard-stop-after` is configured: it would forcibly disconnect long sessions.
Monitor old workers and memory during repeated renewals/reloads; do not use
`restart` to renew certificates. Confirm your distribution's reload is graceful.

In-game copyover instead execs the game binary, retaining its accepted loopback
sockets and recovered client IP metadata. HAProxy is neither restarted nor
reloaded for that operation; TLS lives outside the game process. A game service
restart, proxy restart, crash, host reboot or broken backend connection will
disconnect clients.

Rollback requires a **controlled shutdown/restart** with backups and an executable
compatible with account hashes/world/copyover formats (see maintenance guidance).
Never copyover into an incompatible older executable, change bind/proxy settings
under inherited sockets, disable TLS, expose 4000, or switch off PROXY parsing as
a recovery shortcut. Keep TLS-only ingress in place or close public access while
repairing the deployment.
