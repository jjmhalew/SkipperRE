"""Lingo-bytecode (Director 5) disassembler.

    python tools/lingodis.py extract/Magnus.dxr            # alle handlers naar stdout
    python tools/lingodis.py --out out/lingo extract/*.dxr extract/*.cxt
    python tools/lingodis.py --stats extract/*.dxr extract/*.cxt   # gebruik van ext-calls/properties

Alle Lingo-chunks (Lctx/Lnam/Lscr) zijn big-endian, ook in een XFIR-bestand.
"""
import struct, sys, os, collections, argparse
sys.path.insert(0, os.path.dirname(__file__))
from rifx import RifxFile

OPS1 = {0x01: 'ret', 0x02: 'retfactory', 0x03: 'pushzero', 0x04: 'mul', 0x05: 'add', 0x06: 'sub',
        0x07: 'div', 0x08: 'mod', 0x09: 'inv', 0x0a: 'joinstr', 0x0b: 'joinpadstr', 0x0c: 'lt',
        0x0d: 'lteq', 0x0e: 'nteq', 0x0f: 'eq', 0x10: 'gt', 0x11: 'gteq', 0x12: 'and', 0x13: 'or',
        0x14: 'not', 0x15: 'containsstr', 0x16: 'contains0str', 0x17: 'getchunk', 0x18: 'hilitechunk',
        0x19: 'ontospr', 0x1a: 'intospr', 0x1b: 'getfield', 0x1c: 'starttell', 0x1d: 'endtell',
        0x1e: 'pushlist', 0x1f: 'pushproplist', 0x21: 'swap'}
OPSN = {0x41: 'pushint8', 0x42: 'pusharglistnoret', 0x43: 'pusharglist', 0x44: 'pushcons',
        0x45: 'pushsymb', 0x46: 'pushvarref', 0x48: 'getglobal2', 0x49: 'getglobal', 0x4a: 'getprop',
        0x4b: 'getparam', 0x4c: 'getlocal', 0x4e: 'setglobal2', 0x4f: 'setglobal', 0x50: 'setprop',
        0x51: 'setparam', 0x52: 'setlocal', 0x53: 'jmp', 0x54: 'endrepeat', 0x55: 'jmpifz',
        0x56: 'localcall', 0x57: 'extcall', 0x58: 'objcallv4', 0x59: 'put', 0x5a: 'putchunk',
        0x5b: 'deletechunk', 0x5c: 'get', 0x5d: 'set', 0x5f: 'getmovieprop', 0x60: 'setmovieprop',
        0x61: 'getobjprop', 0x62: 'setobjprop', 0x63: 'tellcall', 0x64: 'peek', 0x65: 'pop',
        0x66: 'theentity', 0x67: 'objcall', 0x6d: 'pushchunkvarref', 0x6e: 'pushint16',
        0x6f: 'pushint32', 0x70: 'getchainedprop', 0x71: 'pushfloat32', 0x72: 'gettoplevelprop',
        0x73: 'newobj'}
NAME_OPS = {'pushsymb', 'getglobal', 'getglobal2', 'setglobal', 'setglobal2', 'getprop', 'setprop',
            'extcall', 'objcall', 'getmovieprop', 'setmovieprop', 'getobjprop', 'setobjprop',
            'tellcall', 'pushvarref', 'getchainedprop', 'gettoplevelprop', 'newobj', 'objcallv4'}


def pstrs(b, off, cnt):
    out = []
    for _ in range(cnt):
        n = b[off]
        out.append(b[off + 1:off + 1 + n].decode('latin1'))
        off += 1 + n
    return out


class Handler:
    pass


class Script:
    def __init__(self, b, names):
        self.names = names
        be = lambda f, o: struct.unpack_from('>' + f, b, o)
        self.number = be('H', 18)[0]
        self.flags, _, self.cast_id, self.factory_name = be('IhiH', 38)
        o = 50
        (hvc, hvo, hvs, pc, po, gc, go, hc, ho, lc, lo, ldc, ldo) = be('HIIHIHIHIHIII', o)
        self.props = [self.name(x) for x in be('%dh' % pc, po)] if pc else []
        self.globals = [self.name(x) for x in be('%dh' % gc, go)] if gc else []
        # literals
        self.literals = []
        for i in range(lc):
            typ, off = be('II', lo + i * 8)
            if typ == 4:
                self.literals.append(off)  # integer
            elif typ in (1, 2):  # string / symbool?
                n = be('I', ldo + off)[0]
                self.literals.append(b[ldo + off + 4:ldo + off + 4 + n].rstrip(b'\0').decode('latin1'))
            elif typ == 9:
                n = be('I', ldo + off)[0]
                raw = b[ldo + off + 4:ldo + off + 4 + n]
                self.literals.append(ext80(raw) if n == 10 else struct.unpack('>d', raw)[0])
            else:
                self.literals.append(('lit?', typ, off))
        self.handlers = []
        for i in range(hc):
            h = Handler()
            (h.name_id, h.vpos, h.clen, h.coff, ac, ao, lcnt, loff, gcnt, goff,
             _u1, _u2, h.lines, h.loff) = be('hHIIHIHIHIIHHI', ho + i * 42)
            h.name = self.name(h.name_id)
            h.args = [self.name(x) for x in be('%dh' % ac, ao)] if ac else []
            h.locals = [self.name(x) for x in be('%dh' % lcnt, loff)] if lcnt else []
            h.hglobals = [self.name(x) for x in be('%dh' % gcnt, goff)] if gcnt else []
            h.code = b[h.coff:h.coff + h.clen]
            self.handlers.append(h)

    def name(self, i):
        return self.names[i] if 0 <= i < len(self.names) else f'#name{i}'


def ext80(raw):
    """80-bit IEEE extended (Mac SANE) -> float."""
    exp = struct.unpack('>H', raw[:2])[0]
    mant = struct.unpack('>Q', raw[2:10])[0]
    sign = -1 if exp & 0x8000 else 1
    exp &= 0x7fff
    if exp == 0 and mant == 0:
        return 0.0
    return sign * mant * 2.0 ** (exp - 16383 - 63)


def decode(code):
    """-> [(pos, opnaam, arg)]"""
    i, out = 0, []
    while i < len(code):
        op = code[i]
        pos = i
        i += 1
        if op >= 0x40:
            base = 0x40 + op % 0x40
            if op >= 0xc0:
                arg = struct.unpack('>i', code[i:i + 4])[0]; i += 4
            elif op >= 0x80:
                arg = struct.unpack('>H', code[i:i + 2])[0]; i += 2
                if base in (0x41, 0x6e):  # pushint signed (2 bytes)
                    arg = struct.unpack('>h', code[i - 2:i])[0]
            else:
                arg = code[i]; i += 1
                if base == 0x41 and arg >= 0x80:  # pushint8 is signed
                    arg -= 256
            out.append((pos, OPSN.get(base, f'op{base:02x}'), arg))
        else:
            out.append((pos, OPS1.get(op, f'op{op:02x}'), None))
    return out


def scripts_of(rf):
    """Alle scripts in een bestand: [(lctx chunk id, index, Script)]."""
    res = []
    for lctx in rf.by_tag.get('Lctx', []) + rf.by_tag.get('LctX', []):
        b = rf.chunk_data(lctx)
        if len(b) < 36:  # lege context (cast zonder scripts)
            continue
        (_, _, cnt, cnt2, eoff, _, _, _, _, lnam_id) = struct.unpack_from('>IIIIHHIIIi', b, 0)
        nb = rf.chunk_data(lnam_id)
        noff, ncnt = struct.unpack_from('>HH', nb, 16)
        names = pstrs(nb, noff, ncnt)
        for k in range(cnt):
            _, sec, _, _ = struct.unpack_from('>iiHH', b, eoff + k * 12)
            if sec < 0 or sec >= len(rf.chunks) or rf.chunks[sec].tag != 'Lscr':
                continue
            res.append((lctx.id, k + 1, Script(rf.chunk_data(sec), names)))
    return res


def fmt_arg(s, op, arg):
    if op in NAME_OPS:
        return s.name(arg)
    if op == 'pushcons':
        v = s.literals[arg // 8] if arg // 8 < len(s.literals) else ('lit', arg)
        return repr(v)
    return '' if arg is None else str(arg)


def dump(rf, fp):
    for lid, idx, s in scripts_of(rf):
        fp.write(f'\n-- script {idx} (Lctx {lid}) castID={s.cast_id} flags={s.flags:#x}'
                 f' props={s.props} globals={s.globals}\n')
        for h in s.handlers:
            fp.write(f'on {h.name} {", ".join(h.args)}\n')
            if h.locals:
                fp.write(f'  -- locals: {", ".join(h.locals)}\n')
            for pos, op, arg in decode(h.code):
                fp.write(f'  [{pos:4d}] {op:<16} {fmt_arg(s, op, arg)}\n')
            fp.write('end\n')


def stats(paths):
    c = collections.Counter()
    nh = ns = 0
    for p in paths:
        rf = RifxFile(p)
        for lid, idx, s in scripts_of(rf):
            ns += 1
            local = {h.name for h in s.handlers}
            for h in s.handlers:
                nh += 1
                ins = decode(h.code)
                for j, (pos, op, arg) in enumerate(ins):
                    if op in ('extcall', 'objcall', 'getmovieprop', 'setmovieprop', 'getobjprop',
                              'setobjprop', 'tellcall', 'newobj', 'getchainedprop', 'gettoplevelprop'):
                        c[(op, s.name(arg))] += 1
                    elif op in ('get', 'set', 'theentity'):
                        prev = ins[j - 1] if j else None
                        key = prev[2] if prev and prev[1] in ('pushint8', 'pushzero') else '?'
                        if prev and prev[1] == 'pushzero':
                            key = 0
                        c[(op, f'{arg}/{key}')] += 1
                    elif op.startswith('op'):
                        c[('UNKNOWN', op)] += 1
                    else:
                        c[('op', op)] += 1
    print(f'{ns} scripts, {nh} handlers')
    for k, v in sorted(c.items(), key=lambda kv: (kv[0][0], -kv[1])):
        print(f'{v:6d}  {k[0]:<14} {k[1]}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--out')
    ap.add_argument('--stats', action='store_true')
    a = ap.parse_args()
    if a.stats:
        return stats(a.files)
    for p in a.files:
        rf = RifxFile(p)
        if a.out:
            os.makedirs(a.out, exist_ok=True)
            with open(os.path.join(a.out, os.path.basename(p) + '.lasm'), 'w', encoding='utf-8') as fp:
                dump(rf, fp)
        else:
            dump(rf, sys.stdout)


if __name__ == '__main__':
    main()
