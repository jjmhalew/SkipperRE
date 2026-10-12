"""Dekking van de Lingo-code van het origineel door de engine van de port, plus een coverage map (HTML).

    python tools/coverage.py                                   # extract/*.dxr + *.cxt, rapport naar stdout
    python tools/coverage.py --trace out/trace.log --write --html
    python tools/coverage.py extract/Mmb04.dxr --html out/mmb04.html

Eenheid = één handler van één script van één bestand, grootte = lengte van zijn bytecode. Klassen, in deze volgorde:
  runs       binnengegaan in een --trace-log (`skipper --trace ... 2> out/trace.log`; alleen de naam staat erin)
  missing    gebruikt iets wat de engine niet heeft: builtin/call, XObject(-methode), DLL-functie, movie-property,
             the-entity, get/set-property, objectproperty, opcode, of het is een event dat de engine nooit stuurt
  unused     geen Director-event en de naam komt nergens in de bytecode voor (call, symbool, woord in een string)
  supported  de rest: statisch is alles wat hij gebruikt geïmplementeerd
Wat de engine heeft komt uit src/*.c: vm_register, de _stricmp-tabellen van player.c en xobj.c, de cases van
player_get/player_set/sprite_get/sprite_set en van vm_call, en de sym("...")-events. --write zet het resultaat in
docs/COVERAGE.md (tussen de markers), --html [FILE] schrijft de map (standaard out/coverage.html: die bevat namen uit
het origineel en blijft buiten de repository). Zie docs/COVERAGE.md voor wat de klassen wel en niet zeggen.
"""
import argparse, collections, html as htmlmod, json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rifx import RifxFile, parse_info
from lingodis import scripts_of, decode, OPS1, OPSN
from lingodec import SPRITE_PROPS, ANIM_PROPS, ANIM2_PROPS, MOVIE_PROPS, MEMBER_PROPS, CHUNKS

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
CLASSES = ['runs', 'supported', 'unused', 'missing']
WHAT = {'runs': 'entered during a traced run (--trace)',
        'supported': 'everything it uses is implemented in the engine (statically)',
        'unused': 'not an event handler, and nothing in the game names it',
        'missing': 'uses something the engine does not implement'}
# Director-events (D4/D5): handlers die het systeem op naam aanroept, ook als niets in de bytecode ze noemt
EVENTS = {e.lower() for e in (
    'mouseDown mouseUp mouseEnter mouseLeave mouseWithin mouseUpOutside rightMouseDown rightMouseUp keyDown keyUp '
    'enterFrame exitFrame prepareFrame idle startMovie stopMovie prepareMovie beginSprite endSprite stepFrame new '
    'birth timeout activateWindow deactivateWindow closeWindow openWindow moveWindow resizeWindow zoomWindow '
    'cuePassed startUp').split()}
KINDS = {'call': 'builtin or handler', 'xobject': 'XObject / Xtra', 'xmethod': 'XObject method',
         'dll': 'DLL function', 'the': 'the-entity', 'movieprop': 'movie property', 'setmovieprop': 'movie property (set)',
         'objprop': 'object property', 'setobjprop': 'object property (set)', 'get': 'property (get)',
         'set': 'property (set)', 'opcode': 'opcode', 'event': 'event never sent'}
OPCODE = {v: k for k, v in list(OPS1.items()) + list(OPSN.items())}
WORD = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


# ------------------------------------------------------------------ wat de engine heeft (src/*.c)
def _strip_c(t):
    """Commentaar weg; strings en tekens blijven staan."""
    return re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*',
                  lambda m: m.group(0) if m.group(0)[0] in '"\'' else ' ', t, flags=re.S)


def _skip_lit(t, k):
    """Na een string- of tekenliteral die op k begint."""
    q, k = t[k], k + 1
    while k < len(t) and t[k] != q:
        k += 2 if t[k] == '\\' else 1
    return k + 1


def _block(t, i):
    """Tekst tussen de accolade op/na positie i en zijn tegenhanger."""
    i = t.index('{', i)
    d, j = 0, i
    while j < len(t):
        c = t[j]
        if c in '"\'':
            j = _skip_lit(t, j)
            continue
        if c == '{':
            d += 1
        elif c == '}':
            d -= 1
            if d == 0:
                return t[i + 1:j]
        j += 1
    return t[i + 1:]


def _func(t, name):
    m = re.search(r'^[A-Za-z_][\w \t\*]*\b' + re.escape(name) + r'\s*\([^;{]*\)\s*\{', t, re.M)
    return _block(t, m.end() - 1) if m else ''


def _lits(body, var):
    return {s.lower() for s in re.findall(r'_stricmp\(\s*' + re.escape(var) + r'\s*,\s*"([^"]*)"\s*\)', body)}


def _cases(body):
    """De buitenste switch van body -> [(set van case-getallen, tekst)]; labels zonder eigen tekst delen de volgende."""
    i = body.find('switch')
    if i < 0:
        return []
    sw = _block(body, i)
    out, labels, start, depth, k = [], [], None, 0, 0
    lab = re.compile(r'case\s+(\d+)\s*:|default\s*:')
    while k < len(sw):
        c = sw[k]
        if c in '"\'':
            k = _skip_lit(sw, k)
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        elif depth == 0 and (k == 0 or not (sw[k - 1].isalnum() or sw[k - 1] == '_')):
            m = lab.match(sw, k)
            if m:
                if start is not None and sw[start:k].strip():
                    out.append((set(labels), sw[start:k]))
                    labels = []
                if m.group(1):
                    labels.append(int(m.group(1)))
                start = k = m.end()
                continue
        k += 1
    if start is not None and labels:
        out.append((set(labels), sw[start:]))
    return out


def _ids(text, funcs):
    """Property-id's die een case-blok afhandelt; None = elke id; 'member' = via MEMBER_PROPS."""
    if 'MEMBER_PROPS' in text:
        return 'member'
    for fn in ('sprite_get', 'sprite_set'):
        if fn + '(' in text:
            return {int(x) for x in re.findall(r'case\s+(\d+)\s*:', funcs.get(fn, ''))}
    ids = {int(x) for x in re.findall(r'case\s+(\d+)\s*:', text)}
    ids |= {int(x) for x in re.findall(r'\bid\s*==\s*(\d+)', text)}
    for a, b in re.findall(r'\bid\s*>=\s*(\d+)\s*&&\s*id\s*<=\s*(\d+)', text):
        ids |= set(range(int(a), int(b) + 1))
    for a in re.findall(r'\bid\s*>\s*(\d+)', text):   # the last char/word/item/line: id - 11 = soort chunk 1..4
        ids |= set(range(int(a) + 1, int(a) + 5))
    if not ids and 'return' in text:
        return None
    return ids


def engine_sets(src):
    """Alles wat de engine implementeert, uit de C-bronnen; namen in kleine letters (Lingo is hoofdletterongevoelig)."""
    txt = {}
    for fn in sorted(os.listdir(src)):
        if fn.endswith('.c'):
            txt[fn] = _strip_c(open(os.path.join(src, fn), encoding='utf-8', errors='replace').read())
    every = '\n'.join(txt.values())
    lingo, player, xobj = txt.get('lingo.c', ''), txt.get('player.c', ''), txt.get('xobj.c', '')
    E = {}
    E['builtins'] = {s.lower() for s in re.findall(r'\bvm_register\(\s*"([^"]+)"', every)}
    # in vm_call/lingo_do zelf afgehandeld: return, pass, go, ...
    E['vmcalls'] = {s.lower() for s in re.findall(r'\bnm\s*==\s*sym\("(\w+)"\)', lingo)}
    E['factories'] = _lits(_func(xobj, 'xobj_factory'), 'name')
    xc = _func(xobj, 'xobj_call')
    E['xmethods'] = _lits(xc, 'm')
    E['dllfns'] = _lits(xc, 'g->fn')
    E['the'] = _lits(_func(player, 'player_the'), 'n')
    E['movieprop'] = _lits(_func(player, 'player_movie_prop'), 'n') | E['the']
    E['setmovieprop'] = _lits(_func(player, 'player_set_movie_prop'), 'n')
    mg, ms = _lits(_func(player, 'member_get'), 'n'), _lits(_func(player, 'member_set'), 'n')
    rect = _lits(_func(player, 'rect_idx'), 'n')
    E['member_get'], E['member_set'] = mg, ms
    E['objprop'] = _lits(_func(player, 'player_objprop'), 'n') | rect | mg
    E['setobjprop'] = _lits(_func(player, 'player_set_objprop'), 'n') | rect | ms
    m = re.search(r'MEMBER_PROPS\[\]\s*=\s*\{(.*?)\};', player, re.S)
    E['member_props'] = {}
    if m:
        for i, s in enumerate(re.findall(r'NULL|"[^"]*"', m.group(1))):
            if s != 'NULL':
                E['member_props'][i] = s.strip('"')
    funcs = {f: _func(player, f) for f in ('sprite_get', 'sprite_set')}
    for key, fn in (('get', 'player_get'), ('set', 'player_set')):
        E[key] = {}
        for labels, text in _cases(_func(player, fn)):
            ids = _ids(text, funcs)
            for t in labels:
                E[key][t] = ids
    E['opcodes'] = {int(x, 16) for x in re.findall(r'case\s+(0x[0-9a-fA-F]+)\s*:', _func(lingo, 'vm_call'))}
    # events die de engine stuurt: de strings in sym(...) (ook sym(right ? "rightMouseDown" : "mouseDown"))
    ev = set()
    for m in re.finditer(r'\bsym\(', every):
        d, k = 1, m.end()
        while k < len(every) and d:
            d += {'(': 1, ')': -1}.get(every[k], 0)
            k += 1
        ev |= {s.lower() for s in re.findall(r'"(\w+)"', every[m.end():k])}
    E['events'] = ev & EVENTS
    return E


# ------------------------------------------------------------------ de handlers van het spel
def _members(rf):
    """Castleden met een script: (lctx-eigenaar -> (nummer -> (naam, scripttype), scriptId -> nummer)), D4-vlag.
    Alleen voor de labels; lukt het niet, dan blijft het bij het scriptnummer."""
    d4 = False
    base = 1
    for cf in (rf.by_tag.get('VWCF', []) + rf.by_tag.get('DRCF', []))[:1]:
        b = rf.chunk_data(cf)
        d4 = struct.unpack_from('>H', b, 36)[0] < 0x4c1
        base = max(1, struct.unpack_from('>H', b, 12)[0])
    owner_of = {}
    for owner, kids in rf.children.items():
        for tag, sec in kids.items():
            owner_of[tag, sec] = owner
    res = {}
    for c, ids in rf.cast_sections():
        owner = owner_of.get(('CAS*', c.id), 1024)
        num, sid = {}, {}
        first = base if owner == 1024 else 1
        for i, cid in enumerate(ids):
            if cid <= 0:
                continue
            try:
                cb = rf.chunk_data(cid)
                if d4:
                    dl, il = struct.unpack_from('>HI', cb, 0)
                    typ, spec, info = cb[6], cb[8:6 + dl] if dl > 1 else b'', cb[6 + dl:6 + dl + il]
                else:
                    typ, il, sl = struct.unpack_from('>III', cb, 0)
                    info, spec = cb[12:12 + il], cb[12 + il:12 + il + sl]
                if typ != 11:
                    continue
                name = parse_info(info).get('name', '') if len(info) >= 20 else ''
                stype = struct.unpack_from('>H', spec, 0)[0] if len(spec) >= 2 else 0
                num[first + i] = (name, stype)
                if len(info) >= 20:
                    sid[struct.unpack_from('>I', info, 16)[0]] = first + i
            except (struct.error, IndexError, ValueError):
                continue
        res[owner] = (num, sid)
    lctx_owner = {sec: owner for (tag, sec), owner in owner_of.items() if tag in ('Lctx', 'LctX')}
    return res, lctx_owner, d4


def _scan(s, h):
    """Stack-simulatie van één handler -> (needs [(soort, sleutel)], namen die hij noemt, symbolen, globals).
    Waarden: ('int', n) ('sym', s) ('str', s) ('varref', s) ('args', [..], noret) of None (onbekend)."""
    needs, uses, syms = [], set(), set()
    glob = {g.lower() for g in list(getattr(s, 'globals', [])) + list(getattr(h, 'hglobals', []))}
    st = []
    pop = lambda: st.pop() if st else None

    def popn(n):
        return [pop() for _ in range(n)][::-1]

    def call(name, args):
        uses.add(name.lower())
        a = args[1] if args and args[0] == 'args' else []
        first = a[0] if a else None
        meth = first[1] if first and first[0] == 'sym' else None
        needs.append(('call', (name, meth)))
        if name.lower() in ('openxlib', 'xtra') and first and first[0] == 'str':
            needs.append(('xobject', re.sub(r'\.[^.\\/]*$', '', re.split(r'[\\/:]', first[1])[-1])))
        dll(a)

    def dll(a):
        # DLLGlue(mNew, dll, "Functie", ret, args)
        if len(a) == 5 and a[0] and a[0][0] == 'sym' and a[0][1].lower() == 'mnew' and a[2] and a[2][0] == 'str':
            needs.append(('dll', a[2][1]))

    for pos, op, arg in decode(h.code):
        code = OPCODE.get(op, int(op[2:], 16) if op.startswith('op') else None)
        needs.append(('opcode', (op, code)))
        name = s.name(arg) if arg is not None else ''
        if op in ('ret', 'retfactory', 'endtell', 'jmp', 'endrepeat'):
            pass
        elif op == 'pushzero':
            st.append(('int', 0))
        elif op in ('pushint8', 'pushint16', 'pushint32'):
            st.append(('int', arg))
        elif op == 'pushfloat32':
            st.append(None)
        elif op == 'pushcons':
            i = arg // s.esz
            v = s.literals[i] if i < len(s.literals) else None
            if isinstance(v, str):
                st.append(('str', v))
                uses.update(w.lower() for w in WORD.findall(v))
            else:
                st.append(('int', v) if isinstance(v, int) else None)
        elif op == 'pushsymb':
            st.append(('sym', name))
            uses.add(name.lower())
            syms.add(name.lower())
        elif op == 'pushvarref':
            st.append(('varref', name))
            uses.add(name.lower())
        elif op in ('getglobal', 'getglobal2', 'getprop', 'getparam', 'getlocal', 'getmovieprop', 'gettoplevelprop'):
            st.append(None)
            if op in ('getglobal', 'getglobal2'):
                glob.add(name.lower())
            if op == 'getmovieprop':
                needs.append(('movieprop', name))
        elif op in ('setglobal', 'setglobal2', 'setprop', 'setparam', 'setlocal', 'setmovieprop', 'jmpifz', 'starttell'):
            pop()
            if op in ('setglobal', 'setglobal2'):
                glob.add(name.lower())
            if op == 'setmovieprop':
                needs.append(('setmovieprop', name))
        elif op in ('mul', 'add', 'sub', 'div', 'mod', 'joinstr', 'joinpadstr', 'lt', 'lteq', 'nteq', 'eq', 'gt',
                    'gteq', 'and', 'or', 'containsstr', 'contains0str', 'ontospr', 'intospr', 'getfield'):
            popn(2)
            st.append(None)
        elif op in ('inv', 'not', 'pushlist', 'pushproplist', 'getchainedprop'):
            pop()
            st.append(None)
        elif op == 'getobjprop':
            pop()
            st.append(None)
            needs.append(('objprop', name))
        elif op == 'setobjprop':
            popn(2)
            needs.append(('setobjprop', name))
        elif op == 'getchunk':
            popn(9)
            st.append(None)
        elif op == 'hilitechunk':
            popn(10)
        elif op == 'swap':
            if len(st) >= 2:
                st[-1], st[-2] = st[-2], st[-1]
        elif op in ('pusharglist', 'pusharglistnoret'):
            st.append(('args', popn(arg), op == 'pusharglistnoret'))
        elif op == 'localcall':
            a = pop()
            hn = s.handlers[arg].name if arg < len(s.handlers) else ''
            uses.add(hn.lower())
            if a and a[0] == 'args':
                dll(a[1])
            if not (a and a[0] == 'args' and a[2]):
                st.append(None)
        elif op in ('extcall', 'tellcall', 'objcall'):
            a = pop()
            call(name, a)
            if not (a and a[0] == 'args' and a[2]):
                st.append(None)
        elif op == 'objcallv4':
            if arg == 6:
                pop()
            vid = pop()
            a = pop()
            lst = a[1] if a and a[0] == 'args' else []
            if vid and vid[0] == 'varref':
                call(vid[1], a)   # D4-syntax f(var, ...): f is een builtin of handler
            else:
                if lst and lst[0] and lst[0][0] == 'sym':
                    needs.append(('xmethod', lst[0][1]))
                dll(lst)
            if not (a and a[0] == 'args' and a[2]):
                st.append(None)
        elif op in ('put', 'putchunk', 'deletechunk'):
            vt = arg & 15
            if vt == 6:
                pop()
            pop()
            if op != 'put':
                popn(8)
            if op != 'deletechunk':
                pop()
        elif op in ('get', 'set'):
            pid = pop()
            i = pid[1] if pid and pid[0] == 'int' else None
            if op == 'set':
                pop()
            nt = 0
            if arg in (9, 10, 11, 13):
                nt = 1 if s.esz == 6 else 2
            elif arg in (4, 6) or (op == 'get' and (arg == 1 or (arg == 0 and (i or 0) > 11) or (arg == 8 and i == 2))):
                nt = 1
            popn(nt)
            if op == 'get':
                st.append(None)
            if i is not None:
                needs.append((op, (arg, i)))
        elif op == 'peek':
            st.append(st[-1 - arg] if arg < len(st) else None)
        elif op == 'pop':
            popn(arg)
        elif op == 'theentity':
            pop()
            st.append(None)
            needs.append(('the', name))
        elif op == 'newobj':
            pop()
            st.append(None)
        elif op == 'pushchunkvarref':
            st.append(None)
        else:
            st.append(None)
    return list(dict.fromkeys(needs)), uses, syms, glob


def default_files():
    d = os.path.join(ROOT, 'extract')
    if not os.path.isdir(d):
        return []
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.lower().endswith(('.dxr', '.cxt')))


def collect(paths):
    """Alle handlers van de bestanden als platte records (dicts), in de volgorde van de scripts."""
    recs = []
    for p in paths:
        rf = RifxFile(p)
        fname = os.path.basename(p)
        try:
            casts, lctx_owner, d4 = _members(rf)
        except Exception:   # alleen voor de labels
            casts, lctx_owner, d4 = {}, {}, False
        off = 0
        for lid, idx, s in scripts_of(rf):
            num, sid = casts.get(lctx_owner.get(lid, 1024), ({}, {}))
            if not num and len(casts) == 1:
                num, sid = next(iter(casts.values()))
            member = sid.get(idx) if d4 else s.cast_id & 0xffff
            mname, stype = num.get(member, ('', 0)) if member else ('', 0)
            for h in s.handlers:
                needs, uses, syms, glob = _scan(s, h)
                recs.append({'file': fname, 'script': idx, 'lctx': lid, 'member': member, 'mname': mname,
                             'stype': stype, 'handler': h.name, 'bytes': len(h.code), 'offset': off,
                             'needs': needs, 'uses': sorted(uses), 'syms': sorted(syms), 'props': list(s.props),
                             'globals': sorted(glob)})
                off += len(h.code)
    return recs


# ------------------------------------------------------------------ trace
TRACE = re.compile(r'^ *> ([A-Za-z_]\w*)\((.*)$')
FIRST = re.compile(r'<offspring (\d+)>|\(script (\d+)\)')


def parse_trace(paths):
    """stderr van `skipper --trace` -> Counter van (handlernaam in kleine letters, scriptnummer of None).
    vm_call schrijft per handler '<2*diepte spaties>> naam(args)'; het script staat er niet bij. Is het eerste argument
    een object of script ('<offspring N>', '(script N)'), dan is N het castlid van zijn script."""
    c = collections.Counter()
    for p in paths:
        with open(p, encoding='latin-1', newline='') as f:
            for line in f.read().split('\n'):
                m = TRACE.match(line)
                if not m:
                    continue
                a = FIRST.match(m.group(2))
                c[m.group(1).lower(), int(a.group(1) or a.group(2)) if a else None] += 1
    return c


def mark_runs(recs, trace):
    """-> ({record-index: aantal keer binnengegaan}, [namen uit de trace zonder handler]).
    Een naam telt voor elke handler met die naam (alle bestanden en scripts); met een object/script als eerste
    argument alleen voor de scripts met dat castnummer die de handler hebben (als die er zijn)."""
    by = collections.defaultdict(list)
    for i, r in enumerate(recs):
        by[r['handler'].lower()].append(i)
    runs, unknown = collections.Counter(), []
    for (name, mem), n in trace.items():
        cand = by.get(name, [])
        if not cand:
            unknown.append(name)
        if mem is not None:
            cand = [i for i in cand if recs[i]['member'] == mem] or cand
        for i in cand:
            runs[i] += n
    return runs, sorted(set(unknown))


# ------------------------------------------------------------------ klassen
def prop_label(op, t, i):
    if t == 0:
        n = MOVIE_PROPS.get(i) if i <= 11 else f'last {CHUNKS.get(i - 11, i)} in'
    elif t == 1:
        n = f'number of {CHUNKS.get(i, i)}s in'
    elif t == 4:
        n = 'volume of sound' if i == 1 else f'sound property {i}'
    elif t == 6:
        n = f'{SPRITE_PROPS.get(i, i)} of sprite'
    elif t == 7:
        n = ANIM_PROPS.get(i, i)
    elif t == 8:
        n = ANIM2_PROPS.get(i, i)
    elif t in (9, 10, 11, 13):
        n = f'{MEMBER_PROPS.get(i, i)} of member'
    else:
        n = f'property {i}'
    return f'{"set " if op == "set" else ""}the {n} ({op} {t}/{i})'


def _prop_ok(E, op, t, i):
    if t not in E[op]:
        return False
    ids = E[op][t]
    if ids is None:
        return True
    if ids == 'member':
        n = E['member_props'].get(i)
        return bool(n) and n.lower() in E['member_get' if op == 'get' else 'member_set']
    return i in ids


def classify(recs, E, runs=None):
    """Zet r['class'] en r['missing'] ([(soort, label)]) op elk record. Puur: alleen recs, E en runs."""
    runs = runs or {}
    handlers = {r['handler'].lower() for r in recs}
    props = {p.lower() for r in recs for p in r['props']}
    syms = {x for r in recs for x in r['syms']}
    globs = {x for r in recs for x in r.get('globals', ())}
    users = collections.defaultdict(set)
    for i, r in enumerate(recs):
        for u in r['uses']:
            users[u].add(i)
    callable_ = handlers | E['builtins'] | E['vmcalls'] | E['factories']
    methods = E['xmethods'] | handlers
    for i, r in enumerate(recs):
        miss = []
        for kind, key in r['needs']:
            if kind == 'call':
                name, meth = key
                n = name.lower()
                if n in handlers or n in E['builtins'] or n in E['vmcalls']:
                    continue
                if n in E['factories'] or n in globs:
                    # factory, of een global met een XObject erin: call_builtin_or_handler stap 4
                    if meth and meth.lower() not in (E['xmethods'] if n in E['factories'] else methods):
                        miss.append(('xmethod', meth))
                    continue
                miss.append(('call', name))
            elif kind == 'xmethod':
                if key.lower() not in methods:
                    miss.append(('xmethod', key))
            elif kind == 'xobject':
                if key.lower() not in E['factories']:
                    miss.append(('xobject', key))
            elif kind == 'dll':
                if key.lower() not in E['dllfns']:
                    miss.append(('dll', key))
            elif kind in ('the', 'movieprop', 'setmovieprop'):
                if key.lower() not in E[kind]:
                    miss.append((kind, ('set the ' if kind == 'setmovieprop' else 'the ') + key))
            elif kind in ('objprop', 'setobjprop'):
                # een object (property), proplijst (symbool) of rect/member/venster: genereus
                k = key.lower()
                if k not in E[kind] and k not in props and k not in syms and k != 'ancestor':
                    miss.append((kind, ('set the ' if kind == 'setobjprop' else 'the ') + key + ' of ...'))
            elif kind in ('get', 'set'):
                t, pid = key
                if not _prop_ok(E, kind, t, pid):
                    miss.append((kind, prop_label(kind, t, pid)))
            elif kind == 'opcode':
                op, code = key
                if code is None or code not in E['opcodes']:
                    miss.append(('opcode', op + (f' ({code:#04x})' if code is not None else '')))
        n = r['handler'].lower()
        others = users.get(n, set()) - {i}
        if n in EVENTS and n not in E['events'] and not others:
            miss.append(('event', 'on ' + r['handler']))
        r['missing'] = list(dict.fromkeys(miss))
        r['runs'] = runs.get(i, 0)
        if r['runs']:
            r['class'] = 'runs'
        elif r['missing']:
            r['class'] = 'missing'
        elif n not in EVENTS and not others:
            r['class'] = 'unused'
        else:
            r['class'] = 'supported'
    return recs


# ------------------------------------------------------------------ uitvoer
def where(r):
    m = f'member {r["member"]}' if r['member'] else f'script {r["script"]}'
    return f'{m} "{r["mname"]}"' if r['mname'] else m


def missing_table(recs):
    """{(soort, label): [records]} met de meest gebruikte eerst."""
    t = collections.defaultdict(list)
    for r in recs:
        if r['class'] in ('missing', 'runs'):
            for k in r['missing']:
                t[k].append(r)
    return sorted(t.items(), key=lambda kv: (-len(kv[1]), kv[0]))


def report(recs, detail=True, trace=None):
    out = []
    files = list(dict.fromkeys(r['file'] for r in recs))
    out.append('## Totals\n')
    out.append('Handlers / bytes of bytecode per class.\n')
    out.append('| file | handlers | bytes | ' + ' | '.join(CLASSES) + ' |')
    out.append('|---|---:|---:|' + '---:|' * len(CLASSES))
    tot = collections.Counter()
    totn = collections.Counter()
    for f in files + ['all']:
        rs = recs if f == 'all' else [r for r in recs if r['file'] == f]
        b = collections.Counter()
        n = collections.Counter()
        for r in rs:
            b[r['class']] += r['bytes']
            n[r['class']] += 1
        nb = sum(b.values()) or 1
        cells = ' | '.join(f'{n[c]} / {b[c]:,} ({100.0 * b[c] / nb:.1f} %)' if n[c] else '-' for c in CLASSES)
        name = '**all**' if f == 'all' else f
        out.append(f'| {name} | {len(rs)} | {sum(b.values()):,} | {cells} |')
    for c in CLASSES:
        out.append(f'- **{c}**: {WHAT[c]}')
    if trace:
        out.append(f'\nTrace: {trace}.')
    mt = missing_table(recs)
    out.append('\n## Missing in the engine\n')
    if not mt:
        out.append('Nothing: every builtin, property, entity, XObject call and opcode the game uses is implemented.')
    else:
        out.append('Most used first. "handlers" counts the handlers that use it (also the ones that ran).\n')
        out.append('| what | kind | handlers | bytes | ' + ('examples |' if detail else 'files |'))
        out.append('|---|---|---:|---:|---|')
        for (kind, label), rs in mt:
            if detail:
                ex = ', '.join(f'{r["file"]} {r["handler"]}' for r in rs[:4]) + (' ...' if len(rs) > 4 else '')
            else:
                ex = ', '.join(dict.fromkeys(r['file'] for r in rs))
            out.append(f'| `{label}` | {KINDS[kind]} | {len(rs)} | {sum(r["bytes"] for r in rs):,} | {ex} |')
    un = [r for r in recs if r['class'] == 'unused']
    out.append('\n## Unused\n')
    if detail:
        out.append(f'{len(un)} handlers, {sum(r["bytes"] for r in un):,} bytes.\n')
        for r in un:
            out.append(f'- {r["file"]} {where(r)}: `{r["handler"]}` ({r["bytes"]} bytes)')
    else:
        out.append('Per file (the names are in the stdout report and the coverage map, not here):\n')
        for f in files:
            rs = [r for r in un if r['file'] == f]
            if rs:
                out.append(f'- {f}: {len(rs)} handler{"s" if len(rs) != 1 else ""}, {sum(r["bytes"] for r in rs):,} bytes')
        if not un:
            out.append('- none')
    return '\n'.join(out) + '\n'


def write_doc(text, path):
    doc = open(path, encoding='utf-8').read()
    a, b = '<!-- generated by tools/coverage.py -->', '<!-- end of generated -->'
    i, j = doc.index(a) + len(a), doc.index(b)
    open(path, 'w', encoding='utf-8', newline='\n').write(doc[:i] + '\n' + text + doc[j:])


HTML = r"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SkipperRE coverage map</title>
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wght@500;700;800&family=JetBrains+Mono:wght@400;600&display=swap">
<style>
:root{--bg:#f6f7f5;--panel:#fff;--fg:#1d2321;--mut:#5d6763;--line:#dde2de;--hl:#1d2321;@LIGHT@;color-scheme:light;
--display:"Archivo","Helvetica Neue",Arial,sans-serif;--body:system-ui,-apple-system,"Segoe UI",sans-serif;--mono:"JetBrains Mono",ui-monospace,Consolas,monospace}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#141816;--panel:#1b201e;--fg:#e4e8e5;--mut:#98a39e;--line:#2c3431;--hl:#fff;@DARK@;color-scheme:dark}}
:root[data-theme="dark"]{--bg:#141816;--panel:#1b201e;--fg:#e4e8e5;--mut:#98a39e;--line:#2c3431;--hl:#fff;@DARK@;color-scheme:dark}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 var(--body)}
main{max-width:1120px;margin:0 auto;padding:28px 16px 56px;display:grid;gap:28px}main>*{min-width:0}
h1{font:800 clamp(26px,4vw,38px)/1.05 var(--display);letter-spacing:-.01em;margin:0}
h2{font:700 19px/1.2 var(--display);margin:0 0 10px}
.lede{color:var(--mut);max-width:68ch;margin:8px 0 0}
.eyebrow{font:600 12px var(--mono);letter-spacing:.08em;text-transform:uppercase;color:var(--mut);margin:0 0 8px}
.bar{display:flex;height:18px;border-radius:4px;overflow:hidden;gap:1px;background:var(--line);margin-bottom:10px}.bar i{display:block;min-width:2px}
.legend{list-style:none;margin:14px 0 0;padding:0;display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:8px 22px}
.legend li{display:grid;grid-template-columns:14px auto 1fr;column-gap:8px;align-items:baseline;min-width:0}
.sw{width:14px;height:14px;border-radius:3px;display:block;align-self:center}
.nm{font-weight:600}.pc{font:600 13px var(--mono);font-variant-numeric:tabular-nums;justify-self:end}.ds{grid-column:2/4;color:var(--mut);font-size:13px}
.s-runs{background:var(--s-runs)}.s-supported{background:var(--s-supported)}.s-unused{background:var(--s-unused)}.s-missing{background:var(--s-missing)}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px}
.wrap{padding:10px 10px 10px 0;overflow:hidden}canvas{display:block;width:100%;cursor:crosshair;touch-action:manipulation}
.sub{color:var(--mut);font:12.5px var(--mono);margin:-4px 0 10px}
#info{position:sticky;bottom:0;font:13px/1.5 var(--mono);padding:10px 12px;min-height:3.2em;overflow-wrap:anywhere;box-shadow:0 -4px 12px rgba(0,0,0,.08)}
#info .st{font-weight:600}#info .m{color:var(--mut)}
.tw{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:14px}
th,td{text-align:left;padding:8px 10px;border-bottom:1px solid var(--line);vertical-align:top}
th{font:600 12px var(--mono);letter-spacing:.06em;text-transform:uppercase;color:var(--mut)}
td.mono{font:12.5px var(--mono);white-space:nowrap}td.num{font:12.5px var(--mono);text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
td.ex{font-size:13px;color:var(--mut);overflow-wrap:anywhere}
tbody tr td:first-child{box-shadow:inset 3px 0 0 var(--s-missing)}
.how{color:var(--mut);font-size:13px;margin:8px 0 0}code{font:12.5px var(--mono)}
</style></head><body><main>
<header><p class="eyebrow">Skipper &amp; Skeeto &middot; @NFILES@ Director files &middot; @NH@ handlers &middot; @KB@ KB of Lingo bytecode</p>
<h1>SkipperRE coverage map</h1>
<p class="lede">Every handler of the original game's Lingo, by what the port's engine does with it. Shares are of the bytecode's bytes.</p>
<ul class="legend">@LEGEND@</ul></header>
@SECTIONS@
<div id="info" class="panel"><span class="m">Point at or tap a handler for its file, class, script, size and what it misses or how often it ran. Click to pin.</span></div>
<p class="how">One row is @ROW@ bytes of bytecode; each handler is as wide as its bytecode, in the order of the file's scripts. A row is labelled with the cast member of the script that starts in it (one row in two at most), otherwise every eighth row with its offset.</p>
<section><h2>Open</h2>
<div class="panel tw"><table><thead><tr><th>Missing</th><th>Kind</th><th>Handlers</th><th>Bytes</th><th>Where</th></tr></thead><tbody>@OPEN@</tbody></table></div>
<p class="how">What the engine does not implement, most used first: the handlers that use it are red on the map (or dark green when they ran anyway).</p>
<p class="how">Made by <code>python tools/coverage.py --html</code> from the game's Lingo (tools/lingodis.py) and what <code>src/</code> implements.@TRACE@ See docs/COVERAGE.md.</p></section>
</main>
<script>
const DATA=@DATA@,N=@N@,ROW=@ROW@,H=7,G=1,LBL=72,info=document.getElementById('info'),HINT=info.innerHTML;let pinned=null;
function css(n){return getComputedStyle(document.documentElement).getPropertyValue(n).trim()}
function esc(t){return String(t).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
const maps=[...document.querySelectorAll('canvas[data-p]')].map(cv=>{const D=DATA[cv.dataset.p].h,S=DATA[cv.dataset.p].s;
const rows=[...new Set(D.flatMap(f=>{const a=Math.floor(f[0]/ROW),b=Math.floor((f[0]+Math.max(f[1],1)-1)/ROW),r=[];for(let k=a;k<=b;k++)r.push(k);return r}))].sort((a,b)=>a-b);
const lab=new Map();D.forEach((f,i)=>{if(i==0||D[i-1][4]!=f[4]){const r=Math.floor(f[0]/ROW);if(!lab.has(r))lab.set(r,S[f[4]][0])}});
return {cv,D,S,rows,lab,ri:new Map(rows.map((r,k)=>[r,k]))}});
function span(m,f,cb){let a=f[0],n=Math.max(f[1],1);while(n>0){const r=Math.floor(a/ROW),o=a-r*ROW,k=Math.min(n,ROW-o);cb(m.ri.get(r),o,k);a+=k;n-=k}}
function draw(m){const {cv,D,rows}=m,w=cv.parentNode.clientWidth-10,dpr=window.devicePixelRatio||1,h=rows.length*(H+G);
cv.width=Math.round(w*dpr);cv.height=Math.round(h*dpr);cv.style.height=h+'px';const x=cv.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);
const col=N.map(s=>css('--s-'+s));x.font='9px '+css('--mono');x.fillStyle=css('--mut');let last=-9;
rows.forEach((r,k)=>{const t=m.lab.get(r);if(t!==undefined&&k-last>1){x.fillText(t.slice(0,11),6,k*(H+G)+H);last=k}else if(k%8==0&&k-last>1){x.fillText('+'+(r*ROW/1024).toFixed(0)+'K',6,k*(H+G)+H);last=k}});
const sc=(w-LBL)/ROW;for(const f of D){x.fillStyle=col[f[2]];span(m,f,(k,o,n)=>x.fillRect(LBL+o*sc,k*(H+G),Math.max(n*sc-0.35,0.6),H))}
if(pinned&&pinned.m===m){x.strokeStyle=css('--hl');x.lineWidth=1.5;span(m,pinned.f,(k,o,n)=>x.strokeRect(LBL+o*sc-1,k*(H+G)-1,Math.max(n*sc,1)+2,H+2))}}
function drawAll(){maps.forEach(draw)}
function at(m,e){const b=m.cv.getBoundingClientRect(),k=Math.floor((e.clientY-b.top)/(H+G));if(k<0||k>=m.rows.length||e.clientX-b.left<LBL)return null;
const a=m.rows[k]*ROW+(e.clientX-b.left-LBL)/((b.width-LBL)/ROW);let lo=0,hi=m.D.length-1,f=null;
while(lo<=hi){const i=(lo+hi)>>1;if(m.D[i][0]<=a){f=m.D[i];lo=i+1}else hi=i-1}return f&&a<f[0]+Math.max(f[1],1)?f:null}
function show(m,f){if(!f){info.innerHTML=HINT;return}const s=m.S[f[4]];
info.innerHTML=esc(m.cv.dataset.n)+' &middot; <span class="st" style="color:var(--s-'+N[f[2]]+')">'+N[f[2]]+'</span> &middot; '+esc(s[1])+
' &middot; <b>'+esc(f[3])+'</b> &middot; '+f[1].toLocaleString()+' bytes'+(f[5]?'<br><span class="m">'+esc(f[5])+'</span>':'')}
maps.forEach(m=>{m.cv.addEventListener('mousemove',e=>{if(!pinned)show(m,at(m,e))});
m.cv.addEventListener('mouseleave',()=>{if(!pinned)show(m,null)});
m.cv.addEventListener('click',e=>{const f=at(m,e),old=pinned;pinned=f&&!(old&&old.f===f)?{m,f}:null;show(m,pinned&&pinned.f);draw(m);if(old&&old.m!==m)draw(old.m)})});
let rt=0;addEventListener('resize',()=>{clearTimeout(rt);rt=setTimeout(drawAll,60)});
try{matchMedia('(prefers-color-scheme: dark)').addEventListener('change',drawAll)}catch(e){}
new MutationObserver(drawAll).observe(document.documentElement,{attributes:true,attributeFilter:['data-theme']});
drawAll();if(document.fonts)document.fonts.ready.then(drawAll);
</script></body></html>
"""
COL = {'runs': ('#1f7a45', '#3cae68'), 'supported': ('#7cc495', '#2f6b45'),
       'unused': ('#d2d3cc', '#3a3f3c'), 'missing': ('#d2453c', '#f0625a')}
STYPE = {1: 'score script', 3: 'movie script', 7: 'parent script'}
ROW = 1024


def write_html(recs, path, trace_note=''):
    e = htmlmod.escape
    light = ';'.join(f'--s-{c}:{COL[c][0]}' for c in CLASSES)
    dark = ';'.join(f'--s-{c}:{COL[c][1]}' for c in CLASSES)
    files = list(dict.fromkeys(r['file'] for r in recs))
    data, sections, allb, alln = {}, [], collections.Counter(), collections.Counter()
    for k, f in enumerate(files):
        rs = [r for r in recs if r['file'] == f]
        scripts, si, hs = [], {}, []
        by = collections.Counter()
        for r in rs:
            key = r['lctx'], r['script']
            if key not in si:
                si[key] = len(scripts)
                lab = f'#{r["member"]} {r["mname"]}' if r['member'] else f's{r["script"]}'
                desc = where(r) + (f' ({STYPE[r["stype"]]})' if r['stype'] in STYPE else '')
                scripts.append([lab.strip(), desc])
            if r['class'] == 'runs':
                extra = f'entered {r["runs"]:,} times in the trace'
                if r['missing']:
                    extra += '; statically missing: ' + ', '.join(l for _, l in r['missing'])
            elif r['missing']:
                extra = 'missing: ' + ', '.join(f'{l} ({KINDS[kd]})' for kd, l in r['missing'])
            elif r['class'] == 'unused':
                extra = 'not an event and no call, symbol or string in the game names it'
            else:
                extra = ''
            hs.append([r['offset'], r['bytes'], CLASSES.index(r['class']), r['handler'], si[key], extra])
            by[r['class']] += r['bytes']
            alln[r['class']] += 1
        allb.update(by)
        data[str(k)] = {'h': hs, 's': scripts}
        tot = sum(by.values()) or 1
        bar = ''.join(f'<i class="s-{c}" style="flex:{by[c]}" title="{c} {100.0 * by[c] / tot:.1f} %"></i>'
                      for c in CLASSES if by[c])
        sub = ' &middot; '.join(f'{c} {100.0 * by[c] / tot:.0f} %' for c in CLASSES if by[c])
        sections.append(f'<section><h2>{e(f)}</h2><p class="sub">{len(rs)} handlers &middot; {sum(by.values()):,} bytes'
                        f' &middot; {sub}</p><div class="bar" aria-hidden="true">{bar}</div>'
                        f'<div class="panel map"><div class="wrap"><canvas data-p="{k}" data-n="{e(f)}" role="img" '
                        f'aria-label="Map of every handler of {e(f)}, coloured by class"></canvas></div></div></section>')
    tot = sum(allb.values()) or 1
    legend = ''.join(f'<li><i class="sw s-{c}"></i><span class="nm">{c}</span><span class="pc">{100.0 * allb[c] / tot:.1f} %'
                     f'</span><span class="ds">{e(WHAT[c])} &middot; {alln[c]:,} handlers, {allb[c]:,} bytes</span></li>'
                     for c in CLASSES)
    mt = missing_table(recs)
    trs = ''.join(f'<tr><td class="mono">{e(label)}</td><td>{e(KINDS[kind])}</td><td class="num">{len(rs)}</td>'
                  f'<td class="num">{sum(r["bytes"] for r in rs):,}</td><td class="ex">'
                  + e(', '.join(f'{r["file"]} {r["handler"]}' for r in rs[:6]) + (f' and {len(rs) - 6} more' if len(rs) > 6 else ''))
                  + '</td></tr>' for (kind, label), rs in mt) \
        or '<tr><td colspan="5">Nothing: every builtin, property, entity, XObject call and opcode the game uses is implemented.</td></tr>'
    js = json.dumps(data, separators=(',', ':')).replace('</', '<\\/')
    page = HTML.replace('@LIGHT@', light).replace('@DARK@', dark).replace('@LEGEND@', legend) \
        .replace('@SECTIONS@', ''.join(sections)).replace('@OPEN@', trs).replace('@TRACE@', e(trace_note)) \
        .replace('@NFILES@', str(len(files))).replace('@NH@', f'{len(recs):,}').replace('@KB@', f'{tot / 1024:,.0f}' if tot >= 10240 else f'{tot / 1024:.1f}') \
        .replace('@ROW@', str(ROW)).replace('@N@', json.dumps(CLASSES)).replace('@DATA@', js)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    open(path, 'w', encoding='utf-8', newline='\n').write(page)


def main():
    ap = argparse.ArgumentParser(description='Coverage of the original Lingo by the engine (see docs/COVERAGE.md).')
    ap.add_argument('files', nargs='*', help='Director files (default extract/*.dxr + extract/*.cxt)')
    ap.add_argument('--trace', action='append', default=[], help='stderr of skipper --trace (repeatable)')
    ap.add_argument('--src', default=os.path.join(ROOT, 'src'), help='engine sources (default src/)')
    ap.add_argument('--write', action='store_true', help='rewrite the generated part of docs/COVERAGE.md')
    ap.add_argument('--html', nargs='?', const='out/coverage.html', help='write the coverage map (default out/coverage.html)')
    a = ap.parse_args()
    files = a.files or default_files()
    if not files:
        sys.exit('no Director files: put the game in extract/ or name the .dxr/.cxt files')
    E = engine_sets(a.src)
    recs = collect(files)
    runs, unknown = mark_runs(recs, parse_trace(a.trace)) if a.trace else ({}, [])
    classify(recs, E, runs)
    note = ''
    if a.trace:
        note = f'{", ".join(os.path.basename(t) for t in a.trace)}: {len(runs)} handlers entered'
        if unknown:
            note += f', {len(unknown)} names without a handler in these files'
    print(f'engine: {len(E["builtins"])} builtins, {len(E["the"])} the-entities, {len(E["movieprop"])} movie properties, '
          f'{len(E["objprop"])} object properties, {len(E["xmethods"])} XObject methods, {len(E["dllfns"])} DLL functions, '
          f'{len(E["opcodes"])} opcodes, events {sorted(E["events"])}\n')
    print(report(recs, True, note))
    if unknown:
        print('Trace names without a handler in these files:', ' '.join(unknown))
    if a.write:
        write_doc(report(recs, False, note), os.path.join(ROOT, 'docs', 'COVERAGE.md'))
        print('-> docs/COVERAGE.md')
    if a.html:
        path = a.html if os.path.isabs(a.html) else os.path.join(ROOT, a.html)
        write_html(recs, path, (' Trace: ' + note + '.') if note else '')
        print('->', path)


if __name__ == '__main__':
    main()
