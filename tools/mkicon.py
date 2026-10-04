"""Draws the app icon of SkipperRE (own artwork, not the game's): a red cap on blue, for Android's launcher.

    python tools/mkicon.py      -> android/app/src/main/res/mipmap-*/ic_launcher.png
"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(__file__), '..')
SIZES = {'mdpi': 48, 'hdpi': 72, 'xhdpi': 96, 'xxhdpi': 144, 'xxxhdpi': 192}


def icon(n):
    k = 8   # draw large, scale down for smooth edges
    s = n * k
    im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([s * 0.04, s * 0.04, s * 0.96, s * 0.96], radius=s * 0.2, fill=(46, 150, 210, 255))
    d.ellipse([s * 0.10, s * 0.55, s * 0.90, s * 1.15], fill=(255, 205, 80, 255))          # yellow ground
    d.pieslice([s * 0.18, s * 0.22, s * 0.78, s * 0.86], 180, 360, fill=(224, 24, 16, 255))  # the cap's dome
    d.rounded_rectangle([s * 0.50, s * 0.50, s * 0.92, s * 0.60], radius=s * 0.05, fill=(190, 12, 8, 255))  # brim
    d.ellipse([s * 0.44, s * 0.18, s * 0.52, s * 0.26], fill=(190, 12, 8, 255))           # button on top
    d.arc([s * 0.26, s * 0.30, s * 0.70, s * 0.80], 200, 330, fill=(255, 255, 255, 255), width=int(s * 0.03))
    mask = Image.new('L', (s, s), 0)   # everything inside the rounded square
    ImageDraw.Draw(mask).rounded_rectangle([s * 0.04, s * 0.04, s * 0.96, s * 0.96], radius=s * 0.2, fill=255)
    out = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    out.paste(im, (0, 0), mask)
    return out.resize((n, n), Image.LANCZOS)


for name, n in SIZES.items():
    d = os.path.join(ROOT, 'android', 'app', 'src', 'main', 'res', 'mipmap-' + name)
    os.makedirs(d, exist_ok=True)
    icon(n).save(os.path.join(d, 'ic_launcher.png'))
icon(256).save(os.path.join(ROOT, 'res', 'icon.png'))
icon(256).save(os.path.join(ROOT, 'res', 'skipperre.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
