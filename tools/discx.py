# Unpack the data track of a CD image (ISO, BIN with or without CUE, CloneCD .img) into a directory.
#   python tools/discx.py <image> <outdir> [--list]
# Raw images (2352-byte sectors) are scanned for the first data sector that has an ISO9660 volume descriptor 16
# sectors on; Joliet names are used when the disc has them.
import os, struct, sys

def open_image(path):
    f = open(path, 'rb')
    size = os.path.getsize(path)
    f.seek(16 * 2048)
    if f.read(6)[1:6] == b'CD001':
        return f, 0, 2048, 0
    sync = b'\x00' + b'\xff' * 10 + b'\x00'
    lba = 0
    while lba * 2352 < size:
        f.seek(lba * 2352)
        h = f.read(16)
        if h[:12] == sync and h[15] in (1, 2):
            hdr = 24 if h[15] == 2 else 16
            for k in range(600):
                f.seek((lba + k + 16) * 2352 + hdr)
                if f.read(6)[1:6] == b'CD001':
                    return f, lba + k, 2352, hdr
            lba += 600
        lba += 1
    raise SystemExit('no ISO9660 data track in ' + path)

def main():
    img, out = sys.argv[1], sys.argv[2]
    listing = '--list' in sys.argv
    f, start, ss, hdr = open_image(img)
    def sec_abs(lba, n=1):
        b = b''
        for i in range(n):
            f.seek((lba + i) * ss + hdr)
            b += f.read(2048)
        return b
    pvd = None; joliet = False
    for i in range(16, 40):
        d = sec_abs(start + i)   # ISO9660 sector numbers are disc LBAs; a raw image starts at LBA 0
        if d[0] == 255: break
        if d[0] == 1 and pvd is None: pvd = d
        if d[0] == 2 and d[88:91] in (b'%/@', b'%/C', b'%/E'): pvd = d; joliet = True; break
    root = pvd[156:190]
    def walk(lba, size, path):
        data = sec_abs(lba, (size + 2047) // 2048)
        i = 0
        while i < len(data):
            l = data[i]
            if l == 0:
                i = (i // 2048 + 1) * 2048; continue
            r = data[i:i + l]; i += l
            elba, esz, flags, nl = struct.unpack_from('<I', r, 2)[0], struct.unpack_from('<I', r, 10)[0], r[25], r[32]
            name = r[33:33 + nl]
            if name in (b'\x00', b'\x01'): continue
            n = name.decode('utf-16-be') if joliet else name.decode('latin1')
            n = n.split(';')[0].rstrip('.')
            p = os.path.join(path, n)
            if flags & 2:
                if not listing: os.makedirs(p, exist_ok=True)
                walk(elba, esz, p)
            else:
                if listing: print('%10d %s' % (esz, os.path.relpath(p, out))); continue
                with open(p, 'wb') as o:
                    left = esz; k = 0
                    while left > 0:
                        b = sec_abs(elba + k); k += 1
                        o.write(b[:min(left, 2048)]); left -= 2048
    if not listing: os.makedirs(out, exist_ok=True)
    print('data track at sector %d, %d-byte sectors, %s' % (start, ss, 'Joliet' if joliet else 'ISO9660'), file=sys.stderr)
    walk(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], out)

main()
