#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Builds resources/scrapers/libretro/<repository>.tsv for the "Libretro-thumbnails" scraper: per system, the games
libretro-thumbnails has no cover for but an online database has, with where that cover is and the CRC32 of the
game's dumps. (The same table as Snes9x PS5's data/snes-db-art.tsv, made by its tools/make_db_art.py, for every
system of resources/scrapers/libretro/systems.tsv.)

Sources, none needing an account or a key:
  * Hasheous' public dump of the system (https://hasheous.org/api/v1/Dumps/platforms/<name>.zip): one JSON file per
    game with its name, its cover (an IGDB image Hasheous serves at https://hasheous.org/api/v1/images/<id>), its
    TheGamesDB id (front box art: https://cdn.thegamesdb.net/images/original/boxart/front/<id>-1.jpg) and its ROMs;
  * libretro-database's No-Intro DAT of the system (CRC32 -> name), when it has one;
  * the names of libretro-thumbnails' Named_Boxarts (git ls-tree of a clone without file contents).

A row, tab separated: the game's name; its cover sources, best first ("h<image id>" for Hasheous, "t<id>" for
TheGamesDB); the CRC32 of its dumps, comma apart, upper case (none for disc systems: ES does not read discs).
Left out: a game with no cover source, and a game libretro-thumbnails has (one of its dumps is in the DAT under a
name it has a box art for, or its name, without tags, is the name of one of its box arts).

  python3 mister/tools/make-db-art.py [--work DIR] [--only snes,megadrive]     (from the repository's root)
"""
import argparse
import io
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "resources", "scrapers", "libretro")
DUMP = "https://hasheous.org/api/v1/Dumps/platforms/%s.zip"
DAT = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/%s.dat"
THUMBS = "https://github.com/libretro-thumbnails/%s"
ARCHIVE = re.compile(r"\.(zip|7z|rar|gz)$", re.I)
CRC = re.compile(r"[0-9a-f]{8}")
IMAGE_ID = re.compile(r"[0-9A-Fa-f]{40}")
ROMAN = {"ii": 2, "iii": 3, "iv": 4, "vi": 6, "vii": 7, "viii": 8, "ix": 9, "xi": 11, "xii": 12, "xiii": 13}
STOP = {"the", "a", "an", "of", "and", "no", "in", "at", "for", "to"}


def key(name):
    """The words of a name without its tags, as one string: two names of one game (any region, revision) agree."""
    title = re.sub(r"[\(\[\{][^\)\]\}]*[\)\]\}]", " ", name)
    title = title.replace("'", "")
    words = re.findall(r"[a-z]+|[0-9]+", title.lower())
    out = []
    for i, w in enumerate(words):
        if w.isdigit():
            out.append(str(int(w)))
        elif w in ROMAN:
            out.append(str(ROMAN[w]))
        elif w in STOP:
            continue
        else:
            out.append(w)
    return "".join(out)


def fetch(url, path):
    if not os.path.exists(path):
        print("  " + url, file=sys.stderr)
        request = urllib.request.Request(url, headers={"User-Agent": "es-mister make-db-art"})
        with urllib.request.urlopen(request, timeout=600) as r:
            data = r.read()
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
    with open(path, "rb") as f:
        return f.read()


def boxart_names(repo, work):
    clone = os.path.join(work, "thumbs", repo)
    if not os.path.isdir(clone):
        subprocess.run(["git", "clone", "-q", "--filter=blob:none", "--no-checkout", "--depth", "1", THUMBS % repo, clone],
                       check=True)
    out = subprocess.run(["git", "-C", clone, "ls-tree", "--name-only", "HEAD", "Named_Boxarts/"], check=True,
                         capture_output=True, text=True).stdout
    names = set()
    for line in out.splitlines():
        line = line.strip().strip('"')
        if line.startswith("Named_Boxarts/") and line.lower().endswith(".png"):
            names.add(line[len("Named_Boxarts/"):-4])
    return names


def nointro(name, work):
    crcs = {}
    text = fetch(DAT % urllib.parse.quote(name), os.path.join(work, "dat", name + ".dat")).decode("utf-8", "replace")
    game = None
    for line in text.splitlines():
        m = re.match(r'\s*name "(.*)"\s*$', line)
        if m and game is None:
            game = m.group(1)
            continue
        if line.startswith(")"):
            game = None
            continue
        m = re.search(r"\bcrc ([0-9A-Fa-f]{8})\b", line)
        if m and game is not None:
            crcs.setdefault(m.group(1).lower(), game)
    return crcs


def thumbnail_name(name):
    return re.sub(r'[&*/:`<>?\\|"]', "_", name)


def build(repo_name, has_dat, dump, work):
    repo = repo_name.replace(" ", "_")
    names = boxart_names(repo, work)
    keys = {key(n) for n in names}
    crc_names = nointro(repo_name, work) if has_dat else {}
    data = fetch(DUMP % urllib.parse.quote(dump), os.path.join(work, "hasheous", dump + ".zip"))
    z = zipfile.ZipFile(io.BytesIO(data))
    rows = {}
    for member in sorted(z.namelist()):
        try:
            game = json.loads(z.read(member))
        except ValueError:
            continue
        name = (game.get("Name") or "").replace("\t", " ").replace("\n", " ").strip()
        attrs = {a.get("attributeName"): a.get("Value") for a in game.get("Attributes", []) or []}
        sources = []
        logo = attrs.get("Logo") or ""
        if isinstance(logo, str) and IMAGE_ID.fullmatch(logo):
            sources.append("h" + logo.upper())
        for m in game.get("Metadata") or []:
            if m.get("Source") == "TheGamesDb" and m.get("Status") == "Mapped" and re.fullmatch(r"\d+", m.get("Id") or ""):
                sources.append("t" + m["Id"])
        crcs = set()
        if has_dat:
            for rom in attrs.get("ROMs") or []:
                crc = (rom.get("Crc") or "").lower()
                if CRC.fullmatch(crc) and (rom.get("Size") or 0) > 0 and not ARCHIVE.search(rom.get("Name") or ""):
                    crcs.add(crc)
        if not name or not sources or (has_dat and not crcs):
            continue
        if any(thumbnail_name(crc_names.get(c, "")) in names for c in crcs):
            continue  # libretro-thumbnails has this dump's game, found by its CRC
        if key(name) in keys:
            continue  # or by its name
        row = rows.setdefault(name, [[], set()])
        for s in sources:
            if s not in row[0]:
                row[0].append(s)
        row[1] |= crcs
    path = os.path.join(OUT, repo + ".tsv")
    with open(path, "w", encoding="utf-8", newline="\n") as out:
        out.write("# %s: games libretro-thumbnails has no cover for, with where an online database has one,\n" % repo_name)
        out.write("# from Hasheous' dump \"%s\"; made by mister/tools/make-db-art.py.\n" % dump)
        out.write("# name<TAB>cover sources (h = hasheous.org image id, t = TheGamesDB id)<TAB>CRC32 of its dumps\n")
        for name in sorted(rows):
            sources, crcs = rows[name]
            out.write("%s\t%s\t%s\n" % (name, " ".join(sources), ",".join(c.upper() for c in sorted(crcs))))
    print("%-48s %5d games (libretro: %d box arts)" % (repo, len(rows), len(names)), file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", default=os.path.join(ROOT, "build-db-art"), help="downloads and clones are kept here")
    ap.add_argument("--only", default="", help="systems (first names of systems.tsv), comma apart")
    args = ap.parse_args()
    only = {s for s in args.only.split(",") if s}
    for sub in ("thumbs", "dat", "hasheous"):
        os.makedirs(os.path.join(args.work, sub), exist_ok=True)
    done = set()
    with open(os.path.join(OUT, "systems.tsv"), encoding="utf-8") as f:
        for line in f:
            if not line.strip() or line[0] == "#":
                continue
            names, repo_name, has_dat, dump = line.rstrip("\n").split("\t")[:4]
            first = names.split(",")[0].split("/")[0]
            if dump == "-" or repo_name in done or (only and first not in only):
                continue
            done.add(repo_name)
            build(repo_name, has_dat == "1", dump, args.work)


if __name__ == "__main__":
    main()
