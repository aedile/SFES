"""Pure-stdlib PNG -> 8-bit box-art converter used by pack_roms.py (no Pillow in the IDF image).

Output pixels index a fixed 6x6x5 RGB cube: index = r*30 + g*5 + b (r,g in 0..5, b in 0..4),
180 colours, which leaves the top of the palette for the UI. Ordered (Bayer 4x4) dithering
hides most of the banding at box-art size. Non-interlaced 8-bit PNGs only (what
libretro-thumbnails serves); anything else raises and the packer uses a placeholder.
"""
import struct, zlib

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

def decode_png(data):
    """-> (w, h, rows) with rows as lists of (r, g, b)."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, plte = 8, [], None
    while pos < len(data):
        ln, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif typ == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif typ == b"IDAT":
            idat.append(body)
        pos += 12 + ln
    assert depth == 8 and interlace == 0, f"unsupported PNG (depth {depth}, interlace {interlace})"
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    prev = bytearray(stride)
    rows = []
    p = 0
    for _ in range(h):
        f = raw[p]; cur = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: cur[i] = (cur[i] + a) & 255
            elif f == 2: cur[i] = (cur[i] + b) & 255
            elif f == 3: cur[i] = (cur[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        if ctype == 2: rows.append([tuple(cur[i:i + 3]) for i in range(0, stride, 3)])
        elif ctype == 6: rows.append([tuple(cur[i:i + 3]) for i in range(0, stride, 4)])
        elif ctype == 0: rows.append([(v, v, v) for v in cur])
        elif ctype == 4: rows.append([(cur[i],) * 3 for i in range(0, stride, 2)])
        elif ctype == 3: rows.append([plte[v] for v in cur])
        prev = cur
    return w, h, rows

def convert(png_bytes, out_w, out_h):
    """Resize (box filter) to fit inside out_w x out_h keeping aspect, then quantise.
    -> (w, h, bytes) of palette indices."""
    w, h, rows = decode_png(png_bytes)
    scale = min(out_w / w, out_h / h)
    ow, oh = max(1, int(w * scale)), max(1, int(h * scale))
    out = bytearray(ow * oh)
    for y in range(oh):
        y0, y1 = y * h // oh, max(y * h // oh + 1, (y + 1) * h // oh)
        for x in range(ow):
            x0, x1 = x * w // ow, max(x * w // ow + 1, (x + 1) * w // ow)
            r = g = b = n = 0
            for yy in range(y0, y1):
                row = rows[yy]
                for xx in range(x0, x1):
                    pr, pg, pb = row[xx]; r += pr; g += pg; b += pb; n += 1
            r, g, b = r / n, g / n, b / n
            d = (BAYER[y & 3][x & 3] + 0.5) / 16 - 0.5     # -0.47 .. +0.47 of one step
            ri = min(5, max(0, int(r / 51 + d + 0.5)))
            gi = min(5, max(0, int(g / 51 + d + 0.5)))
            bi = min(4, max(0, int(b / 63.75 + d + 0.5)))
            out[y * ow + x] = ri * 30 + gi * 5 + bi
    return ow, oh, bytes(out)

def palette_rgb(index):
    """The RGB the firmware assigns to a cube index (keep in sync with main/ui.c)."""
    r, g, b = index // 30, (index // 5) % 6, index % 5
    return r * 51, g * 51, b * 63

if __name__ == "__main__":
    import sys, time
    t = time.time()
    w, h, px = convert(open(sys.argv[1], "rb").read(), 96, 134)
    print(f"{w}x{h}, {len(px)} bytes, {len(set(px))} colours, {time.time()-t:.1f}s")
    # write a PGM-ish preview as PPM for eyeballing
    with open("/tmp/art_preview.ppm", "wb") as f:
        f.write(f"P6 {w} {h} 255\n".encode())
        for v in px: f.write(bytes(palette_rgb(v)))
