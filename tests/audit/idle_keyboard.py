#!/usr/bin/env python3
"""Persistent uinput keyboard for first-input-after-idle tests on the MiSTer.

Start before the frontend so SDL's evdev scan sees it. The keyboard is removed on
exit. No configuration is changed. Use only with an isolated benchmark session.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import struct
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--trials", type=int, default=16)
    parser.add_argument("--idle", type=float, default=15.0)
    parser.add_argument("--startup", action="store_true", help="Inject once as soon as the first interactive frame is reported")
    parser.add_argument("--startup-log", choices=("startup.log", "es-startup.log"), default="startup.log")
    args = parser.parse_args()
    if args.startup:
        args.trials = 1
        args.idle = 0
    if not 1 <= args.trials <= 128 or (not args.startup and not 1 <= args.idle <= 120):
        parser.error("require 1..128 trials and 1..120 idle seconds")
    if Path("/tmp/CORENAME").read_text().strip() != "MENU":
        raise RuntimeError("MENU core required")
    args.run_dir.mkdir(parents=True, exist_ok=True)
    if (args.run_dir / "pid").exists():
        raise RuntimeError("Use a new run directory; keyboard must precede SDL initialization")
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    rows = []
    created = False
    try:
        for event_type in (0, 1):
            fcntl.ioctl(fd, 0x40045564, event_type)  # UI_SET_EVBIT
        for key in range(1, 256):
            fcntl.ioctl(fd, 0x40045565, key)  # UI_SET_KEYBIT; full keyboard classification.
        name = b"Codex performance keyboard"
        # BUS_VIRTUAL, no USB vendor identity. 1209:0001 is recognized as an
        # X-Gunner by Main_MiSTer and must not be borrowed by this keyboard.
        os.write(fd, struct.pack("80sHHHHI", name, 6, 0, 1, 1, 0) + bytes(4 * 64 * 4))
        fcntl.ioctl(fd, 0x5501)  # UI_DEV_CREATE
        created = True
        deadline = time.monotonic() + 5
        while name.decode() not in Path("/proc/bus/input/devices").read_text():
            if time.monotonic() > deadline:
                raise RuntimeError("uinput device did not become available")
            time.sleep(.05)
        (args.run_dir / "keyboard-ready").write_text(str(time.monotonic_ns()))
        print("keyboard ready before SDL initialization", flush=True)
        deadline = time.monotonic() + 120
        while True:
            try:
                marker = " first interactive frame shown" if args.startup else " ready"
                ready = any(line.rstrip().endswith(marker)
                            for line in (args.run_dir / args.startup_log).read_text().splitlines())
                pid = int((args.run_dir / "pid").read_text())
                if ready and Path("/proc", str(pid)).exists():
                    break
            except FileNotFoundError:
                pass
            if time.monotonic() > deadline:
                raise RuntimeError("frontend readiness timeout")
            time.sleep(.01 if args.startup else .05)
        ready_seen = time.monotonic_ns()
        if not args.startup:
            time.sleep(3)  # Allow initial animations to finish; separate from the fixed idle interval.

        def event(kind, code, value):
            # Native Linux input_event: 16 bytes on ARMv7, 24 on 64-bit hosts.
            sec, ns = divmod(time.time_ns(), 1000000000)
            os.write(fd, struct.pack("llHHi", sec, ns // 1000, kind, code, value))

        for trial in range(args.trials):
            if not Path("/proc", str(pid)).exists():
                raise RuntimeError("frontend exited before all trials")
            idle_begin = time.monotonic_ns()
            if args.idle:
                time.sleep(args.idle)
            at = time.monotonic_ns()
            key = 106 if trial % 2 == 0 else 105  # Linux KEY_RIGHT / KEY_LEFT.
            event(1, key, 1)
            event(0, 0, 0)
            time.sleep(.06)
            event(1, key, 0)
            event(0, 0, 0)
            rows.append(dict(trial=trial, idle_begin_ns=idle_begin, inject_ns=at,
                             idle_ns=at - idle_begin, key=key, ready_seen_ns=ready_seen,
                             mode="startup" if args.startup else "idle"))
            print("injected", trial + 1, "after", round((at - idle_begin) / 1e9, 6), "s idle", flush=True)
            time.sleep(1 if args.startup else 3)  # Capture subsequent animation work separately.
        print(urllib.request.urlopen("http://127.0.0.1:1234/quit", timeout=5).read().decode(), flush=True)
    finally:
        (args.run_dir / "injections.json").write_text(json.dumps(rows, indent=2))
        if created:
            fcntl.ioctl(fd, 0x5502)  # UI_DEV_DESTROY
        os.close(fd)


if __name__ == "__main__":
    main()
