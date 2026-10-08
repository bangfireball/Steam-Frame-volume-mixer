#!/usr/bin/env python3
"""Read-only Frame audio metadata probe. Requires local Paramiko; writes only out/.

Credentials are read locally from .env and never printed or passed as process args.
Remote commands are fixed read-only queries. Existing OpenSSH host keys are required.
Audio samples are never captured. Saved metadata can include media/app titles.
"""
import datetime
import json
import pathlib
import re

import paramiko

ROOT = pathlib.Path(__file__).resolve().parents[1]


def connection_values():
    values = {}
    for line in (ROOT / ".env").read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$", line)
        if not match:
            continue
        value = match.group(2).strip()
        if len(value) >= 2 and value[0] in "\"'" and value[-1] == value[0]:
            value = value[1:-1]
        values[match.group(1)] = value
    for name in ("STEAM_FRAME_IP", "STEAM_FRAME_USER", "STEAM_FRAME_SSH_PASSWORD"):
        if not values.get(name):
            raise RuntimeError(f"Missing connection key: {name}")
    return values


def main():
    values = connection_values()
    output = ROOT / "out" / "diagnostics" / ("frame-audio-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    output.mkdir(parents=True, exist_ok=True)
    queries = {
        "server.json": "pactl --format=json info",
        "sink-inputs.json": "pactl --format=json list sink-inputs",
        "clients.json": "pactl --format=json list clients",
        "sinks.json": "pactl --format=json list sinks",
        "pipewire.json": "pw-dump",
        "wireplumber.txt": "wpctl status -n",
        "processes.txt": "ps -eo pid,ppid,comm",
    }
    with paramiko.SSHClient() as ssh:
        ssh.load_host_keys(str(pathlib.Path.home() / ".ssh" / "known_hosts"))
        ssh.set_missing_host_key_policy(paramiko.RejectPolicy())
        ssh.connect(values["STEAM_FRAME_IP"], username=values["STEAM_FRAME_USER"],
                    password=values["STEAM_FRAME_SSH_PASSWORD"], look_for_keys=False,
                    allow_agent=False, timeout=15, auth_timeout=15, banner_timeout=15)
        for filename, command in queries.items():
            _, stdout, stderr = ssh.exec_command(command, timeout=20)
            data = stdout.read().decode("utf-8", errors="replace")
            errors = stderr.read().decode("utf-8", errors="replace")
            code = stdout.channel.recv_exit_status()
            (output / filename).write_text(data, encoding="utf-8")
            if errors:
                (output / (filename + ".stderr")).write_text(errors, encoding="utf-8")
            print(f"{filename}: exit {code}, {len(data)} characters")
    streams = json.loads((output / "sink-inputs.json").read_text(encoding="utf-8"))
    for stream in streams:
        props = stream.get("properties", {})
        # Bound console output; full unredacted metadata stays in ignored diagnostics.
        selected = {k: props[k] for k in (
            "application.name", "application.id", "application.process.binary",
            "application.process.id", "node.name", "node.id", "object.id",
            "object.serial", "client.id", "target.object", "media.class", "media.role",
            "stream.is-live", "node.virtual") if k in props}
        print(json.dumps({"index": stream.get("index"), "client": stream.get("client"),
                          "sink": stream.get("sink"), "corked": stream.get("corked"),
                          "properties": selected}, ensure_ascii=False))
    print("Saved local metadata:", output.relative_to(ROOT))


if __name__ == "__main__":
    main()
