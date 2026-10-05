# Compare two copies of the CD (directories), file by file, ignoring case in names.
#   python tools/reldiff.py <dir A> <dir B> [--skip Catalog,VFW]
import hashlib, os, sys

def tree(root, skip):
    out = {}
    for dp, dn, fn in os.walk(root):
        dn[:] = [d for d in dn if d.lower() not in skip]
        for f in fn:
            p = os.path.join(dp, f)
            out[os.path.relpath(p, root).replace(os.sep, '/').lower()] = (p, os.path.getsize(p))
    return out

def sha1(p):
    return hashlib.sha1(open(p, 'rb').read()).hexdigest()

def main():
    skip = {'catalog', 'vfw'}
    if '--skip' in sys.argv:
        skip = {s.lower() for s in sys.argv[sys.argv.index('--skip') + 1].split(',')}
    a, b = tree(sys.argv[1], skip), tree(sys.argv[2], skip)
    same = diff = 0
    for k in sorted(set(a) | set(b)):
        if k not in b: print('only A  %s' % k)
        elif k not in a: print('only B  %s' % k)
        elif a[k][1] == b[k][1] and sha1(a[k][0]) == sha1(b[k][0]): same += 1
        else:
            diff += 1
            print('differ  %-28s %10d %10d' % (k, a[k][1], b[k][1]))
    print('%d identical, %d differ' % (same, diff))

main()
