"""Replicate the NEW frame-level decoder logic (median5 + whole-row Otsu +
segment local Otsu) in Python, printing per-row decisions."""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location('dbg', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dbg_ean13_frame.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

def median5(a, b, c, d, e):
    v = sorted([a, b, c, d, e])
    return v[2]

def find_segment(gray_row, w):
    cur_s = -1
    best_s = 0; best_e = 0; best_len = 0
    last_trans = 0xFFFFFFFF
    for x in range(w):
        is_trans = False
        if x > 0:
            d = abs(int(gray_row[x]) - int(gray_row[x-1]))
            is_trans = d > 24
        if is_trans:
            last_trans = x
        if (x - last_trans) <= 16:
            if cur_s < 0:
                cur_s = x
        else:
            if cur_s >= 0:
                ln = x - cur_s
                if ln > best_len:
                    best_len = ln; best_s = cur_s; best_e = x - 1
                cur_s = -1
    if cur_s >= 0:
        ln = w - cur_s
        if ln > best_len:
            best_len = ln; best_s = cur_s; best_e = w - 1
    if best_len < 40:
        return False, 0, 0
    return True, best_s, best_e

def main(path, w=640, h=480, verbose_rows=None):
    data = open(path, 'rb').read()
    verbose_rows = verbose_rows or set()
    for y in range(0, h, 4):
        y0 = max(y-2, 0); y1 = max(y-1, 0); y2 = y
        y3 = min(y+1, h-1); y4 = min(y+2, h-1)
        rows = [data[r*w:(r+1)*w] for r in (y0, y1, y2, y3, y4)]
        med = [median5(rows[0][x], rows[1][x], rows[2][x], rows[3][x], rows[4][x]) for x in range(w)]
        lo = min(med); hi = max(med)
        if hi - lo < 24:
            if y in verbose_rows:
                print(f'y={y}: contrast {hi-lo} skip')
            continue
        hist = [0]*256
        for v in med: hist[v] += 1
        thr = m.otsu_threshold(hist, lo, hi, w)
        if thr == 0:
            if y in verbose_rows:
                print(f'y={y}: otsu 0')
            continue
        binrow = [1 if v < thr else 0 for v in med]
        has_seg, ss, se = find_segment(med, w)
        if y in verbose_rows:
            print(f'y={y}: lo={lo} hi={hi} thr={thr} seg=({ss},{se}) len={se-ss+1 if has_seg else 0} black={sum(binrow)}')
        ok1 = None
        if has_seg:
            seg_med = med[ss:se+1]
            hist1 = [0]*256
            for v in seg_med: hist1[v] += 1
            thr1 = m.otsu_threshold(hist1, min(seg_med), max(seg_med), len(seg_med))
            if thr1 != 0:
                bin1 = binrow[:]
                for x in range(ss, se+1):
                    bin1[x] = 1 if med[x] < thr1 else 0
                ok1, r1 = m.decode_line(bin1, w)
                if y in verbose_rows:
                    print(f'   seg thr1={thr1} fwd={ok1} {r1}')
                if ok1:
                    print(f'y={y}: SUCCESS(seg) {r1}')
                    return
                rev = bin1[::-1]
                ok1m, r1m = m.decode_line(rev, w)
                if y in verbose_rows:
                    print(f'   seg thr1={thr1} mir={ok1m} {r1m}')
                if ok1m:
                    print(f'y={y}: SUCCESS(seg-mir) {r1m}')
                    return
        ok2, r2 = m.decode_line(binrow, w)
        if y in verbose_rows:
            print(f'   whole fwd={ok2} {r2}')
        if ok2:
            print(f'y={y}: SUCCESS(whole) {r2}')
            return
        rev = binrow[::-1]
        ok2m, r2m = m.decode_line(rev, w)
        if y in verbose_rows:
            print(f'   whole mir={ok2m} {r2m}')
        if ok2m:
            print(f'y={y}: SUCCESS(whole-mir) {r2m}')
            return
    print('FAIL')

if __name__ == '__main__':
    p = sys.argv[1]
    verbose = set()
    if len(sys.argv) > 2:
        verbose = set(int(v) for v in sys.argv[2].split(','))
    main(p, verbose_rows=verbose)
