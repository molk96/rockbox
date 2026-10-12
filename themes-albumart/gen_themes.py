#!/usr/bin/env python3
"""Generate the 8 Zen X-Fi album-art themes (320x240).
Output: out/.rockbox/{themes,wps,backdrops}  (copy to the SD card)
Needs Pillow.  Run:  python3 gen_themes.py
"""
import os, random, shutil
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 320, 240
FD = "/usr/share/fonts/truetype/dejavu/"
def f(sz, b=False, m=False):
    n = "DejaVuSansMono" if m else "DejaVuSans"
    return ImageFont.truetype(FD + n + ("-Bold" if b else "") + ".ttf", sz)

def bevel(d, box, raised=True, fill=(192, 192, 192)):
    x0, y0, x1, y1 = box
    d.rectangle(box, fill=fill)
    lt, dk = ((255, 255, 255), (64, 64, 64)) if raised else ((64, 64, 64), (255, 255, 255))
    d.line((x0, y0, x1, y0), fill=lt); d.line((x0, y0, x0, y1), fill=lt)
    d.line((x0, y1, x1, y1), fill=dk); d.line((x1, y0, x1, y1), fill=dk)
    if raised:
        d.line((x0+1, y1-1, x1-1, y1-1), fill=(128, 128, 128)); d.line((x1-1, y0+1, x1-1, y1-1), fill=(128, 128, 128))

def grad(top, bot, size=(W, H)):
    im = Image.new("RGB", size); d = ImageDraw.Draw(im)
    for y in range(size[1]):
        t = y / size[1]; d.line((0, y, size[0], y), fill=tuple(int(top[i]*(1-t)+bot[i]*t) for i in range(3)))
    return im

def glow(im, layer, blur):
    g = layer.filter(ImageFilter.GaussianBlur(blur))
    return Image.alpha_composite(im.convert("RGBA"), g).convert("RGB")

def scan(im, a=50):
    d = ImageDraw.Draw(im, "RGBA")
    for y in range(0, im.size[1], 2): d.line((0, y, im.size[0], y), fill=(0, 0, 0, a))
    return im

def hx(c): return "%02X%02X%02X" % tuple(c)

# ------------------------------------------------------------------ chrome
def chrome_mono(col, dim, bg):
    im = Image.new("RGB", (W, H), bg); d = ImageDraw.Draw(im)
    d.rectangle((0, 0, W-1, 17), fill=dim); d.text((6, 2), "> NOW PLAYING", font=f(11, True, True), fill=bg)
    d.rectangle((10, 32, 133, 157), outline=col); d.rectangle((6, 28, 137, 161), outline=dim)
    for t, y in (("TITLE", 34), ("ARTIST", 72), ("ALBUM", 106)): d.text((148, y), t, font=f(9, False, True), fill=dim)
    d.rectangle((12, 194, W-12, 204), outline=col)
    return scan(im)

def chrome_win95_classic():
    im = Image.new("RGB", (W, H), (192, 192, 192)); d = ImageDraw.Draw(im)
    bevel(d, (0, 0, W-1, H-1)); d.rectangle((3, 3, W-4, 19), fill=(0, 0, 128)); d.text((22, 3), "Now Playing", font=f(12, True), fill="white")
    d.rectangle((6, 6, 16, 16), fill=(255, 255, 0), outline="black")
    for xx in (W-50, W-34, W-18): bevel(d, (xx, 5, xx+12, 17))
    d.text((W-47, 3), "_", font=f(10, True), fill="black"); d.text((W-31, 4), "□", font=f(9), fill="black"); d.text((W-15, 3), "x", font=f(10, True), fill="black")
    bevel(d, (10, 30, 133, 157), False, (128, 128, 128))
    x = 146
    for lab, y in (("Title:", 32), ("Artist:", 66), ("Album:", 100)):
        d.text((x, y), lab, font=f(9), fill=(64, 64, 64)); bevel(d, (x, y+12, W-10, y+28), False, (255, 255, 255))
    bevel(d, (x, 134, W-10, 157), False, (0, 0, 0))
    bevel(d, (10, 168, W-10, 180), False, (255, 255, 255))
    for i, lab in enumerate(["|<", ">", "||", "[]", ">|"]):
        bx = 14 + i*60; bevel(d, (bx, 194, bx+50, 218))
        cx, cy = bx+25, 206
        if lab == ">": d.polygon([(cx-4, cy-6), (cx-4, cy+6), (cx+5, cy)], fill="black")
        elif lab == "||": d.rectangle((cx-5, cy-6, cx-2, cy+6), fill="black"); d.rectangle((cx+2, cy-6, cx+5, cy+6), fill="black")
        elif lab == "[]": d.rectangle((cx-5, cy-5, cx+5, cy+5), fill="black")
        elif lab == "|<": d.rectangle((cx-6, cy-6, cx-4, cy+6), fill="black"); d.polygon([(cx+6, cy-6), (cx+6, cy+6), (cx-3, cy)], fill="black")
        else: d.rectangle((cx+4, cy-6, cx+6, cy+6), fill="black"); d.polygon([(cx-6, cy-6), (cx-6, cy+6), (cx+3, cy)], fill="black")
    bevel(d, (10, 224, W-10, 236), False)
    return im

def chrome_win95_setup():
    im = Image.new("RGB", (W, H)); d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / H; c = (0, 0, int(170*(1-t)+30*t)); d.line((0, y, W, y), fill=c)
        if y % 2 == 0: d.line((0, y, W, y), fill=(0, 0, max(c[2]-14, 0)))
    d.text((14, 8), "Now Playing", font=f(20, True), fill=(0, 0, 60)); d.text((12, 6), "Now Playing", font=f(20, True), fill="white")
    d.text((14, 32), "Please wait while your music plays...", font=f(9), fill=(190, 190, 255))
    bevel(d, (8, 48, W-8, H-8)); d.rectangle((11, 51, W-11, 65), fill=(0, 0, 128)); d.text((15, 51), "Audio Player Wizard", font=f(10, True), fill="white")
    bevel(d, (16, 74, 139, 201), False, (128, 128, 128))
    bevel(d, (152, 150, W-16, 166), False, (255, 255, 255))
    for i, lab in enumerate(["|<", ">", "||", ">|"]):
        bx = 152 + i*38; bevel(d, (bx, 184, bx+32, 200)); cx, cy = bx+16, 192
        if lab == ">": d.polygon([(cx-3, cy-5), (cx-3, cy+5), (cx+4, cy)], fill="black")
        elif lab == "||": d.rectangle((cx-4, cy-5, cx-2, cy+5), fill="black"); d.rectangle((cx+2, cy-5, cx+4, cy+5), fill="black")
        elif lab == "|<": d.rectangle((cx-5, cy-5, cx-4, cy+5), fill="black"); d.polygon([(cx+5, cy-5), (cx+5, cy+5), (cx-3, cy)], fill="black")
        else: d.rectangle((cx+4, cy-5, cx+5, cy+5), fill="black"); d.polygon([(cx-5, cy-5), (cx-5, cy+5), (cx+3, cy)], fill="black")
    d.line((16, 208, W-16, 208), fill=(128, 128, 128)); d.line((16, 209, W-16, 209), fill=(255, 255, 255))
    return im

def chrome_ps1_card():
    im = grad((60, 62, 70), (20, 22, 30)); d = ImageDraw.Draw(im)
    d.rectangle((0, 0, W, 26), fill=(30, 40, 90)); d.text((12, 5), "NOW PLAYING", font=f(13, True), fill=(220, 220, 235)); d.polygon([(W-30, 6), (W-14, 6), (W-22, 20)], fill=(150, 160, 210))
    d.rectangle((10, 36, 134, 162), fill=(10, 10, 16), outline=(150, 160, 200), width=2)
    d.rectangle((146, 36, W-10, 162), fill=(10, 10, 24), outline=(110, 120, 170))
    d.rectangle((10, 178, W-10, 188), fill=(10, 10, 16), outline=(150, 160, 200))
    cx, cy = W//2, 222
    d.polygon([(cx-9, cy-7), (cx-9, cy+7), (cx+4, cy)], fill=(60, 200, 100))
    d.rectangle((cx+28, cy-7, cx+40, cy+7), fill=(220, 80, 80)); d.ellipse((cx-48, cy-7, cx-34, cy+7), outline=(90, 160, 255), width=3)
    d.polygon([(cx+60, cy+7), (cx+67, cy-7), (cx+74, cy+7)], outline=(240, 150, 200))
    return im

def chrome_ps1_boot():
    im = Image.new("RGB", (W, H), (0, 0, 0))
    L = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ld = ImageDraw.Draw(L)
    ld.polygon([(160, -30), (330, 110), (160, 250), (-10, 110)], fill=(150, 152, 165, 70))
    im = glow(im, L, 18); d = ImageDraw.Draw(im, "RGBA")
    d.polygon([(160, 2), (318, 110), (160, 218), (2, 110)], outline=(120, 122, 135, 120))
    d.polygon([(160, 22), (290, 110), (160, 198), (30, 110)], outline=(90, 92, 105, 110))
    d.rectangle((12, 28, 135, 151), outline=(200, 200, 215), width=2)
    d.line((14, 154, 134, 154), fill=(210, 210, 225, 200)); d.line((14, 156, 134, 156), fill=(120, 122, 135, 90))
    d.text((152, 30), "NOW PLAYING", font=f(9, True), fill=(150, 152, 170)); d.line((152, 44, W-12, 44), fill=(120, 122, 140))
    d.rectangle((14, 212, W-14, 216), fill=(40, 42, 52))
    return im

def chrome_ps2_towers():
    im = grad((0, 2, 10), (2, 10, 32)); random.seed(7)
    L = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ld = ImageDraw.Draw(L)
    for i in range(34):
        x = random.randint(-5, W); w = random.choice([4, 6, 8, 12]); h = random.randint(30, 150); al = random.randint(60, 150)
        for k in range(h): ld.line((x, H-k, x+w, H-k), fill=(40, 110, 255, int(al*(1-k/h)**1.3)))
        ld.line((x, H-h, x+w, H-h), fill=(150, 200, 255, 200))
    im = glow(im, L, 3); im = Image.alpha_composite(im.convert("RGBA"), L).convert("RGB")
    G = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ImageDraw.Draw(G).rectangle((10, 28, 138, 160), fill=(40, 110, 255, 150)); im = glow(im, G, 12)
    d = ImageDraw.Draw(im, "RGBA")
    for i in range(60):
        x, y = random.randint(0, W), random.randint(0, H); d.ellipse((x, y, x+1, y+1), fill=(160, 210, 255, random.randint(70, 220)))
    d.rectangle((12, 30, 135, 153), outline=(170, 210, 255, 230))
    d.rectangle((14, 170, W-14, 173), fill=(255, 255, 255, 40))
    return im

def chrome_ps2_void():
    im = Image.new("RGB", (W, H), (0, 0, 3)); random.seed(11)
    L = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ImageDraw.Draw(L).ellipse((-40, 40, 340, 300), fill=(10, 40, 150, 150)); im = glow(im, L, 40)
    d = ImageDraw.Draw(im, "RGBA")
    for r in (40, 70, 100, 135): d.ellipse((160-r*1.5, 200-r*0.28, 160+r*1.5, 200+r*0.28), outline=(70, 130, 255, max(20, 120-r)))
    for i in range(22):
        x, y = random.randint(0, W), random.randint(0, 190); s = random.choice([2, 3, 4, 6]); al = random.randint(60, 190)
        d.rectangle((x, y, x+s, y+s), outline=(150, 200, 255, al))
        if s < 4: d.rectangle((x, y, x+s, y+s), fill=(150, 200, 255, al//2))
    G = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ImageDraw.Draw(G).rectangle((10, 28, 138, 160), fill=(30, 90, 255, 170)); im = glow(im, G, 14)
    d = ImageDraw.Draw(im, "RGBA"); d.rectangle((12, 30, 135, 153), outline=(140, 190, 255, 230))
    d.line((14, 174, W-14, 174), fill=(60, 100, 190, 150))
    return im

# ------------------------------------------------------------------ list backdrops (menus)
def list_bd(name):
    if name in ("terminal_green", "amber"):
        c = (0, 8, 2) if name == "terminal_green" else (10, 5, 0)
        return scan(Image.new("RGB", (W, H), c), 40)
    if name == "win95_classic": return Image.new("RGB", (W, H), (192, 192, 192))
    if name == "win95_setup":
        im = Image.new("RGB", (W, H)); d = ImageDraw.Draw(im)
        for y in range(H):
            t = y/H; d.line((0, y, W, y), fill=(0, 0, int(160*(1-t)+30*t)))
        return im
    if name == "ps1_card": return grad((60, 62, 70), (20, 22, 30))
    if name == "ps1_boot":
        im = Image.new("RGB", (W, H)); L = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(L).polygon([(160, -30), (330, 110), (160, 250), (-10, 110)], fill=(150, 152, 165, 55)); return glow(im, L, 18)
    if name == "ps2_towers": return chrome_ps2_towers_list()
    return chrome_ps2_void_list()

def chrome_ps2_towers_list():
    im = grad((0, 2, 10), (2, 10, 32)); random.seed(5)
    L = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ld = ImageDraw.Draw(L)
    for i in range(20):
        x = random.randint(-5, W); w = random.choice([4, 6, 8]); h = random.randint(20, 70)
        for k in range(h): ld.line((x, H-k, x+w, H-k), fill=(40, 110, 255, int(60*(1-k/h)**1.3)))
    return Image.alpha_composite(im.convert("RGBA"), L).convert("RGB")

def chrome_ps2_void_list():
    im = Image.new("RGB", (W, H), (0, 0, 3)); L = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(L).ellipse((-40, 120, 340, 340), fill=(10, 40, 150, 120)); return glow(im, L, 40)

# ------------------------------------------------------------------ no-art fallback (120x120)
def noart(name, a, b, fg):
    im = grad(a, b, (120, 120)); d = ImageDraw.Draw(im)
    d.ellipse((30, 22, 90, 82), outline=fg, width=3); d.ellipse((50, 42, 70, 62), outline=fg, width=2)
    d.text((60, 100), "NO ART", font=f(11, True, True), fill=fg, anchor="mm")
    return im

# ------------------------------------------------------------------ pb image for Win95 setup (segmented)
def segbar(w, h):
    im = Image.new("RGB", (w, h), (255, 255, 255)); d = ImageDraw.Draw(im)
    for x in range(0, w, 9): d.rectangle((x, 0, x+6, h-1), fill=(0, 0, 128))
    return im

# ------------------------------------------------------------------ theme definitions
# text element: (x,y,w,h,fontid,colour,content)
TH = {}
def T(name, title, **kw): TH[name] = dict(name=name, title=title, **kw)

SETT_ART = (12, 34)
T("terminal_green", "Terminal Green", chrome=lambda: chrome_mono((60, 255, 90), (20, 110, 40), (0, 8, 2)),
  fonts={2: "14-Terminus-Bold", 3: "14-Terminus", 4: "12-Terminus"}, art=(12, 34),
  noart=((10, 40, 14), (0, 70, 20), (60, 255, 90)),
  els=[(W-112, 3, 106, 13, 4, "08020A".replace("08020A", "000802"), "%ar%pp/%pe"),
       (148, 45, 164, 18, 2, "3CFF5A", "%s%?it<%it|%fn>"),
       (148, 83, 164, 16, 3, "3CFF5A", "%s%?ia<%ia|%?iA<%iA|Unknown artist>>"),
       (148, 117, 164, 16, 3, "3CFF5A", "%s%?id<%id|%?d(1)<%d(1)|Unknown album>>"),
       (148, 140, 164, 14, 4, "146E28", "%fc  %fb kbps"),
       (12, 176, 100, 14, 3, "3CFF5A", "%pc"), (W-112, 176, 100, 14, 3, "3CFF5A", "%ar-%pr"),
       (12, 214, 296, 14, 4, "146E28", "%?mp<[STOP]|[PLAY]|[PAUSE]|[FF]|[REW]>  VOL %pv dB")],
  bar=(14, 196, 292, 7, "3CFF5A"),
  cfg=dict(fg="3CFF5A", bg="000802", sel1="14702A", sel2="14702A", seltxt="3CFF5A", font="14-Terminus"), selector="bar (color)")
T("amber", "Amber", chrome=lambda: chrome_mono((255, 176, 0), (120, 80, 0), (10, 5, 0)),
  fonts={2: "14-Terminus-Bold", 3: "14-Terminus", 4: "12-Terminus"}, art=(12, 34),
  noart=((30, 18, 0), (90, 55, 0), (255, 176, 0)),
  els=[(W-112, 3, 106, 13, 4, "0A0500", "%ar%pp/%pe"),
       (148, 45, 164, 18, 2, "FFB000", "%s%?it<%it|%fn>"),
       (148, 83, 164, 16, 3, "FFB000", "%s%?ia<%ia|%?iA<%iA|Unknown artist>>"),
       (148, 117, 164, 16, 3, "FFB000", "%s%?id<%id|%?d(1)<%d(1)|Unknown album>>"),
       (148, 140, 164, 14, 4, "785000", "%fc  %fb kbps"),
       (12, 176, 100, 14, 3, "FFB000", "%pc"), (W-112, 176, 100, 14, 3, "FFB000", "%ar-%pr"),
       (12, 214, 296, 14, 4, "785000", "%?mp<[STOP]|[PLAY]|[PAUSE]|[FF]|[REW]>  VOL %pv dB")],
  bar=(14, 196, 292, 7, "FFB000"),
  cfg=dict(fg="FFB000", bg="0A0500", sel1="785000", sel2="785000", seltxt="FFB000", font="14-Terminus"), selector="bar (color)")
T("win95_classic", "Win95 Classic", chrome=chrome_win95_classic,
  fonts={2: "12-Adobe-Helvetica-Bold", 3: "12-Adobe-Helvetica", 4: "14-Terminus"}, art=(12, 32),
  noart=((128, 128, 128), (192, 192, 192), (0, 0, 128)),
  els=[(150, 45, 156, 14, 2, "000000", "%s%?it<%it|%fn>"),
       (150, 79, 156, 14, 3, "000000", "%s%?ia<%ia|%?iA<%iA|Unknown artist>>"),
       (150, 113, 156, 14, 3, "000000", "%s%?id<%id|%?d(1)<%d(1)|Unknown album>>"),
       (154, 139, 152, 16, 4, "00FF00", "%ac%pc / %pt"),
       (14, 224, 292, 12, 3, "000000", "Track %pp of %pe     %?mp<Stopped|Playing|Paused|Fast forward|Rewind>     Vol %pv dB")],
  bar=(12, 170, 296, 9, "000080"),
  cfg=dict(fg="000000", bg="C0C0C0", sel1="000080", sel2="000080", seltxt="FFFFFF", font="12-Adobe-Helvetica"), selector="bar (color)")
T("win95_setup", "Win95 Setup", chrome=chrome_win95_setup,
  fonts={2: "12-Adobe-Helvetica-Bold", 3: "12-Adobe-Helvetica"}, art=(18, 76),
  noart=((40, 40, 160), (200, 200, 255), (255, 255, 255)),
  els=[(152, 74, 152, 16, 2, "000000", "%s%?it<%it|%fn>"),
       (152, 94, 152, 14, 3, "000000", "%s%?ia<%ia|%?iA<%iA|Unknown artist>>"),
       (152, 110, 152, 14, 3, "404040", "%s%?id<%id|%?d(1)<%d(1)|Unknown album>>"),
       (152, 134, 152, 14, 3, "000000", "Elapsed: %pc of %pt"),
       (152, 168, 152, 14, 3, "000000", "%px%% complete"),
       (16, 214, 288, 14, 3, "000000", "Vol: %pv dB     Track %pp/%pe")],
  bar=(154, 152, 148, 12, "000080", "segbar.bmp"),
  cfg=dict(fg="FFFFFF", bg="0000A0", sel1="C0C0C0", sel2="C0C0C0", seltxt="000000", font="12-Adobe-Helvetica"), selector="bar (color)")
T("ps1_card", "PS1 Memory Card", chrome=chrome_ps1_card,
  fonts={2: "14-Adobe-Helvetica-Bold", 3: "12-Adobe-Helvetica", 4: "12-Adobe-Helvetica"}, art=(12, 39),
  noart=((70, 70, 120), (160, 120, 60), (220, 220, 235)),
  els=[(156, 44, 148, 18, 2, "EBEBF5", "%s%?it<%it|%fn>"),
       (156, 70, 148, 15, 3, "B4BEE6", "%s%?ia<%ia|%?iA<%iA|Unknown artist>>"),
       (156, 90, 148, 15, 3, "96A0C8", "%s%?id<%id|%?d(1)<%d(1)|Unknown album>>"),
       (156, 130, 148, 14, 4, "7882B4", "Track %pp/%pe"),
       (156, 144, 148, 14, 4, "7882B4", "Vol %pv dB"),
       (10, 193, 100, 14, 3, "D2D2E1", "%pc"), (W-110, 193, 100, 14, 3, "D2D2E1", "%ar%pt")],
  bar=(12, 180, 296, 7, "506EE6"),
  cfg=dict(fg="DCDCEB", bg="1E1E28", sel1="3050B0", sel2="1E3278", seltxt="FFFFFF", font="12-Adobe-Helvetica"), selector="bar (gradient)")
T("ps1_boot", "PS1 Boot", chrome=chrome_ps1_boot,
  fonts={2: "14-Adobe-Helvetica-Bold", 3: "12-Adobe-Helvetica", 4: "12-Adobe-Helvetica", 5: "16-Terminus-Bold"}, art=(14, 30),
  noart=((150, 150, 165), (40, 40, 60), (230, 230, 240)),
  els=[(152, 52, 160, 18, 2, "F0F0F8", "%s%?it<%it|%fn>"),
       (152, 78, 160, 15, 3, "B9BBC8", "%s%?ia<%ia|%?iA<%iA|Unknown artist>>"),
       (152, 98, 160, 15, 3, "828496", "%s%?id<%id|%?d(1)<%d(1)|Unknown album>>"),
       (152, 128, 160, 14, 4, "828496", "Track %pp / %pe"),
       (152, 142, 100, 20, 5, "EBEBF5", "%pc"),
       (14, 222, 150, 14, 4, "BEC0CD", "%?mp<STOP|PLAY|PAUSE|FF|REW>"), (W-114, 222, 100, 14, 4, "828496", "%ar%pt")],
  bar=(14, 212, 292, 5, "D2D4E1"),
  cfg=dict(fg="E6E6EF", bg="000000", sel1="55576A", sel2="2A2B38", seltxt="FFFFFF", font="12-Adobe-Helvetica"), selector="bar (gradient)")
T("ps2_towers", "PS2 Towers", chrome=chrome_ps2_towers,
  fonts={2: "14-Adobe-Helvetica-Bold", 3: "12-Adobe-Helvetica", 4: "12-Adobe-Helvetica", 5: "16-Terminus-Bold"}, art=(14, 32),
  noart=((20, 50, 150), (120, 180, 255), (230, 240, 255)),
  els=[(152, 34, 160, 18, 2, "EBF4FF", "%s%?it<%it|%fn>"),
       (152, 62, 160, 15, 3, "96C3FF", "%s%?ia<%ia|%?iA<%iA|Unknown artist>>"),
       (152, 82, 160, 15, 3, "6496E6", "%s%?id<%id|%?d(1)<%d(1)|Unknown album>>"),
       (152, 126, 60, 20, 5, "C8E1FF", "%pc"), (212, 130, 100, 14, 4, "6496E6", "/ %pt"),
       (14, 184, 150, 14, 4, "6EA0EB", "Track %pp / %pe"), (W-114, 184, 100, 14, 4, "6EA0EB", "%arVol %pv dB")],
  bar=(14, 170, 292, 4, "78BEFF"),
  cfg=dict(fg="C8E1FF", bg="000A20", sel1="1E5AFF", sel2="0A2A80", seltxt="FFFFFF", font="12-Adobe-Helvetica"), selector="bar (gradient)")
T("ps2_void", "PS2 Void", chrome=chrome_ps2_void,
  fonts={2: "14-Adobe-Helvetica-Bold", 3: "12-Adobe-Helvetica", 4: "12-Adobe-Helvetica", 5: "16-Terminus-Bold"}, art=(14, 32),
  noart=((10, 30, 120), (90, 150, 255), (230, 240, 255)),
  els=[(152, 34, 160, 18, 2, "E6F0FF", "%s%?it<%it|%fn>"),
       (152, 62, 160, 15, 3, "82B4FF", "%s%?ia<%ia|%?iA<%iA|Unknown artist>>"),
       (152, 82, 160, 15, 3, "5082DC", "%s%?id<%id|%?d(1)<%d(1)|Unknown album>>"),
       (152, 126, 60, 20, 5, "BEDCFF", "%pc"), (212, 130, 100, 14, 4, "5082DC", "/ %pt"),
       (14, 186, 150, 14, 4, "96C3FF", "%?mp<STOP|PLAY|PAUSE|FF|REW>"), (W-114, 186, 100, 14, 4, "5082DC", "%arVol %pv dB")],
  bar=(14, 173, 292, 3, "A0D2FF"),
  cfg=dict(fg="BEDCFF", bg="000003", sel1="1E5AFF", sel2="0A2A80", seltxt="FFFFFF", font="12-Adobe-Helvetica"), selector="bar (gradient)")

def wps_text(t):
    L = ["# %s - album art theme for Creative Zen X-Fi (320x240)" % t["title"],
         "# Title line: song title tag if present, otherwise the file name (%?it<%it|%fn>)",
         "%wd", "%X(bg.bmp)"]
    for fid, fn in sorted(t["fonts"].items()): L.append("%%Fl(%d,%s.fnt)" % (fid, fn))
    L.append("%xl(N,noart.bmp,0,0)")
    ax, ay = t["art"]
    L += ["# album art (120x120) with fallback", "%%V(%d,%d,120,120,-)" % (ax, ay), "%Cl(0,0,120,120,c,c)", "%?C<%Cd|%xd(N)>"]
    bx, by, bw, bh, bc, *img = t["bar"]
    L += ["# progress", "%%V(%d,%d,%d,%d,-)" % (bx, by, bw, bh), "%%Vf(%s)" % bc]
    L.append("%%pb(0,0,%d,%d,image,%s)" % (bw, bh, img[0]) if img else "%%pb(0,0,%d,%d,noborder)" % (bw, bh))
    for (x, y, w, h, fid, col, txt) in t["els"]:
        L += ["%%V(%d,%d,%d,%d,%d)" % (x, y, w, h, fid), "%%Vf(%s)" % col, txt]
    return "\n".join(L) + "\n"

def cfg_text(t):
    c = t["cfg"]; n = t["name"]
    return "\n".join([
        "#Rockbox album-art theme: %s (Creative Zen X-Fi 320x240)" % t["title"],
        "wps: /.rockbox/wps/%s.wps" % n,
        "backdrop: /.rockbox/backdrops/%s.bmp" % n,
        "font: /.rockbox/fonts/%s.fnt" % c["font"],
        "iconset: /.rockbox/icons/tango_icons.16x16.bmp",
        "viewers iconset: /.rockbox/icons/tango_icons_viewers.16x16.bmp",
        "statusbar: top",
        "foreground color: %s" % c["fg"], "background color: %s" % c["bg"],
        "selector type: %s" % t["selector"],
        "line selector start color: %s" % c["sel1"], "line selector end color: %s" % c["sel2"],
        "line selector text color: %s" % c["seltxt"], ""])

def main():
    out = "out/.rockbox"; shutil.rmtree("out", ignore_errors=True)
    for d in ("themes", "wps", "backdrops"): os.makedirs("%s/%s" % (out, d))
    for n, t in TH.items():
        os.makedirs("%s/wps/%s" % (out, n))
        t["chrome"]().save("%s/wps/%s/bg.bmp" % (out, n))
        a, b, fg = t["noart"]; noart(n, a, b, fg).save("%s/wps/%s/noart.bmp" % (out, n))
        if len(t["bar"]) > 5: segbar(t["bar"][2], t["bar"][3]).save("%s/wps/%s/%s" % (out, n, t["bar"][5]))
        list_bd(n).save("%s/backdrops/%s.bmp" % (out, n))
        open("%s/wps/%s.wps" % (out, n), "w").write(wps_text(t))
        open("%s/themes/%s.cfg" % (out, n), "w").write(cfg_text(t))
    print("ok", len(TH), "themes")
main()
