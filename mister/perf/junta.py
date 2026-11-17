#!/usr/bin/env python3
"""Phase 3: several f3.sh result folders put together for compara.py, each run renamed so that the same
variant in two batches stays two groups (KIND-TAG@BATCH-N), and runs listed to be merged into one group.
   junta.py OUT BATCH=RESDIR [BATCH=RESDIR...] [--as NEWTAG=BATCH:KIND-TAG ...]
Symbolic links only: the runs are not copied.
"""
import os, re, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
batches, renames = [], {}
args = sys.argv[2:]
i = 0
while i < len(args):
    if args[i] == "--as":
        new, old = args[i + 1].split("=", 1)
        renames[old] = new
        i += 2
        continue
    name, path = args[i].split("=", 1)
    batches.append((name, os.path.abspath(path)))
    i += 1

counters = {}
for batch, path in batches:
    for run in sorted(os.listdir(path)):
        m = re.match(r"(grid|list|ggrid|glist|boot|launch)-(.+)-(\d+)$", run)
        if not m or not os.path.isdir(os.path.join(path, run)):
            continue
        group = f"{m.group(1)}-{m.group(2)}"
        target = renames.get(f"{batch}:{group}")
        if target is None:
            target = f"{m.group(1)}-{m.group(2)}@{batch}"
        n = counters.get(target, 0) + 1
        counters[target] = n
        link = os.path.join(out, f"{target}-{n}")
        if not os.path.exists(link):
            os.symlink(os.path.join(path, run), link)
for k, v in sorted(counters.items()):
    print(f"{k}: {v}")
