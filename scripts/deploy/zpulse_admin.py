#!/usr/bin/env python3
"""Authenticated z-pulse.cn administration helper.

Secrets are read at runtime from the user's local secrets.json and are never
accepted as command-line arguments or printed. The saved SSH host key is used
to reject unexpected servers.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import paramiko


DEFAULT_SECRETS = Path(r"C:\Users\Administrator\secrets.json")
DEFAULT_HOST = "z-pulse.cn"


def load_config(path: Path) -> dict[str, str]:
    data = json.loads(path.read_text(encoding="utf-8"))
    required = ("ZPULSE_SERVER_ROOT_PASSWORD", "ZPULSE_DEPLOY_KNOWN_HOSTS")
    missing = [key for key in required if not data.get(key)]
    if missing:
        raise RuntimeError(f"Missing required secret keys: {', '.join(missing)}")
    return data


def connect(config: dict[str, str], host: str) -> paramiko.SSHClient:
    client = paramiko.SSHClient()
    host_keys = paramiko.HostKeys()
    for raw_line in config["ZPULSE_DEPLOY_KNOWN_HOSTS"].splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        entry = paramiko.hostkeys.HostKeyEntry.from_line(line)
        if entry is None or entry.key is None:
            raise RuntimeError("Invalid saved SSH known_hosts entry")
        for hostname in entry.hostnames:
            host_keys.add(hostname, entry.key.get_name(), entry.key)
    client._host_keys = host_keys
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    client.connect(
        hostname=host,
        username="root",
        password=config["ZPULSE_SERVER_ROOT_PASSWORD"],
        timeout=15,
        banner_timeout=15,
        auth_timeout=15,
        allow_agent=False,
        look_for_keys=False,
    )
    return client


def run_script(client: paramiko.SSHClient, script: str, timeout: int) -> int:
    stdin, stdout, stderr = client.exec_command("bash -se", timeout=timeout)
    stdin.write(script)
    if not script.endswith("\n"):
        stdin.write("\n")
    stdin.channel.shutdown_write()
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    code = stdout.channel.recv_exit_status()
    if out:
        sys.stdout.write(out)
    if err:
        sys.stderr.write(err)
    return code


def upload(client: paramiko.SSHClient, local: Path, remote: str) -> int:
    with client.open_sftp() as sftp:
        sftp.put(str(local), remote)
    print(f"uploaded {local.name} -> {remote}")
    return 0


def download(client: paramiko.SSHClient, remote: str, local: Path) -> int:
    local.parent.mkdir(parents=True, exist_ok=True)
    with client.open_sftp() as sftp:
        sftp.get(remote, str(local))
    print(f"downloaded remote file -> {local.name}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--secrets", type=Path, default=DEFAULT_SECRETS)
    parser.add_argument("--host", default=DEFAULT_HOST)
    sub = parser.add_subparsers(dest="action", required=True)

    run_parser = sub.add_parser("run")
    run_parser.add_argument("--file", type=Path)
    run_parser.add_argument("--timeout", type=int, default=120)

    upload_parser = sub.add_parser("upload")
    upload_parser.add_argument("local", type=Path)
    upload_parser.add_argument("remote")

    download_parser = sub.add_parser("download")
    download_parser.add_argument("remote")
    download_parser.add_argument("local", type=Path)

    args = parser.parse_args()
    config = load_config(args.secrets)
    client = connect(config, args.host)
    try:
        if args.action == "run":
            script = args.file.read_text(encoding="utf-8") if args.file else sys.stdin.read()
            return run_script(client, script, args.timeout)
        if args.action == "upload":
            return upload(client, args.local, args.remote)
        if args.action == "download":
            return download(client, args.remote, args.local)
        raise AssertionError("unreachable")
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
