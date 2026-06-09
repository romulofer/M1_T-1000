#!/usr/bin/env python3
"""Preview + downscale the 14x14 main-menu icons to a smaller square size."""
import re, sys

SRC = "m1_csrc/m1_display_data.c"
ICONS = ["bluetooth","gpio","infrared","nfc","rfid","wave","setting",
         "wifi","badusb","games","apps"]
SW = SH = 14

def load(name):
    txt = open(SRC).read()
    m = re.search(r"const uint8_t menu_m1_icon_%s\[\]\s*=\s*\{(.*?)\};" % name, txt, re.S)
    vals = re.findall(r"0x[0-9a-fA-F]+", m.group(1))
    return [int(v,16) for v in vals]

def to_grid(data,w,h):
    rb=(w+7)//8; g=[[0]*w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            g[y][x]=(data[y*rb+(x>>3)]>>(x&7))&1
    return g

def show(g,w,h,pad=""):
    for y in range(h):
        print(pad+"".join("#" if g[y][x] else "." for x in range(w)))

def downscale(g,sw,sh,dw,dh,thresh):
    out=[[0]*dw for _ in range(dh)]
    for dy in range(dh):
        y0=dy*sh//dh; y1=max(y0+1,(dy+1)*sh//dh)
        for dx in range(dw):
            x0=dx*sw//dw; x1=max(x0+1,(dx+1)*sw//dw)
            on=tot=0
            for yy in range(y0,y1):
                for xx in range(x0,x1):
                    tot+=1; on+=g[yy][xx]
            out[dy][dx]=1 if on/tot>=thresh else 0
    return out

def emit(g,w,h,name):
    rb=(w+7)//8; out=[]
    for y in range(h):
        for b in range(rb):
            v=0
            for bit in range(8):
                x=b*8+bit
                if x<w and g[y][x]: v|=(1<<bit)
            out.append(v)
    print("const uint8_t menu_m1_icon_%s[] = {" % name)
    print("\t"+", ".join("0x%02x"%b for b in out))
    print("};")

dw=int(sys.argv[1]); dh=int(sys.argv[2]); th=float(sys.argv[3]) if len(sys.argv)>3 else 0.35
mode=sys.argv[4] if len(sys.argv)>4 else "preview"
for name in ICONS:
    g=to_grid(load(name),SW,SH)
    small=downscale(g,SW,SH,dw,dh,th)
    if mode=="emit":
        emit(small,dw,dh,name)
    else:
        print("== %s ==  (orig 14 -> %dx%d, th %.2f)" % (name,dw,dh,th))
        for y in range(max(SH,dh)):
            l = "".join("#" if y<SH and g[y][x] else " " for x in range(SW)) if True else ""
            r = "".join("#" if y<dh and small[y][x] else " " for x in range(dw))
            ol="".join("#" if y<SH and g[y][x] else "." for x in range(SW))
            rl="".join("#" if y<dh and small[y][x] else "." for x in range(dw))
            print("  %-16s   %s" % (ol if y<SH else "", rl if y<dh else ""))
        print()
