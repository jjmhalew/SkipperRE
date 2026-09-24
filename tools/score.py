"""Director 5 score (VWSC), labels (VWLB), config (VWCF) en cast-libs (MCsL).

    python tools/score.py extract/Mmb04.dxr                 # frames + kanalen tekstueel
    python tools/score.py extract/Mmb04.dxr --render 3 out/f3.png

Score: kop van 20 bytes (streamgrootte, offset frame 1, #frames, versie, spritegrootte 24, #kanalen 50);
per frame u16 lengte + delta's (u16 len, u16 offset, data) op een kanaalbuffer van 50x24 bytes.
Kanaal 0-1 (48 bytes) = hoofdkanalen, sprite n staat op 48 + (n-1)*24.
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from rifx import RifxFile
from dcast import Cast, Bitmap, grey_palette

MOVIE_OWNER = 1024


class Sprite:
    """24 bytes: type, ink, castLib, member, scriptLib, scriptMember, fore, back, locV, locH, h, w, ..."""

    def __init__(self, b):
        (self.type, ink, self.cast_lib, self.member, self.script_lib, self.script_member,
         self.fore, self.back, self.loc_v, self.loc_h, self.height, self.width,
         self.colorcode, self.blend, self.thick, self.unk) = struct.unpack('>BBHHHHBBhhhhBBBB', b)
        self.ink = ink & 0x3f
        self.trails = bool(ink & 0x40)
        self.stretch = bool(ink & 0x80)

    def __repr__(self):
        return (f'type={self.type} cast={self.cast_lib}:{self.member} ink={self.ink} loc=({self.loc_h},{self.loc_v}) '
                f'size={self.width}x{self.height} fg={self.fore} bg={self.back}'
                + (f' script={self.script_lib}:{self.script_member}' if self.script_member else ''))


class MainChannels:
    def __init__(self, b):
        self.script = struct.unpack_from('>HH', b, 0)
        self.sound1 = struct.unpack_from('>HH', b, 4)
        self.sound2 = struct.unpack_from('>HH', b, 8)
        self.trans = struct.unpack_from('>HH', b, 12)
        self.tempo = b[21]
        self.palette = struct.unpack_from('>hh', b, 24)
        self.pal_speed, self.pal_flags = b[28], b[29]
        self.raw = b

    def __repr__(self):
        s = []
        for k in ('script', 'sound1', 'sound2', 'trans', 'palette'):
            v = getattr(self, k)
            if v[1]:
                s.append(f'{k}={v[0]}:{v[1]}')
        if self.tempo:
            s.append(f'tempo={self.tempo}')
        return ' '.join(s)


class Score:
    def __init__(self, b):
        self.frames = []
        if not b:
            return
        size, f1, self.nframes, self.version, self.sprsize, self.nchan = struct.unpack_from('>IIIHHH', b, 0)
        buf = bytearray(self.nchan * self.sprsize)
        p = f1
        while p < size:
            flen = struct.unpack_from('>H', b, p)[0]
            q, end = p + 2, p + flen
            while q < end:
                ln, off = struct.unpack_from('>HH', b, q)
                buf[off:off + ln] = b[q + 4:q + 4 + ln]
                q += 4 + ln
            self.frames.append(bytes(buf))
            p = end

    def main(self, f):
        return MainChannels(self.frames[f - 1][:48])

    def sprites(self, f):
        b = self.frames[f - 1]
        out = {}
        for ch in range(1, self.nchan - 1):
            o = 48 + (ch - 1) * self.sprsize
            s = Sprite(b[o:o + 24])
            if s.member:
                out[ch] = s
        return out


def labels(b):
    if not b:
        return {}
    n = struct.unpack_from('>H', b, 0)[0]
    ent = [struct.unpack_from('>HH', b, 2 + i * 4) for i in range(n + 1)]
    base = 2 + (n + 1) * 4
    return {b[base + ent[i][1]:base + ent[i + 1][1]].decode('latin1'): ent[i][0] for i in range(n)}


class Config:
    def __init__(self, b):
        self.top, self.left, self.bottom, self.right = struct.unpack_from('>4h', b, 4)
        self.min_member, self.max_member = struct.unpack_from('>HH', b, 12)
        self.stage_color = struct.unpack_from('>H', b, 26)[0]
        self.depth = struct.unpack_from('>H', b, 28)[0]
        self.version = struct.unpack_from('>H', b, 36)[0]
        self.tempo = struct.unpack_from('>h', b, 54)[0]
        self.palette = struct.unpack_from('>hh', b, 76)
        self.w, self.h = self.right - self.left, self.bottom - self.top


def cast_libs(b):
    """MCsL -> [(naam, pad, min, max)]; lib 1 = de eerste."""
    if not b:
        return []
    # kop: dataOffset u32, ? u16, #casts u16, items per cast u16 (4); dan een lijst: u16 #offsets,
    # u32 offsets, u32 lengte, data. Item 0 is leeg; per cast: naam, pad, preload, (min, max, id).
    doff, _, cnt, per = struct.unpack_from('>IHHH', b, 0)
    n = struct.unpack_from('>H', b, doff)[0]
    offs = struct.unpack_from('>%dI' % n, b, doff + 2)
    data = doff + 2 + 4 * n + 4
    item = lambda k: b[data + offs[k]:data + (offs[k + 1] if k + 1 < n else offs[k])] if k < n else b''
    pstr = lambda x: x[1:1 + x[0]].decode('latin1') if x else ''
    libs = []
    for i in range(cnt):
        name, path, _, rng = (item(1 + i * per + k) for k in range(4))
        lo, hi = struct.unpack('>HH', rng[:4]) if len(rng) >= 4 else (1, 0)
        libs.append((pstr(name), pstr(path), lo, hi))
    return libs


class Movie:
    def __init__(self, path):
        self.path = path
        self.rf = RifxFile(path)
        rf = self.rf
        movie_owned = rf.children.get(MOVIE_OWNER, {})
        pick = lambda t: rf.chunk_data(movie_owned[t]) if t in movie_owned else None
        self.config = Config(pick('VWCF') or rf.first('VWCF'))
        self.score = Score(pick('VWSC'))
        self.labels = labels(pick('VWLB'))
        self.libs = cast_libs(pick('MCsL'))
        self.cast = Cast(rf, self.config.min_member)
        self.ext = {}   # lib-nummer -> Cast van een extern .cxt

    def lib(self, n):
        if n <= 1:
            return self.cast
        if n not in self.ext:
            name = self.libs[n - 1][0] if n - 1 < len(self.libs) else ''
            base = os.path.dirname(self.path)
            cand = [f for f in os.listdir(base) if os.path.splitext(f)[0].lower() == name.lower()
                    and f.lower().endswith(('.cxt', '.cst'))]
            self.ext[n] = Cast(RifxFile(os.path.join(base, cand[0])), 1) if cand else None
        return self.ext[n]


def render(movie, f, pal=None):
    """Frame f van de score tekenen (zonder Lingo) -> PIL Image."""
    from PIL import Image
    cfg = movie.config
    # palet: laatst gezette paletkanaal t/m frame f
    pal_ref = None
    for k in range(1, f + 1):
        p = movie.score.main(k).palette
        if p[1]:
            pal_ref = p
    if pal is None and pal_ref and pal_ref[1] > 0:
        pal = movie.lib(pal_ref[0]).palette(pal_ref[1])
    if pal is None:
        pal = grey_palette()
    img = Image.new('RGB', (cfg.w, cfg.h), pal[cfg.stage_color & 0xff])
    for ch, s in sorted(movie.score.sprites(f).items()):
        c = movie.lib(s.cast_lib)
        m = c.members.get(s.member) if c else None
        if not m or m[0] != 1 or 'BITD' not in m[3]:
            continue
        bm = Bitmap(m[2])
        px = bm.pixels(c.rf.chunk_data(m[3]['BITD']))
        im = to_image(bm, px, pal, s)
        if s.stretch and (s.width, s.height) != (bm.w, bm.h) and s.width > 0 and s.height > 0:
            im = im.resize((s.width, s.height))
        x = s.loc_h - (bm.reg_x - bm.left)
        y = s.loc_v - (bm.reg_y - bm.top)
        img.paste(im, (x, y), im)
    return img


def to_image(bm, px, pal, s):
    """Bitmap + ink -> RGBA. Ondersteund: 0 copy, 8 matte, 36 background transparent (rest = copy)."""
    from PIL import Image
    w, h = bm.w, bm.h
    if bm.bpp == 8:
        idx = bytes(px[y * bm.pitch + x] for y in range(h) for x in range(w))
    else:  # 1 bpp: 1 = voorgrond
        idx = bytes(255 if (px[y * bm.pitch + (x >> 3)] >> (7 - (x & 7))) & 1 else 0
                    for y in range(h) for x in range(w))
    im = Image.frombytes('P', (w, h), idx)
    flat = [v for rgb in pal for v in rgb] + [0] * (768 - 3 * len(pal))
    im.putpalette(flat)
    alpha = bytearray(b'\xff' * (w * h))
    if s.ink == 36:  # background transparent: pixels met de achtergrondkleur (index 0 = wit)
        for i, v in enumerate(idx):
            if v == s.back:
                alpha[i] = 0
    elif s.ink == 8:  # matte: wit dat met de rand verbonden is
        white = 0
        stack = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
        while stack:
            x, y = stack.pop()
            i = y * w + x
            if 0 <= x < w and 0 <= y < h and alpha[i] and idx[i] == white:
                alpha[i] = 0
                stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    out = im.convert('RGBA')
    out.putalpha(Image.frombytes('L', (w, h), bytes(alpha)))
    return out


def main():
    m = Movie(sys.argv[1])
    c = m.config
    print(f'stage {c.w}x{c.h} kleur {c.stage_color} tempo {c.tempo} palet {c.palette} members {c.min_member}..{c.max_member}')
    print('libs', m.libs)
    print('labels', m.labels)
    if '--render' in sys.argv:
        i = sys.argv.index('--render')
        f, out = int(sys.argv[i + 1]), sys.argv[i + 2]
        render(m, f).save(out)
        print('->', out)
        return
    for f in range(1, len(m.score.frames) + 1):
        print(f'frame {f}: {m.score.main(f)}')
        for ch, s in m.score.sprites(f).items():
            print(f'   {ch:2d} {s}')


if __name__ == '__main__':
    main()
