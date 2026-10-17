#!/usr/bin/env python3
"""Validate the shipped TLS template without listeners or a live game.

Requires Python 3, HAProxy (OpenSSL enabled), and openssl on PATH.
Only a private temporary self-signed fixture is generated; secrets are not logged.
"""
from pathlib import Path
import os
import re
import shutil
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=30)
    # Avoid dumping subprocess output: future versions could include key material.
    require(result.returncode == 0,
            Path(command[0]).name + " failed (exit " + str(result.returncode) + ")")


def main():
    for tool in ("haproxy", "openssl"):
        require(shutil.which(tool) is not None, tool + " is required on PATH")
    template = (ROOT / "deploy/tls/haproxy.cfg").read_text()
    lines = [line.split("#", 1)[0].strip() for line in template.splitlines()]
    lines = [line for line in lines if line]
    require([line for line in lines if line.startswith("bind ")] ==
            ["bind 0.0.0.0:4001 ssl crt /etc/haproxy/certs/dbzc.pem"],
            "exactly one public TLS-only bind is required")
    require("ssl-default-bind-options ssl-min-ver TLSv1.2 no-tls-tickets" in lines,
            "TLS 1.2 minimum and session ticket policy changed")
    require("user haproxy" in lines and "group haproxy" in lines,
            "production worker must use the distribution haproxy identity")
    require([line for line in lines if line.startswith("server ")] ==
            ["server game 127.0.0.1:4000 send-proxy maxconn 256"],
            "backend must be bounded loopback with PROXY v1")
    require(not any(re.search(r"\b(accept-proxy|send-proxy-v2|stats|"
                              r"hard-stop-after)\b", line) for line in lines),
            "unexpected trust boundary, admin interface or forced reload deadline")
    for directive in (
            "mode tcp", "maxconn 256", "maxsslconn 256",
            "timeout connect 3s", "timeout queue 5s", "timeout client-hs 10s",
            "timeout client 12h", "timeout server 12h", "timeout tunnel 12h",
            "timeout client-fin 10s", "timeout server-fin 10s",
            "stick-table type ip size 100k expire 1m store conn_cur,conn_rate(10s)",
            "tcp-request connection track-sc0 src",
            "tcp-request connection reject if { sc_conn_cur(0) gt 8 }",
            "tcp-request connection reject if { sc_conn_rate(0) gt 20 }",
            "default_backend dbzc_game"):
        require(directive in lines, "missing TLS/connection invariant: " + directive)

    unit = (ROOT / "deploy/tls/dbzc.service").read_text()
    for directive in (
            "User=dbzc", "Group=dbzc",
            "WorkingDirectory=/srv/dbzc/dbzc/area",
            "Environment=DBZC_BIND_ADDRESS=127.0.0.1",
            "Environment=DBZC_PROXY_PROTOCOL=1",
            "ExecStartPre=/bin/sh /srv/dbzc/scripts/preflight.sh /srv/dbzc/dbzc",
            "ExecStart=/srv/dbzc/dbzc/src/dbzc 4000",
            "Restart=on-failure", "ProtectSystem=strict",
            "ReadWritePaths=/srv/dbzc/dbzc /srv/dbzc/public_html",
            "ReadOnlyPaths=/srv/dbzc/dbzc/src /srv/dbzc/scripts"):
        require(directive in unit.splitlines(), "missing service invariant: " + directive)
    copyover = (ROOT / "dbzc/src/copyover.h").read_text()
    require(re.search(r'^\s*#define\s+EXE_FILE\s+"\.\./src/dbzc"\s*$',
                      copyover, re.M) is not None,
            "unit executable must match the game's working-directory copyover path")

    with tempfile.TemporaryDirectory(prefix="dbzc-tls-config-") as directory:
        tmp = Path(directory)
        tmp.chmod(0o700)
        key, cert, pem = tmp / "key.pem", tmp / "cert.pem", tmp / "dbzc.pem"
        # Create with restricted permissions even before invoking OpenSSL.
        for path in (key, cert, pem):
            path.touch(mode=0o600)
        run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
             "-days", "1", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost",
             "-keyout", str(key), "-out", str(cert)])
        pem.write_bytes(cert.read_bytes() + key.read_bytes())
        require(stat.S_IMODE(tmp.stat().st_mode) == 0o700,
                "fixture directory must be private")
        for path in (key, cert, pem):
            require(stat.S_IMODE(path.stat().st_mode) == 0o600,
                    "fixture PEM permissions must be private")
        require(template.count("/etc/haproxy/certs/dbzc.pem") == 1,
                "certificate substitution must affect only the configured PEM")
        config = tmp / "haproxy.cfg"
        fixture = template.replace("/etc/haproxy/certs/dbzc.pem", str(pem))
        # Nix/developer hosts may lack the distro-created haproxy account.
        # Substitute only process identity, not any transport/security settings.
        fixture = fixture.replace("    user haproxy\n", "    uid " + str(os.getuid()) + "\n")
        fixture = fixture.replace("    group haproxy\n", "    gid " + str(os.getgid()) + "\n")
        config.write_text(fixture)
        # -c parses the actual full template and loads the certificate, but
        # never binds public or loopback ports and never starts a daemon.
        run(["haproxy", "-c", "-f", str(config)])
    print("TLS template/service invariants and HAProxy certificate validation: passed")


if __name__ == "__main__":
    main()
