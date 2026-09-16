#!/usr/bin/env python3
"""Download box art for every ROM in roms/ from libretro-thumbnails into roms/art/<rom name>.png.
The thumbnail set uses No-Intro names; GoodSNES-style names ("(U) (V1.2) [!]") are translated.
Already-present files are skipped. Usage: tools/fetch_art.py [rom_dir]"""
import os, sys, urllib.request, urllib.parse, zipfile

BASE = "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Super_Nintendo_Entertainment_System/master/Named_Boxarts/"

def rom_names(d):
    for fn in sorted(os.listdir(d)):
        p = os.path.join(d, fn)
        if fn.lower().endswith((".sfc", ".smc", ".zip")):
            yield fn.rsplit(".", 1)[0]   # the packer names ROMs after the file, zip or not

rom_dir = sys.argv[1] if len(sys.argv) > 1 else "roms"
art_dir = os.path.join(rom_dir, "art")
os.makedirs(art_dir, exist_ok=True)
names = list(rom_names(rom_dir))
if os.path.isdir(os.path.join(rom_dir, "disabled")):
    names += list(rom_names(os.path.join(rom_dir, "disabled")))
import re
def candidates(name):
    """the thumbnail set doesn't carry every No-Intro revision/region tag: try looser names"""
    yield name
    base = re.sub(r" \[[^\]]*\]", "", name)                 # [!] and friends
    base = re.sub(r" \(V[0-9.]+\)", "", base)               # (V1.2)
    base = re.sub(r" \(Rev [^)]*\)", "", base)
    base = base.replace("(U)", "(USA)").replace("(E)", "(Europe)").replace("(J)", "(Japan)").replace("_", " -")
    yield base
    region = re.search(r" \(([^)]*)\)", base)
    stem = base[:region.start()] if region else base
    for r in ("USA", "World", "Japan, USA", "USA, Europe"):
        yield f"{stem} ({r})"

for name in names:
    out = os.path.join(art_dir, name + ".png")
    if os.path.exists(out):
        continue
    for cand in candidates(name):
        url = BASE + urllib.parse.quote(cand + ".png")
        try:
            data = urllib.request.urlopen(url, timeout=30).read()
        except Exception:
            continue
        open(out, "wb").write(data)
        print(f"fetched {name}" + (f" (as {cand})" if cand != name else "") + f" ({len(data)//1024} KB)")
        break
    else:
        print(f"MISSING {name}: drop a PNG at {out}", file=sys.stderr)
