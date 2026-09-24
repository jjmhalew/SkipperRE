"""Castleden van Director 5 decoderen: bitmaps (BITD), paletten (CLUT), geluid (snd ), tekst (STXT).

    python tools/dcast.py extract/Mmb04.dxr out/cast/Mmb04      # alles naar PNG/WAV/TXT
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from rifx import RifxFile, CAST_TYPES


def clut(b):
    """CLUT-chunk -> [(r,g,b)] (6 bytes per kleur, 16-bit big-endian)."""
    return [(b[i], b[i + 2], b[i + 4]) for i in range(0, len(b) - 5, 6)]


def grey_palette():
    return [(255 - i, 255 - i, 255 - i) for i in range(256)]


class Bitmap:
    def __init__(self, spec):
        flags = struct.unpack_from('>H', spec, 0)[0]
        self.pitch = flags & 0x0fff
        self.top, self.left, self.bottom, self.right = struct.unpack_from('>4h', spec, 2)
        self.reg_y, self.reg_x = struct.unpack_from('>2h', spec, 18)
        self.bpp, self.clut_lib, self.clut = 1, 0, 0
        if flags & 0x8000 and len(spec) >= 28:
            self.bpp = spec[23]
            self.clut_lib, self.clut = struct.unpack_from('>2h', spec, 24)
        self.w = self.right - self.left
        self.h = self.bottom - self.top

    def pixels(self, bitd):
        """BITD -> bytes van pitch*h (indices voor 8 bpp, gepakte bits voor 1 bpp)."""
        need = self.pitch * self.h
        if len(bitd) == need:
            return bitd
        out = bytearray()
        i = 0
        while i < len(bitd) and len(out) < need:
            n = bitd[i]; i += 1
            if n < 0x80:
                out += bitd[i:i + n + 1]; i += n + 1
            else:
                out += bytes([bitd[i]]) * (257 - n); i += 1
        out += bytes(max(0, need - len(out)))
        return bytes(out[:need])

    def to_rgb(self, bitd, pal):
        """-> (w, h, bytes RGB)"""
        px = self.pixels(bitd)
        rgb = bytearray()
        for y in range(self.h):
            row = px[y * self.pitch:(y + 1) * self.pitch]
            if self.bpp == 8:
                for x in range(self.w):
                    rgb += bytes(pal[row[x]])
            elif self.bpp == 1:
                for x in range(self.w):
                    bit = (row[x >> 3] >> (7 - (x & 7))) & 1
                    rgb += b'\0\0\0' if bit else b'\xff\xff\xff'
            else:
                raise NotImplementedError(f'bpp {self.bpp}')
        return self.w, self.h, bytes(rgb)


def save_png(path, w, h, rgb):
    from PIL import Image
    Image.frombytes('RGB', (w, h), rgb).save(path)


class Cast:
    """Alle leden van de (eerste) cast in een bestand: nummer -> (type, naam, spec, children)."""

    def __init__(self, rf, first=1):
        """first = nummer van het eerste slot in CAS* (VWCF minMember voor de interne cast)."""
        self.rf = rf
        self.members = {}
        secs = rf.cast_sections()
        if not secs:
            return
        for i, cid in enumerate(secs[0][1]):
            if cid > 0:
                t, info, spec = rf.cast_member(cid)
                self.members[i + first] = (t, info.get('name', ''), spec, rf.children.get(cid, {}), info)

    def palette(self, num):
        m = self.members.get(num)
        if m and m[0] == 4 and 'CLUT' in m[3]:
            return clut(self.rf.chunk_data(m[3]['CLUT']))
        return None

    def bitmap(self, num, pal=None):
        t, name, spec, ch, _ = self.members[num]
        bm = Bitmap(spec)
        if pal is None:
            pal = (self.palette(bm.clut) if bm.clut > 0 else None) or grey_palette()
        return bm, bm.to_rgb(self.rf.chunk_data(ch['BITD']), pal)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    cast = Cast(RifxFile(src))
    for num, (t, name, spec, ch, info) in sorted(cast.members.items()):
        base = os.path.join(dst, f'{num:04d}_{name or "noname"}'.replace('/', '_'))
        try:
            if t == 1 and 'BITD' in ch:
                bm, (w, h, rgb) = cast.bitmap(num)
                if w > 0 and h > 0:
                    save_png(base + '.png', w, h, rgb)
            elif t == 3 and 'STXT' in ch:
                b = cast.rf.chunk_data(ch['STXT'])
                ln = struct.unpack_from('>I', b, 4)[0]
                open(base + '.txt', 'wb').write(b[12:12 + ln])
        except Exception as e:
            print(num, name, CAST_TYPES.get(t), 'FOUT', e)


if __name__ == '__main__':
    main()
