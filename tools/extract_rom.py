#!/usr/bin/env python3
"""Copy a bare .sfc/.smc, or the first one inside a .zip, to <out>, dropping a 512-byte copier header. Usage: extract_rom.py <rom or zip> <out>"""
import sys, zipfile
src, out = sys.argv[1:3]
if src.lower().endswith(".zip"):
    with zipfile.ZipFile(src) as zf:
        name = next(n for n in zf.namelist() if n.lower().endswith((".sfc", ".smc")))
        data = zf.read(name)
else:
    data = open(src, "rb").read()
if len(data) % 0x8000 == 512:
    data = data[512:]
open(out, "wb").write(data)
print(f"extract_rom: {src} -> {out} ({len(data)} bytes)")
