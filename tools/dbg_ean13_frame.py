"""Host-side debug replica of barcode_1d.c EAN-13 decoder.

Reads a raw grayscale frame and replicates the firmware decoder line-by-line,
printing where/why each scan row fails. Usage:
    python dbg_ean13_frame.py <frame.raw> [w] [h]
"""
import struct
import sys

def otsu_threshold(hist, lo, hi, total):
    """Replicate firmware Otsu: last t achieving max between-class variance,
    constrained to [lo+4, hi-4]."""
    sum_all = sum(i * hist[i] for i in range(256))
    best_var = -1
    thr = 0
    sum_b = 0
    cnt_b = 0
    lo_v = 0 if lo < 4 else lo - 4
    hi_v = 255 if hi > 251 else hi + 4
    for t in range(lo_v, hi_v + 1):
        sum_b += t * hist[t]
        cnt_b += hist[t]
        cnt_f = total - cnt_b
        if cnt_b == 0 or cnt_f == 0:
            continue
        mean_b = sum_b // cnt_b
        mean_f = (sum_all - sum_b) // cnt_f
        diff = abs(mean_b - mean_f)
        var = cnt_b * cnt_f * diff * diff
        if var >= best_var:
            best_var = var
            thr = t
    return thr

# ---- code tables (copy from barcode_1d.c) ----
L = [0b0001101, 0b0011001, 0b0010011, 0b0111101, 0b0100011,
     0b0110001, 0b0101111, 0b0111011, 0b0110111, 0b0001011]
G = [0b0100111, 0b0110011, 0b0011011, 0b0100001, 0b0011101,
     0b0111001, 0b0000101, 0b0010001, 0b0001001, 0b0010111]
R = [0b1110010, 0b1100110, 0b1101100, 0b1000010, 0b1011100,
     0b1001110, 0b1010000, 0b1000100, 0b1001000, 0b1110100]
FIRST = [0b000000, 0b001011, 0b001101, 0b001110, 0b010011,
         0b011001, 0b011100, 0b010101, 0b010110, 0b011010]

def pattern_from_bits(bits):
    runs = []
    cur = (bits >> 6) & 1
    start_bar = (cur == 1)
    cnt = 1
    for b in range(5, -1, -1):
        v = (bits >> b) & 1
        if v == cur:
            cnt += 1
        else:
            runs.append(cnt)
            cur = v
            cnt = 1
    runs.append(cnt)
    while len(runs) < 4:
        runs.append(0)
    return start_bar, runs

Lpat = [pattern_from_bits(b) for b in L]
Gpat = [pattern_from_bits(b) for b in G]
Rpat = [pattern_from_bits(b) for b in R]

def match_code(runs4, first_is_bar, table):
    total = sum(runs4)
    if total == 0:
        return -1, None
    best = -1
    best_err = 0xFFFFFFFF
    for c in range(10):
        sb, mods = table[c]
        if sb != first_is_bar:
            continue
        err = 0
        for i in range(4):
            a = runs4[i] * 7
            e = abs(a - mods[i] * total)
            err += e
        if err < best_err:
            best_err = err
            best = c
    return best, best_err

def decode_line(binrow, w, verbose=False):
    # 1. runs
    runs = []
    i = 0
    while i < w:
        v = binrow[i]
        ln = 0
        while i < w and binrow[i] == v:
            ln += 1
            i += 1
        runs.append(ln)
    # 2. find start guard
    def start_guard_ok(a, b, c):
        if not ((a * 5 >= c * 2) and (c * 5 >= a * 2)):
            return False
        if b * 5 < a * 2:
            return False
        if b > a * 5:
            return False
        return True

    guard_idx = -1
    for i in range(len(runs) - 2):
        a, b, c = runs[i], runs[i + 1], runs[i + 2]
        total = a + b + c
        if a == 0 or b == 0 or c == 0:
            continue
        # run_is_bar: bin value at start of run i
        off = sum(runs[:i])
        if binrow[off] != 1:
            continue
        if total < 5 or total > 90:
            continue
        if start_guard_ok(a, b, c):
            guard_idx = i
            break
    if guard_idx < 0:
        if verbose:
            print(f'  no start guard; runs={runs[:25]}...')
        return False, 'no_guard'
    if verbose:
        print(f'  guard at run {guard_idx}: {runs[guard_idx:guard_idx+3]} (sum={sum(runs[guard_idx:guard_idx+3])})')
    # 3. left 6 digits
    run_pos = guard_idx + 3
    digits = [0] * 13
    parity = 0
    total_err = 0
    for k in range(6):
        if run_pos + 3 >= len(runs):
            return False, f'left{k} overflow'
        cur = runs[run_pos:run_pos + 4]
        run_pos += 4
        off = sum(runs[:run_pos - 4])
        first_is_bar = (binrow[off] == 1)
        dl, el = match_code(cur, first_is_bar, Lpat)
        dg, eg = match_code(cur, first_is_bar, Gpat)
        if dl >= 0 and (dg < 0 or el <= eg):
            digits[k] = dl
            total_err += el
        elif dg >= 0:
            digits[k] = dg
            parity |= (1 << (5 - k))
            total_err += eg
        else:
            if verbose:
                print(f'  left digit {k} no match: runs={cur} first_bar={first_is_bar}')
            return False, f'left{k}_nomatch'
    # 4. middle guard (5 runs ~1:1:1:1:1, ratio in [0.4,2.5])
    if run_pos + 4 >= len(runs):
        return False, 'mid overflow'
    mid = runs[run_pos:run_pos + 5]
    total = sum(mid)
    if total < 5 or total > 150:
        return False, 'mid total'
    avg = total // 5
    if avg == 0:
        return False, 'mid avg0'
    for mm in mid:
        if mm * 5 < avg * 2 or mm * 2 > avg * 5:
            if verbose:
                print(f'  mid guard fail: {mid}')
            return False, 'mid shape'
    run_pos += 5
    # 5. right 6
    for k in range(6):
        if run_pos + 3 >= len(runs):
            return False, f'right{k} overflow'
        cur = runs[run_pos:run_pos + 4]
        run_pos += 4
        off = sum(runs[:run_pos - 4])
        first_is_bar = (binrow[off] == 1)
        dr, er = match_code(cur, first_is_bar, Rpat)
        if dr < 0:
            return False, f'right{k}_nomatch'
        digits[6 + k] = dr
        total_err += er
    # 6. end guard (lenient: only check first two runs 1:1, third may merge with bg)
    if run_pos + 2 < len(runs):
        a, b = runs[run_pos], runs[run_pos + 1]
        total = a + b
        if total < 4 or total > 60:
            return False, 'end total'
        if not ((a * 5 >= b * 2) and (b * 5 >= a * 2)):
            return False, 'end shape'
    # 7. first digit from parity
    first = -1
    for i in range(10):
        if FIRST[i] == parity:
            first = i
            break
    if first < 0:
        return False, 'parity'
    all13 = [first] + digits[:6] + digits[6:12]
    # checksum
    s = 0
    for i in range(12):
        wgt = 1 if i % 2 == 0 else 3
        s += all13[i] * wgt
    expect = (10 - s % 10) % 10
    if expect != all13[12]:
        return False, f'checksum got={all13[12]} exp={expect}'
    # confidence gate
    sum_total = runs[guard_idx] + runs[guard_idx + 1] + runs[guard_idx + 2]
    if sum_total == 0:
        return False, 'mod0'
    mod_est = sum_total // 3
    if mod_est == 0:
        return False, 'mod0'
    err_budget = 7 * 2 * 4 * 12 * mod_est
    if total_err > err_budget:
        return False, f'errbudget {total_err}>{err_budget}'
    return True, ''.join(str(d) for d in all13)

def main():
    path = sys.argv[1]
    w = int(sys.argv[2]) if len(sys.argv) > 2 else 640
    h = int(sys.argv[3]) if len(sys.argv) > 3 else 480
    data = open(path, 'rb').read()
    for y in range(0, h, 4):
        y0 = y - 1 if y > 0 else y
        y1 = y + 1 if y + 1 < h else y
        rows = [data[y0 * w:(y0 + 1) * w], data[y * w:(y + 1) * w], data[y1 * w:(y1 + 1) * w]]
        med = [0] * w
        for x in range(w):
            a, b, c = rows[0][x], rows[1][x], rows[2][x]
            if a > b:
                a, b = b, a
            if b > c:
                b, c = c, b
            if a > b:
                a, b = b, a
            med[x] = b
        lo = min(med)
        hi = max(med)
        if hi - lo < 24:
            continue
        hist = [0] * 256
        for v in med:
            hist[v] += 1
        thr = otsu_threshold(hist, lo, hi, w)
        binrow = [1 if v < thr else 0 for v in med]
        ok, res = decode_line(binrow, w)
        if ok:
            print(f'y={y}: SUCCESS {res}')
            return
        # mirrored
        rev = binrow[::-1]
        ok2, res2 = decode_line(rev, w)
        if ok2:
            print(f'y={y}: SUCCESS(mirror) {res2}')
            return
        n1 = sum(1 for v in binrow if v == 1)
        if y in (130, 134, 138, 142, 146, 150, 154, 158, 162, 166, 170, 174, 178, 182, 186, 190, 194, 198):
            print(f'y={y}: thr={thr} black={n1}/{w} fail={res}')

if __name__ == '__main__':
    main()
