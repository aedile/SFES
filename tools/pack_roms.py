#!/usr/bin/env python3
"""Pack every .sfc/.smc (bare or inside a .zip) in a folder into one image for the 'roms' partition.

Layout (little-endian): "SFES" u32 count, count x { char name[48]; u32 offset; u32 size; }, then the
ROMs 16-byte aligned with any 512-byte copier header dropped. Usage: pack_roms.py <rom_dir> <out.bin>
"""
import os, struct, sys, zipfile
ENTRY = "<48sII"

def rom_bytes(data):
    return data[512:] if len(data) % 0x8000 == 512 else data

def collect(rom_dir):
    roms = []
    for fn in sorted(os.listdir(rom_dir), key=str.lower):
        path = os.path.join(rom_dir, fn)
        if fn.lower().endswith((".sfc", ".smc")):
            roms.append((fn.rsplit(".", 1)[0], rom_bytes(open(path, "rb").read())))
        elif fn.lower().endswith(".zip"):
            with zipfile.ZipFile(path) as zf:
                names = [n for n in zf.namelist() if n.lower().endswith((".sfc", ".smc"))]
                if names:
                    roms.append((fn[:-4], rom_bytes(zf.read(names[0]))))
                else:
                    print(f"pack_roms: {fn}: no .sfc/.smc inside, skipped", file=sys.stderr)
    return roms

def pack(roms):
    hdr = 8 + len(roms) * struct.calcsize(ENTRY)
    off = (hdr + 15) & ~15
    table, blob = b"", b""
    for name, data in roms:
        table += struct.pack(ENTRY, name.encode("utf8")[:47], off, len(data))
        blob += data + b"\xff" * (-len(data) & 15)
        off += len(data) + (-len(data) & 15)
    head = b"SFES" + struct.pack("<I", len(roms)) + table
    return head + b"\xff" * (-len(head) & 15) + blob

if __name__ == "__main__":
    rom_dir, out = sys.argv[1:3]
    roms = collect(rom_dir)
    img = pack(roms)
    open(out, "wb").write(img)
    for name, data in roms:
        print(f"pack_roms: {name:48s} {len(data) // 1024:5d} KB")
    print(f"pack_roms: {len(roms)} ROMs, {len(img) // 1024} KB -> {out}")
