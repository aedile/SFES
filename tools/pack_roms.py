#!/usr/bin/env python3
"""Pack every .sfc/.smc (bare or inside a .zip) in a folder into one image for the 'roms' partition.

Layout (little-endian): "SFES" u32 count, count x { char name[48]; u32 offset; u32 size; u32 art_offset;
u16 art_w; u16 art_h; } (64 bytes), then the ROMs 16-byte aligned with any 512-byte copier header
dropped, then the box art. Art: <rom_dir>/art/<rom name>.png converted by artconv.py to 8-bit indices
into a 6x6x5 RGB cube, at most ART_W x ART_H; art_offset 0 = none. Usage: pack_roms.py <rom_dir> <out.bin>
"""
import os, struct, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import artconv
ENTRY = "<48sIIIHH"
ART_W, ART_H = 134, 96   # SNES boxes are landscape

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

def load_art(rom_dir, name):
    p = os.path.join(rom_dir, "art", name + ".png")
    if not os.path.exists(p):
        return 0, 0, b""
    try:
        return artconv.convert(open(p, "rb").read(), ART_W, ART_H)
    except Exception as e:
        print(f"pack_roms: art for {name} unusable ({e}), skipped", file=sys.stderr)
        return 0, 0, b""

def pack(roms, arts):
    hdr = 8 + len(roms) * struct.calcsize(ENTRY)
    off = (hdr + 15) & ~15
    offs, blob = [], b""
    for name, data in roms:
        offs.append(off)
        blob += data + b"\xff" * (-len(data) & 15)
        off += len(data) + (-len(data) & 15)
    art_offs = []
    for w, h, px in arts:
        art_offs.append(off if px else 0)
        blob += px + b"\xff" * (-len(px) & 15)
        off += len(px) + (-len(px) & 15)
    table = b"".join(struct.pack(ENTRY, name.encode("utf8")[:47], ro, len(data), ao, w, h)
                     for (name, data), ro, ao, (w, h, _) in zip(roms, offs, art_offs, arts))
    head = b"SFES" + struct.pack("<I", len(roms)) + table
    return head + b"\xff" * (-len(head) & 15) + blob

if __name__ == "__main__":
    rom_dir, out = sys.argv[1:3]
    roms = collect(rom_dir)
    arts = [load_art(rom_dir, n) for n, _ in roms]
    img = pack(roms, arts)
    open(out, "wb").write(img)
    for (name, data), (w, h, px) in zip(roms, arts):
        print(f"pack_roms: {name:48s} {len(data) // 1024:5d} KB  art {f'{w}x{h}' if px else 'none'}")
    print(f"pack_roms: {len(roms)} ROMs, {len(img) // 1024} KB -> {out}")
