#!/usr/bin/env python3
"""Draws test_ui's draw ops (one a line) as a PNG, to look at a window on
the host: python3 tests/ui_render.py ops.txt window.png

Boxes, gradients, borders, marks and clips as olui_draw hands them over;
text in DejaVu Sans at each op's pixel size. Not the Amiga's drawing: that
is amiga/olui_amiga.c on OpenGfx. MIT, Copyright (c) 2026 AmiCradle Team.
"""
import sys

from PIL import Image, ImageDraw, ImageFont


def argb(h):
    v = int(h, 16)
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(4))


def main():
    src, out = sys.argv[1], sys.argv[2]
    img = None
    clip = None
    fonts = {}
    for line in open(src, encoding="utf-8"):
        f = line.rstrip("\n").split(" ")
        if f[0] == "size":
            img = Image.new("RGBA", (int(f[1]), int(f[2])), (255, 0, 255, 255))
            continue
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        if f[0] == "clip":
            x, y, w, h = map(int, f[1:5])
            clip = None if w <= 0 or h <= 0 else (x, y, x + w, y + h)
            continue
        if f[0] == "box":
            x, y, w, h, bw = map(int, f[1:6])
            border, radius, stops = argb(f[6]), int(f[7]), int(f[8])
            cols = [(argb(f[9 + 2 * i]), int(f[10 + 2 * i])) for i in range(stops)]
            if w <= 0 or h <= 0:
                continue
            mask = Image.new("L", img.size, 0)
            ImageDraw.Draw(mask).rounded_rectangle((x, y, x + w - 1, y + h - 1), radius=radius, fill=255)
            if stops == 1:
                d.rectangle((x, y, x + w - 1, y + h - 1), fill=cols[0][0])
            elif stops > 1:
                for row in range(h):
                    t = row * 1000 // max(h - 1, 1)
                    c = cols[-1][0]
                    for i in range(len(cols) - 1):
                        if cols[i][1] <= t <= cols[i + 1][1]:
                            span = max(cols[i + 1][1] - cols[i][1], 1)
                            c = mix(cols[i][0], cols[i + 1][0], (t - cols[i][1]) / span)
                            break
                    d.line((x, y + row, x + w - 1, y + row), fill=c)
            if bw:
                d.rounded_rectangle((x, y, x + w - 1, y + h - 1), radius=radius, outline=border, width=bw)
            layer.putalpha(Image.composite(layer.getchannel("A"), Image.new("L", img.size, 0), mask))
        elif f[0] == "text":
            x, base, size = int(f[1]), int(f[2]), int(f[3])
            col, flags = argb(f[4]), int(f[5])
            text = " ".join(f[6:])
            key = (size, flags & 1, flags & 8)
            if key not in fonts:
                name = "DejaVuSansMono" if flags & 8 else ("DejaVuSans-Bold" if flags & 1 else "DejaVuSans")
                fonts[key] = ImageFont.truetype(f"/usr/share/fonts/truetype/dejavu/{name}.ttf", size)
            d.text((x, base), text, font=fonts[key], fill=col, anchor="ls")
        elif f[0] == "mark":
            x, y, w, h, mark = map(int, f[1:6])
            col = argb(f[6])
            if mark == 1:
                d.line((x + 3, y + h // 2, x + w // 2 - 1, y + h - 4, x + w - 3, y + 3), fill=col, width=2)
            elif mark == 2:
                d.ellipse((x + 4, y + 4, x + w - 5, y + h - 5), fill=col)
            elif mark == 3:
                d.polygon([(x + 1, y + 2), (x + w - 2, y + 2), (x + w // 2, y + h - 2)], fill=col)
            elif mark == 4:
                d.polygon([(x + 2, y + 1), (x + w - 2, y + h // 2), (x + 2, y + h - 2)], fill=col)
            elif mark == 5:
                d.rectangle((x, y, x + w - 1, y + h - 1), fill=col)
        if clip:
            m = Image.new("L", img.size, 0)
            ImageDraw.Draw(m).rectangle((clip[0], clip[1], clip[2] - 1, clip[3] - 1), fill=255)
            a = layer.getchannel("A")
            layer.putalpha(Image.composite(a, Image.new("L", img.size, 0), m))
        img.alpha_composite(layer)
    img.convert("RGB").save(out)


if __name__ == "__main__":
    main()
