"""Generates the TwinDeck app icon (res/app.ico) and a preview sheet of icon concepts.

usage:
  python tools/make_icon.py                 # writes res/app.ico using the default concept
  python tools/make_icon.py --variant B     # another concept
  python tools/make_icon.py --preview       # build/icon_candidates.png comparing all concepts
"""
import argparse
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
DEFAULT = "C"


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def gradient_tile(s, top, bottom, radius):
    """Rounded-square tile with a vertical gradient."""
    grad = Image.new("RGBA", (s, s))
    gd = ImageDraw.Draw(grad)
    for y in range(s):
        gd.line([(0, y), (s, y)], fill=lerp(top, bottom, y / (s - 1)) + (255,))
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=radius, fill=255)
    tile = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    tile.paste(grad, (0, 0), mask)
    return tile


def concept_a(s):
    """Dual-pane commander: two panels on a blue tile with a copy arrow between them."""
    m = int(s * 0.04)
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    img.alpha_composite(gradient_tile(s - 2 * m, (59, 130, 246), (49, 46, 129), int(s * 0.22)), (m, m))
    d = ImageDraw.Draw(img)
    pw, ph, py = s * 0.30, s * 0.50, s * 0.25
    r = s * 0.05
    lx, rx = s * 0.15, s * 0.55
    d.rounded_rectangle([lx, py, lx + pw, py + ph], radius=r, fill=(255, 255, 255, 255))
    d.rounded_rectangle([rx, py, rx + pw, py + ph], radius=r, fill=(214, 222, 255, 255))
    # list rows inside the panels
    for i in range(3):
        y = py + s * 0.10 + i * s * 0.11
        d.rounded_rectangle([lx + s * 0.05, y, lx + pw - s * 0.05, y + s * 0.045], radius=s * 0.02, fill=(59, 130, 246, 255))
        d.rounded_rectangle([rx + s * 0.05, y, rx + pw - s * 0.05, y + s * 0.045], radius=s * 0.02, fill=(129, 140, 248, 255))
    # amber arrow crossing from left to right panel
    cy = s * 0.50
    ax0, ax1 = s * 0.35, s * 0.70
    t = s * 0.075
    d.rounded_rectangle([ax0, cy - t / 2, ax1 - s * 0.06, cy + t / 2], radius=t / 2, fill=(251, 191, 36, 255))
    d.polygon([(ax1 - s * 0.10, cy - s * 0.11), (ax1 + s * 0.04, cy), (ax1 - s * 0.10, cy + s * 0.11)], fill=(251, 191, 36, 255))
    return img


def concept_b(s):
    """Command folder: a Fluent-style folder with a terminal prompt '>_'."""
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = s * 0.08
    d.rounded_rectangle([s * 0.06, s * 0.16, s * 0.48, s * 0.36], radius=r * 0.6, fill=(217, 150, 30, 255))
    d.rounded_rectangle([s * 0.06, s * 0.24, s * 0.94, s * 0.86], radius=r, fill=(240, 186, 70, 255))
    d.rounded_rectangle([s * 0.06, s * 0.34, s * 0.94, s * 0.86], radius=r, fill=(250, 208, 102, 255))
    # dark prompt plate
    d.rounded_rectangle([s * 0.20, s * 0.44, s * 0.80, s * 0.78], radius=r * 0.7, fill=(30, 41, 59, 255))
    w = max(2, int(s * 0.055))
    d.line([(s * 0.30, s * 0.52), (s * 0.40, s * 0.61), (s * 0.30, s * 0.70)], fill=(74, 222, 128, 255), width=w, joint="curve")
    d.rounded_rectangle([s * 0.46, s * 0.665, s * 0.66, s * 0.665 + w], radius=w / 2, fill=(226, 232, 240, 255))
    return img


MONOGRAM = "TD"


# Noto Sans KR (SIL Open Font License) so the logo artwork carries no proprietary font.
# Ships with Windows 11; otherwise get it from https://fonts.google.com/noto/specimen/Noto+Sans+KR
FONT_CANDIDATES = [
    "C:/Windows/Fonts/NotoSansKR-VF.ttf",
    os.path.expandvars("%LOCALAPPDATA%/Microsoft/Windows/Fonts/NotoSansKR-VF.ttf"),
]


def monogram_font(text, px):
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            font = ImageFont.truetype(path, px)
            try:
                font.set_variation_by_name("Bold")
            except (OSError, ValueError):
                font.set_variation_by_axes([700])
            return font
    raise SystemExit("Noto Sans KR not found - install it or edit FONT_CANDIDATES")


def concept_c(s, text=None):
    """Monogram tile: letters on a teal gradient with a folder-tab notch."""
    text = text or MONOGRAM
    m = int(s * 0.04)
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    img.alpha_composite(gradient_tile(s - 2 * m, (20, 184, 166), (15, 76, 129), int(s * 0.22)), (m, m))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([s * 0.18, s * 0.16, s * 0.48, s * 0.26], radius=s * 0.04, fill=(153, 246, 228, 255))
    scale = {1: 0.56, 2: 0.46}.get(len(text), 0.36)
    font = monogram_font(text, int(s * scale))
    d.text((s * 0.5, s * 0.57), text, font=font, fill=(255, 255, 255, 255), anchor="mm")
    return img


CONCEPTS = {"A": concept_a, "B": concept_b, "C": concept_c}
TITLES = {"A": "A  Dual pane", "B": "B  Command folder", "C": "C  Monogram"}


def render(variant, size, text=None):
    big = concept_c(size * 4, text) if (variant == "C" and text) else CONCEPTS[variant](size * 4)  # supersample
    return big.resize((size, size), Image.LANCZOS)


def write_ico(variant, text=None):
    out = os.path.join(ROOT, "res", "app.ico")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    imgs = [render(variant, n, text) for n in sizes]
    imgs[-1].save(out, format="ICO", sizes=[(n, n) for n in sizes], append_images=imgs[:-1])
    render(variant, 256, text).save(os.path.join(ROOT, "res", "app.png"))
    print("wrote", out, "variant", variant)


def preview_names(entries):
    """entries: list of (label, monogram) rendered in concept C on dark/light backgrounds."""
    sizes = [16, 24, 32, 48, 96]
    cell_w, row_h = 760, 140
    sheet = Image.new("RGB", (cell_w * 2, row_h * len(entries)), (0, 0, 0))
    d = ImageDraw.Draw(sheet)
    font = monogram_font("가", 24)
    for i, (label, mono) in enumerate(entries):
        for j, bg in enumerate([(32, 35, 41), (238, 240, 243)]):
            x0, y0 = j * cell_w, i * row_h
            d.rectangle([x0, y0, x0 + cell_w, y0 + row_h], fill=bg)
            d.text((x0 + 16, y0 + 10), label, font=font, fill=(255, 255, 255) if j == 0 else (20, 20, 20))
            x = x0 + 20
            for n in sizes:
                im = render("C", n, mono)
                sheet.paste(im, (x, y0 + 36 + (row_h - 36 - n) // 2), im)
                x += n + 28
    out = os.path.join(ROOT, "build", "name_candidates.png")
    sheet.save(out)
    print("wrote", out)


def preview():
    sizes = [16, 24, 32, 48, 96, 160]
    cell_w, row_h = 900, 200
    sheet = Image.new("RGBA", (cell_w, row_h * len(CONCEPTS) * 2), (0, 0, 0, 255))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 22)
    except OSError:
        font = ImageFont.load_default()
    row = 0
    for key in CONCEPTS:
        for bg in [(32, 35, 41), (238, 240, 243)]:
            y0 = row * row_h
            d.rectangle([0, y0, cell_w, y0 + row_h], fill=bg + (255,))
            d.text((16, y0 + 10), TITLES[key], font=font, fill=(255, 255, 255) if bg[0] < 128 else (20, 20, 20))
            x = 20
            for n in sizes:
                im = render(key, n)
                sheet.alpha_composite(im, (x, y0 + 40 + (row_h - 40 - n) // 2))
                x += n + 30
            row += 1
    out = os.path.join(ROOT, "build", "icon_candidates.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.convert("RGB").save(out)
    print("wrote", out)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", default=DEFAULT, choices=list(CONCEPTS))
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--text", help="monogram text for concept C (e.g. FC, 바)")
    ap.add_argument("--names", nargs="*", help="label=monogram pairs for a concept C name preview")
    a = ap.parse_args()
    if a.names:
        preview_names([tuple(n.split("=", 1)) for n in a.names])
    elif a.preview:
        preview()
    else:
        write_ico(a.variant, a.text)
