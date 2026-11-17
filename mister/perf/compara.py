#!/usr/bin/env python3
"""Phase 3: f3.sh's runs grouped by kind and variant (res/KIND-TAG-N), each group put through analisa.py,
and the groups side by side.
   compara.py RESDIR [GROUP...]  ->  markdown on stdout, compara.json beside RESDIR
A group is KIND-TAG (grid-a1, boot-a6-sa5-ha7, ...); without GROUP arguments, all of them.
"""
import json, os, re, subprocess, sys, tempfile, glob

RES = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
ANALISA = os.path.join(HERE, "analisa.py")

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
                os.symlink(path, os.path.join(tmp, f"frio-{n}"))
            elif kind == "launch":
                with open(os.path.join(tmp, "launch.txt"), "a") as out:
                    out.write(open(os.path.join(path, "launch.txt")).read())
        subprocess.run([sys.executable, ANALISA, tmp], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            result[group] = json.load(open(os.path.join(tmp, "resumo.json")))
        except OSError:
            result[group] = {}

json.dump(result, open(os.path.join(RES, "compara.json"), "w"), indent=1)


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
    print("| Métrica (p50 / p95 / p99) | " + " | ".join(groups) + " |")
    print("|---|" + "---|" * len(groups))
    for label, get in rows:
        print(f"| {label} | " + " | ".join(triple(get(result[g])) for g in groups) + " |")


nav = {"grid": [g for g in result if g.startswith("grid-")], "list": [g for g in result if g.startswith("list-")]}
for view, groups in nav.items():
    rows = []
    phases = ["carrossel-lento", "carrossel-rapido", "entrar-lista", "ocioso-lista", "lista-passos", "lista-rolagem-rapida",
              "lista-paginas", "lista-volta-rapida", "sair-lista"]
    for ph in phases:
        for metric, label in (("answer", "resposta ms"), ("work", "trabalho/quadro ms"), ("es", "ES/quadro ms"),
                              ("raster", "desenho ms"), ("covers_ms", "capas completas ms"), ("dropped", "perdidos/quadro"),
                              ("allocs", "alocações/volta"), ("cpu_pct", "CPU %"), ("pictures", "imagens")):
            rows.append((f"{ph}: {label}", lambda r, ph=ph, metric=metric: r.get(view, {}).get(ph, {}).get(metric)))
    table(f"Navegação ({view})", groups, rows)

boots = [g for g in result if g.startswith("boot-")]
keys = ["script_to_ui_s", "script_to_first_frame_s", "pre_es_s", "es_systems_s", "sleep_s", "console_f9_s", "es_ready_s",
        "logo_ms", "load_systems_s", "window_init_s", "go_to_start_s"]
table("Abertura a frio", boots, [(k, lambda r, k=k: r.get("boot", {}).get("frio", {}).get(k)) for k in keys])

launches = [g for g in result if g.startswith("launch-")]
table("Lançamento", launches, [(k, lambda r, k=k: r.get("launch", {}).get(k)) for k in ("core", "menu", "back")])
