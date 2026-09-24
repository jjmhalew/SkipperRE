"""Losse BMP's van --every tot één overzichtsplaat: python tools/filmstrip.py out/run out/strip.png [kolommen]"""
import sys, glob
from PIL import Image, ImageDraw
fs = sorted(glob.glob(sys.argv[1] + '.*.bmp'))
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 4
w, h = 320, 240
rows = (len(fs) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (w + 4), rows * (h + 18)), (60, 60, 60))
d = ImageDraw.Draw(sheet)
for i, f in enumerate(fs):
    im = Image.open(f).convert('RGB').resize((w, h))
    x, y = (i % cols) * (w + 4), (i // cols) * (h + 18)
    sheet.paste(im, (x, y + 16))
    d.text((x + 2, y + 2), f.split('.')[-2], fill=(255, 255, 0))
sheet.save(sys.argv[2])
