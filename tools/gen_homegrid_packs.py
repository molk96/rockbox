#!/usr/bin/env python3
"""
Generates apps/homegrid_packs.h: the icon packs of the Zen X-Fi icon home.

Each pack is 8 pictures (Books, Music, Podcasts, Ambience, Sleep, Settings,
Shortcuts, Mode) stored as palette indexes.  Index 0 is transparent.  A
palette entry is either a fixed RGB colour or a theme colour (text colour,
background, or a mix of the two), so the "themed" packs follow light and
dark themes.

The icons are drawn here as SVG and rasterised with headless Chromium
(python3 -m pip install playwright, Chromium must be installed), pixel-exact
without anti-aliasing for the pixel packs.

Usage: tools/gen_homegrid_packs.py > apps/homegrid_packs.h
"""
import io
import sys
from PIL import Image
from playwright.sync_api import sync_playwright

NAMES = ["book", "music", "mic", "cloud", "moon", "gear", "bolt", "list"]

# ---- 24x24 pixel glyphs (classes k h m s a b g = colour roles) -----------
GLYPHS = {
"book": '<rect class="k" x="1" y="3" width="22" height="19"/><rect class="h" x="2" y="4" width="8" height="13"/><rect class="h" x="14" y="4" width="8" height="13"/><rect class="m" x="10" y="4" width="4" height="13"/><rect class="s" x="2" y="18" width="20" height="3"/><rect class="m" x="2" y="17" width="20" height="1"/><path class="a" d="M3 6h6v1H3zM3 8h6v1H3zM3 10h6v1H3zM3 12h5v1H3zM3 14h6v1H3zM15 6h6v1h-6zM15 8h6v1h-6zM15 10h6v1h-6zM15 12h4v1h-4zM15 14h6v1h-6z"/><rect class="b" x="17" y="17" width="2" height="6"/>',
"music": '<rect class="k" x="9" y="4" width="2" height="14"/><rect class="k" x="20" y="2" width="2" height="14"/><path class="k" d="M9 3l13-2v7L9 10z"/><path class="a" d="M11 5l9-1.5V6.5L11 8z"/><ellipse class="k" cx="6" cy="18" rx="4.5" ry="3.5"/><ellipse class="m" cx="6" cy="18" rx="3.5" ry="2.5"/><rect class="h" x="4" y="17" width="2" height="1"/><ellipse class="k" cx="17" cy="16" rx="4.5" ry="3.5"/><ellipse class="m" cx="17" cy="16" rx="3.5" ry="2.5"/><rect class="h" x="15" y="15" width="2" height="1"/>',
"mic": '<path class="k" d="M8 4h1V2h1V1h4v1h1v2h1v9h-1v1h-1v1h-4v-1H9v-1H8z"/><rect class="h" x="9" y="4" width="6" height="9"/><rect class="h" x="10" y="2" width="4" height="2"/><rect class="m" x="12" y="3" width="3" height="10"/><path class="s" d="M9 6h6v1H9zM9 8h6v1H9zM9 10h6v1H9z"/><path class="k" d="M4 9h1v6H4zM19 9h1v6h-1zM5 15h1v2H5zM18 15h1v2h-1zM6 17h2v1H6zM16 17h2v1h-2zM8 18h8v1H8zM11 19h2v2h-2z"/><rect class="k" x="7" y="21" width="10" height="2"/><rect class="h" x="8" y="21" width="8" height="1"/><rect class="m" x="8" y="22" width="8" height="1"/>',
"cloud": '<path class="k" d="M8 4h6v1h3v1h2v2h2v1h1v6h-1v1H5v-1H3v-1H2V9h1V8h2V7h1V5h2z"/><path class="h" d="M9 5h4v1h3v1h2v2h2v1h1v4h-1v1H6v-1H4v-1H3V9h1V8h2V7h1V6h2z"/><rect class="m" x="4" y="13" width="16" height="2"/><path class="a" d="M6 18h1v3H6zM10 19h1v3h-1zM14 18h1v3h-1zM18 19h1v3h-1z"/>',
"moon": '<path class="k" d="M15 2A10 10 0 1 0 22 17 8 8 0 0 1 15 2z"/><path class="m" d="M14.5 4.5A8 8 0 1 0 20 16.5 6.5 6.5 0 0 1 14.5 4.5z"/><rect class="h" x="5" y="10" width="1" height="4"/><rect class="h" x="6" y="8" width="1" height="2"/><path class="a" d="M20 4h1v3h-1zM19 5h3v1h-3zM21 10h1v1h-1zM17 8h1v1h-1z"/>',
"gear": '<rect class="k" x="10" y="1" width="4" height="22"/><rect class="k" x="1" y="10" width="22" height="4"/><rect class="k" x="10" y="1" width="4" height="22" transform="rotate(45 12 12)"/><rect class="k" x="10" y="1" width="4" height="22" transform="rotate(-45 12 12)"/><circle class="k" cx="12" cy="12" r="9.5"/><circle class="m" cx="12" cy="12" r="8"/><path class="h" d="M6 11a6 6 0 0 1 6-6v2a4 4 0 0 0-4 4z"/><circle class="k" cx="12" cy="12" r="4"/><circle class="g" cx="12" cy="12" r="3"/>',
"bolt": '<path class="k" d="M15 1L4 13h6l-2 10L21 9h-6z"/><path class="a" d="M14 4L7.5 12H12l-1 6 6.5-7H13z"/><path class="h" d="M13 5L9 10h2z"/>',
"list": '<rect class="k" x="1" y="3" width="22" height="18"/><rect class="b" x="2" y="4" width="20" height="3"/><rect class="h" x="2" y="7" width="20" height="13"/><rect class="m" x="2" y="19" width="20" height="1"/><rect class="h" x="18" y="5" width="2" height="1"/><path class="a" d="M4 9h2v2H4zM4 13h2v2H4zM4 17h2v2H4z"/><path class="m" d="M8 9h12v2H8zM8 13h12v2H8zM8 17h9v2H8z"/>',
}

# ---- smooth line glyphs (pack 5), 24 unit grid ----------------------------
LINE = {
"book": ("M12 6c-1.5-1.2-3.5-2-6-2H3v14h3c2.5 0 4.5.8 6 2 1.5-1.2 3.5-2 6-2h3V4h-3c-2.5 0-4.5.8-6 2zM12 6v14", "round", "round"),
"music": ("M9 18V5l11-2v13M9 18a3 3 0 11-6 0 3 3 0 016 0zM20 16a3 3 0 11-6 0 3 3 0 016 0z", "round", "round"),
"mic": ("M12 14a3 3 0 003-3V6a3 3 0 00-6 0v5a3 3 0 003 3zM6 11a6 6 0 0012 0M12 17v4M8 21h8", "round", "round"),
"cloud": ("M3 12h2M7 8v8M11 4v16M15 8v8M19 10v4", "round", "round"),
"moon": ("M20 14.5A8 8 0 119.5 4a6.5 6.5 0 0010.5 10.5z", "round", "round"),
"gear": ("M4 7h9M17 7h3M4 17h3M11 17h9M15 5v4M9 15v4", "round", "round"),
"bolt": ("M13 2L4 14h7l-1 8 9-12h-7z", "butt", "round"),
"list": ("M4 4h6v6H4zM14 4h6v6h-6zM4 14h6v6H4zM14 14h6v6h-6z", "butt", "round"),
}

def v(k, h="#000000", m="#000000", s="#000000", a="#000000", b="#000000", g="#000000"):
    return dict(k=k, h=h, m=m, s=s, a=a, b=b, g=g)

WIN95 = [
    v("#000000", h="#ffffff", m="#c0c0c0", s="#800000", a="#000080", b="#ff0000"),
    v("#000000", h="#8080ff", m="#000080", a="#ff0000"),
    v("#000000", h="#c0c0c0", m="#808080", s="#000000"),
    v("#000000", h="#ffffff", m="#c0c0c0", a="#0000ff"),
    v("#000000", m="#ffff00", h="#ffffaa", a="#ffffff"),
    v("#000000", m="#c0c0c0", h="#ffffff", g="#008080"),
    v("#000000", a="#ffff00", h="#ffffff"),
    v("#000000", b="#000080", h="#ffffff", a="#008080", m="#808080"),
]
WIN31 = [
    v("#000000", h="#ffffff", m="#aaaaaa", s="#aa0000", a="#0000aa", b="#aa0000"),
    v("#000000", h="#55ffff", m="#aa00aa", a="#00aaaa"),
    v("#000000", h="#aaaaaa", m="#555555", s="#000000"),
    v("#000000", h="#55ffff", m="#00aaaa", a="#0000aa"),
    v("#000000", m="#aaaa00", h="#ffff55", a="#0000aa"),
    v("#000000", m="#aaaaaa", h="#ffffff", g="#ffffff"),
    v("#000000", a="#ffff55", h="#ffffff"),
    v("#000000", b="#0000aa", h="#ffffff", a="#aa0000", m="#00aaaa"),
]
VAPOR = [
    v("#2b1055", h="#ffffff", m="#d9c8ff", s="#ff2d95", a="#00b8d4", b="#00e5ff"),
    v("#2b1055", h="#d9b8ff", m="#7b2dff", a="#ff2d95"),
    v("#2b1055", h="#ffd1ec", m="#ff2d95", s="#2b1055"),
    v("#2b1055", h="#b8fff6", m="#7fd9ff", a="#ff2d95"),
    v("#2b1055", m="#9a5cff", h="#e0c8ff", a="#ff2d95"),
    v("#2b1055", m="#00e5ff", h="#e0ffff", g="#d9c8ff"),
    v("#2b1055", a="#ffe14d", h="#ffffff"),
    v("#2b1055", b="#ff2d95", h="#ffffff", a="#7b2dff", m="#d9c8ff"),
]
# System 7: 1-bit; k s a b = ink, h g = paper, m = 50% dither
SYS7 = [v("#000000", h="#ffffff", m="#ff00ff", s="#000000", a="#000000", b="#000000", g="#ffffff")] * 8

KIND = {"rgb": 0x00000000, "fg": 0x01000000, "bg": 0x02000000,
        "mix1": 0x03000000, "mix2": 0x04000000}


def hexrgb(s):
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


class Renderer:
    def __init__(self, pw):
        self.browser = pw.chromium.launch()
        self.page = self.browser.new_page(viewport={"width": 64, "height": 64})

    def render(self, svg, size):
        self.page.set_viewport_size({"width": size, "height": size})
        self.page.set_content(
            '<html><body style="margin:0;background:transparent">%s</body></html>' % svg)
        png = self.page.screenshot(omit_background=True,
                                   clip={"x": 0, "y": 0, "width": size, "height": size})
        return Image.open(io.BytesIO(png)).convert("RGBA")


def pixel_svg(name, cols, size):
    css = "".join(".%s{fill:%s}" % (c, cols[c]) for c in "khmsabg")
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
            'viewBox="0 0 24 24" shape-rendering="crispEdges"><style>%s</style>%s</svg>'
            % (size, size, css, GLYPHS[name]))


def snap(px, cols):
    r, g, b, a = px
    if a < 128:
        return None
    best, bd = None, 1 << 30
    for c in cols:
        cr, cg, cb = hexrgb(c)
        d = (r - cr) ** 2 + (g - cg) ** 2 + (b - cb) ** 2
        if d < bd:
            best, bd = c, d
    return best


def build_color_pack(rend, sets):
    pal = []          # list of "#rrggbb"
    pix = []
    for name, cols in zip(NAMES, sets):
        img = rend.render(pixel_svg(name, cols, 24), 24)
        used = sorted(set(cols.values()))
        data = []
        for y in range(24):
            for x in range(24):
                c = snap(img.getpixel((x, y)), used)
                if c is None:
                    data.append(0)
                    continue
                if c not in pal:
                    pal.append(c)
                data.append(pal.index(c) + 1)
        pix.append(data)
    assert len(pal) < 64, len(pal)
    entries = [KIND["rgb"] | (hexrgb(c)[0] << 16) | (hexrgb(c)[1] << 8) | hexrgb(c)[2]
               for c in pal]
    return 24, 2, entries, pix


def build_sys7(rend):
    # palette: 1 = ink (text colour), 2 = paper (background)
    pix = []
    for name, cols in zip(NAMES, SYS7):
        img = rend.render(pixel_svg(name, cols, 48), 48)
        used = ["#000000", "#ffffff", "#ff00ff"]
        data = []
        for y in range(48):
            for x in range(48):
                c = snap(img.getpixel((x, y)), used)
                if c is None:
                    data.append(0)
                elif c == "#000000":
                    data.append(1)
                elif c == "#ffffff":
                    data.append(2)
                else:
                    data.append(1 if (x + y) % 2 == 0 else 2)
        pix.append(data)
    return 48, 1, [KIND["fg"], KIND["bg"]], pix


def build_line(rend):
    # palette: 1 = 1/3 ink, 2 = 2/3 ink, 3 = ink
    pix = []
    for name in NAMES:
        d, cap, join = LINE[name]
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 24 24">'
               '<path d="%s" fill="none" stroke="#000" stroke-width="1" '
               'stroke-linecap="%s" stroke-linejoin="%s"/></svg>' % (d, cap, join))
        img = rend.render(svg, 48)
        data = []
        for y in range(48):
            for x in range(48):
                a = img.getpixel((x, y))[3] / 255.0
                data.append(0 if a < 0.17 else 1 if a < 0.5 else 2 if a < 0.83 else 3)
        pix.append(data)
    return 48, 1, [KIND["mix1"], KIND["mix2"], KIND["fg"]], pix


def c_array(name, ctype, values, per_line):
    out = ["static const %s %s[] = {" % (ctype, name)]
    for i in range(0, len(values), per_line):
        out.append("    " + ",".join(str(x) for x in values[i:i + per_line]) + ",")
    out.append("};")
    return "\n".join(out)


def main():
    packs = []   # (name, own_colours, style, size, scale, pal, pix)
    with sync_playwright() as pw:
        rend = Renderer(pw)
        s, sc, pal, pix = build_color_pack(rend, WIN95)
        packs.append(("Win95", True, ("008080", "ffffff", "c0c0c0", "ffff00", "006868",
                                      "000080", "1084d0", "ffffff"), s, sc, pal, pix))
        s, sc, pal, pix = build_color_pack(rend, WIN31)
        packs.append(("Win 3.1", True, ("ffffff", "000000", "555555", "0000aa", "ccccff",
                                        "0000aa", "0000aa", "ffffff"), s, sc, pal, pix))
        s, sc, pal, pix = build_sys7(rend)
        packs.append(("System 7 (theme)", False, None, s, sc, pal, pix))
        s, sc, pal, pix = build_color_pack(rend, VAPOR)
        packs.append(("Vaporwave 95", True, ("d9c8ff", "2b1055", "6b4fa0", "ff2d95", "c2a6f5",
                                             "ff2d95", "00e5ff", "ffffff"), s, sc, pal, pix))
        s, sc, pal, pix = build_line(rend)
        packs.append(("Line minimal (theme)", False, None, s, sc, pal, pix))

    o = []
    o.append("/* Generated by tools/gen_homegrid_packs.py, do not edit. */")
    o.append("#ifndef _HOMEGRID_PACKS_H")
    o.append("#define _HOMEGRID_PACKS_H")
    o.append("")
    o.append("/* palette entry: kind in bits 24..31, RGB in the low 24 bits */")
    o.append("#define PK_FG   0x01000000ul   /* text colour */")
    o.append("#define PK_BG   0x02000000ul   /* background behind the icon */")
    o.append("#define PK_MIX1 0x03000000ul   /* 1/3 text colour over background */")
    o.append("#define PK_MIX2 0x04000000ul   /* 2/3 text colour over background */")
    o.append("")
    o.append("struct icon_pack")
    o.append("{")
    o.append("    const char *name;")
    o.append("    unsigned char size;      /* pictures are size x size */")
    o.append("    unsigned char scale;     /* every picture pixel is drawn scale x scale */")
    o.append("    unsigned char npal;")
    o.append("    unsigned char own_colors;/* the pack sets the page colours itself */")
    o.append("    unsigned long bg, fg, dim, accent, sel_bg;   /* 0xRRGGBB */")
    o.append("    unsigned long hdr1, hdr2, hdr_fg;            /* header gradient and text */")
    o.append("    const unsigned long *pal;    /* index 1.. ; index 0 is transparent */")
    o.append("    const unsigned char *pix;    /* 8 pictures of size*size indexes */")
    o.append("};")
    o.append("")
    for i, (name, own, style, size, scale, pal, pix) in enumerate(packs):
        flat = [p for pic in pix for p in pic]
        o.append(c_array("pk%d_pal" % i, "unsigned long",
                         ["0x%08Xul" % e for e in pal], 8))
        o.append(c_array("pk%d_pix" % i, "unsigned char", flat, size))
        o.append("")
    o.append("static const struct icon_pack homegrid_packs[] = {")
    for i, (name, own, style, size, scale, pal, pix) in enumerate(packs):
        st = style or ("000000",) * 8
        o.append('    { "%s", %d, %d, %d, %d,' % (name, size, scale, len(pal), 1 if own else 0))
        o.append("      %s,\n      %s,\n      pk%d_pal, pk%d_pix }," % (
            ", ".join("0x" + c for c in st[:5]), ", ".join("0x" + c for c in st[5:]), i, i))
    o.append("};")
    o.append("#define HOMEGRID_NUM_PACKS ((int)(sizeof(homegrid_packs) / sizeof(homegrid_packs[0])))")
    o.append("")
    o.append("#endif")
    sys.stdout.write("\n".join(o) + "\n")


if __name__ == "__main__":
    main()
