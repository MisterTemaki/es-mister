#!/usr/bin/env python3
"""Phase 1 analysis: the batch's results (f1.sh) as percentiles.
   analisa.py RESDIR  ->  tables on stdout (markdown) and resumo.json beside them.
Definitions (all from the measurement build's lines, es-core/src/utils/Perf.h):
 - frame work: ES before the frame + the slowest band's drawing + its copy (F: es + raster + copy), ms
 - present interval: time between two frames drawn one after the other while the screen moves (F t deltas)
 - dropped frames: in a moving phase, the 60 Hz slots that ended without a new frame: round(interval/16.67) - 1
 - input to frame: a press to the first frame drawn after it (F answer), ms
 - covers after a page: a press to the last picture loaded before the next press (T lines), ms
 - framebuffer bytes: pixels copied to the framebuffer (counted at its size, 1920x1080) x 4 B
"""
import json, os, re, sys, glob, statistics

RES = sys.argv[1]
SLOT = 1000.0 / 60.0
SCREEN = 1280 * 720
FB_PER_PX = 4  # the copied pixels are counted at the framebuffer's size (1920x1080)


def pct(values, p):
    if not values:
        return None
    v = sorted(values)
    k = (len(v) - 1) * p
    lo = int(k)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def summary(values):
    if not values:
        return None
    return {"n": len(values), "p50": pct(values, 0.5), "p95": pct(values, 0.95), "p99": pct(values, 0.99), "max": max(values)}


def fmt(s, unit=""):
    if s is None:
        return "—"
    f = lambda x: f"{x:.1f}" if abs(x) < 100 else f"{x:.0f}"
    return f"{f(s['p50'])} / {f(s['p95'])} / {f(s['p99'])} / {f(s['max'])}{unit} (n={s['n']})"


def read_perf(path):
    L, F, T, M = [], [], [], []
    uptime0 = None
    for line in open(path):
        if line.startswith("#"):
            m = re.search(r"uptime ([\d.]+) s", line)
            if m:
                uptime0 = float(m.group(1))
            continue
        p = line.rstrip("\n").split(",")
        if p[0] == "L":
            L.append([float(x) for x in p[1:]])
        elif p[0] == "F":
            F.append([float(x) for x in p[1:]])
        elif p[0] == "T":
            T.append([float(x) for x in p[1:]])
        elif p[0] == "M":
            M.append((float(p[1]), p[2], float(p[3]), float(p[4]), int(p[5])))
    return uptime0, L, F, T, M


def phases(M, end):
    out = []
    for i, m in enumerate(M):
        t1 = M[i + 1][0] if i + 1 < len(M) else end
        cpu = (M[i + 1][2] + M[i + 1][3]) if i + 1 < len(M) else None
        out.append((m[1], m[0], t1, cpu))
    return out


def trace_times(path):
    d = {}
    if not os.path.exists(path):
        return d
    for line in open(path):
        m = re.match(r"\s+([\d.]+) \[\d+\] (.*)", line)
        if not m:
            continue
        t, s = float(m.group(1)), m.group(2)
        u = re.search(r"trace started \(uptime ([\d.]+) s\)", s)
        if u:
            d["uptime0"] = float(u.group(1))
        if s == "first frame shown":
            d.setdefault("first_frame", t)
        if s == "ready":
            d["ready"] = t
        for key, pat in (("load_systems", r"< load systems \(([\d.]+)"), ("go_to_start", r"< go to start \(([\d.]+)"),
                         ("window_init", r"< window init \(([\d.]+)"), ("group", r"< group systems and collections \(([\d.]+)"),
                         ("input_init", r"< input init \(([\d.]+)"), ("gamelist", r"< game list view: snes \(([\d.]+)"),
                         ("logo_ms", r"image loaded on the main thread: (\d+) ms, :/logo")):
            mm = re.search(pat, s)
            if mm:
                d.setdefault(key, float(mm.group(1)))
    return d


def script_times(path):
    """es-mister.sh's uptimes: start, es_systems.cfg ready, menu core, framebuffer asked, ES started"""
    d = {}
    if not os.path.exists(path):
        return d
    for line in open(path):
        for key, pat in (("start", r"es-mister.sh \(uptime ([\d.]+) s\)"), ("es_systems", r"es_systems.cfg ready \(uptime ([\d.]+)"),
                         ("menu", r"the menu core is loaded \(uptime ([\d.]+)"), ("asked", r"asking for the framebuffer \(uptime ([\d.]+)"),
                         ("es", r"starting ES: .*uptime ([\d.]+) s")):
            m = re.search(pat, line)
            if m and key not in d:
                d[key] = float(m.group(1))
    return d


def boot_metrics(run):
    s = script_times(os.path.join(run, "script.txt"))
    t = trace_times(os.path.join(run, "trace.txt"))
    if "start" not in s or "uptime0" not in t or "ready" not in t:
        return None
    m = {
        "script_to_ui_s": t["uptime0"] + t["ready"] - s["start"],
        "script_to_first_frame_s": t["uptime0"] + t.get("first_frame", 0) - s["start"],
        "pre_es_s": s.get("es", t["uptime0"]) - s["start"],
        "es_ready_s": t["ready"],
        "es_first_frame_s": t.get("first_frame"),
        "load_systems_s": t.get("load_systems"),
        "go_to_start_s": t.get("go_to_start"),
        "window_init_s": t.get("window_init"),
        "logo_ms": t.get("logo_ms"),
    }
    if "es_systems" in s:
        m["es_systems_s"] = s["es_systems"] - s["start"]
    if "menu" in s and "es_systems" in s:
        m["menu_wait_s"] = s["menu"] - s["es_systems"]
    if "asked" in s and "menu" in s:
        m["sleep_s"] = s["asked"] - s["menu"]
    if "es" in s and "asked" in s:
        m["console_f9_s"] = s["es"] - s["asked"]
    if "es" in s:
        m["exec_to_trace_s"] = t["uptime0"] - s["es"]
    return {k: v for k, v in m.items() if v is not None}


MOVING = {"carrossel-lento", "carrossel-rapido", "lista-passos", "lista-rolagem-rapida", "lista-paginas", "lista-volta-rapida", "telas"}
IDLE = {"ocioso-sistemas", "ocioso-lista"}


def nav_metrics(run, agg):
    path = os.path.join(run, "perf.csv")
    if not os.path.exists(path):
        return
    uptime0, L, F, T, M = read_perf(path)
    end = max([x[0] for x in L] + [0])
    for name, t0, t1, cpu in phases(M, end):
        a = agg.setdefault(name, {k: [] for k in ("work", "es", "raster", "copy", "interval", "dropped", "answer", "redraw_pct",
                                                  "fb_mb_s", "fb_kb_frame", "allocs", "reads", "writes", "turn", "cpu_pct", "covers_ms",
                                                  "picture_ms", "pictures", "frames_drawn_per_s")})
        fs = [f for f in F if t0 <= f[0] < t1]
        ls = [l for l in L if t0 <= l[0] < t1]
        ts = [x for x in T if t0 <= x[0] < t1]
        dur = (t1 - t0) / 1000.0
        for f in fs:
            a["work"].append(f[1] + f[2] + f[3])
            a["es"].append(f[1])
            a["raster"].append(f[2])
            a["copy"].append(f[3])
            a["redraw_pct"].append(100.0 * f[5] / SCREEN)
            a["fb_kb_frame"].append(f[4] * FB_PER_PX / 1024)
            if f[7] >= 0:
                a["answer"].append(f[7])
        if fs and dur > 0:
            a["fb_mb_s"].append(sum(f[4] for f in fs) * FB_PER_PX / 1e6 / dur)
            a["frames_drawn_per_s"].append(len(fs) / dur)
        if name in MOVING:
            for x, y in zip(fs, fs[1:]):
                iv = y[0] - x[0]
                if iv < 300:  # the screen still moving (a pause between presses is not a dropped frame)
                    a["interval"].append(iv)
                    a["dropped"].append(max(0, round(iv / SLOT) - 1))
        for l in ls:
            a["turn"].append(sum(l[1:7]))
            a["allocs"].append(l[7])
            a["reads"].append(l[8])
            a["writes"].append(l[9])
        if cpu is not None and dur > 0:
            a["cpu_pct"].append(100.0 * cpu / (dur * 1000.0))
        for x in ts:
            a["picture_ms"].append(x[1])
        a["pictures"].append(len(ts))
        # covers after a press: press time = answer frame t - answer; covers = last picture before the next press
        if name in ("lista-paginas", "lista-passos"):
            presses = [f[0] - f[7] for f in fs if f[7] >= 0]
            for i, p in enumerate(presses):
                nxt = presses[i + 1] if i + 1 < len(presses) else t1
                loaded = [x[0] for x in ts if p <= x[0] < nxt]
                if loaded:
                    a["covers_ms"].append(max(loaded) - p)


def mem_metrics(run):
    path = os.path.join(run, "mem.txt")
    rss, hwm = [], []
    if os.path.exists(path):
        for line in open(path):
            p = line.split()
            if len(p) == 3:
                (rss if p[1].startswith("VmRSS") else hwm).append(int(p[2]))
    return (max(rss) if rss else None, rss[-1] if rss else None, max(hwm) if hwm else None)


def main():
    out = {}
    lines = []
    boots = {"frio": [], "quente-grid": [], "quente-list": []}
    for run in sorted(glob.glob(os.path.join(RES, "*"))):
        name = os.path.basename(run)
        if not os.path.isdir(run):
            continue
        b = boot_metrics(run)
        if b is None:
            continue
        if name.startswith("frio-"):
            boots["frio"].append(b)
        elif name.startswith("nav-grid-"):
            boots["quente-grid"].append(b)
        elif name.startswith("nav-list-"):
            boots["quente-list"].append(b)
    lines.append("## Abertura (s) — p50 / p95 / p99 / máx\n")
    keys = ["script_to_ui_s", "pre_es_s", "es_systems_s", "menu_wait_s", "sleep_s", "console_f9_s", "exec_to_trace_s",
            "es_first_frame_s", "logo_ms", "window_init_s", "load_systems_s", "go_to_start_s", "es_ready_s", "script_to_first_frame_s"]
    lines.append("| Métrica | " + " | ".join(boots) + " |")
    lines.append("|---|" + "---|" * len(boots))
    for k in keys:
        row = [fmt(summary([b[k] for b in v if k in b])) for v in boots.values()]
        lines.append(f"| {k} | " + " | ".join(row) + " |")
    out["boot"] = {kind: {k: summary([b[k] for b in v if k in b]) for k in keys} for kind, v in boots.items()}

    for view in ("grid", "list"):
        agg = {}
        mems = []
        for run in sorted(glob.glob(os.path.join(RES, f"nav-{view}-*"))):
            nav_metrics(run, agg)
            mems.append(mem_metrics(run))
        if not agg:
            continue
        lines.append(f"\n## Navegação, vista {view} ({len(mems)} execuções) — p50 / p95 / p99 / máx\n")
        metrics = ["work", "es", "raster", "copy", "interval", "dropped", "answer", "covers_ms", "picture_ms", "redraw_pct", "fb_kb_frame",
                   "fb_mb_s", "frames_drawn_per_s", "turn", "allocs", "reads", "writes", "cpu_pct", "pictures"]
        lines.append("| Fase | " + " | ".join(metrics) + " |")
        lines.append("|---|" + "---|" * len(metrics))
        out[view] = {}
        for ph, a in agg.items():
            row = []
            out[view][ph] = {}
            for k in metrics:
                s = summary(a[k])
                out[view][ph][k] = s
                if k == "dropped" and a[k]:
                    s2 = {"n": len(a[k]), "p50": sum(a[k]), "p95": 100.0 * sum(a[k]) / max(1, sum(a[k]) + len(a[k])), "p99": 0, "max": max(a[k])}
                    row.append(f"{sum(a[k])} perdidos em {len(a[k])} quadros ({s2['p95']:.0f}%)")
                else:
                    row.append(fmt(s))
            lines.append(f"| {ph} | " + " | ".join(row) + " |")
        hw = [m[2] for m in mems if m[2]]
        rs = [m[1] for m in mems if m[1]]
        lines.append(f"\nMemória: VmHWM {fmt(summary([h / 1024 for h in hw]))} MB; VmRSS no fim {fmt(summary([r / 1024 for r in rs]))} MB")
        out[view]["mem_hwm_mb"] = summary([h / 1024 for h in hw])

    launch = os.path.join(RES, "launch.txt")
    if os.path.exists(launch):
        L, B, D = [], [], []
        for line in open(launch):
            m = re.search(r"start ([\d.]+) core ([\d.]*) menu-asked ([\d.]+) menu ([\d.]*) script-done ([\d.]+)", line)
            if m and m.group(2) and m.group(4):
                s, c, ma, mn, d = (float(x) for x in m.groups())
                L.append(c - s)
                B.append(mn - ma)
                D.append(d - mn)
        lines.append("\n## Lançamento de core (s) — p50 / p95 / p99 / máx\n")
        lines.append(f"- comando do ES até o core carregado (CORENAME): {fmt(summary(L))}")
        lines.append(f"- load_core do menu até MENU: {fmt(summary(B))}")
        lines.append(f"- MENU até o mister-launch.sh devolver a tela ao ES: {fmt(summary(D))}")
        out["launch"] = {"core": summary(L), "menu": summary(B), "back": summary(D)}

    print("\n".join(lines))
    json.dump(out, open(os.path.join(RES, "resumo.json"), "w"), indent=1)


main()
