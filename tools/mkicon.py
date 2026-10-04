"""Renders the app icon of SkipperRE (own artwork, not the game's) from res/icon.svg.

    python tools/mkicon.py [preview.png]
        -> res/icon.png (256), res/skipperre.ico (16-256), android/app/src/main/res/mipmap-*/ic_launcher.png

The SVG is drawn by a headless Edge or Chrome (whichever is installed) at 1024 px and scaled down with Pillow. The parts
marked class="detail" (mosquito, clouds, the sun's glow) are left out at 32 px and below, where they would only be noise.
With an argument it also writes a preview sheet of all sizes.
"""
import os, subprocess, sys, tempfile
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SVG = os.path.join(ROOT, 'res', 'icon.svg')
MIPMAPS = {'mdpi': 48, 'hdpi': 72, 'xhdpi': 96, 'xxhdpi': 144, 'xxxhdpi': 192}
ICO = [16, 24, 32, 48, 64, 128, 256]
SMALL = 32   # this size and below: the simple version
BROWSERS = [r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
            r'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
            r'C:\Program Files\Google\Chrome\Application\chrome.exe',
            '/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/google-chrome', '/usr/bin/microsoft-edge']


def render(svg_text, tmp, name):
    """the SVG as a 1024x1024 RGBA image"""
    browser = next((b for b in BROWSERS if os.path.isfile(b)), None)
    if not browser:
        sys.exit('mkicon: needs Edge or Chrome to draw the SVG')
    src, png = os.path.join(tmp, name + '.svg'), os.path.join(tmp, name + '.png')
    with open(src, 'w', encoding='utf-8') as f:
        f.write(svg_text)
    subprocess.run([browser, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--default-background-color=00000000',
                    '--window-size=1024,1024', '--screenshot=' + png, 'file:///' + src.replace('\\', '/')],
                   check=True, capture_output=True)
    return Image.open(png).convert('RGBA')


def main():
    svg = open(SVG, encoding='utf-8').read()
    with tempfile.TemporaryDirectory() as tmp:
        big = render(svg, tmp, 'full')
        small = render(svg.replace('class="detail"', 'display="none"'), tmp, 'small')
    size = lambda n: (small if n <= SMALL else big).resize((n, n), Image.LANCZOS)

    for name, n in MIPMAPS.items():
        d = os.path.join(ROOT, 'android', 'app', 'src', 'main', 'res', 'mipmap-' + name)
        os.makedirs(d, exist_ok=True)
        size(n).save(os.path.join(d, 'ic_launcher.png'), optimize=True)
    size(256).save(os.path.join(ROOT, 'res', 'icon.png'), optimize=True)
    imgs = [size(n) for n in ICO]
    imgs[-1].save(os.path.join(ROOT, 'res', 'skipperre.ico'), sizes=[(n, n) for n in ICO], append_images=imgs[:-1])

    if len(sys.argv) > 1:   # preview: large, then every icon size, on light and dark
        w = 420 + sum(n + 16 for n in ICO[:-1])
        sheet = Image.new('RGBA', (w, 860), (240, 240, 240, 255))
        sheet.paste((32, 32, 36, 255), (0, 430, w, 860))
        for y in (10, 440):
            sheet.alpha_composite(big.resize((400, 400), Image.LANCZOS), (10, y))
            x = 430
            for n in reversed(ICO[:-1]):
                sheet.alpha_composite(size(n), (x, y))
                x += n + 16
        sheet.save(sys.argv[1])


if __name__ == '__main__':
    main()
