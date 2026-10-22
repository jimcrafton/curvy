import re, sys, json
from collections import defaultdict

def numbers(s):
    return [float(x) for x in re.findall(r'-?\d*\.?\d+(?:e-?\d+)?', s)]

def bbox_and_count(d):
    xs, ys, n = [], [], 0
    cx = cy = 0.0
    sx = sy = 0.0
    for m in re.finditer(r'([MmLlHhVvCcSsQqTtZz])([^MmLlHhVvCcSsQqTtZz]*)', d):
        c, a = m.group(1), numbers(m.group(2))
        rel = c.islower()
        C = c.upper()
        if C == 'Z':
            cx, cy = sx, sy
            continue
        if C == 'H':
            for v in a:
                cx = cx + v if rel else v; xs.append(cx); ys.append(cy); n += 1
        elif C == 'V':
            for v in a:
                cy = cy + v if rel else v; xs.append(cx); ys.append(cy); n += 1
        else:
            step = {'M': 2, 'L': 2, 'T': 2, 'C': 6, 'S': 4, 'Q': 4}[C]
            for i in range(0, len(a) - step + 1, step):
                seg = a[i:i + step]
                for j in range(0, step, 2):
                    px = seg[j] + (cx if rel else 0); py = seg[j + 1] + (cy if rel else 0)
                    xs.append(px); ys.append(py)
                cx, cy = (seg[-2] + (cx if rel else 0)), (seg[-1] + (cy if rel else 0))
                n += 1
                if C == 'M' and i == 0:
                    sx, sy = cx, cy
    return (min(xs), min(ys), max(xs), max(ys)), n

def load(path):
    s = open(path, encoding='utf8').read()
    style = re.search(r'<style[^>]*>(.*?)</style>', s, re.S).group(1)
    fills = {}
    for m in re.finditer(r'\.(fil\d+)\s*\{fill:([^}]+)\}', style):
        fills[m.group(1)] = m.group(2).strip()
    grads = re.findall(r'<linearGradient.*?</linearGradient>', s, re.S)
    shapes = []
    for m in re.finditer(r'<(path|polygon)\b([^>]*)/?>', s):
        tag, attrs = m.group(1), m.group(2)
        cls = re.search(r'class="([^"]*)"', attrs)
        cls = cls.group(1).split()[0] if cls else ''
        if tag == 'path':
            d = re.search(r'\sd="([^"]*)"', attrs)
            if not d: continue
            d = d.group(1)
        else:
            pts = numbers(re.search(r'points="([^"]*)"', attrs).group(1))
            d = 'M' + ' L'.join(f'{pts[i]},{pts[i+1]}' for i in range(0, len(pts) - 1, 2)) + 'Z'
        bb, n = bbox_and_count(d)
        shapes.append({'c': cls, 'd': d, 'b': [round(v) for v in bb], 'n': n})
    return fills, grads, shapes

if __name__ == '__main__':
    fills, grads, shapes = load(sys.argv[1])
    print(len(shapes), 'shapes', sum(x['n'] for x in shapes), 'points', len(grads), 'gradients')
    by = defaultdict(list)
    for i, x in enumerate(shapes): by[x['c']].append(i)
    for c, ids in sorted(by.items(), key=lambda kv: -len(kv[1])):
        bs = [shapes[i]['b'] for i in ids]
        print(c, fills.get(c), len(ids), 'x', min(b[0] for b in bs), min(b[1] for b in bs), max(b[2] for b in bs), max(b[3] for b in bs))
