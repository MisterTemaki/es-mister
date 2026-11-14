#!/usr/bin/env python3
"""Summarize kernel_probe + existing renderer CSVs without guessing CPU clocks."""
import argparse
import bisect
import collections
import csv
import json
import math
from pathlib import Path
import statistics


def stats(values):
    values = sorted(values)
    if not values:
        return {"n": 0}
    return dict(n=len(values), median=round(statistics.median(values), 3),
                p95=round(values[math.ceil(.95 * len(values)) - 1], 3),
                p99=round(values[math.ceil(.99 * len(values)) - 1], 3),
                min=round(values[0], 3), max=round(values[-1], 3))


def integers_csv(path):
    with path.open() as source:
        return [{k: int(v) for k, v in row.items()} for row in csv.DictReader(source)]


def summarize(run):
    inputs = integers_csv(run / "trace.csv.input.csv")
    if not inputs:
        raise RuntimeError("No input responses: " + str(run))
    frames = collections.defaultdict(list)
    for row in integers_csv(run / "trace.csv"):
        frames[row["frame"]].append(row)
    first_event = inputs[0]["at_ns"] - inputs[0]["event_ns"]
    last_response = inputs[-1]["at_ns"]
    active = [rows for rows in frames.values()
              if rows[0]["at_ns"] >= first_event and rows[0]["redraw"] and rows[0]["start_ns"]]
    full = [rows for rows in active if not rows[0]["partial"]]

    def renderer(rows):
        values = collections.defaultdict(list)
        for workers in rows:
            f = workers[0]
            values["execute_ms"].append(f["execute_ns"] / 1e6)
            values["wake_max_ms"].append(max(w["start_ns"] - f["published_ns"] for w in workers) / 1e6)
            critical = max(workers, key=lambda w: w["drawn_ns"] - w["start_ns"])
            wall = critical["drawn_ns"] - critical["start_ns"]
            values["critical_draw_wall_ms"].append(wall / 1e6)
            if critical["cpu_draw_ns"] >= 0:
                values["same_worker_draw_cpu_ms"].append(critical["cpu_draw_ns"] / 1e6)
                values["same_worker_draw_off_cpu_ms"].append(max(0, wall - critical["cpu_draw_ns"]) / 1e6)
            values["copy_max_ms"].append(max(w["copied_ns"] - w["drawn_ns"] for w in workers) / 1e6)
        return {key: stats(v) for key, v in values.items()}

    probe = json.loads((run / "kernel-samples.json").read_text())
    samples = probe["rows"]
    selected = [r for r in samples if first_event <= r["at_ns"] <= last_response]
    result = dict(run=run.name, input_sdl_to_first_redraw_ms=stats([r["event_ns"] / 1e6 for r in inputs]),
                  input_dispatch_to_first_redraw_ms=stats([r["dispatch_ns"] / 1e6 for r in inputs]),
                  queue_before_dispatch_ms=stats([(r["event_ns"] - r["dispatch_ns"]) / 1e6 for r in inputs]),
                  active_renderer=renderer(active), full_renderer=renderer(full),
                  frame_trace=dict(frames=len(frames), active_after_first_input=len(active), full_after_first_input=len(full),
                                   first_at_ns=min(r[0]["at_ns"] for r in frames.values()),
                                   last_at_ns=max(r[0]["at_ns"] for r in frames.values())),
                  kernel_sample_count=len(samples), monitor_hit_limit=probe["hit_limit"],
                  cpufreq_policies_seen=sorted({p for row in samples for p in row["cpufreq"]}))
    ordered_frames = sorted((rows[0] for rows in frames.values()), key=lambda r: r["at_ns"])
    frame_times = [r["at_ns"] for r in ordered_frames]
    matched = []
    for answer in inputs:
        index = bisect.bisect_right(frame_times, answer["at_ns"]) - 1
        if index < 0:
            continue
        frame = ordered_frames[index]
        end = frame["finished_ns"] + frame["post_ns"] + frame["diagnostics_ns"] + 500000
        if frame["redraw"] and answer["at_ns"] <= end:
            matched.append(frame)
    result["first_response_redraws_within_frame_trace"] = dict(
        matched=len(matched), partial=sum(r["partial"] for r in matched),
        full=sum(not r["partial"] for r in matched),
        renderer_execute_ms=stats([r["execute_ns"] / 1e6 for r in matched]))
    if len(selected) >= 2:
        first, last = selected[0], selected[-1]
        result["between_first_and_last_input"] = dict(
            sampled_seconds=round((last["at_ns"] - first["at_ns"]) / 1e9, 3),
            available_memory_mib=stats([r["meminfo_kib"]["MemAvailable"] / 1024 for r in selected]),
            rss_mib=stats([r["process"]["rss_kib"] / 1024 for r in selected if r["process"]["rss_kib"] is not None]),
            swap_total_kib=sorted({r["meminfo_kib"]["SwapTotal"] for r in selected}),
            vmstat_deltas={k: last["vmstat"][k] - v for k, v in first["vmstat"].items()},
            process_major_faults=last["process"]["majflt"] - first["process"]["majflt"],
            process_read_bytes=last["process_io"].get("read_bytes", 0) - first["process_io"].get("read_bytes", 0))
    injections = run / "injections.json"
    if injections.exists():
        actions = json.loads(injections.read_text())
        if len(actions) != len(inputs):
            raise RuntimeError(f"Expected {len(actions)} input responses, got {len(inputs)}: {run}")
        # Different clocks: SDL's millisecond timestamp is reported separately from
        # monotonic uinput submission -> software framebuffer-copy completion.
        result["uinput_to_first_redraw_ms"] = stats([(answer["at_ns"] - action["inject_ns"]) / 1e6
                                                     for action, answer in zip(actions, inputs)])
        result["fixed_idle_seconds"] = stats([a["idle_ns"] / 1e9 for a in actions])
        percentages = []
        for action in actions:
            idle = [r for r in samples if action["idle_begin_ns"] <= r["at_ns"] < action["inject_ns"]]
            if len(idle) >= 2:
                a, b = idle[0], idle[-1]
                ticks = sum(b["process"][k] - a["process"][k] for k in ("utime_ticks", "stime_ticks"))
                percentages.append(100 * ticks / probe["clock_ticks_per_second"] /
                                   ((b["at_ns"] - a["at_ns"]) / 1e9))
        result["idle_process_cpu_percent_of_one_core"] = stats(percentages)
        result["small_sample_percentiles"] = "p95/p99 are descriptive nearest-rank; p99 equals max for n<100"
    # A 1 Hz monitor can associate coarse windows, not prove causality for a 100 ms pause.
    times = [r["at_ns"] for r in samples]
    windows = []
    for r in sorted(inputs, key=lambda x: x["event_ns"], reverse=True)[:10]:
        begin = max(0, bisect.bisect_right(times, r["at_ns"] - r["event_ns"] - 1000000000) - 1)
        end = min(len(samples) - 1, bisect.bisect_left(times, r["at_ns"] + 1000000000))
        a, b = samples[begin], samples[end]
        windows.append(dict(response_at_ns=r["at_ns"], latency_ms=round(r["event_ns"] / 1e6, 3),
                            vmstat_deltas={k: b["vmstat"][k] - v for k, v in a["vmstat"].items()},
                            process_major_faults=b["process"]["majflt"] - a["process"]["majflt"]))
    result["ten_slowest_input_windows_with_one_second_margin"] = windows
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runs", nargs="+", type=Path)
    args = parser.parse_args()
    for directory in args.runs:
        data = summarize(directory)
        (directory / "kernel-summary.json").write_text(json.dumps(data, indent=2))
        print(json.dumps(data, indent=2))
