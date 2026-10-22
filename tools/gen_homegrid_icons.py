#!/usr/bin/env python3
"""Generate apps/homegrid_icons.h: eight 20x20 one-bit icons for the Zen X-Fi
icon home page (apps/homegrid.c).  Each icon is 20 rows of a 32-bit word,
bit 19 is the leftmost pixel.  The firmware draws every set pixel as a 2x2
square in the theme's colours, so the icons follow the active theme.

Usage: tools/gen_homegrid_icons.py [--preview] > apps/homegrid_icons.h
"""
import math
import sys

N = 20


def blank():
    return [[0] * N for _ in range(N)]


def put(g, x, y):
    if 0 <= x < N and 0 <= y < N:
        g[y][x] = 1


def line(g, x0, y0, x1, y1):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        put(g, x0, y0)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def rect(g, x0, y0, x1, y1):
    line(g, x0, y0, x1, y0)
    line(g, x1, y0, x1, y1)
    line(g, x1, y1, x0, y1)
    line(g, x0, y1, x0, y0)


def poly(g, pts, close=False):
    for a, b in zip(pts, pts[1:]):
        line(g, a[0], a[1], b[0], b[1])
    if close:
        line(g, pts[-1][0], pts[-1][1], pts[0][0], pts[0][1])


def fill_poly(g, pts):
    """Even-odd scanline fill, sampled at pixel centres."""
    n = len(pts)
    for y in range(N):
        yc = y + 0.5
        xs = []
        for i in range(n):
            (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
            if (y0 <= yc < y1) or (y1 <= yc < y0):
                xs.append(x0 + (yc - y0) * (x1 - x0) / (y1 - y0))
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            for x in range(int(math.ceil(a - 0.5)), int(math.floor(b - 0.5)) + 1):
                put(g, x, y)


def circle(g, cx, cy, r):
    x, y, err = r, 0, 1 - r
    while x >= y:
        for px, py in ((x, y), (y, x), (-y, x), (-x, y),
                       (-x, -y), (-y, -x), (y, -x), (x, -y)):
            put(g, cx + px, cy + py)
        y += 1
        if err < 0:
            err += 2 * y + 1
        else:
            x -= 1
            err += 2 * (y - x) + 1


def icon_audiobooks():
    g = blank()
    rect(g, 1, 3, 9, 16)
    rect(g, 10, 3, 18, 16)
    for y in (6, 9, 12):
        line(g, 3, y, 7, y)
        line(g, 12, y, 16, y)
    return g


def icon_music():
    g = blank()
    circle(g, 5, 15, 2)
    circle(g, 14, 13, 2)
    line(g, 7, 15, 7, 3)
    line(g, 16, 13, 16, 1)
    line(g, 7, 3, 16, 1)
    line(g, 7, 5, 16, 3)
    return g


def icon_podcasts():
    g = blank()
    rect(g, 7, 1, 12, 9)
    line(g, 8, 0, 11, 0)
    line(g, 8, 10, 11, 10)
    poly(g, [(3, 7), (3, 10), (5, 13), (9, 15), (10, 15), (14, 13),
             (16, 10), (16, 7)])
    line(g, 9, 15, 9, 17)
    line(g, 10, 15, 10, 17)
    line(g, 6, 18, 13, 18)
    return g


def icon_ambience():
    g = blank()
    for base in (4, 10, 16):
        for x in range(1, 19):
            put(g, x, base + round(1.5 * math.sin((x - 1) * math.pi / 4.5)))
    return g


def icon_sleep():
    g = blank()
    # solid crescent moon (inside a big circle but outside a smaller one)
    # plus a small "z"
    inside = [[0] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            d1 = math.hypot(x - 7.5, y - 11.5)
            d2 = math.hypot(x - 12.0, y - 8.0)
            if d1 <= 7.6 and d2 > 6.4:
                inside[y][x] = 1
    for y in range(N):
        for x in range(N):
            if inside[y][x]:
                put(g, x, y)
    line(g, 13, 1, 18, 1)
    line(g, 18, 1, 13, 6)
    line(g, 13, 6, 18, 6)
    return g


def icon_settings():
    g = blank()
    circle(g, 10, 10, 5)
    circle(g, 10, 10, 2)
    for k in range(8):
        a = k * math.pi / 4
        for r in (6.6, 7.6, 8.6):
            x = round(10 + r * math.cos(a))
            y = round(10 + r * math.sin(a))
            put(g, x, y)
            put(g, x + 1, y)
            put(g, x, y + 1)
            put(g, x + 1, y + 1)
    return g


def icon_video():
    g = blank()
    for x in range(1, 19):
        put(g, x, 3)
        put(g, x, 15)
    for y in range(3, 16):
        put(g, 1, y)
        put(g, 18, y)
    fill_poly(g, [(7, 6), (13, 9), (7, 12)])
    for x in range(6, 14):
        put(g, x, 17)
    return g


def icon_mode():
    g = blank()
    line(g, 1, 6, 17, 6)
    poly(g, [(13, 2), (17, 6), (13, 10)])
    line(g, 2, 14, 18, 14)
    poly(g, [(6, 10), (2, 14), (6, 18)])
    return g


# order = tile order in apps/homegrid.c
ICONS = [
    ("audiobooks", icon_audiobooks),
    ("music", icon_music),
    ("podcasts", icon_podcasts),
    ("ambience", icon_ambience),
    ("sleep", icon_sleep),
    ("settings", icon_settings),
    ("video", icon_video),
    ("mode", icon_mode),
]


def preview():
    for name, fn in ICONS:
        print(name)
        for row in fn():
            print("".join("#" if v else "." for v in row))
        print()


def emit():
    out = []
    out.append("/* Generated by tools/gen_homegrid_icons.py - do not edit. */")
    out.append("#define HOMEGRID_ICON_SIZE %d" % N)
    out.append("static const unsigned long homegrid_icons[%d][%d] = {"
               % (len(ICONS), N))
    for name, fn in ICONS:
        rows = []
        for row in fn():
            v = 0
            for x, bit in enumerate(row):
                if bit:
                    v |= 1 << (N - 1 - x)
            rows.append("0x%05lx" % v)
        out.append("    /* %s */ {" % name)
        for i in range(0, N, 5):
            out.append("        " + ", ".join(rows[i:i + 5]) + ",")
        out.append("    },")
    out.append("};")
    print("\n".join(out))


if __name__ == "__main__":
    if "--preview" in sys.argv:
        preview()
    else:
        emit()
