#!/usr/bin/env python3
"""Decode the m1_device_82x36 XBM array and emit a downscaled variant."""
import re, sys

SRC = "m1_csrc/m1_display_data.c"
NAME = "m1_device_82x36"
SW, SH = 82, 36

def load_bytes():
    txt = open(SRC).read()
    m = re.search(r"const uint8_t %s\[\]\s*=\s*\{(.*?)\};" % NAME, txt, re.S)
    body = m.group(1)
    vals = re.findall(r"0x[0-9a-fA-F]+", body)
    return [int(v, 16) for v in vals]

def to_grid(data, w, h):
    row_bytes = (w + 7) // 8
    g = [[0]*w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            byte = data[y*row_bytes + (x >> 3)]
            g[y][x] = (byte >> (x & 7)) & 1   # XBM: LSB = leftmost
    return g

def show(g, w, h):
    for y in range(h):
        print("".join("#" if g[y][x] else " " for x in range(w)))

def downscale(g, sw, sh, dw, dh, thresh):
    """Box downscale: output pixel on if coverage fraction >= thresh."""
    out = [[0]*dw for _ in range(dh)]
    for dy in range(dh):
        y0 = dy*sh//dh; y1 = max(y0+1, (dy+1)*sh//dh)
        for dx in range(dw):
            x0 = dx*sw//dw; x1 = max(x0+1, (dx+1)*sw//dw)
            on = tot = 0
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    tot += 1; on += g[yy][xx]
            out[dy][dx] = 1 if (on/tot) >= thresh else 0
    return out

def emit(g, w, h, name):
    row_bytes = (w + 7) // 8
    out = []
    for y in range(h):
        for b in range(row_bytes):
            v = 0
            for bit in range(8):
                x = b*8 + bit
                if x < w and g[y][x]:
                    v |= (1 << bit)
            out.append(v)
    lines = []
    for i in range(0, len(out), 12):
        lines.append("\t" + ", ".join("0x%02x" % b for b in out[i:i+12]) + ",")
    text = "\t" + name.replace("const uint8_t ", "") + "\n"
    body = "\n".join(lines).rstrip(",")
    print("const uint8_t %s[] = {" % name)
    print(body)
    print("};")

data = load_bytes()
g = to_grid(data, SW, SH)
dw, dh = int(sys.argv[1]), int(sys.argv[2])
thresh = float(sys.argv[3]) if len(sys.argv) > 3 else 0.35
print("=== ORIGINAL %dx%d ===" % (SW, SH))
show(g, SW, SH)
small = downscale(g, SW, SH, dw, dh, thresh)
print("\n=== SCALED %dx%d (thresh %.2f) ===" % (dw, dh, thresh))
show(small, dw, dh)
print()
emit(small, dw, dh, "m1_device_%dx%d" % (dw, dh))
