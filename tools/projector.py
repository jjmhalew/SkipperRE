"""Films uit een Director 5-projector (start32.exe) halen.

    python tools/projector.py game/start32.exe extract/

De projector bevat een 'APPL'-RIFX (via de '59JP'-kop) met een mmap van File-chunks. Een ingebedde film
is een gewone XFIR, maar alle offsets in zijn mmap zijn absoluut in de exe; die worden hier
terugberekend zodat er een losse .dxr ontstaat.
"""
import struct, sys, os


def rebase(exe, base):
    """XFIR op offset base -> zelfstandige bytes."""
    size = struct.unpack_from('<I', exe, base + 4)[0] + 8
    out = bytearray(exe[base:base + size])
    imap_mmap = struct.unpack_from('<I', out, 12 + 8 + 4)[0]
    struct.pack_into('<I', out, 12 + 8 + 4, imap_mmap - base)
    m = imap_mmap - base
    assert out[m:m + 4] == b'pamm', out[m:m + 4]
    hl, el = struct.unpack_from('<HH', out, m + 8)
    used = struct.unpack_from('<i', out, m + 8 + 8)[0]
    for i in range(used):
        e = m + 8 + hl + i * el
        off = struct.unpack_from('<i', out, e + 8)[0]
        if off >= base:
            struct.pack_into('<i', out, e + 8, off - base)
    return bytes(out)


def main():
    exe = open(sys.argv[1], 'rb').read()
    dst = sys.argv[2]
    pj = exe.rfind(b'59JP')
    appl = struct.unpack_from('<I', exe, pj + 4)[0]
    assert exe[appl:appl + 4] == b'XFIR' and exe[appl + 8:appl + 12] == b'LPPA'
    # mmap van de APPL
    mm = struct.unpack_from('<I', exe, appl + 24)[0]
    hl, el = struct.unpack_from('<HH', exe, mm + 8)
    used = struct.unpack_from('<i', exe, mm + 16)[0]
    n = 0
    for i in range(used):
        e = mm + 8 + hl + i * el
        tag = exe[e:e + 4][::-1].decode('latin1')
        size, off = struct.unpack_from('<Ii', exe, e + 4)
        print(tag, size, off)
        if tag == 'File' and exe[off:off + 4] == b'XFIR':
            data = rebase(exe, off)
            codec = data[8:12][::-1].decode()
            name = 'start.dxr' if n == 0 and codec == 'MV93' else f'embedded{n}.{"dxr" if codec == "MV93" else "cxt"}'
            open(os.path.join(dst, name), 'wb').write(data)
            print('  ->', name, len(data), codec)
            n += 1


if __name__ == '__main__':
    main()
