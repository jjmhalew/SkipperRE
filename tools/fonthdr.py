"""res/fonts/*.ttf -> src/fonts.h (C-arrays) voor text_ttf.c, de tekst buiten Windows.

    python tools/fonthdr.py

De lettertypen zijn Liberation Sans / Mono 2.1.5 (SIL Open Font License, res/fonts/LICENSE), metrisch gelijk aan
Arial en Courier New, ingekort tot de tekens van Windows-1252 met fontTools:
    python -m fontTools.subset X.ttf --unicodes=<cp1252> --no-hinting --desubroutinize --output-file=res/fonts/X.ttf
"""
import os

ROOT = os.path.join(os.path.dirname(__file__), '..')
FONTS = ['LiberationSans-Regular', 'LiberationSans-Bold', 'LiberationSans-Italic', 'LiberationSans-BoldItalic',
         'LiberationMono-Regular', 'LiberationMono-Bold']


def main():
    out = ['/* gegenereerd door tools/fonthdr.py uit res/fonts (SIL Open Font License) - niet met de hand wijzigen */']
    for f in FONTS:
        data = open(os.path.join(ROOT, 'res', 'fonts', f + '.ttf'), 'rb').read()
        name = 'font_' + f.replace('Liberation', '').replace('-', '_').lower()
        out.append('static const unsigned char %s[%d] = {' % (name, len(data)))
        for i in range(0, len(data), 24):
            out.append(''.join('%d,' % b for b in data[i:i + 24]))
        out.append('};')
    with open(os.path.join(ROOT, 'src', 'fonts.h'), 'w', newline='\n') as o:
        o.write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
