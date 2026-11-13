#!/usr/bin/env python3
"""Copies an ES theme keeping only the art of the systems the MiSTer package shows, so the theme fits
the SD card comfortably (Carbon's art is 175 MB for 300 systems; a few dozen need a tenth of that).

    trim-theme.py THEME_DIR OUT_DIR SYSTEMS_TSV

In the theme's per-system art folders (art/background, art/consoles, art/logos, art/controllers), a
file is kept when its name starts with one of the systems' names (nes.jpg, nes-w.svg, nesen.png...)
or names something every theme screen uses (default, auto-*, collections...); everything else in the
theme is copied as it is. The .git folder is left out.

Pictures stb_image cannot read (the MiSTer build decodes with it) are saved again in a form it can:
a progressive JPEG as a baseline one, a WebP named .png or .jpg as what its name says (needs Pillow).
"""
import os
import re
import shutil
import sys

try:
    from PIL import Image
except ImportError:
    Image = None

PER_SYSTEM = {"art/background", "art/consoles", "art/logos", "art/controllers"}
SHARED = ("default", "auto", "all", "favorites", "favourites", "custom", "collection", "recent",
          "lastplayed", "retroachievements", "neverplayed", "multiplayer", "imageviewer", "windows",
          "screenshots", "library", "unknown")


def readable_by_stb(path):
    """Saves the picture again when stb_image could not read it; True when it was saved again"""
    if Image is None or not path.lower().endswith((".jpg", ".jpeg", ".png")):
        return False
    try:
        with Image.open(path) as image:
            fmt = image.format
            progressive = fmt == "JPEG" and (image.info.get("progressive") or image.info.get("progression"))
            wanted = "PNG" if path.lower().endswith(".png") else "JPEG"
            if fmt == wanted and not progressive:
                return False
            image.load()
            if wanted == "JPEG":
                image.convert("RGB").save(path, "JPEG", quality=92, progressive=False)
            else:
                image.save(path, "PNG")
            return True
    except Exception as error:
        print("theme: cannot check %s: %s" % (path, error))
        return False


def main():
    theme, out, tsv = sys.argv[1:4]
    systems = []
    with open(tsv) as f:
        for line in f:
            if line.strip() and not line.startswith("#"):
                systems.append(line.split("\t")[0])
    keep = re.compile(r"^(%s)([-_].*|en|jp|us|eu)?$" % "|".join(map(re.escape, systems + list(SHARED))), re.I)

    kept = dropped = resaved = 0
    for folder, dirs, files in os.walk(theme):
        rel = os.path.relpath(folder, theme)
        if rel == ".git" or rel.startswith(".git" + os.sep):
            continue
        dirs[:] = [d for d in dirs if d != ".git"]
        target = os.path.join(out, rel)
        os.makedirs(target, exist_ok=True)
        filter_here = rel.replace(os.sep, "/") in PER_SYSTEM
        for name in files:
            stem = os.path.splitext(name)[0]
            if filter_here and not keep.match(stem):
                dropped += 1
                continue
            shutil.copy2(os.path.join(folder, name), os.path.join(target, name))
            kept += 1
            if readable_by_stb(os.path.join(target, name)):
                resaved += 1
    print("theme: %d files kept, %d left out, %d pictures saved again for stb_image" % (kept, dropped, resaved))


if __name__ == "__main__":
    main()
