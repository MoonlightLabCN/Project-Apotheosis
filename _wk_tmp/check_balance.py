import subprocess, os
root = r'E:\Apotheosis\WebKit'
paths = subprocess.run(['git', '-C', root, 'diff', '--name-only', 'webkitgtk-2.54.0'],
                       capture_output=True, text=True).stdout.split()
bad = []
for p in paths:
    fp = os.path.join(root, p.replace('/', os.sep))
    try:
        L = open(fp, encoding='utf-8', errors='replace').read().split('\n')
    except Exception:
        continue
    depth = 0
    for ln in L:
        s = ln.strip()
        if s.startswith('#if'):
            depth += 1
        elif s.startswith('#endif'):
            depth -= 1
    if depth != 0:
        bad.append((depth, p))
for d, p in bad:
    print(d, p)
print('checked:', len(paths), 'unbalanced:', len(bad))
