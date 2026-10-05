"""Headless fuzzing: willekeurige klikken/sleepacties/toetsen in elk minispel en in de wereld.
Zoekt crashes (exitcode), UBSan-traps (127) en Lingo-fouten die niet in de bekende lijst staan.

    python tools/fuzz.py [exe] [frames] [seed] [spel,spel,...]
"""
import os, random, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

EXE = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else 'out/skipper.exe')
FRAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
SEED = int(sys.argv[3]) if len(sys.argv) > 3 else 1
ONLY = sys.argv[4].split(',') if len(sys.argv) > 4 else None   # bijv. Music,Calc
DATA = os.environ.get('DATA', 'extract')   # DATA=releases/da/files: de Deense cd van 1996 (Director 4)

# hotspot in de kamer -> minispel (zie tools/regress.sh)
GAMES = {'Spell': (439, 115), 'ABC': (400, 98), 'Music': (369, 102), 'Clock': (214, 126), 'Balloon': (250, 106),
         'Paint': (274, 98), 'Memory': (287, 99), 'Calc': (301, 104), 'Count': (319, 98), 'Animals': (343, 109),
         'Hide': (576, 386), 'World': None}
KNOWN = re.compile(r'member niet gevonden|handler niet gevonden: (Main|Global)(Mouse|Key)')
# Mac-keyCodes van letters/cijfers + ASCII
KEYS = [(0, 97), (11, 98), (8, 99), (2, 100), (14, 101), (31, 111), (18, 49), (19, 50), (36, 13), (49, 32)]


def run(name, spot, seed):
    rnd = random.Random(seed)
    args = [EXE, DATA, '--click', '320', '240', '170']
    if spot:
        args += ['--click', str(spot[0]), str(spot[1]), '300']
    f = 400
    while f < FRAMES - 20:
        r = rnd.random()
        x, y = rnd.randrange(0, 640), rnd.randrange(0, 480)
        if r < 0.7:
            args += ['--click', str(x), str(y), str(f)]
        elif r < 0.85:
            args += ['--drag', str(x), str(y), str(rnd.randrange(0, 640)), str(rnd.randrange(0, 480)), str(f)]
        else:
            k = rnd.choice(KEYS)
            args += ['--key', str(k[0]), str(k[1]), str(f)]
        f += rnd.randrange(8, 40)
    # nooit het hoofdmenu/afsluiten: geen Esc (53) en de EXIT-knop wordt wel eens geraakt, dat mag
    tmp = tempfile.mkdtemp(prefix='fuzz')
    env = dict(os.environ, APPDATA=tmp)
    args += ['--shot', str(FRAMES), os.path.join('out', 'fuzz', f'{name}.bmp')]
    try:
        p = subprocess.run(args, capture_output=True, text=True, errors='replace', timeout=900, env=env)
        code, err = p.returncode, p.stderr
    except subprocess.TimeoutExpired:
        code, err = 'TIMEOUT', ''
    msgs = sorted({l for l in err.splitlines() if l.startswith('[lingo]') and not KNOWN.search(l)})
    crash = [l for l in err.splitlines() if l.startswith('[crash]')]
    last = [l for l in err.splitlines() if l.startswith('frame ')]
    return name, code, msgs, crash, last[-1] if last else ''


def main():
    os.makedirs('out/fuzz', exist_ok=True)
    with ThreadPoolExecutor(6) as ex:
        res = list(ex.map(lambda kv: run(kv[0], kv[1], SEED * 1000 + sum(map(ord, kv[0]))), [kv for kv in GAMES.items() if not ONLY or kv[0] in ONLY]))
    bad = 0
    for name, code, msgs, crash, last in res:
        ok = code == 0 and not msgs and not crash
        bad += not ok
        print(f"{'OK  ' if ok else 'FOUT'} {name:8} exit={code} {last}")
        for m in msgs[:15]:
            print('      ', m)
        for c in crash[:12]:
            print('      ', c)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
