"""Director 5 RIFX/XFIR-container (.dxr/.cxt/.dir/.cst) lezen.

Gebruik als module (RifxFile) of los:
    python tools/rifx.py extract/Magnus.dxr        # chunkoverzicht + castleden
"""
import struct, sys, collections

CAST_TYPES = {1: 'bitmap', 2: 'filmloop', 3: 'text', 4: 'palette', 5: 'picture', 6: 'sound',
              7: 'button', 8: 'shape', 9: 'movie', 10: 'digitalvideo', 11: 'script', 12: 'richtext',
              13: 'ole', 14: 'transition', 15: 'xtra'}


class Chunk:
    __slots__ = ('id', 'tag', 'size', 'offset')

    def __init__(self, id, tag, size, offset):
        self.id, self.tag, self.size, self.offset = id, tag, size, offset


class RifxFile:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        d = self.data
        magic = d[:4]
        if magic == b'XFIR':
            self.le = True
        elif magic == b'RIFX':
            self.le = False
        else:
            raise ValueError(f'{path}: geen RIFX ({magic!r})')
        self.e = '<' if self.le else '>'
        self.codec = self.tag(d[8:12])
        # imap -> mmap
        assert self.tag(d[12:16]) == 'imap'
        mmap_off = self.u32(d, 12 + 8 + 4)
        assert self.tag(d[mmap_off:mmap_off + 4]) == 'mmap'
        m = mmap_off + 8
        hl, el = struct.unpack(self.e + 'HH', d[m:m + 4])
        cmax, cused = struct.unpack(self.e + 'ii', d[m + 4:m + 12])
        self.chunks = []
        for i in range(cused):
            e = d[m + hl + i * el: m + hl + (i + 1) * el]
            tag = self.tag(e[:4])
            size, off = struct.unpack(self.e + 'Ii', e[4:12])
            self.chunks.append(Chunk(i, tag, size, off))
        self.by_tag = collections.defaultdict(list)
        for c in self.chunks:
            if c.tag not in ('free', 'junk'):
                self.by_tag[c.tag].append(c)
        self._read_keys()

    # --- helpers -------------------------------------------------------
    def tag(self, b):
        return (b[::-1] if self.le else b).decode('latin1')

    def u32(self, b, o):
        return struct.unpack(self.e + 'I', b[o:o + 4])[0]

    def chunk_data(self, c):
        if isinstance(c, int):
            c = self.chunks[c]
        o = c.offset
        return self.data[o + 8:o + 8 + c.size]

    def first(self, tag):
        l = self.by_tag.get(tag)
        return self.chunk_data(l[0]) if l else None

    # --- KEY*: resource -> eigenaar (castlid) --------------------------
    def _read_keys(self):
        self.children = collections.defaultdict(dict)   # owner id -> {tag: chunk id}
        k = self.first('KEY*')
        if k is None:
            return
        esz, esz2, cnt, used = struct.unpack(self.e + 'HHii', k[:12])
        for i in range(used):
            sec, owner = struct.unpack(self.e + 'ii', k[12 + i * 12:20 + i * 12])
            tag = self.tag(k[20 + i * 12:24 + i * 12])
            self.children[owner][tag] = sec

    # --- castlijst -----------------------------------------------------
    def cast_sections(self):
        """[(casttabel-chunk, [castlid-chunk-id of 0 per slot])] ; in D5 is CAS* big-endian."""
        out = []
        for c in self.by_tag.get('CAS*', []):
            b = self.chunk_data(c)
            ids = list(struct.unpack('>%di' % (len(b) // 4), b))
            out.append((c, ids))
        return out

    def cast_member(self, cid):
        """CASt-chunk (D5): type, info-lijst, specifieke data (big-endian)."""
        b = self.chunk_data(cid)
        typ, info_len, spec_len = struct.unpack('>III', b[:12])
        info = b[12:12 + info_len]
        spec = b[12 + info_len:12 + info_len + spec_len]
        return typ, parse_info(info), spec


def parse_info(b):
    """Info-lijst van een castlid: kop + offsettabel met strings. Veld 0 = scripttekst, 1 = naam."""
    if len(b) < 20:
        return {}
    data_off = struct.unpack('>I', b[:4])[0]
    cnt = struct.unpack('>H', b[data_off:data_off + 2])[0]
    offs = struct.unpack('>%dI' % (cnt + 1), b[data_off + 2:data_off + 2 + 4 * (cnt + 1)])
    base = data_off + 2 + 4 * (cnt + 1)
    items = [b[base + offs[i]:base + offs[i + 1]] for i in range(cnt)]
    res = {'raw': items}
    if len(items) > 1 and items[1]:
        n = items[1][0]
        res['name'] = items[1][1:1 + n].decode('latin1')
    if items and items[0]:
        res['script'] = items[0].decode('latin1')
    return res


def main():
    for p in sys.argv[1:]:
        f = RifxFile(p)
        print(f'== {p}  codec={f.codec} chunks={len(f.chunks)}')
        cnt = collections.Counter(c.tag for c in f.chunks if c.tag not in ('free', 'junk'))
        print('   ', dict(sorted(cnt.items())))
        for c, ids in f.cast_sections():
            types = collections.Counter()
            for i, cid in enumerate(ids):
                if cid <= 0:
                    continue
                typ, info, spec = f.cast_member(cid)
                types[CAST_TYPES.get(typ, typ)] += 1
            print(f'    cast {c.id}: {sum(1 for i in ids if i > 0)} leden {dict(types)}')


if __name__ == '__main__':
    main()
