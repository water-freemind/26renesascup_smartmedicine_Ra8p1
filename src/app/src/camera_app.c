#include "camera_app.h"
#include "camera_drv.h"
#include "Camera_thread.h"
#include "sys_log.h"

/* CEU 閿欒鎭㈠榄旀暟锛堜笌 camera_drv.c 涓€鑷达級锛氭竻闄ゆ墍鏈?CETCR 閿欒浣?*/
#define CEU_CETCR_CLEAR_MAGIC    (0x0317F313UL)

/* Temporary single-variable sensor-output diagnostic.  Keep disabled in the
 * normal firmware; enable only to distinguish ISP image-path problems from
 * missing parallel timing. */
#ifndef CAMERA_COLOR_BAR_DIAG
#define CAMERA_COLOR_BAR_DIAG    (0U)
#endif

volatile uint32_t s_dbg_ov_vsync;  /* P708 VSYNC 寮曡剼 PFS 閲囨牱 */
volatile uint32_t s_dbg_ov_pclk;   /* P414 PCLK 寮曡剼 PFS 閲囨牱 */
volatile uint32_t s_dbg_ov_d0;     /* P400 D0 寮曡剼 PFS 閲囨牱 */
volatile uint32_t s_dbg_ov_d7;     /* P703 D7 寮曡剼 PFS 閲囨牱 */volatile uint32_t s_dbg_ov_com7;
volatile uint32_t s_dbg_ov_clkrc;
volatile uint32_t s_dbg_ov_com4;
volatile uint32_t s_dbg_ov_com1;
volatile uint32_t s_dbg_ov_com3;
volatile uint32_t s_dbg_ov_com12;
volatile uint32_t s_dbg_ov_com10;volatile uint32_t s_dbg_ov_pid;   /* OV7725 PID 璇诲洖鍊?*/
volatile uint32_t s_dbg_ov_last_write_reg;
volatile uint32_t s_dbg_ov_com2;    /* COM2 读回 */
volatile uint32_t s_dbg_ov_dsp1;    /* DSP_CTRL1(0x64) 读回 */
volatile uint32_t s_dbg_ov_dsp2;    /* DSP_CTRL2(0x65) 读回 */
volatile uint32_t s_dbg_ov_dsp3;    /* DSP_CTRL3(0x66) 读回 */
volatile uint32_t s_dbg_ov_com8;    /* COM8(0x13) 读回 */
volatile uint32_t s_dbg_ov_dspauto; /* DSPAUTO(0xAC) 读回 */
volatile uint32_t s_dbg_ov_ver;   /* OV7725 VER 璇诲洖鍊?*/
volatile uint32_t s_dbg_ov_vts;   /* VTS(0x2D/0x2E) 读回，帧率调优用 */
uint16_t g_ov7725_hts;            /* OV7725 HTS锛堣鎬诲懆鏈燂紝PCLK 鏁帮級鈥斺€?鐢ㄤ簬 CMCYR.HCYL */
uint16_t g_ov7725_vts;            /* OV7725 VTS锛堝抚鎬诲懆鏈燂紝HD 鏁帮級鈥斺€?鐢ㄤ簬 CMCYR.VCYL */

/***********************************************************************************************************************
 * CEU 鍙岄噰闆嗙紦鍐?& 灏辩华鏍囧織
 *  - 32 瀛楄妭瀵归綈锛堟弧瓒?CEU DMA 绐佸彂浼犺緭瑕佹眰锛岄珮浜?FSP 瑕佹眰鐨?8 瀛楄妭锛? *  - 鍙岀紦鍐诧細閲囬泦鍒?buffer_0 鏃跺鐞?buffer_1锛屼氦鏇夸娇鐢? **********************************************************************************************************************/
uint8_t g_ceu_buffer_0[CAMERA_IMAGE_SIZE]
    __attribute__((section(".sdram_noinit"), aligned(32)));
uint8_t g_ceu_buffer_1[CAMERA_IMAGE_SIZE]
    __attribute__((section(".sdram_noinit"), aligned(32)));
volatile bool g_frame_ready = false;
static volatile bool s_capture_requested;
static volatile bool s_capture_active;
static bool s_camera_initialized;
static bool s_camera_init_blocked;
static TickType_t s_next_init_retry_tick;
volatile uint32_t s_dbg_camera_init_attempts;
volatile uint32_t s_dbg_camera_init_fail_step;
volatile uint32_t s_dbg_camera_init_blocked;

/* 实测帧率（×10 定点，99 = 9.9 FPS）：由 CEU FRAME_END 计数在 1s 滑动窗口内
 * 计算。s_camera_fps_x10 只在采集激活期间更新；s_camera_last_fps_x10 保留最近
 * 一次有效实测值，供管理后台在相机停止后显示“上次”帧率。 */
volatile uint32_t s_camera_fps_x10;
volatile uint32_t s_camera_last_fps_x10;
static TickType_t s_fps_win_start_tick;
static uint32_t   s_fps_win_start_cnt;

static void camera_app_abort_init(void)
{
    camera_power_off();
    (void) camera_i2c_recover();
}

/***********************************************************************************************************************
 * OV7725 瀵勫瓨鍣ㄩ厤缃〃
 **********************************************************************************************************************/
static const ov7725_reg_t g_ov7725_config[] =
{
    /* OV7725 QVGA 320x240 YUV422 init table
     * [FIX-2026-08-07] Rebuilt from authoritative datasheet/Linux ov772x.c analysis:
     *  1. Soft-reset {0x12,0x80} FIRST (was missing entirely -> ISP state undefined)
     *  2. Removed 0x90-0xAE GAM/MTX/BRIGHT/UVADJ/SDE segment - addresses misaligned
     *     (esp32 table copies wrong offsets; {0xAC,0x30} was overwriting DSPAUTO=0xFF)
     *  3. COM7=0x40 (QVGA|YUV422) written LAST to trigger window reload;
     *     this is the official 61ec657 baseline that matches the CEU
     *     YCbCr422 input configuration
     *  4. COM10=0x02 (VSYNC_NEG only) - NEVER set bit6(HSYNC_EN): reroutes HREF pin!
     *  5. COM3=0x50: HFLIP|SWAP_YUV (no color bar - bit0=0)
     */
    { 0x12, 0x80 },              /* COM7: SCCB soft reset - MUST be first */
    { 0x3D, 0x03 },              /* COM12: DCW enable */
    { 0x17, 0x22 },              /* HSTART: VGA window (openmv default) */
    { 0x18, 0xA4 },              /* HSIZE */
    { 0x19, 0x07 },              /* VSTART */
    { 0x1A, 0xF0 },              /* VSIZE */
    { 0x32, 0x00 },              /* HREF: window LSB */
    { 0x29, 0xA0 },              /* HOUTSIZE: 640/4 */
    { 0x2C, 0xF0 },              /* VOUTSIZE: 480/2 */
    { 0x2A, 0x00 },              /* EXHCH: outsize LSB */
    { 0x11, 0xC0 },              /* CLKRC: NO_PRESCALE */
    { 0x42, 0x7F },              /* TGT_B */
    { 0x4D, 0x09 },              /* FIXGAIN */
    { 0x63, 0xE0 },              /* AWB_CTRL0 */
    { 0x64, 0xFF },              /* DSP_CTRL1: FIFO+UV+SDE+MTX+INTRP+GAMMA+defect */
    { 0x65, 0x2F },              /* DSP_CTRL2: VDCW|HDCW|VZOOM|HZOOM (openmv) */
    { 0x66, 0x00 },              /* DSP_CTRL3: color bar OFF */
    { 0x67, 0x48 },              /* DSP_CTRL4 */
    { 0x13, 0xF0 },              /* COM8: AGC/AEC/AWB auto (partial) */
    { 0x0D, 0x41 },              /* COM4: PLL 4x */
    { 0x0F, 0xC5 },              /* COM6 */
    { 0x14, 0x11 },              /* COM9: AGC ceiling */
    { 0x22, 0x7F },              /* BDBASE */
    { 0x23, 0x03 },              /* BDSTEP */
    { 0x24, 0x40 },              /* AEW */
    { 0x25, 0x30 },              /* AEB */
    { 0x26, 0xA1 },              /* VPT */
    { 0x2B, 0x00 },              /* EXHCL */
    { 0x6B, 0xAA },              /* AWB_CTRL3 */
    { 0x13, 0xFF },              /* COM8: AGC/AEC/AWB all auto */
    { 0x90, 0x05 },              /* EDGE1 */
    { 0x91, 0x01 },              /* DNSOFF */
    { 0x92, 0x03 },              /* EDGE2 */
    { 0x93, 0x00 },              /* EDGE3 */
    { 0x94, 0xB0 },              /* MTX1 */
    { 0x95, 0x9D },              /* MTX2 */
    { 0x96, 0x13 },              /* MTX3 */
    { 0x97, 0x16 },              /* MTX4 */
    { 0x98, 0x7B },              /* MTX5 */
    { 0x99, 0x91 },              /* MTX6 */
    { 0x9A, 0x1E },              /* MTX_CTRL */
    { 0x9B, 0x08 },              /* BRIGHTNESS */
    { 0x9C, 0x20 },              /* CONTRAST */
    { 0x9E, 0x81 },              /* UVADJ0 */
    { 0xA6, 0x06 },              /* SDE: CONT_BRIGHT|SATURATION */
    { 0x7E, 0x0C },              /* GAM1 */
    { 0x7F, 0x16 },              /* GAM2 */
    { 0x80, 0x2A },              /* GAM3 */
    { 0x81, 0x4E },              /* GAM4 */
    { 0x82, 0x61 },              /* GAM5 */
    { 0x83, 0x6F },              /* GAM6 */
    { 0x84, 0x7B },              /* GAM7 */
    { 0x85, 0x86 },              /* GAM8 */
    { 0x86, 0x8E },              /* GAM9 */
    { 0x87, 0x97 },              /* GAM10 */
    { 0x88, 0xA4 },              /* GAM11 */
    { 0x89, 0xAF },              /* GAM12 */
    { 0x8A, 0xC5 },              /* GAM13 */
    { 0x8B, 0xD7 },              /* GAM14 */
    { 0x8C, 0xE8 },              /* GAM15 */
    { 0x8D, 0x20 },              /* SLOP */
    { 0x4A, 0x10 },              /* LC_RADI */
    { 0x49, 0x10 },              /* LC_COEF */
    { 0x4B, 0x14 },              /* LC_COEFB */
    { 0x4C, 0x17 },              /* LC_COEFR */
    { 0x46, 0x01 },              /* LC_CTR: lens correction enable */
    { 0x0E, 0xF5 },              /* COM5: frame rate */
    { 0x15, 0x02 },              /* COM10: VSYNC_NEG only, HREF normal */
    { 0x0C, 0x50 },              /* COM3: HFLIP|SWAP_YUV, color bar OFF */
    { 0xAC, 0xF3 },              /* DSPAUTO: 关闭自动缩放（VGA 直出） */
    { 0xA0, 0x00 },              /* SCAL0 */
    { 0xA1, 0x40 },              /* SCAL1 */
    { 0xA2, 0x40 },              /* SCAL2 */
    { 0x12, 0x00 },              /* COM7: VGA(0x00)|YUV(0x00) - LAST, 触发窗口重载 */
    { 0xFF, 0xFF }               /* END */

};

/***********************************************************************************************************************
 * OV7725 閫氳繃 SCCB 鍒濆鍖? **********************************************************************************************************************/
bool camera_ov7725_init(void)
{
    const ov7725_reg_t *p_entry = g_ov7725_config;

    while ((p_entry->reg != 0xFF) || (p_entry->val != 0xFF))
    {
        s_dbg_ov_last_write_reg = p_entry->reg;
        if (false == camera_i2c_write(p_entry->reg, p_entry->val))
        {
            return false;
        }
        p_entry++;

        if ((p_entry - 1)->reg == 0x12 && (p_entry - 1)->val == 0x80)
        {
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }

    /* 鏍￠獙浼犳劅鍣?ID */
    uint8_t pid = 0, ver = 0;
    if (camera_i2c_read(0x0A, &pid) && camera_i2c_read(0x0B, &ver))
    {
        s_dbg_ov_pid = pid;
        s_dbg_ov_ver = ver;
        if (pid != 0x77)
        {
            return false;
        }
    }

#if defined(CAMERA_COM5_OVERRIDE) && (CAMERA_COM5_OVERRIDE > 0)
    /* Frame-rate tuning: COM5 (0x0E) low nibble is the frame-rate divider
     * setting.  The stock table writes 0xF5; lower nibbles may raise the
     * frame rate at the same XCLK. */
    (void) camera_i2c_write(0x0E, (uint8_t) CAMERA_COM5_OVERRIDE);
#endif

#if defined(CAMERA_VTS_OVERRIDE) && (CAMERA_VTS_OVERRIDE > 0)
    /* Frame-rate tuning: write a smaller VTS (0x2D/0x2E) to shrink vertical
     * blanking.  Frame rate = PCLK / (HTS * VTS); the module caps XCLK at
     * ~13 MHz, so VTS is the only remaining software lever.  The value must
     * stay above ~264 (240 active lines + vertical blanking). */
    {
        uint16_t const vts = (uint16_t) CAMERA_VTS_OVERRIDE;
        (void) camera_i2c_write(0x2D, (uint8_t) ((vts >> 8) & 0xFFU));
        (void) camera_i2c_write(0x2E, (uint8_t) (vts & 0xFFU));
        s_dbg_ov_vts = vts;
    }
#endif

    /* 璇婃柇锛氳鍥炲叧閿瘎瀛樺櫒楠岃瘉鍐欏叆鐢熸晥锛堢粡 uint8_t 涓浆锛岄伩鍏嶇被鍨嬩笉鍖归厤锛?*/
    uint8_t dbg_val = 0;
    if (camera_i2c_read(0x12, &dbg_val)) { s_dbg_ov_com7 = dbg_val; }    /* COM7 */
    if (camera_i2c_read(0x11, &dbg_val)) { s_dbg_ov_clkrc = dbg_val; }   /* CLKRC */
    if (camera_i2c_read(0x0D, &dbg_val)) { s_dbg_ov_com4 = dbg_val; }    /* COM4 */
    if (camera_i2c_read(0x04, &dbg_val)) { s_dbg_ov_com1 = dbg_val; }    /* COM1 */
    if (camera_i2c_read(0x0C, &dbg_val)) { s_dbg_ov_com3 = dbg_val; }    /* COM3 */
    if (camera_i2c_read(0x3D, &dbg_val)) { s_dbg_ov_com12 = dbg_val; }   /* COM12 */
    if (camera_i2c_read(0x15, &dbg_val)) { s_dbg_ov_com10 = dbg_val; }   /* COM10 */
    /* [新增] 读回 DSP 数据通路关键寄存器：D0-D7 全 0 的直接嫌疑 */
    if (camera_i2c_read(0x09, &dbg_val)) { s_dbg_ov_com2 = dbg_val; }    /* COM2: soft sleep(bit4) */
    if (camera_i2c_read(0x64, &dbg_val)) { s_dbg_ov_dsp1 = dbg_val; }    /* DSP_CTRL1: FIFO enable(bit7) */
    if (camera_i2c_read(0x65, &dbg_val)) { s_dbg_ov_dsp2 = dbg_val; }    /* DSP_CTRL2 */
    if (camera_i2c_read(0x66, &dbg_val)) { s_dbg_ov_dsp3 = dbg_val; }    /* DSP_CTRL3 */
    if (camera_i2c_read(0x13, &dbg_val)) { s_dbg_ov_com8 = dbg_val; }    /* COM8: AEC/AGC/AWB */
    if (camera_i2c_read(0xAC, &dbg_val)) { s_dbg_ov_dspauto = dbg_val; } /* DSPAUTO */

    /* 璇诲洖 HTS/VTS 鈥斺€?鐢ㄤ簬 CMCYR 绮剧‘閰嶇疆锛圚CYL=琛孭CLK鏁? VCYL=甯D鏁帮級銆?     * OV7725 鐨?HTS 鐢?HREF 瀵勫瓨鍣ㄥ喅瀹氾細
     *   HTS = (0x32[5:0] << 8) | 0x33            锛?x32=HREF 楂樹綅[5:0], 0x33=HREF 浣庝綅[7:0]锛?     * 鍙傝€?RA CEU 鎵嬪唽锛欼GHS 瑙﹀彂鏉′欢 = 瀹為檯 HD 鍛ㄦ湡 鈮?CMCYR.HCYL锛?     * 蹇呴』璇诲洖浼犳劅鍣ㄧ湡瀹炶鍛ㄦ湡锛屼笉鑳界敤 VGA 榛樿鍊?784锛圦VGA 缂╂斁鍚庝笉鍚岋級銆?*/
    uint8_t href_h = 0, href_l = 0;
    if (camera_i2c_read(0x32, &href_h) && camera_i2c_read(0x33, &href_l))
    {
        g_ov7725_hts = (uint16_t) (((href_h & 0x3FU) << 8) | href_l);
    }

    /* Frame-rate tuning diagnostics: read back VTS (0x2D/0x2E) and HTS so the
     * actual timing budget (frame = PCLK/(HTS*VTS)) can be computed. */
    {
        uint8_t vts_h = 0, vts_l = 0;
        if (camera_i2c_read(0x2D, &vts_h) && camera_i2c_read(0x2E, &vts_l))
        {
            s_dbg_ov_vts = (uint32_t) (((uint32_t) vts_h << 8) | vts_l);
        }
    }

    /* 姝ｅ父鎴愬儚妯″紡锛堝僵鏉″凡绉婚櫎鈥斺€旇瘖鏂‘璁ゆ崟鑾烽摼璺悗鎭㈠姝ｅ父鍥惧儚锛?*/

    vTaskDelay(pdMS_TO_TICKS(50));

    /* 閲囨牱 VSYNC(P708)/PCLK(P414)/D0(P400)/D7(P703) 寮曡剼鐘舵€?*/
    s_dbg_ov_vsync = R_PFS->PORT[7].PIN[8].PmnPFS;     /* P708 */
    s_dbg_ov_pclk  = R_PFS->PORT[4].PIN[14].PmnPFS;    /* P414 */
    s_dbg_ov_d0    = R_PFS->PORT[4].PIN[0].PmnPFS;     /* P400 */
    s_dbg_ov_d7    = R_PFS->PORT[7].PIN[3].PmnPFS;     /* P703 */

    return true;
}

/***********************************************************************************************************************
 * CEU 閿欒鎭㈠锛堝湪 ISR 涓婁笅鏂囦腑璋冪敤锛? * RA8 CEU 鎵嬪唽锛歏BP/IGHS 绛夐敊璇Е鍙戝悗锛孋EU 杩涘叆閿佸畾鐘舵€侊紝
 * 蹇呴』 CPKIL 杞欢澶嶄綅 + 閲嶆柊 CaptureStart 鎵嶈兘鎭㈠鎹曡幏銆? * 浣跨敤 xHigherPriorityTaskWoken 瀹夊叏閲嶅惎銆? **********************************************************************************************************************/
/***********************************************************************************************************************
 * CEU 閲囬泦鍥炶皟锛堝湪 ISR 涓婁笅鏂囦腑璋冪敤锛? *  - 浜嬩欢鍒嗙被璁℃暟锛圝-Link 鍙锛夛細s_dbg_ceu_frame_cnt 閫掑 = 涓柇纭疄鍦ㄨЕ鍙? **********************************************************************************************************************/
volatile uint32_t s_dbg_ceu_frame_cnt;   /* CEU_EVENT_FRAME_END 娆℃暟锛堜腑鏂Е鍙戣瘉鏄庯級 */
volatile uint32_t s_dbg_ceu_evt_vd;      /* CEU_EVENT_VD 娆℃暟 */
volatile uint32_t s_dbg_ceu_evt_cram;    /* CEU_EVENT_CRAM_OVERFLOW 娆℃暟 */
volatile uint32_t s_dbg_ceu_evt_hd_mis;  /* CEU_EVENT_HD_MISMATCH 娆℃暟 */
volatile uint32_t s_dbg_ceu_evt_vd_mis;  /* CEU_EVENT_VD_MISMATCH 娆℃暟 */
volatile uint32_t s_dbg_ceu_evt_vd_err;  /* CEU_EVENT_VD_ERROR 娆℃暟 */
volatile uint32_t s_dbg_ceu_evt_hd_miss; /* CEU_EVENT_HD_MISSING 娆℃暟 */
volatile uint32_t s_dbg_ceu_evt_vd_miss; /* CEU_EVENT_VD_MISSING 娆℃暟 */
volatile uint32_t s_dbg_ceu_evt_other;   /* 鍏朵粬浜嬩欢娆℃暟 */

void g_ceu0_user_callback(capture_callback_args_t *p_args)
{
    if (NULL == p_args) return;

    /* [淇] 鐢ㄤ綅鎺╃爜鍒ゆ柇浜嬩欢锛屼笉鑳界敤 switch 绮剧‘鍖归厤锛?     * FSP ISR (r_ceu.c:516) 浼犵殑鏄?events & interrupts_enabled 鈥斺€?涓€娆?ISR 鍙兘鎼哄甫
     * 澶氫釜鍚屾椂缃綅鐨勬爣蹇楋紙濡傚抚缁撴潫鏃?CPE=0x1 涓?VD=0x200 鍚屾椂涓?1 鈫?event=0x201锛夈€?     * 鍘熸潵鐨?switch 瀵?0x201 涓€涓?case 閮戒笉鍖归厤 鈫?钀藉叆 default 璁颁负 other锛?     * 瀵艰嚧 FRAME_END 姘歌繙"鐪嬩笉鍒?銆乬_frame_ready 姘镐笉缃綅銆佷富寰幆姘镐笉閲嶅惎鎹曡幏銆?*/
    uint32_t evt = (uint32_t) p_args->event;

    if (evt & CEU_EVENT_FRAME_END)
    {
        s_dbg_ceu_frame_cnt++;
        /* D-Cache 澶辨晥锛欳EU DMA 鐩存帴鍐欑墿鐞?SRAM锛堢粫杩?D-Cache锛夛紝
         * 鍦?ISR 涓珛鍗冲け鏁堬紝纭繚涓诲惊鐜鍒扮殑鏄?DMA 鍐欏叆鐨勬柊鏁版嵁銆?         * 锛圖-Cache 鏈娇鑳芥椂姝よ皟鐢ㄤ负 no-op锛屽畨鍏ㄦ棤瀹筹級 */
        SCB_InvalidateDCache_by_Addr((uint32_t *) p_args->p_buffer,
                                     (int32_t) CAMERA_IMAGE_SIZE);
        g_frame_ready = true;
    }
    if (evt & CEU_EVENT_VD)
    {
        s_dbg_ceu_evt_vd++;
    }
    if (evt & CEU_EVENT_CRAM_OVERFLOW)
    {
        s_dbg_ceu_evt_cram++;
    }
    if (evt & CEU_EVENT_HD_MISMATCH)
    {
        s_dbg_ceu_evt_hd_mis++;
    }
    if (evt & CEU_EVENT_VD_MISMATCH)
    {
        s_dbg_ceu_evt_vd_mis++;
    }
    if (evt & CEU_EVENT_VD_ERROR)
    {
        /* VBP锛堝瀭鐩村墠鑲╀笉瓒筹級锛歊Z/A CEU 鎵嬪唽 p.1961 鈥斺€?VBP 瑙﹀彂鏃剁‖浠惰嚜鎰堬紝
         * "CE=1 浣嗘殏鍋滀紶杈擄紝绛変笅涓€涓?VD 鑷姩鎭㈠"銆傛棤闇€杞欢骞查锛?         * [淇] 鍘熸潵璋?ceu_recover_from_error()锛圕PKIL 澶嶄綅+閲嶅惎锛夊弽鑰屽埗閫?         *        鏃犻檺鎭㈠寰幆锛坮ecover_cnt 瀹炴祴 106 娆★級鈥斺€旀瘡娆￠噸鍚張绔嬪嵆 VBP锛?         *        CPKIL 澶嶄綅杩樻竻鎺?CETCR锛屽鑷存案杩滅湅涓嶅埌 FRAME_END銆?*/
        s_dbg_ceu_evt_vd_err++;
    }
    if (evt & CEU_EVENT_HD_MISSING)
    {
        /* NHD锛氭煇琛?HD 瓒呮椂锛?6376 PCLK 鏃?HD锛夈€侼HDIE 鏈娇鑳戒笉瑙﹀彂 ISR銆?         * 鑻ュ嚭鐜拌鏄庢椂搴忔湁闂锛屼絾涓嶈 CPKIL 澶嶄綅鎵撴柇閲囬泦锛堝悓涓婏級銆?*/
        s_dbg_ceu_evt_hd_miss++;
    }
    if (evt & CEU_EVENT_VD_MISSING)
    {
        s_dbg_ceu_evt_vd_miss++;
    }
    if (0U == (evt & (CEU_EVENT_FRAME_END | CEU_EVENT_VD | CEU_EVENT_CRAM_OVERFLOW |
                      CEU_EVENT_HD_MISMATCH | CEU_EVENT_VD_MISMATCH | CEU_EVENT_VD_ERROR |
                      CEU_EVENT_HD_MISSING | CEU_EVENT_VD_MISSING)))
    {
        s_dbg_ceu_evt_other++;
    }
}

/***********************************************************************************************************************
 * 瀹屾暣鐨勬憚鍍忓ご鍒濆鍖栧簭鍒? * @return true = 鎽勫儚澶村氨缁紱false = 鍒濆鍖栧け璐ワ紙璋冪敤鏂瑰喅瀹氬浣曞鐞嗭紝涓嶅啀姝诲惊鐜級
 **********************************************************************************************************************/
bool camera_app_init(void)
{
    if (s_camera_initialized)
    {
        return true;
    }

    s_dbg_camera_init_attempts++;
    s_dbg_camera_init_fail_step = 0U;

    /* Keep the known-good bring-up order from the validated camera baseline:
     * XCLK -> sensor reset release -> IIC open.  In RTT-only mode the touch
     * controller is not initialized, so there is no shared-bus device to
     * protect before camera startup. */
    camera_xclk_init();
    camera_power_on();

    /* Open IIC only after the sensor has a valid clock and reset state. */
    if (!camera_i2c_init())
    {
        s_dbg_camera_init_fail_step = 1U;
        camera_app_abort_init();
        s_camera_init_blocked = true;
        s_dbg_camera_init_blocked = 1U;
        return false;
    }

    if (!camera_i2c_bus_is_idle())
    {
        s_dbg_camera_init_fail_step = 2U;
        camera_app_abort_init();
        s_camera_init_blocked = true;
        s_dbg_camera_init_blocked = 1U;
        return false;
    }

    /* Power/reset must not make the shared touch bus busy. */
    if (!camera_i2c_bus_is_idle())
    {
        s_dbg_camera_init_fail_step = 3U;
        camera_app_abort_init();
        s_camera_init_blocked = true;
        s_dbg_camera_init_blocked = 1U;
        return false;
    }

    if (false == camera_ov7725_init())
    {
        s_dbg_camera_init_fail_step = 4U;
        s_camera_initialized = false;
        camera_app_abort_init();
        s_camera_init_blocked = true;
        s_dbg_camera_init_blocked = 1U;
        return false;
    }

#if (CAMERA_COLOR_BAR_DIAG == 1U)
    /* Historical 2026-08-07 breakthrough: forcing the internal color-bar
     * generator made D0-D7 and CEU frame events observable. */
    (void) camera_i2c_write(0x0C, 0x51U); /* retain COM3 settings + color bar */
    (void) camera_i2c_write(0x66, 0x20U); /* DSP_CTRL3 color-bar enable */
    {
        uint8_t color_bar = 0U;
        if (camera_i2c_read(0x66, &color_bar))
        {
            s_dbg_ov_dsp3 = color_bar;
        }
    }
#endif

#if defined(CAMERA_SLOW_XCLK_HZ) && (CAMERA_SLOW_XCLK_HZ > 0)
    /* Clock-chain diagnostic: the whole register table (including SCCB) was
     * just written at the normal 25 MHz; only now slow XCLK so the sensor's
     * PLL keeps lock while its timing outputs become countable by the J-Link
     * pin-toggle counters.  Give the PLL time to re-lock. */
    camera_xclk_slow();
    vTaskDelay(pdMS_TO_TICKS(200U));
#endif

#if defined(CAMERA_RTT_ONLY)
    /* RTT-only diagnostic firmware records whether XCLK/PCLK/HREF/VSYNC
     * actually toggle before CEU is started. */
#if defined(CAMERA_SLOW_XCLK_HZ) && (CAMERA_SLOW_XCLK_HZ > 0)
    camera_diag_sample_sync_pins(500U);
#else
    camera_diag_sample_sync_pins(50U);
#endif
#endif

    s_camera_initialized = true;
    s_capture_active = false;
    g_frame_ready = false;
    sys_log_add(SYS_LOG_OK, "摄像头初始化成功 (OV7725)");

#if defined(CAMERA_RTT_ONLY)
    /* Match the validated pre-screen camera firmware: start CEU immediately
     * after sensor initialization instead of introducing a second scheduling
     * boundary through the on-demand service path. */
    vTaskDelay(pdMS_TO_TICKS(100U));
    if (FSP_SUCCESS == camera_ceu_start(g_ceu_buffer_0))
    {
        s_capture_active = true;
        /* Sample again after CEU is enabled; some OV7725 modules gate the
         * parallel timing outputs until the capture engine is running. */
#if defined(CAMERA_SLOW_XCLK_HZ) && (CAMERA_SLOW_XCLK_HZ > 0)
        camera_diag_sample_sync_pins(500U);
        /* At a slowed XCLK the OV7725 PLL re-lock + AGC settle can take
         * seconds before PCLK/HREF/VSYNC appear (observed: D-bus and VSYNC
         * changed only around t+10 s in the previous run).  Re-sample at
         * t+3 s and t+6 s so the J-Link counters capture the wake-up. */
        vTaskDelay(pdMS_TO_TICKS(3000U));
        camera_diag_sample_sync_pins(1000U);
        vTaskDelay(pdMS_TO_TICKS(3000U));
        camera_diag_sample_sync_pins(1000U);
#else
        camera_diag_sample_sync_pins(50U);
#endif
    }
#endif
    return true;
}

void camera_app_request_capture(bool enable)
{
    s_capture_requested = enable;
    if (!enable)
    {
        s_camera_init_blocked = false;
        s_dbg_camera_init_blocked = 0U;
        s_next_init_retry_tick = xTaskGetTickCount();
    }
}

bool camera_app_capture_active(void)
{
    return s_capture_active;
}

/***********************************************************************************************************************
 * 实测帧率统计（Camera 线程 ~1ms 周期调用）
 *  - 以 CEU FRAME_END 中断计数为基准，1s 滑动窗口内计算 fps×10；
 *  - 采集停止时当前值清零，但保留最近一次有效实测值供后台展示。
 **********************************************************************************************************************/
void camera_app_update_fps(void)
{
    TickType_t const now = xTaskGetTickCount();
    uint32_t const cnt = s_dbg_ceu_frame_cnt;

    if (0U == s_fps_win_start_tick)
    {
        s_fps_win_start_tick = now;
        s_fps_win_start_cnt = cnt;
        return;
    }

    TickType_t const dt = now - s_fps_win_start_tick;   /* 无符号差，tick 回绕安全 */
    if (dt >= pdMS_TO_TICKS(1000U))
    {
        uint32_t const dc = cnt - s_fps_win_start_cnt;  /* 窗口内帧数 */
        uint32_t const dt_ms = (uint32_t) ((uint64_t) dt * (uint64_t) portTICK_PERIOD_MS);
        /* fps = dc * 1000 / dt_ms；×10 定点 → fps_x10 = dc * 10000 / dt_ms */
        uint32_t const fps_x10 = (dt_ms > 0U)
                                 ? (uint32_t) (((uint64_t) dc * 10000U) / (uint64_t) dt_ms)
                                 : 0U;
        s_camera_fps_x10 = fps_x10;
        if (fps_x10 > 0U)
        {
            s_camera_last_fps_x10 = fps_x10;
        }
        s_fps_win_start_tick = now;
        s_fps_win_start_cnt = cnt;
    }
}

uint32_t camera_app_get_fps_x10(void)
{
    return s_camera_fps_x10;
}

uint32_t camera_app_get_last_fps_x10(void)
{
    return s_camera_last_fps_x10;
}

bool camera_app_service_capture(void)
{
    if (s_capture_requested && !s_capture_active)
    {
        if (s_camera_init_blocked)
        {
            return false;
        }

        /* Keep the shared IIC0 available to CST816S until a camera page
         * actually requests a frame. */
        if (!s_camera_initialized)
        {
            TickType_t const now = xTaskGetTickCount();
            if (now < s_next_init_retry_tick)
            {
                return false;
            }

            s_next_init_retry_tick = now + pdMS_TO_TICKS(1000U);
            if (!camera_app_init())
            {
                return false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        if (FSP_SUCCESS == camera_ceu_start(g_ceu_buffer_0))
        {
            g_frame_ready = false;
            s_capture_active = true;
            sys_log_add(SYS_LOG_INFO, "摄像头采集启动");
        }
    }
    else if (!s_capture_requested && s_capture_active)
    {
        (void) R_CEU_Close(g_ceu0.p_ctrl);
        g_frame_ready = false;
        s_capture_active = false;
        /* Release the shared IIC0 and stop module idle draw: the uninitialized
         * powered-on OV7725 pulls SDA low (measured).  Power it down so the
         * bus stays free for CST816S, and require a full re-init on the next
         * Pickup/Scan entry. */
        camera_power_off();
        s_camera_initialized = false;
        /* 采集停止：当前帧率清零（保留最近一次实测值供后台展示） */
        s_camera_fps_x10 = 0U;
        s_fps_win_start_tick = 0U;
        sys_log_add(SYS_LOG_INFO, "摄像头采集停止");
    }

    if (s_capture_active)
    {
        camera_app_update_fps();
    }

    return s_capture_active;
}
