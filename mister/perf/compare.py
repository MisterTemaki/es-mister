#!/usr/bin/env python3
"""Phase 3: f3.sh's runs grouped by kind and variant (res/KIND-TAG-N), each group put through analyze.py,
and the groups side by side.
   compare.py RESDIR [GROUP...]  ->  markdown on stdout, compare.json beside RESDIR
A group is KIND-TAG (grid-a1, boot-a6-sa5-ha7, ...); without GROUP arguments, all of them.
"""
import json, os, re, subprocess, sys, tempfile, glob

RES = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
ANALYZE = os.path.join(HERE, "analyze.py")

runs = {}
for path in sorted(glob.glob(os.path.join(os.path.abspath(RES), "*-*"))):
    if not os.path.isdir(path):
        continue
    m = re.match(r"(grid|list|ggrid|glist|boot|launch)-(.+)-(\d+)$", os.path.basename(path))
    if m:
        runs.setdefault(f"{m.group(1)}-{m.group(2)}", []).append((int(m.group(3)), path))

wanted = sys.argv[2:] or sorted(runs)
result = {}
for group in wanted:
    kind = group.split("-", 1)[0]
    if kind in ("ggrid", "glist"):
        continue
    with tempfile.TemporaryDirectory() as tmp:
        for n, path in runs.get(group, []):
            if kind == "grid":
                os.symlink(path, os.path.join(tmp, f"nav-grid-{n}"))
            elif kind == "list":
                os.symlink(path, os.path.join(tmp, f"nav-list-{n}"))
            elif kind == "boot":
                os.symlink(path, os.path.join(tmp, f"cold-{n}"))
            elif kind == "launch":
                with open(os.path.join(tmp, "launch.txt"), "a") as out:
                    out.write(open(os.path.join(path, "launch.txt")).read())
        subprocess.run([sys.executable, ANALYZE, tmp], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            result[group] = json.load(open(os.path.join(tmp, "summary.json")))
        except OSError:
            result[group] = {}

json.dump(result, open(os.path.join(RES, "compare.json"), "w"), indent=1)


def cell(s, key="p50"):
    if not s or s.get(key) is None:
        return "—"
    x = s[key]
    return f"{x:.1f}" if abs(x) < 100 else f"{x:.0f}"


def triple(s):
    if not s:
        return "—"
    return f"{cell(s)} / {cell(s, 'p95')} / {cell(s, 'p99')}"


def table(title, groups, rows):
    if not groups:
        return
    print(f"\n### {title}\n")
    print("| Metric (p50 / p95 / p99) | " + " | ".join(groups) + " |")
    print("|---|" + "---|" * len(groups))
    for label, get in rows:
        print(f"| {label} | " + " | ".join(triple(get(result[g])) for g in groups) + " |")


nav = {"grid": [g for g in result if g.startswith("grid-")], "list": [g for g in result if g.startswith("list-")]}
for view, groups in nav.items():
    rows = []
    phases = ["carousel-slow", "carousel-fast", "enter-list", "idle-list", "list-steps", "list-fast-scroll",
              "list-pages", "list-fast-back", "leave-list"]
    for ph in phases:
        for metric, label in (("answer", "answer ms"), ("work", "work/frame ms"), ("es", "ES/frame ms"),
                              ("raster", "drawing ms"), ("covers_ms", "covers complete ms"), ("dropped", "dropped/frame"),
                              ("allocs", "allocations/turn"), ("cpu_pct", "CPU %"), ("pictures", "pictures")):
            rows.append((f"{ph}: {label}", lambda r, ph=ph, metric=metric: r.get(view, {}).get(ph, {}).get(metric)))
    table(f"Navigation ({view})", groups, rows)

boots = [g for g in result if g.startswith("boot-")]
keys = ["script_to_ui_s", "script_to_first_frame_s", "pre_es_s", "es_systems_s", "sleep_s", "console_f9_s", "es_ready_s",
        "logo_ms", "load_systems_s", "window_init_s", "go_to_start_s"]
table("Cold start", boots, [(k, lambda r, k=k: r.get("boot", {}).get("cold", {}).get(k)) for k in keys])

launches = [g for g in result if g.startswith("launch-")]
table("Launch", launches, [(k, lambda r, k=k: r.get("launch", {}).get(k)) for k in ("core", "menu", "back")])
