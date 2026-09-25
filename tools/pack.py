"""Eén exe: plakt de spelbestanden achter skipper.exe (zie src/pack.c voor de opbouw).

    python tools/pack.py [exe] [datamap] [uit.exe]

Standaard: out/skipper.exe + extract/ (of %APPDATA%/SkipperRE/data) -> dist/Skipper.exe.
Alleen wat het spel gebruikt gaat mee: films (.dxr), casts (.cxt), video's, het icoon en .ini's;
niet de Catalog-map, de installers en de 16-bit DLL's. start.dxr komt uit de datamap of uit de
opslagmap (die maakt skipper.exe zelf bij de eerste start)."""
import os, struct, sys, zlib

EXE = sys.argv[1] if len(sys.argv) > 1 else 'out/skipper.exe'
APP = os.path.join(os.environ.get('APPDATA', ''), 'SkipperRE')
DATA = sys.argv[2] if len(sys.argv) > 2 else next(
    (d for d in ('extract', os.path.join(APP, 'data')) if os.path.isfile(os.path.join(d, 'Magnus.dxr'))), None)
OUT = sys.argv[3] if len(sys.argv) > 3 else 'dist/Skipper.exe'
EXTS = ('.dxr', '.cxt', '.ico', '.ini')


def files():
    top = sorted(f for f in os.listdir(DATA) if os.path.isfile(os.path.join(DATA, f)) and f.lower().endswith(EXTS))
    for f in top:
        yield f, os.path.join(DATA, f)
    vid = os.path.join(DATA, 'Video')
    if os.path.isdir(vid):
        for f in sorted(os.listdir(vid)):
            yield 'Video\\' + f, os.path.join(vid, f)
    if not any(f.lower() == 'start.dxr' for f in top):
        for d in (APP, os.path.join(APP, 'save')):
            p = os.path.join(d, 'start.dxr')
            if os.path.isfile(p):
                yield 'start.dxr', p
                break
        else:
            sys.exit('start.dxr niet gevonden: start skipper.exe eerst een keer (die haalt hem uit SETUP.EXE)')


def main():
    if not DATA:
        sys.exit('geen spelbestanden: geef de map met Magnus.dxr op')
    exe = open(EXE, 'rb').read()
    if exe[-16:-8] == b'SKPACK01':   # al ingepakt: alleen de exe zelf houden
        sys.exit(f'{EXE} bevat al spelbestanden; geef de gewone skipper.exe op')
    os.makedirs(os.path.dirname(OUT) or '.', exist_ok=True)
    tot = totc = 0
    with open(OUT, 'wb') as o:
        o.write(exe)
        ents = []
        for name, path in files():
            raw = open(path, 'rb').read()
            c = zlib.compressobj(9, zlib.DEFLATED, -15)
            z = c.compress(raw) + c.flush()
            method = 1 if len(z) < len(raw) * 0.98 else 0
            blob = z if method else raw
            ents.append((name, o.tell(), len(blob), len(raw), method))
            o.write(blob)
            tot += len(raw)
            totc += len(blob)
        d = o.tell()
        o.write(struct.pack('<I', len(ents)))
        for name, off, cs, us, m in ents:
            n = name.encode('latin-1')
            o.write(struct.pack('<H', len(n)) + n + struct.pack('<QIIB', off, cs, us, m))
        o.write(b'SKPACK01' + struct.pack('<Q', d))
    print(f'{len(ents)} bestanden, {tot >> 20} MB -> {totc >> 20} MB; {OUT}: {os.path.getsize(OUT) >> 20} MB')


if __name__ == '__main__':
    main()
