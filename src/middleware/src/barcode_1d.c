#include "barcode_1d.h"

#include <string.h>

/**********************************************************************************************************************
 * EAN-13 码表（每个字符 7 模块，bit6..bit0 = 最左..最右模块，1=条 0=空）
 *
 * 相位约定：EAN-13 全码为"条空交替"，每个字符 4 个 run（7 模块）。
 *   - 起始 guard 101 以条开始；
 *   - 左侧 6 字符用 L/G 码：**以空开始**（前导 guard 以条结束）；
 *   - 中间 guard 01010 以空开始；
 *   - 右侧 6 字符用 R 码：**以条开始**（中间 guard 以空结束）；
 *   - 结束 guard 101 以条开始。
 * 解码时按 run 的实际颜色生成位串（第一个 run 的颜色由图像像素决定），
 * 左侧匹配 L/G 表、右侧匹配 R 表即可，无需硬编码相位。
 **********************************************************************************************************************/

/* L 码（左侧，奇校验） */
static const uint8_t s_l_code[10] =
{
    0b0001101, 0b0011001, 0b0010011, 0b0111101, 0b0100011,
    0b0110001, 0b0101111, 0b0111011, 0b0110111, 0b0001011,
};

/* G 码（左侧，偶校验） */
static const uint8_t s_g_code[10] =
{
    0b0100111, 0b0110011, 0b0011011, 0b0100001, 0b0011101,
    0b0111001, 0b0000101, 0b0010001, 0b0001001, 0b0010111,
};

/* R 码（右侧，= L 码按位取反） */
static const uint8_t s_r_code[10] =
{
    0b1110010, 0b1100110, 0b1101100, 0b1000010, 0b1011100,
    0b1001110, 0b1010000, 0b1000100, 0b1001000, 0b1110100,
};

/* 首位数字 → 左侧 6 位奇偶模式（bit5..bit0 = 第 1..6 位，0=L 奇 1=G 偶） */
static const uint8_t s_first_parity[10] =
{
    0b000000, 0b001011, 0b001101, 0b001110, 0b010011,
    0b011001, 0b011100, 0b010101, 0b010110, 0b011010,
};

/**********************************************************************************************************************
 * 内部工具
 **********************************************************************************************************************/

/* EAN-13 校验位：前 12 位（奇数位×1 + 偶数位×3），校验=(10-sum%10)%10 */
static bool check_ean13_checksum(const uint8_t * p_digits)
{
    uint32_t sum = 0U;
    for (uint32_t i = 0U; i < 12U; i++)
    {
        uint32_t const weight = ((i % 2U) == 0U) ? 1U : 3U;
        sum += (uint32_t) p_digits[i] * weight;
    }
    uint8_t const expect = (uint8_t) ((10U - (sum % 10U)) % 10U);
    return (expect == p_digits[12]);
}

/* 起始 guard 专用检查：EAN-13 标准结构是 101（黑1白1黑1），但左侧第一个
 * 字符用 L/G 码且**以白开始**——guard 的白 run 会与字符起始白 run 合并成
 * 2~4 模块宽（实测低对比度 3px 模块条码：guard 白3 + 字符白6 = 白9）。
 * 因此起始 guard 只要求黑1:黑1 ≈ 1:1，白 run 允许 1~5 倍黑 run（1 guard 白
 * + 最多 4 字符白）。结束 guard 后是静区无合并，仍用严格 guard_ratio_ok。 */
static bool start_guard_ok(uint32_t a, uint32_t b, uint32_t c)
{
    /* 黑 run 1:1（±2.5 倍） */
    if (!((a * 5U >= c * 2U) && (c * 5U >= a * 2U)))
    {
        return false;
    }
    /* 白 run：≥ 0.4 倍黑（不短于模块），≤ 5 倍黑（guard白+字符白合并上限） */
    if (b * 5U < a * 2U)
    {
        return false;
    }
    if (b > a * 5U)
    {
        return false;
    }
    return true;
}

/* ============================================================================
 * 码表 → 4-run 模块模式（每字符 7 模块，条/空交替）。
 * 由 7-bit 模式（1=条）展开：连续 1/0 的模块数。
 * start_bar：字符最左模块是否为条（L/G 码从空开始、R 码从条开始——
 * 这是 L/G/R 三套码的核心差异，误差匹配必须校验起始颜色，否则 L 与 R
 * 宽度序列相同无法区分）。
 * ==========================================================================*/
typedef struct
{
    uint8_t runs[4];    /* 4 个 run 的模块数，总和 7 */
    bool    start_bar;  /* 最左模块颜色（1=条） */
} ean_code_pattern_t;

static ean_code_pattern_t pattern_from_bits(uint8_t bits)
{
    ean_code_pattern_t p;
    int idx = 0;
    uint8_t cur = (bits >> 6U) & 1U;   /* 最左模块颜色 */
    p.start_bar = (cur == 1U);
    uint8_t cnt = 1U;
    for (int b = 5; b >= 0; b--)
    {
        uint8_t const v = (bits >> (uint32_t) b) & 1U;
        if (v == cur)
        {
            cnt++;
        }
        else
        {
            if (idx < 4) { p.runs[idx++] = cnt; }
            cur = v;
            cnt = 1U;
        }
    }
    if (idx < 4) { p.runs[idx++] = cnt; }
    while (idx < 4) { p.runs[idx++] = 0U; }
    return p;
}

static ean_code_pattern_t s_l_pattern[10];
static ean_code_pattern_t s_g_pattern[10];
static ean_code_pattern_t s_r_pattern[10];
static bool s_pattern_init;

static void init_patterns(void)
{
    if (s_pattern_init)
    {
        return;
    }
    for (int i = 0; i < 10; i++)
    {
        s_l_pattern[i] = pattern_from_bits(s_l_code[i]);
        s_g_pattern[i] = pattern_from_bits(s_g_code[i]);
        s_r_pattern[i] = pattern_from_bits(s_r_code[i]);
    }
    s_pattern_init = true;
}

/* 误差匹配：实际 4-run 宽度 vs 码字理论模块模式（归一化比例）。
 * 同时校验起始颜色（L/G 从空开始、R 从条开始）：颜色不符直接排除。
 * 返回最佳匹配的码字索引；*p_error 为该码字的归一化误差。 */
static int match_code(const uint32_t * p_runs, bool first_is_bar,
                      const ean_code_pattern_t * p_table, uint32_t * p_error)
{
    uint32_t const total = p_runs[0] + p_runs[1] + p_runs[2] + p_runs[3];
    if (total == 0U)
    {
        return -1;
    }
    int      best = -1;
    uint32_t best_err = 0xFFFFFFFFU;
    for (int c = 0; c < 10; c++)
    {
        if (p_table[c].start_bar != first_is_bar)
        {
            continue;   /* 起始颜色不符：非本表码字（L/R 宽度相同靠颜色区分） */
        }
        /* 误差 = Σ |actual_i/total - mod_i/7|，用定点整数：×7×total 避免浮点 */
        uint32_t err = 0U;
        for (int i = 0; i < 4; i++)
        {
            uint32_t const a = p_runs[i] * 7U;        /* actual×7 */
            uint32_t const e = (a > (uint32_t) p_table[c].runs[i] * total)
                                   ? (a - (uint32_t) p_table[c].runs[i] * total)
                                   : ((uint32_t) p_table[c].runs[i] * total - a);
            err += e;
        }
        if (err < best_err)
        {
            best_err = err;
            best = c;
        }
    }
    if (p_error != NULL)
    {
        *p_error = best_err;
    }
    return best;
}

/* 取第 idx 个 run 的起始像素位置 */
static uint32_t run_start_offset(const uint16_t * p_runs, int idx)
{
    uint32_t off = 0U;
    for (int i = 0; i < idx; i++)
    {
        off += p_runs[i];
    }
    return off;
}

/* 第 idx 个 run 的实际颜色（1=条/黑） */
static bool run_is_bar(const uint8_t * p_bin, const uint16_t * p_runs, int idx)
{
    return (p_bin[run_start_offset(p_runs, idx)] == 1U);
}

/**********************************************************************************************************************
 * 单行解码。p_bin 为二值化行（1=黑/条），p_bin[0] 对应 x=0。
 * EAN-13 只能单向读（L/G/R 三套码无镜像对称）；OV7725 HFLIP 镜像由调用方
 * 整行像素反转（=画面水平翻转恢复正向）后再次调用本函数。
 **********************************************************************************************************************/
static bool ean13_decode_binary_line(const uint8_t * p_bin, uint32_t w, char * p_out)
{
    init_patterns();

    /* 1. 测量 run 长度（条/空交替） */
    uint16_t runs[256];
    int      run_count = 0;
    {
        uint32_t i = 0U;
        while (i < w)
        {
            uint8_t const v = p_bin[i];
            uint32_t len = 0U;
            while ((i < w) && (p_bin[i] == v))
            {
                len++;
                i++;
            }
            if (run_count < 256)
            {
                runs[run_count++] = (uint16_t) len;
            }
        }
    }

    /* 2. 找起始 guard "101"（3 run ≈ 1:1:1，首个 run 必须是条）。
     * 模块下限 2px（total≥6）→ 1.5px（total≥4.5）：纸盒小条码远放时
     * 模块仅 1.5-2px，过高的下限直接错过。EAN-13 校验兜底防误报。 */
    int guard_idx = -1;
    for (int i = 0; i + 2 < run_count; i++)
    {
        uint16_t const a = runs[i], b = runs[i + 1], c = runs[i + 2];
        uint32_t const total = (uint32_t) a + b + c;
        if ((a == 0U) || (b == 0U) || (c == 0U))
        {
            continue;
        }
        if (!run_is_bar(p_bin, runs, i))
        {
            continue; /* 起始 guard 必须从条开始 */
        }
        if ((total < 5U) || (total > 90U))
        {
            continue;
        }
        if (start_guard_ok(a, b, c))
        {
            guard_idx = i;
            break;
        }
    }
    if (guard_idx < 0)
    {
        return false;
    }

    /* 3. 左侧 6 字符（L/G 码，误差匹配） */
    uint8_t digits[13];
    int     run_pos = guard_idx + 3;
    uint8_t parity = 0U; /* bit5..bit0 = 左第 1..6 位，1=G(偶) */
    uint32_t total_err = 0U; /* 全码匹配误差累计（可信度门槛） */

    for (int k = 0; k < 6; k++)
    {
        if (run_pos + 3 >= run_count)
        {
            return false;
        }
        uint32_t cur[4];
        cur[0] = runs[run_pos];
        cur[1] = runs[run_pos + 1];
        cur[2] = runs[run_pos + 2];
        cur[3] = runs[run_pos + 3];
        run_pos += 4;

        uint32_t err_l, err_g;
        bool const first_is_bar = run_is_bar(p_bin, runs, run_pos - 4);
        int const d_l = match_code(cur, first_is_bar, s_l_pattern, &err_l);
        int const d_g = match_code(cur, first_is_bar, s_g_pattern, &err_g);
        /* 取误差较小的匹配；两者接近时优先 L（奇）——EAN 首位模式 L 居多 */
        if ((d_l >= 0) && ((d_g < 0) || (err_l <= err_g)))
        {
            digits[k] = (uint8_t) d_l;          /* L 奇 */
            total_err += err_l;
        }
        else if (d_g >= 0)
        {
            digits[k] = (uint8_t) d_g;
            parity |= (uint8_t) (1U << (5 - k)); /* G 偶 */
            total_err += err_g;
        }
        else
        {
            return false;
        }
    }

    /* 4. 中间 guard：5 run ≈ 1:1:1:1:1（01010） */
    {
        if (run_pos + 4 >= run_count)
        {
            return false;
        }
        uint32_t mid[5];
        uint32_t total = 0U;
        for (int i = 0; i < 5; i++)
        {
            mid[i] = runs[run_pos + i];
            total += mid[i];
        }
        if ((total < 5U) || (total > 150U))
        {
            return false;
        }
        uint32_t const avg = total / 5U;
        for (int i = 0; i < 5; i++)
        {
            /* 与 guard 同步放宽到比例 ∈ [0.4, 2.5] */
            if ((avg == 0U) || (mid[i] * 5U < avg * 2U) || (mid[i] * 2U > avg * 5U))
            {
                return false;
            }
        }
        run_pos += 5;
    }

    /* 5. 右侧 6 字符（R 码） */
    for (int k = 0; k < 6; k++)
    {
        if (run_pos + 3 >= run_count)
        {
            return false;
        }
        uint32_t cur[4];
        cur[0] = runs[run_pos];
        cur[1] = runs[run_pos + 1];
        cur[2] = runs[run_pos + 2];
        cur[3] = runs[run_pos + 3];
        run_pos += 4;

        uint32_t err_r;
        bool const first_is_bar = run_is_bar(p_bin, runs, run_pos - 4);
        int const d = match_code(cur, first_is_bar, s_r_pattern, &err_r);
        if (d < 0)
        {
            return false;
        }
        digits[6 + k] = (uint8_t) d;
        total_err += err_r;
    }

    /* 6. 结束 guard：3 run ≈ 1:1:1。纸盒深色包装/阴影常让结束 guard 的
     * 第三个黑 run 与右侧背景合并成超长黑段（实测 2px 模块条码右侧 16px
     * 背景全判黑 → c 巨大），严格 1:1:1 会把真实条码拒掉。因此只严格检查
     * 前两个 run（黑1:白1 ≈ 1:1），c 允许被背景吞并（无论多长）；若 c
     * 不存在（行尾截断）则跳过。EAN-13 校验位兜底防误报。 */
    if (run_pos + 2 < run_count)
    {
        uint16_t const a = runs[run_pos], b = runs[run_pos + 1];
        uint32_t const total = (uint32_t) a + b;
        if ((total < 4U) || (total > 60U))
        {
            return false;
        }
        if (!((a * 5U >= b * 2U) && (b * 5U >= a * 2U)))
        {
            return false;
        }
    }

    /* 7. 首位数字 ← 奇偶模式；组装 13 位；校验 */
    int first = -1;
    for (int i = 0; i < 10; i++)
    {
        if (s_first_parity[i] == parity)
        {
            first = i;
            break;
        }
    }
    if (first < 0)
    {
        return false;
    }

    uint8_t all[13];
    all[0] = (uint8_t) first;
    for (int i = 0; i < 6; i++)
    {
        all[1 + i] = digits[i];
        all[7 + i] = digits[6 + i];
    }

    if (!check_ean13_checksum(all))
    {
        return false;
    }

    /* 可信度门槛：12 个数据字符的平均匹配误差超过阈值即拒绝。
     * 误差 = Σ|actual×7 - 理论×total|（像素×7 量纲），对清晰条码每字符
     * 误差很小（几十以内）；文字/噪点行凑巧通过结构+校验时误差巨大。
     * 归一化到每字符：total_err/(12×7×total)，< 0.5 模块平均偏差可接受。 */
    {
        uint32_t const sum_total = (uint32_t) runs[guard_idx] + runs[guard_idx + 1] +
                                   runs[guard_idx + 2];   /* 起始 guard 总宽 = 3 模块 */
        if (sum_total == 0U)
        {
            return false;
        }
        uint32_t const mod_est = sum_total / 3U;          /* 单模块像素宽 */
        if (mod_est == 0U)
        {
            return false;
        }
        /* 每字符误差上界：7 模块 × 3 像素偏差 × 4 run × 12 字符。
         * 3px（原 2px）：纸盒印刷条码经模糊/反光后 run 边界偏移可达 2-3px，
         * 2px 门槛会把真实码拒掉；EAN-13 校验位保证放宽后仍是有效码。 */
        uint32_t const err_budget = 7U * 3U * 4U * 12U * mod_est;
        if (total_err > err_budget)
        {
            return false;
        }
    }

    for (uint32_t i = 0U; i < EAN13_DIGITS; i++)
    {
        p_out[i] = (char) ('0' + all[i]);
    }
    p_out[EAN13_DIGITS] = '\0';
    return true;
}

/* 5 值中值（排序网络展开，替代简单排序循环：编译器易向量化） */
static uint8_t median5(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e)
{
    uint8_t t;
#define SWAP5(x, y) do { if ((x) > (y)) { t = (x); (x) = (y); (y) = t; } } while (0)
    SWAP5(a, b); SWAP5(c, d);
    if (a > c) { t = a; a = c; c = t; }
    if (b > d) { t = b; b = d; d = t; }
    if (b > c) { t = b; b = c; c = t; }
    SWAP5(a, e);
    if (b > e) { t = b; b = e; e = t; }
    if (c > e) { t = c; c = e; e = t; }
    if (b > c) { t = b; b = c; c = t; }
#undef SWAP5
    return c;
}

/* 从直方图算 Otsu 阈值（约束在 [lo+4, hi-4]）。返回 0 表示无有效分割。 */
static uint32_t otsu_threshold(const uint32_t * p_hist, uint32_t lo, uint32_t hi,
                               uint32_t total)
{
    uint32_t sum_all = 0U;
    for (int i = 0; i < 256; i++)
    {
        sum_all += (uint32_t) i * p_hist[i];
    }
    uint32_t thr = 0U;
    uint64_t best_var = 0U;
    uint32_t sum_b = 0U, cnt_b = 0U;
    uint32_t lo_v = (lo < 4U) ? 0U : (lo - 4U);
    uint32_t hi_v = (hi > 251U) ? 255U : (hi + 4U);
    /* 记录**最后一个**达到最大类间方差的 t：对严格二值数据（0/255），
     * 任意分割点都等价，但阈值取谷底（接近 hi）才能让 `v < thr`
     * 正确分出黑条（取首个最大点会落在 0 → 全白）。 */
    for (uint32_t t = lo_v; t <= hi_v; t++)
    {
        sum_b += t * p_hist[t];
        cnt_b += p_hist[t];
        uint32_t const cnt_f = total - cnt_b;
        if ((cnt_b == 0U) || (cnt_f == 0U))
        {
            continue;
        }
        uint32_t const mean_b = sum_b / cnt_b;
        uint32_t const mean_f = (sum_all - sum_b) / cnt_f;
        /* 类间距离取绝对值：mean_b（前景均值）必然 ≤ mean_f（背景均值），
         * 但 uint32 相减会下溢成巨大数，改变 argmax → 阈值漂移。 */
        uint32_t const diff = (mean_b >= mean_f) ? (mean_b - mean_f)
                                                 : (mean_f - mean_b);
        /* 类间方差可能超 uint32（640 像素 × 255² ≈ 6.6e9 > 2^32），
         * 用 uint64 避免溢出回绕导致 argmax 错乱。 */
        uint64_t const var = (uint64_t) cnt_b * cnt_f * diff * diff;
        if (var >= best_var)
        {
            best_var = var;
            thr = t;
        }
    }
    return thr;
}

/* 在灰度行上找"条码候选段"：EAN-13 有 59 个 run（58 过渡）。用**灰度显著
 * 过渡**（|gray[x]-gray[x-1]| > 48）而非二值化行——整行 Otsu 阈值被大面积
 * 背景主导时二值化行上条码区可能无过渡（整体同色），灰度过渡不受阈值影响，
 * 条码黑白分明处灰度差大、背景噪声差小，仍能定位候选区。
 * 显著过渡阈值取 48（而非 24）：纸盒印刷图案/文字的弱过渡（30-50）不会
 * 被误并入条码段，只有条码的强过渡（黑条↔白底通常 >60）才算；模糊条码
 * 过渡可能压到 50-80，48 留有余量。
 * 相邻显著过渡间距 ≤ 16px（模块 ≤4px 时最长 run 4 模块）视为条码密集区，
 * 段宽 ≥ 40px 才算候选（最小条码 ~143px @模块 1.5px，40px 只是下限过滤
 * 文字噪声）。返回 true 且 p_s/p_e 指向段起止（闭区间）。 */
static bool find_barcode_segment(const uint8_t * p_gray_row, uint32_t w,
                                 uint32_t * p_s, uint32_t * p_e)
{
    int      cur_s = -1;
    uint32_t best_s = 0U, best_e = 0U;
    uint32_t best_len = 0U;
    uint32_t last_trans = 0xFFFFFFFFU;
    for (uint32_t x = 0U; x < w; x++)
    {
        bool const is_trans = (x > 0U) &&
                              (((uint32_t) p_gray_row[x] > (uint32_t) p_gray_row[x - 1U])
                                   ? ((uint32_t) p_gray_row[x] - (uint32_t) p_gray_row[x - 1U]) > 48U
                                   : ((uint32_t) p_gray_row[x - 1U] - (uint32_t) p_gray_row[x]) > 48U);
        if (is_trans)
        {
            last_trans = x;
        }
        /* 距上一个显著过渡 ≤ 16px 视为仍在条码密集区（模糊后 run 拉宽，留余量） */
        if ((x - last_trans) <= 16U)
        {
            if (cur_s < 0)
            {
                cur_s = (int) x;
            }
        }
        else
        {
            if (cur_s >= 0)
            {
                uint32_t const len = (uint32_t) (x - (uint32_t) cur_s);
                if (len > best_len)
                {
                    best_len = len;
                    best_s = (uint32_t) cur_s;
                    best_e = x - 1U;
                }
                cur_s = -1;
            }
        }
    }
    if (cur_s >= 0)
    {
        uint32_t const len = w - (uint32_t) cur_s;
        if (len > best_len)
        {
            best_len = len;
            best_s = (uint32_t) cur_s;
            best_e = w - 1U;
        }
    }
    if (best_len < 40U)
    {
        return false;   /* 太短：非条码（条码最小 ~143px @模块1.5px） */
    }
    *p_s = best_s;
    *p_e = best_e;
    return true;
}

/**********************************************************************************************************************
 * 帧级解码：多行扫描；每行先正向，失败则整行像素反转（镜像恢复）再试。
 *
 * 鲁棒性（纸盒印刷条码实测驱动）：
 *  - 垂直 5 行中值：低对比度/单行噪声下比 3 行更稳，竖条跨行保留；
 *  - 整行 Otsu 后，若存在"灰度显著过渡密集段"（条码候选区），对段内
 *    像素**重新 Otsu** 再解码：纸盒条码常只占画面一小块，整行 Otsu 被
 *    大面积暗背景/亮背景主导导致条码区阈值错误——局部重阈值解决；
 *  - 段内失败后回退整行阈值；两者都失败再镜像反转。
 **********************************************************************************************************************/
bool ean13_decode_frame(const uint8_t * p_gray, uint32_t w, uint32_t h,
                        char * p_out)
{
    if ((NULL == p_gray) || (NULL == p_out) || (w < 80U) || (h < 20U) || (w > 1024U))
    {
        return false;
    }

    /* 行缓冲（栈）：中值 + 二值化 + 局部二值化 + 镜像（最大 1024B × 4） */
    uint8_t med[1024];
    uint8_t bin[1024];
    uint8_t bin1[1024];
    uint8_t rev[1024];

    for (uint32_t y = 0U; y < h; y += EAN13_SCAN_ROW_STEP)
    {
        /* 垂直 5 行中值（y-2..y+2）：抑制传感器/打印噪声的同时**保留
         * 竖条边缘**（条码竖条跨多行，中值取众数；均值会把细条边缘软化，
         * 在模糊/亮屏场景下反而更难分辨）。 */
        uint32_t const y0 = (y > 1U) ? y - 2U : y;
        uint32_t const y1 = (y > 0U) ? y - 1U : y;
        uint32_t const y2 = y;
        uint32_t const y3 = (y + 1U < h) ? y + 1U : y;
        uint32_t const y4 = (y + 2U < h) ? y + 2U : y;
        uint8_t const * p_a = p_gray + (size_t) y0 * w;
        uint8_t const * p_b = p_gray + (size_t) y1 * w;
        uint8_t const * p_c = p_gray + (size_t) y2 * w;
        uint8_t const * p_d = p_gray + (size_t) y3 * w;
        uint8_t const * p_e = p_gray + (size_t) y4 * w;

        /* 行内对比度检查：太低（无条码/全黑全白）跳过 */
        uint32_t lo = 0xFFU, hi = 0U;
        for (uint32_t x = 0U; x < w; x++)
        {
            uint8_t const v = median5(p_a[x], p_b[x], p_c[x], p_d[x], p_e[x]);
            med[x] = v;
            if (v < lo) { lo = v; }
            if (v > hi) { hi = v; }
        }
        if ((hi - lo) < 24U)
        {
            continue;
        }

        /* Otsu 阈值（逐行 256-bin 直方图）：比行均值更鲁棒——亮屏场景白底
         * 可能占大部分（均值高→白被误判黑），暗背景则相反。Otsu 找双峰
         * 分割点，条码黑白分明时稳定。 */
        uint32_t hist[256];
        memset(hist, 0, sizeof(hist));
        for (uint32_t x = 0U; x < w; x++)
        {
            hist[med[x]]++;
        }
        uint32_t thr = otsu_threshold(hist, lo, hi, w);
        if (thr == 0U)
        {
            continue;
        }

        /* 对比度自适应拉伸：亮屏（白底 ~255 黑条 ~0）直接阈值；
         * 模糊/反光（白底压到 ~150）时仍可分割。 */
        for (uint32_t x = 0U; x < w; x++)
        {
            bin[x] = (med[x] < thr) ? 1U : 0U;
        }

        /* 候选段检测：在灰度行上找条码密集区（不受整行阈值影响）。若存在
         * 且够长，段内重新 Otsu（局部阈值）再解码——解决纸盒条码只占画面
         * 一小块、整行阈值被背景主导的问题。 */
        uint32_t seg_s = 0U, seg_e = 0U;
        bool const has_seg = find_barcode_segment(med, w, &seg_s, &seg_e);
        if (has_seg)
        {
            /* 段内直方图 + Otsu（只统计条码候选区，背景不参与） */
            uint32_t hist1[256];
            memset(hist1, 0, sizeof(hist1));
            uint32_t lo1 = 0xFFU, hi1 = 0U;
            for (uint32_t x = seg_s; x <= seg_e; x++)
            {
                uint8_t const v = med[x];
                hist1[v]++;
                if (v < lo1) { lo1 = v; }
                if (v > hi1) { hi1 = v; }
            }
            uint32_t const thr1 = otsu_threshold(hist1, lo1, hi1, seg_e - seg_s + 1U);
            if (thr1 != 0U)
            {
                /* 段内多阈值尝试：Otsu 对"段内含图案/印刷"的四峰直方图可能
                 * 选到把图案分开的分割点（图案变黑白交替干扰）。多试几档：
                 *  - thr1      ：常规 Otsu
                 *  - thr1*0.85 ：低对比度（白底贴近 thr 时需更低阈值分离）
                 *  - thr1*1.2  ：图案弱对比时整体归一侧（图案变纯色块不干扰）
                 * 每档段外置白（静区），正反双向。EAN-13 校验位保证只有真码
                 * 通过，多阈值只是增加命中机会。 */
                uint32_t const thr_tries[3] =
                {
                    thr1,
                    (uint32_t) (((uint64_t) thr1 * 85U) / 100U),
                    (uint32_t) (((uint64_t) thr1 * 120U) / 100U),
                };
                for (int t = 0; t < 3; t++)
                {
                    uint32_t const tt = thr_tries[t];
                    if ((tt == 0U) || (tt >= 255U) || (tt == thr1 && t > 0))
                    {
                        continue;
                    }
                    /* bin1 = 段外全部置白（静区），段内用当前阈值重判。
                     * 段外置白：纸盒条码常紧贴深色背景/印刷，起始 guard 黑条
                     * 会与左侧背景合并成超长黑 run（实测 227px），guard 检测
                     * 被跳过、解码从错误位置开始；段外置白让 guard 前是干净
                     * 的静区，起始/结束 guard 都能正确定位。段内已含条码。 */
                    memset(bin1, 0, w);
                    for (uint32_t x = seg_s; x <= seg_e; x++)
                    {
                        bin1[x] = (med[x] < tt) ? 1U : 0U;
                    }

                    if (ean13_decode_binary_line(bin1, w, p_out))
                    {
                        return true;
                    }
                    for (uint32_t x = 0U; x < w; x++)
                    {
                        rev[x] = bin1[w - 1U - x];
                    }
                    if (ean13_decode_binary_line(rev, w, p_out))
                    {
                        return true;
                    }
                }
            }
        }

        /* 整行阈值解码 */
        if (ean13_decode_binary_line(bin, w, p_out))
        {
            return true;
        }

        /* 镜像（OV7725 HFLIP）：整行像素反转 = 画面水平翻转恢复正向 */
        for (uint32_t x = 0U; x < w; x++)
        {
            rev[x] = bin[w - 1U - x];
        }
        if (ean13_decode_binary_line(rev, w, p_out))
        {
            return true;
        }
    }

    return false;
}
