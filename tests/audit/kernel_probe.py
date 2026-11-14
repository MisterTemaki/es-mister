#!/usr/bin/env python3
"""Read-only MiSTer kernel inventory and bounded /proc sampling (Python stdlib).

This tool does not load modules, change governors, drop caches or write sysctls.
Sampling stores rows in memory and writes JSON only after the process exits.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time


def read(path):
    try:
        return Path(path).read_text().strip()
    except (OSError, UnicodeError):
        return None


def fields(path):
    result = {}
    for line in (read(path) or "").splitlines():
        parts = line.replace(":", " ", 1).split()
        if len(parts) >= 2:
            try:
                result[parts[0]] = int(parts[1])
            except ValueError:
                pass
    return result


def policy_inventory():
    names = ("affected_cpus related_cpus scaling_driver scaling_governor "
             "scaling_available_governors scaling_available_frequencies "
             "scaling_min_freq scaling_max_freq cpuinfo_min_freq cpuinfo_max_freq "
             "scaling_cur_freq cpuinfo_cur_freq").split()
    paths = sorted(set(Path("/sys/devices/system/cpu/cpufreq").glob("policy*")) |
                   set(Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq")))
    return {str(p): {n: read(p / n) for n in names} for p in paths if p.is_dir()}


def inventory():
    config = None
    try:
        with gzip.open("/proc/config.gz", "rt") as source:
            prefixes = ("CONFIG_CPU_FREQ", "CONFIG_ARM_SOCFPGA_CPUFREQ", "CONFIG_CPU_IDLE",
                        "CONFIG_SWAP", "CONFIG_ZSWAP", "CONFIG_ZRAM", "CONFIG_PSI",
                        "CONFIG_PREEMPT", "CONFIG_HZ_", "CONFIG_SCHEDSTATS")
            config = [line.rstrip() for line in source if line.removeprefix("# ").startswith(prefixes)]
    except OSError:
        pass
    module = subprocess.run(["modinfo", "socfpga-cpufreq"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return dict(uname=platform.uname()._asdict(), monotonic_ns=time.monotonic_ns(),
                boot_id=read("/proc/sys/kernel/random/boot_id"), uptime=read("/proc/uptime"),
                cmdline=read("/proc/cmdline"), cpuinfo=read("/proc/cpuinfo"),
                online_cpus=read("/sys/devices/system/cpu/online"), cpufreq=policy_inventory(),
                kernel_config=config, module_metadata=module.stdout, modules=read("/proc/modules"),
                cpuidle={n: read("/sys/devices/system/cpu/cpuidle/" + n)
                         for n in ("current_driver", "current_governor_ro", "available_governors")},
                cpuidle_states={str(p): {n: read(p / n) for n in ("name", "desc", "latency", "usage", "time", "disable")}
                                for p in Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpuidle/state*")},
                meminfo_kib=fields("/proc/meminfo"), swaps=read("/proc/swaps"),
                vmstat=fields("/proc/vmstat"),
                vm_read_only={n: read("/proc/sys/vm/" + n) for n in ("swappiness", "overcommit_memory")},
                pressure={n: read("/proc/pressure/" + n) for n in ("cpu", "memory", "io")},
                clock_summary=read("/sys/kernel/debug/clk/clk_summary"))


def sample(pid, interval, limit):
    base = Path("/proc") / str(pid)
    rows = []
    start = time.monotonic()
    identity = None
    while len(rows) < limit:
        try:
            stat = (base / "stat").read_text().rsplit(")", 1)[1].split()
            if stat[0] == "Z" or (identity is not None and identity != stat[19]):
                break
            identity = stat[19]  # starttime, field 22; protects against PID reuse.
            status = fields(base / "status")
            row = dict(at_ns=time.monotonic_ns(), process={
                "state": stat[0], "minflt": int(stat[7]), "majflt": int(stat[9]),
                "utime_ticks": int(stat[11]), "stime_ticks": int(stat[12]),
                "rss_kib": status.get("VmRSS"), "hwm_kib": status.get("VmHWM"),
                "threads": status.get("Threads"),
                "main_voluntary_switches": status.get("voluntary_ctxt_switches"),
                "main_involuntary_switches": status.get("nonvoluntary_ctxt_switches")},
                meminfo_kib={k: v for k, v in fields("/proc/meminfo").items()
                             if k in ("MemAvailable", "MemFree", "Cached", "SwapTotal", "SwapFree", "Dirty", "Writeback")},
                vmstat={k: v for k, v in fields("/proc/vmstat").items()
                        if k.startswith(("pswp", "pgmajfault", "pgscan", "pgsteal", "allocstall", "oom_kill", "workingset_refault", "compact_stall"))},
                process_io=fields(base / "io"), process_schedstat=read(base / "schedstat"),
                cpu_stat=[line for line in (read("/proc/stat") or "").splitlines() if line.startswith("cpu")],
                cpufreq=policy_inventory())
            rows.append(row)
        except (FileNotFoundError, ProcessLookupError):
            break
        # Absolute deadlines avoid accumulating the cost of reading /proc.
        time.sleep(max(0, start + len(rows) * interval - time.monotonic()))
    return dict(pid=pid, clock_ticks_per_second=os.sysconf("SC_CLK_TCK"),
                interval_seconds=interval, sample_limit=limit, hit_limit=len(rows) == limit, rows=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("inventory", "sample"))
    parser.add_argument("--output", required=True)
    parser.add_argument("--pid", type=int)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--limit", type=int, default=3600)
    parser.add_argument("--binary")
    args = parser.parse_args()
    if args.interval < 0.25 or not 1 <= args.limit <= 7200:
        parser.error("interval >= 0.25 s and 1 <= limit <= 7200 required")
    if args.action == "sample" and (args.pid is None or args.pid <= 0):
        parser.error("sample requires a positive --pid")
    result = inventory() if args.action == "inventory" else sample(args.pid, args.interval, args.limit)
    if args.binary:
        result["binary_sha256"] = hashlib.sha256(Path(args.binary).read_bytes()).hexdigest()
    Path(args.output).write_text(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
