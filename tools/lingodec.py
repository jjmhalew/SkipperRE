"""Lingo-decompiler (Director 5 bytecode -> leesbare Lingo).

    python tools/lingodec.py extract/Mmb04.dxr               # naar stdout
    python tools/lingodec.py --out out/src extract/*.dxr extract/*.cxt

Reconstrueert expressies uit de stackcode en herkent if/else en repeat-lussen. Waar het patroon niet
past valt hij terug op commentaar met de opcode, zodat er nooit iets stil verdwijnt.
In D5 zijn indices van locals/args/literals vermenigvuldigd met 8.
"""
import sys, os, re, argparse
sys.path.insert(0, os.path.dirname(__file__))
from rifx import RifxFile
from lingodis import scripts_of, decode

SPRITE_PROPS = {1: 'type', 2: 'backColor', 3: 'bottom', 4: 'castNum', 5: 'constraint', 6: 'cursor',
                7: 'foreColor', 8: 'height', 9: 'immediate', 10: 'ink', 11: 'left', 12: 'lineSize',
                13: 'locH', 14: 'locV', 15: 'movieRate', 16: 'movieTime', 17: 'pattern', 18: 'puppet',
                19: 'right', 20: 'startTime', 21: 'stopTime', 22: 'stretch', 23: 'top', 24: 'trails',
                25: 'visible', 26: 'volume', 27: 'width', 28: 'blend', 29: 'scriptNum',
                30: 'moveableSprite', 31: 'editableText', 32: 'scoreColor', 33: 'loc', 34: 'rect',
                35: 'memberNum', 36: 'castLibNum', 37: 'member', 38: 'scriptInstanceList',
                39: 'currentTime', 40: 'mostRecentCuePoint', 41: 'tweened', 42: 'name'}
ANIM_PROPS = {1: 'beepOn', 2: 'buttonStyle', 3: 'centerStage', 4: 'checkBoxAccess', 5: 'checkboxType',
              6: 'colorDepth', 7: 'colorQD', 8: 'exitLock', 9: 'fixStageSize', 10: 'fullColorPermit',
              11: 'imageDirect', 12: 'doubleClick', 13: 'key', 14: 'lastClick', 15: 'lastEvent',
              16: 'keyCode', 17: 'lastKey', 18: 'lastRoll', 19: 'timeoutLapsed', 20: 'multiSound',
              21: 'pauseState', 22: 'quickTimePresent', 23: 'selEnd', 24: 'selStart', 25: 'soundEnabled',
              26: 'soundLevel', 27: 'stageColor', 28: '(dontPassEvent)', 29: 'switchColorDepth',
              30: 'timeoutKeyDown', 31: 'timeoutLength', 32: 'timeoutMouse', 33: 'timeoutPlay',
              34: 'timer', 35: 'preLoadRAM', 36: 'videoForWindowsPresent', 37: 'netPresent',
              38: 'safePlayer', 39: 'soundKeepDevice', 40: 'soundMixMedia'}
ANIM2_PROPS = {1: 'perFrameHook', 2: 'number of castMembers', 3: 'number of menus',
               4: 'number of castLibs', 5: 'number of xtras'}
MOVIE_PROPS = {0: 'floatPrecision', 1: 'mouseDownScript', 2: 'mouseUpScript', 3: 'keyDownScript',
               4: 'keyUpScript', 5: 'timeoutScript', 6: 'short time', 7: 'abbr time', 8: 'long time',
               9: 'short date', 10: 'abbr date', 11: 'long date'}
MEMBER_PROPS = {1: 'name', 2: 'text', 3: 'textStyle', 4: 'textFont', 5: 'textHeight', 6: 'textAlign',
                7: 'textSize', 8: 'picture', 9: 'hilite', 10: 'number', 11: 'size', 12: 'loop',
                13: 'duration', 14: 'controller', 15: 'directToStage', 16: 'sound', 17: 'foreColor',
                18: 'backColor', 19: 'type'}
CHUNKS = {1: 'char', 2: 'word', 3: 'item', 4: 'line'}
BINOPS = {'mul': ('*', 5), 'add': ('+', 4), 'sub': ('-', 4), 'div': ('/', 5), 'mod': ('mod', 5),
          'joinstr': ('&', 3), 'joinpadstr': ('&&', 3), 'lt': ('<', 2), 'lteq': ('<=', 2),
          'nteq': ('<>', 2), 'eq': ('=', 2), 'gt': ('>', 2), 'gteq': ('>=', 2), 'and': ('and', 1),
          'or': ('or', 0), 'containsstr': ('contains', 2), 'contains0str': ('starts', 2)}


class E:
    """Expressie-knoop: tekst + prioriteit (9 = atoom)."""
    __slots__ = ('s', 'p', 'args', 'noret', 'val')

    def __init__(self, s, p=9, args=None, noret=False, val=None):
        self.s, self.p, self.args, self.noret, self.val = s, p, args, noret, val

    def __str__(self):
        return self.s


def paren(e, p):
    return f'({e})' if e.p < p else str(e)


def lit(v):
    if isinstance(v, str):
        return '"' + v.replace('"', '" & QUOTE & "').replace('\r', '" & RETURN & "') + '"'
    return repr(v)


class Dec:
    def __init__(self, script, handler):
        self.s, self.h = script, handler
        self.ins = decode(handler.code)
        self.at = {p: i for i, (p, o, a) in enumerate(self.ins)}
        self.lines = []

    # --- namen ---------------------------------------------------------
    def local(self, a):
        i = a // 8
        return self.h.locals[i] if i < len(self.h.locals) else f'local{i}'

    def param(self, a):
        i = a // 8
        return self.h.args[i] if i < len(self.h.args) else f'arg{i}'

    def var(self, st, vt):
        cast = st.pop() if vt == 6 else None
        v = st.pop()
        name = v.val if v.val is not None else str(v)
        if vt in (1, 2, 3):
            return E(str(name) if isinstance(name, str) else str(v))
        if vt == 4:
            return E(self.param(name if isinstance(name, int) else 0))
        if vt == 5:
            return E(self.local(name if isinstance(name, int) else 0))
        if vt == 6:
            return E(f'field {paren(v, 9)}' + (f' of castLib {cast}' if cast and str(cast) != '0' else ''))
        return E(f'var?{vt}({v})')

    def chunk(self, st, target):
        vals = [st.pop() for _ in range(8)]  # lastLine firstLine lastItem firstItem lastWord firstWord lastChar firstChar
        parts = []
        for k, name in ((6, 'char'), (4, 'word'), (2, 'item'), (0, 'line')):
            last, first = vals[k], vals[k + 1]
            if str(first) != '0':
                parts.append(f'{name} {first}' + (f' to {last}' if str(last) not in ('0', str(first)) else ''))
        return E(' of '.join(parts[::-1][::-1]) + f' of {paren(target, 9)}', 8)

    def the(self, st, typ, pid):
        if typ == 0:
            if pid <= 11:
                return f'the {MOVIE_PROPS.get(pid, pid)}'
            s = st.pop()
            return f'the last {CHUNKS.get(pid - 11, pid)} in {s}'
        if typ == 1:
            s = st.pop()
            return f'the number of {CHUNKS.get(pid, pid)}s in {s}'
        if typ == 4:
            ch = st.pop()
            return f'the {"volume" if pid == 1 else pid} of sound {ch}'
        if typ == 6:
            spr = st.pop()
            return f'the {SPRITE_PROPS.get(pid, pid)} of sprite {spr}'
        if typ == 7:
            return f'the {ANIM_PROPS.get(pid, pid)}'
        if typ == 8:
            if pid == 2:
                lib = st.pop()
                return f'the number of castMembers of castLib {lib}'
            return f'the {ANIM2_PROPS.get(pid, pid)}'
        if typ in (9, 10, 11):   # 11 = field-properties (D5), zelfde id's als 9/10
            lib = st.pop()
            mem = st.pop()
            return (f'the {MEMBER_PROPS.get(pid, pid)} of {"member" if typ == 9 else "field"} {mem}'
                    + (f' of castLib {lib}' if str(lib) != '0' else ''))
        return f'the prop{typ}_{pid}'

    # --- blokken ---------------------------------------------------------
    def emit(self, depth, s):
        self.lines.append('  ' * depth + s)

    def run(self):
        self.block(0, len(self.ins), 1, [], None)
        return self.lines

    def target(self, i):
        p, op, a = self.ins[i]
        if op == 'endrepeat':
            return p - a
        return p + a

    def block(self, i, end, depth, st, loop):
        """Instructies [i, end) als statements. loop = (startpos, eindpos) van de omliggende repeat."""
        while i < end:
            p, op, a = self.ins[i]
            if op == 'peek' and a == 0 and self.is_repeat_in(i):
                # repeat with v in lijst: [lijst] peek0 count 1 | peek0 peek2 lteq jmpifz | peek2 peek1 getAt set v
                lst = st.pop() if st else E('?')
                j = i + 7  # jmpifz
                tgt = self.target(j)
                k = self.at[tgt]  # na endrepeat: pop 3
                tmp = [E('?')]
                self.step(self.ins[j + 5][1], self.ins[j + 5][2], tmp, depth)  # de set-instructie
                v = self.lines.pop().strip().split(' = ')[0]
                self.emit(depth, f'repeat with {v} in {lst}')
                self.block(j + 6, k - 3, depth + 1, [], (self.ins[i + 4][0], tgt))
                self.emit(depth, 'end repeat')
                i = k + 1 if k < len(self.ins) and self.ins[k][1] == 'pop' else k
                continue
            if op == 'peek' and a == 0 and st:
                i = self.case(i, end, depth, st, loop)
                continue
            if op == 'jmpifz':
                cond = st.pop() if st else E('?')
                tgt = self.target(i)
                j = self.at.get(tgt, end)
                prev = self.ins[j - 1] if j - 1 > i else None
                if prev and prev[1] == 'endrepeat' and self.target(j - 1) <= p:
                    # repeat while cond ... end repeat
                    self.emit(depth, f'repeat while {cond}')
                    self.block(i + 1, j - 1, depth + 1, [], (self.target(j - 1), tgt))
                    self.emit(depth, 'end repeat')
                    i = j
                    continue
                if prev and prev[1] == 'jmp' and self.target(j - 1) > tgt and not (
                        loop and self.target(j - 1) >= loop[1]):
                    k = self.at.get(self.target(j - 1), end)
                    self.emit(depth, f'if {cond} then')
                    self.block(i + 1, j - 1, depth + 1, [], loop)
                    self.emit(depth, 'else')
                    self.block(j, k, depth + 1, [], loop)
                    self.emit(depth, 'end if')
                    i = k
                    continue
                self.emit(depth, f'if {cond} then')
                self.block(i + 1, min(j, end), depth + 1, [], loop)
                self.emit(depth, 'end if')
                i = j
                continue
            if op == 'jmp':
                t = self.target(i)
                if loop and t >= loop[1]:
                    self.emit(depth, 'exit repeat')
                elif loop and t < p:
                    self.emit(depth, 'next repeat')
                else:
                    self.emit(depth, f'-- jmp {t}')
                i += 1
                continue
            if op == 'endrepeat':
                i += 1
                continue
            if op in ('ret', 'retfactory') and i < len(self.ins) - 1 and not (
                    i > 0 and self.ins[i - 1][1] == 'extcall' and self.s.name(self.ins[i - 1][2]) == 'return'):
                self.emit(depth, 'exit')   # ret midden in de handler (na `return x` staat er al een return)
                i += 1
                continue
            self.step(op, a, st, depth)
            i += 1
        for e in st:
            if e.args is None:
                self.emit(depth, f'-- stack: {e}')

    def is_repeat_in(self, i):
        ops = [o for _, o, _ in self.ins[i:i + 14]]
        return (ops[:8] == ['peek', 'pusharglist', 'extcall', 'pushint8', 'peek', 'peek', 'lteq', 'jmpifz']
                and ops[8:12] == ['peek', 'peek', 'pusharglist', 'extcall'])

    def case(self, i, end, depth, st, loop):
        """case x of ... : [x] (peek0 <v> eq jmpifz L | pop1 <body> jmp END)* [pop1 <otherwise>] END."""
        subj = st.pop()
        self.emit(depth, f'case {subj} of')
        endpos = None
        while i < end and self.ins[i][1] == 'peek' and self.ins[i][2] == 0:
            # waarden verzamelen tot aan jmpifz (meerdere waarden: ... or ...)
            j = i + 1
            tmp = [subj]
            while j < end and self.ins[j][1] != 'jmpifz':
                o, a = self.ins[j][1], self.ins[j][2]
                if o == 'peek':
                    tmp.append(subj)
                else:
                    self.step(o, a, tmp, depth)
                j += 1
            cond = tmp[-1] if tmp else E('?')
            vals = str(cond).replace(f'{subj} = ', '').replace(' or ', ', ')
            tgt = self.target(j)
            k = self.at.get(tgt, end)
            body_start = j + 1
            if body_start < k and self.ins[body_start][1] == 'pop':
                body_start += 1
            body_end = k
            if self.ins[k - 1][1] == 'jmp':
                body_end = k - 1
                endpos = self.target(k - 1)
            self.emit(depth + 1, f'{vals}:')
            self.block(body_start, body_end, depth + 2, [], loop)
            i = k
        stop = self.at.get(endpos, end) if endpos is not None else i
        if i < stop:
            if self.ins[i][1] == 'pop':
                i += 1
            if i < stop:
                self.emit(depth + 1, 'otherwise:')
                self.block(i, stop, depth + 2, [], loop)
        self.emit(depth, 'end case')
        return max(stop, i)

    def call(self, name, args):
        a = args.args or []
        if name == 'return':
            return f'return {a[0]}' if a else 'return'
        return f'{name}({", ".join(map(str, a))})' if not args.noret or a else name

    def step(self, op, a, st, depth):
        s, h = self.s, self.h
        push = st.append
        pop = lambda: st.pop() if st else E('?')
        name = lambda: s.name(a)
        if op == 'pushzero':
            push(E('0', val=0))
        elif op in ('pushint8', 'pushint16', 'pushint32'):
            push(E(str(a), val=a))
        elif op == 'pushfloat32':
            import struct
            push(E(repr(struct.unpack('>f', struct.pack('>i', a))[0])))
        elif op == 'pushcons':
            v = s.literals[a // 8] if a // 8 < len(s.literals) else f'lit{a}'
            push(E(lit(v), val=v))
        elif op == 'pushsymb':
            push(E('#' + name(), val=name()))
        elif op == 'pushvarref':
            push(E(name(), val=('varref', name())))
        elif op in ('getglobal', 'getglobal2', 'getprop'):
            push(E(name(), val=name()))
        elif op == 'getparam':
            push(E(self.param(a)))
        elif op == 'getlocal':
            push(E(self.local(a)))
        elif op in ('setglobal', 'setglobal2', 'setprop'):
            self.emit(depth, f'{name()} = {pop()}')
        elif op == 'setparam':
            self.emit(depth, f'{self.param(a)} = {pop()}')
        elif op == 'setlocal':
            self.emit(depth, f'{self.local(a)} = {pop()}')
        elif op in BINOPS:
            sym, p = BINOPS[op]
            b2, a2 = pop(), pop()
            push(E(f'{paren(a2, p)} {sym} {paren(b2, p + 1)}', p))
        elif op == 'inv':
            push(E(f'-{paren(pop(), 9)}', 8))
        elif op == 'not':
            push(E(f'not {paren(pop(), 2)}', 1))
        elif op in ('pusharglist', 'pusharglistnoret'):
            args = [pop() for _ in range(a)][::-1]
            push(E('<args>', args=args, noret=op == 'pusharglistnoret'))
        elif op in ('extcall', 'tellcall'):
            args = pop()
            c = self.call(name(), args)
            (self.emit(depth, c) if args.noret else push(E(c)))
        elif op == 'localcall':
            args = pop()
            hn = s.handlers[a].name if a < len(s.handlers) else f'handler{a}'
            c = self.call(hn, args)
            (self.emit(depth, c) if args.noret else push(E(c)))
        elif op == 'objcallv4':
            top = st[-2] if a == 6 and len(st) >= 2 else st[-1] if st else None
            if top is not None and isinstance(top.val, tuple) and top.val[0] == 'varref':
                # D4-syntax f(var, ...): de functie staat in de varref, het eerste argument is de NAAM van een
                # variabele (als symbool gecompileerd), zoals symbolp(gEffectNotify) in Speak
                if a == 6:
                    pop()
                fn = pop().val[1]
                args = pop()
                al = list(args.args or [])
                # behalve bij XObjects (INI(#mnew), File(#mReadLine)): daar is het symbool de methode
                if (al and isinstance(al[0].val, str) and al[0].s == '#' + al[0].val
                        and not re.match(r'm[A-Z]|mnew$|mdispose$', al[0].val)):
                    al[0] = E(al[0].val)
                c = f'{fn}({", ".join(map(str, al))})'
                (self.emit(depth, c) if args.noret else push(E(c)))
                return
            obj = self.var(st, a)
            args = pop()
            c = f'{obj}({", ".join(map(str, args.args or []))})'
            (self.emit(depth, c) if args.noret else push(E(c)))
        elif op == 'objcall':
            args = pop()
            al = args.args or []
            c = f'{name()}({", ".join(map(str, al))})'
            (self.emit(depth, c) if args.noret else push(E(c)))
        elif op == 'theentity':
            args = pop()
            push(E(f'the {name()}'))
        elif op == 'get':
            pid = pop().val
            push(E(self.the(st, a, pid if isinstance(pid, int) else 0), 8))
        elif op == 'set':
            pid = pop().val
            v = pop()
            self.emit(depth, f'set {self.the(st, a, pid if isinstance(pid, int) else 0)} to {v}')
        elif op == 'getmovieprop':
            push(E(f'the {name()}'))
        elif op == 'setmovieprop':
            self.emit(depth, f'set the {name()} to {pop()}')
        elif op == 'getobjprop':
            o = pop()
            push(E(f'the {name()} of {paren(o, 9)}', 8))
        elif op == 'setobjprop':
            v = pop()
            o = pop()
            self.emit(depth, f'set the {name()} of {paren(o, 9)} to {v}')
        elif op == 'put':
            kind = {1: 'into', 2: 'after', 3: 'before'}.get(a >> 4, '?')
            var = self.var(st, a & 15)
            self.emit(depth, f'put {pop()} {kind} {var}')
        elif op == 'putchunk':
            kind = {1: 'into', 2: 'after', 3: 'before'}.get(a >> 4, '?')
            var = self.var(st, a & 15)
            ch = self.chunk(st, var)
            self.emit(depth, f'put {pop()} {kind} {ch}')
        elif op == 'deletechunk':
            var = self.var(st, a & 15)
            self.emit(depth, f'delete {self.chunk(st, var)}')
        elif op == 'getchunk':
            s_ = pop()
            push(self.chunk(st, s_))
        elif op == 'hilitechunk':
            lib = pop()
            fld = pop()
            self.emit(depth, f'hilite {self.chunk(st, E("field " + str(fld) + (f" of castLib {lib}" if str(lib) != "0" else "")))}')
        elif op == 'getfield':
            lib = pop()
            f = pop()
            push(E(f'field {paren(f, 9)}' + (f' of castLib {lib}' if str(lib) != '0' else ''), 8))
        elif op == 'ontospr':
            b2, a2 = pop(), pop()
            push(E(f'sprite {a2} intersects {b2}', 2))
        elif op == 'intospr':
            b2, a2 = pop(), pop()
            push(E(f'sprite {a2} within {b2}', 2))
        elif op == 'pushlist':
            args = pop()
            push(E('[' + ', '.join(map(str, args.args or [])) + ']'))
        elif op == 'pushproplist':
            al = pop().args or []
            push(E('[' + ', '.join(f'{al[k]}: {al[k + 1]}' for k in range(0, len(al) - 1, 2)) + ']' if al else '[:]'))
        elif op == 'swap':
            st[-1], st[-2] = st[-2], st[-1]
        elif op == 'peek':
            push(st[-1 - a] if a < len(st) else E('?'))
        elif op == 'pop':
            for _ in range(a):
                if st:
                    e = st.pop()
                    if e.args is None and ('(' in e.s):
                        self.emit(depth, str(e))
        elif op == 'starttell':
            self.emit(depth, f'tell {pop()}')
        elif op == 'endtell':
            self.emit(depth, 'end tell')
        elif op in ('ret', 'retfactory'):
            pass
        elif op == 'newobj':
            args = pop()
            push(E(f'new {name()}({", ".join(map(str, args.args or []))})'))
        else:
            self.emit(depth, f'-- {op} {a}')


def decompile(rf, fp):
    for lid, idx, s in scripts_of(rf):
        mem = s.cast_id & 0xffff
        fp.write(f'\n-- ===== script {idx}: member {s.cast_id >> 16}:{mem} =====\n')
        if s.props:
            fp.write('property ' + ', '.join(s.props) + '\n')
        if s.globals:
            fp.write('global ' + ', '.join(s.globals) + '\n')
        for h in s.handlers:
            fp.write(f'\non {h.name} {", ".join(h.args)}\n')
            if [g for g in h.hglobals if not g.startswith('#name')]:
                fp.write('  global ' + ', '.join(g for g in h.hglobals if not g.startswith('#name')) + '\n')
            try:
                for ln in Dec(s, h).run():
                    fp.write(ln + '\n')
            except Exception as e:  # nooit stil: dan de disassembly
                fp.write(f'  -- DECOMPILE-FOUT: {e!r}\n')
                for p, op, a in decode(h.code):
                    fp.write(f'  -- [{p}] {op} {a if a is not None else ""}\n')
            fp.write('end\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--out')
    a = ap.parse_args()
    for p in a.files:
        rf = RifxFile(p)
        if a.out:
            os.makedirs(a.out, exist_ok=True)
            with open(os.path.join(a.out, os.path.basename(p) + '.ls'), 'w', encoding='utf-8') as fp:
                decompile(rf, fp)
        else:
            decompile(rf, sys.stdout)


if __name__ == '__main__':
    main()
