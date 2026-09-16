#!/usr/bin/env python3
"""Fetch the menu music: one SPC (a snapshot of the SNES sound chip's RAM and registers, played by
the emulated SPC700 + DSP, so it is the game's own music) from Josh W's archive -> roms/music/menu.spc.
Default: Super Mario World's overworld theme. Needs bsdtar (ships with macOS) to unpack the 7z.
Usage: tools/fetch_music.py [track-name-substring] [out]"""
import os, subprocess, sys, tempfile, urllib.request
URL = "https://spc.joshw.info/s/Super%20Mario%20World%20%5bSuper%20Mario%20World%20-%20Super%20Mario%20Bros.%204%5d%20(1990-11-21)(Nintendo%20EAD)(Nintendo)%5bSNES%5d.7z"
want = sys.argv[1] if len(sys.argv) > 1 else "Overworld"
out = sys.argv[2] if len(sys.argv) > 2 else "roms/music/menu.spc"
os.makedirs(os.path.dirname(out), exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    arc = os.path.join(tmp, "set.7z")
    open(arc, "wb").write(urllib.request.urlopen(URL, timeout=120).read())
    subprocess.check_call(["bsdtar", "-xf", arc, "-C", tmp])
    spcs = sorted(os.path.join(r, f) for r, _, fs in os.walk(tmp) for f in fs if f.lower().endswith(".spc"))
    print("tracks:", *[os.path.basename(p) for p in spcs], sep="\n  ")
    pick = next((p for p in spcs if want.lower() in os.path.basename(p).lower()), spcs[0])
    open(out, "wb").write(open(pick, "rb").read())
    print(f"wrote {out} from {os.path.basename(pick)}")
