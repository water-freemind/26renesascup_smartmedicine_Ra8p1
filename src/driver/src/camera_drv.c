#include "camera_drv.h"
#include "Camera_thread.h"

#include "FreeRTOS.h"
#include "semphr.h"

/* Shared IIC0 pins (P409/SDA, P410/SCL); used by the power-on bus-silence
 * guard and by the GPIO 9-pulse recovery. */
#define CAMERA_IIC_SDA_PIN    BSP_IO_PORT_04_PIN_09
#define CAMERA_IIC_SCL_PIN    BSP_IO_PORT_04_PIN_10
volatile uint32_t s_dbg_ceu_open_err;  /* R_CEU_Open 返回码 */
volatile uint32_t s_dbg_ceu_cap_err;  /* R_CEU_CaptureStart 返回码 */
volatile uint32_t s_dbg_ceu_camcr;     /* CAMCR after VSYNC polarity override */
volatile uint32_t s_dbg_pin_xclk_low;
volatile uint32_t s_dbg_pin_xclk_high;
volatile uint32_t s_dbg_pin_xclk_toggles;
volatile uint32_t s_dbg_pin_pclk_low;
volatile uint32_t s_dbg_pin_pclk_high;
volatile uint32_t s_dbg_pin_pclk_toggles;
volatile uint32_t s_dbg_pin_href_low;
volatile uint32_t s_dbg_pin_href_high;
volatile uint32_t s_dbg_pin_href_toggles;
volatile uint32_t s_dbg_pin_vsync_low;
volatile uint32_t s_dbg_pin_vsync_high;
volatile uint32_t s_dbg_pin_vsync_toggles;
volatile uint32_t s_dbg_xclk_open_err; /* R_GPT_Open 返回码 */
volatile uint32_t s_dbg_xclk_start_err; /* R_GPT_Start 返回码 */
volatile uint32_t s_dbg_power_rst;   /* power_on after RST(P710) PFS readback */
volatile uint32_t s_dbg_power_pwdn;  /* power_on after PWDN(P709) PFS readback */
volatile uint32_t s_dbg_power_levels; /* bit0=PWDN PIDR, bit1=RST PIDR */

/***********************************************************************************************************************
 * XCLK锛圙PT10 PWM ~25MHz锛岃蛋 RASC 鐢熸垚鐨?g_timer_xclk 瀹炰緥锛? *
 * 璇存槑锛氭棭鏈熻８瀵勫瓨鍣ㄧ洿鍐?GPT10 鏃犳晥锛圝-Link 璇诲洖 0锛夛紝鍘熷洜鏄粫杩?FSP 澶栬浣胯兘/鍐欎繚鎶ゆ椂搴忋€? *      鏀圭敤 RASC 鐢熸垚鐨?GPT 瀹炰緥锛圧_GPT_Open + R_GPT_Start锛夛紝鐢?FSP 椹卞姩缁熶竴瀹屾垚
 *      鏃堕挓浣胯兘銆佸瘎瀛樺櫒鍒濆鍖栦笌 PWM 杈撳嚭閰嶇疆锛圙TIOC10A = P109锛夈€? **********************************************************************************************************************/
void camera_xclk_init(void)
{
    fsp_err_t err;

    /* Open：FSP 驱动完成 GPT10 时钟使能、寄存器初始化与 PWM 输出配置
     * （周期 40ns = 25MHz，占空比 50%，GTIOC10A = P109） */
    err = R_GPT_Open(&g_timer_xclk_ctrl, &g_timer_xclk_cfg);
    s_dbg_xclk_open_err = (uint32_t) err;
    if (FSP_SUCCESS == err)
    {
#if defined(CAMERA_XCLK_KHZ) && (CAMERA_XCLK_KHZ > 0)
        /* XCLK frequency override (diagnostic / tuning): PCLKD=250 MHz.
         * The OV7725 module only starts its clock chain around 12-13 MHz,
         * so the operating frequency is tuned empirically per module. */
        uint32_t const period = 250000000UL / (uint32_t) CAMERA_XCLK_KHZ;
        if (period > 1U)
        {
            (void) R_GPT_PeriodSet(&g_timer_xclk_ctrl, period - 1U);
            (void) R_GPT_DutyCycleSet(&g_timer_xclk_ctrl, period / 2U, GPT_IO_PIN_GTIOCA);
        }
#endif
        err = R_GPT_Start(&g_timer_xclk_ctrl);
        s_dbg_xclk_start_err = (uint32_t) err;
        if (FSP_SUCCESS == err)
        {
            /* RA8P1 GPT10 may retain GTIOCA=1 while OAE is clear when the
             * generated periodic-timer output configuration is not applied
             * by the FSP version in use.  The counter can run in that state,
             * but P109 remains static and OV7725 produces no video timing. */
            (void) R_GPT_OutputEnable(&g_timer_xclk_ctrl, GPT_IO_PIN_GTIOCA);
        }
    }
    else
    {
        s_dbg_xclk_start_err = (uint32_t) err;
    }
}

/***********************************************************************************************************************
 * Slow-XCLK diagnostic (module clock-chain / output-stage check).
 *
 * Only call AFTER the OV7725 register table has been written at the normal
 * 25 MHz: the sensor's SCCB logic is derived from XCLK and fails below its
 * operating range (measured: at 100 kHz XCLK the init table broke after the
 * soft reset).  Slowing to CAMERA_SLOW_XCLK_HZ afterwards keeps PLL lock
 * (>= 6 MHz) while making PCLK/HREF/VSYNC slow enough for the J-Link
 * pin-toggle counters to prove whether the sensor timing chain is alive.
 **********************************************************************************************************************/
void camera_xclk_slow(void)
{
#if defined(CAMERA_SLOW_XCLK_HZ) && (CAMERA_SLOW_XCLK_HZ > 0)
    uint32_t const slow_period = 250000000UL / (uint32_t) CAMERA_SLOW_XCLK_HZ;
    if (slow_period > 1U)
    {
        (void) R_GPT_PeriodSet(&g_timer_xclk_ctrl, slow_period - 1U);
        (void) R_GPT_DutyCycleSet(&g_timer_xclk_ctrl, slow_period / 2U, GPT_IO_PIN_GTIOCA);
    }
#else
    (void) 0;
#endif
}

/***********************************************************************************************************************
 * 涓婄數鏃跺簭锛圥WDN/RST GPIO锛? **********************************************************************************************************************/
void camera_power_on(void)
{
    /* IMPORTANT: RA8P1 PFS registers are write-protected by PWPR. Direct writes to
     * PmnPFS_b.PODR are silently ignored! Use the atomic PORT registers instead:
     *   POSR[bit n]=1 -> set Pn.n output high (write-1-to-set)
     *   PORR[bit n]=1 -> clear Pn.n output low  (write-1-to-clear)
     * R_PORT7 base 0x404000E0, POSR @ +0x08, PORR @ +0x0A */

    /* Bus-silence guard: while PWDN/RST toggle, the OV7725 SIO_D pin can
     * glitch and drive the shared IIC0 SDA.  A falling edge on SDA while
     * SCL is high is a valid I2C START condition, which latches the CST816S
     * touch controller into a broken state that pulls SDA low forever
     * (measured: after entering the scan page, PWDN=1 no longer releases
     * SDA; only a power cycle recovers the touch chip).  Driving both lines
     * low with GPIO during the power sequence prevents any START condition
     * from reaching the touch controller. */
    R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN,
                    IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
    R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN,
                    IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);

    /* Put the sensor into a real power-down/reset state first.  A short MCU
     * reset alone is not sufficient for some OV7725 modules: the sensor can
     * retain its parallel-output state while VDD remains applied. */
    R_PORT7->POSR = (1U << 9);  /* PWDN(P709)=1: power-down */
    R_PORT7->PORR = (1U << 10);

    vTaskDelay(pdMS_TO_TICKS(10));

    /* Release reset while the sensor is still powered down. */
    R_PORT7->POSR = (1U << 10);

    vTaskDelay(pdMS_TO_TICKS(20));

    /* Wake the sensor only after reset has been released. */
    R_PORT7->PORR = (1U << 9);

    /* Keep the bus lines clamped low while the module powers up: its SIO_D
     * pin settles to high-impedance only after power-on completes. */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Restore the IIC peripheral pin mux; the bus idles high via pull-ups. */
    R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN,
                    IOPORT_CFG_DRIVE_MID | IOPORT_CFG_NMOS_ENABLE |
                    IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_IIC);
    R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN,
                    IOPORT_CFG_DRIVE_MID | IOPORT_CFG_NMOS_ENABLE |
                    IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_IIC);

    /* Diagnostics: read back PWDN/RST pin PFS (J-Link readable) */
    s_dbg_power_rst  = R_PFS->PORT[7].PIN[10].PmnPFS;
    s_dbg_power_pwdn = R_PFS->PORT[7].PIN[9].PmnPFS;
    s_dbg_power_levels = (R_BSP_PinRead(PIN_PWDN) ? 1U : 0U) |
                         (R_BSP_PinRead(PIN_RST) ? 2U : 0U);

    /* Wait for the first stable XCLK/SCCB state before opening IIC0. */
    vTaskDelay(pdMS_TO_TICKS(50));
}

void camera_power_off(void)
{
    /* Same bus-silence guard as camera_power_on(): the PWDN/RST GPIO
     * transitions can glitch the OV7725 SIO_D and latch the shared-bus
     * CST816S touch controller (measured: ~90 touch read failures right
     * after leaving the scan page before this guard). */
    R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN,
                    IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
    R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN,
                    IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);

    /* PWDN high and RST low put OV7725 into a non-driving state before the
     * shared IIC controller is reset.  This is preferable to forcing SDA
     * high, which would violate I2C open-drain electrical rules. */
    R_PORT7->POSR = (1U << 9);
    R_PORT7->PORR = (1U << 10);
    vTaskDelay(pdMS_TO_TICKS(5U));

    R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN,
                    IOPORT_CFG_DRIVE_MID | IOPORT_CFG_NMOS_ENABLE |
                    IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_IIC);
    R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN,
                    IOPORT_CFG_DRIVE_MID | IOPORT_CFG_NMOS_ENABLE |
                    IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_IIC);

    s_dbg_power_rst  = R_PFS->PORT[7].PIN[10].PmnPFS;
    s_dbg_power_pwdn = R_PFS->PORT[7].PIN[9].PmnPFS;
    s_dbg_power_levels = (R_BSP_PinRead(PIN_PWDN) ? 1U : 0U) |
                         (R_BSP_PinRead(PIN_RST) ? 2U : 0U);
}

/***********************************************************************************************************************
 * Pin-level timing diagnostic (J-Link readable)
 *
 * Samples the real input levels on XCLK(P109), PCLK(P414), HREF(P415) and
 * VSYNC(P708) through PCNTR2.  Unlike PmnPFS readback this proves whether
 * the sensor is actually toggling each timing line.
 **********************************************************************************************************************/
void camera_diag_sample_sync_pins(uint32_t sample_ms)
{
    uint32_t i;
    uint32_t last_xclk  = 0xFFU;
    uint32_t last_pclk  = 0xFFU;
    uint32_t last_href  = 0xFFU;
    uint32_t last_vsync = 0xFFU;

    if (0U == sample_ms)
    {
        sample_ms = 50U;
    }

    for (i = 0U; i < sample_ms; i++)
    {
        uint32_t const port1 = R_PORT1->PCNTR2_b.PIDR;
        uint32_t const port4 = R_PORT4->PCNTR2_b.PIDR;
        uint32_t const port7 = R_PORT7->PCNTR2_b.PIDR;

        uint32_t const xclk  = (port1 >> 9U) & 1U;
        uint32_t const pclk  = (port4 >> 14U) & 1U;
        uint32_t const href  = (port4 >> 15U) & 1U;
        uint32_t const vsync = (port7 >> 8U) & 1U;

        if (xclk)  { s_dbg_pin_xclk_high++; }  else { s_dbg_pin_xclk_low++; }
        if (pclk)  { s_dbg_pin_pclk_high++; }  else { s_dbg_pin_pclk_low++; }
        if (href)  { s_dbg_pin_href_high++; }  else { s_dbg_pin_href_low++; }
        if (vsync) { s_dbg_pin_vsync_high++; } else { s_dbg_pin_vsync_low++; }

        if (0xFFU != last_xclk)  { s_dbg_pin_xclk_toggles  += (xclk  != last_xclk); }
        if (0xFFU != last_pclk)  { s_dbg_pin_pclk_toggles  += (pclk  != last_pclk); }
        if (0xFFU != last_href)  { s_dbg_pin_href_toggles  += (href  != last_href); }
        if (0xFFU != last_vsync) { s_dbg_pin_vsync_toggles += (vsync != last_vsync); }

        last_xclk  = xclk;
        last_pclk  = pclk;
        last_href  = href;
        last_vsync = vsync;

        R_BSP_SoftwareDelay(1000U, BSP_DELAY_UNITS_MICROSECONDS);
    }

    /* Tight second pass catches MHz-range toggles that 1 ms samples miss. */
    for (i = 0U; i < 20000U; i++)
    {
        uint32_t const port1 = R_PORT1->PCNTR2_b.PIDR;
        uint32_t const port4 = R_PORT4->PCNTR2_b.PIDR;

        uint32_t const xclk = (port1 >> 9U) & 1U;
        uint32_t const pclk = (port4 >> 14U) & 1U;

        if (xclk != last_xclk) { s_dbg_pin_xclk_toggles++; last_xclk = xclk; }
        if (pclk != last_pclk) { s_dbg_pin_pclk_toggles++; last_pclk = pclk; }
    }
}

/***********************************************************************************************************************
 * I2C锛圧IIC0锛岃蛋 RASC 鐢熸垚鐨?g_i2c_master0 瀹炰緥锛? ~100kHz 涓绘満妯″紡
 *
 * 璇存槑锛氭敼鐢?FSP I2C Master 椹卞姩鏇夸唬瑁稿瘎瀛樺櫒杞銆侳SP 渚濇嵁瀹為檯 PCLKB 璁＄畻
 *      娉㈢壒鐜囷紙绾?97.8kHz锛夛紝涓斾腑鏂┍鍔ㄦ柟寮忎笉鍙?CPU 蹇欑骞叉壈銆? **********************************************************************************************************************/

/* I2C 浼犺緭瀹屾垚淇″彿閲忥紙鍥炶皟涓?give锛?*/
static SemaphoreHandle_t s_i2c_sem;
static SemaphoreHandle_t s_i2c_bus_mutex;
static SemaphoreHandle_t s_i2c_init_mutex;
static bool s_i2c_opened;

/* 鏈€杩戜竴娆′紶杈撶粨鏋滐紙鍥炶皟涓褰曪紝渚涗换鍔′笂涓嬫枃鍒ゆ柇锛?*/
static volatile bool s_i2c_ok;

/* I2C 浼犺緭鐘舵€侊細0=绌洪棽 1=鍐欒繘琛屼腑 2=璇昏繘琛屼腑锛堜緵鍥炶皟鍖哄垎浜嬩欢锛?*/
static volatile uint8_t s_i2c_state;

static void i2c_abort_transaction(void);


/* ---- 诊断计数器（J-Link 可读）---- */
volatile uint32_t s_dbg_i2c_open_err;
volatile uint32_t s_dbg_i2c_write_calls;
volatile uint32_t s_dbg_i2c_write_fail;
volatile uint32_t s_dbg_i2c_read_calls;
volatile uint32_t s_dbg_i2c_read_fail;
volatile uint32_t s_dbg_i2c_cb_tx;
volatile uint32_t s_dbg_i2c_cb_rx;
volatile uint32_t s_dbg_i2c_cb_abort;
volatile uint32_t s_dbg_i2c_cb_other;
volatile uint32_t s_dbg_i2c_mutex_timeout;
volatile uint32_t s_dbg_i2c_init_mutex_timeout;
volatile uint32_t s_dbg_i2c_timeout;
volatile uint32_t s_dbg_i2c_abort_calls;
volatile uint32_t s_dbg_i2c_addr_fail;
volatile uint32_t s_dbg_i2c_last_err;
volatile uint32_t s_dbg_i2c_recovery_calls;
volatile uint32_t s_dbg_i2c_recovery_success;
volatile uint32_t s_dbg_i2c_recovery_pulses;

static bool i2c_gpio_recover_bus(void)
{
    uint32_t pulses = 0U;
    bsp_io_level_t sda = BSP_IO_LEVEL_LOW;
    bsp_io_level_t scl = BSP_IO_LEVEL_LOW;
    uint32_t const gpio_input = IOPORT_CFG_PORT_DIRECTION_INPUT | IOPORT_CFG_PULLUP_ENABLE;
    uint32_t const gpio_low = IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                               IOPORT_CFG_PORT_OUTPUT_LOW |
                               IOPORT_CFG_NMOS_ENABLE | IOPORT_CFG_DRIVE_MID;
    uint32_t const iic_cfg = IOPORT_CFG_DRIVE_MID | IOPORT_CFG_NMOS_ENABLE |
                             IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_IIC;

    s_dbg_i2c_recovery_calls++;

    /* Release both lines first. The external pull-ups, not a push-pull high,
     * define the idle level. */
    (void) R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN, gpio_input);
    (void) R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN, gpio_input);
    R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MICROSECONDS);

    /* Clock a slave that stopped in the middle of a byte. */
    for (pulses = 0U; pulses < 9U; pulses++)
    {
        (void) R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN, gpio_low);
        R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MICROSECONDS);
        (void) R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN, gpio_input);
        R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MICROSECONDS);
        s_dbg_i2c_recovery_pulses++;
        (void) R_IOPORT_PinRead(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN, &sda);
        if (BSP_IO_LEVEL_HIGH == sda)
        {
            break;
        }
    }

    /* Generate a STOP: SDA low while SCL is low, release SCL high, then
     * release SDA. This is the only safe software way to request a slave to
     * leave the bus; SDA is never driven high. */
    (void) R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN, gpio_low);
    R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MICROSECONDS);
    (void) R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN, gpio_input);
    R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MICROSECONDS);
    (void) R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN, gpio_input);
    R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MICROSECONDS);

    (void) R_IOPORT_PinRead(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN, &sda);
    (void) R_IOPORT_PinRead(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN, &scl);

    (void) R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SDA_PIN, iic_cfg);
    (void) R_IOPORT_PinCfg(&g_ioport_ctrl, CAMERA_IIC_SCL_PIN, iic_cfg);

    if ((BSP_IO_LEVEL_HIGH == sda) && (BSP_IO_LEVEL_HIGH == scl))
    {
        s_dbg_i2c_recovery_success++;
        return true;
    }

    return false;
}

/***********************************************************************************************************************
 * I2C 鍥炶皟锛圧ASC 閰嶇疆寮曠敤锛孎SP 涓柇涓婁笅鏂囦腑璋冪敤锛? **********************************************************************************************************************/
void camera_i2c_callback(i2c_master_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    switch (p_args->event)
    {
        case I2C_MASTER_EVENT_TX_COMPLETE:
            s_dbg_i2c_cb_tx++;
            s_i2c_ok = true;
            break;

        case I2C_MASTER_EVENT_RX_COMPLETE:
            s_dbg_i2c_cb_rx++;
            s_i2c_ok = true;
            break;

        case I2C_MASTER_EVENT_ABORTED:
            s_dbg_i2c_cb_abort++;
            s_i2c_ok = false;
            break;

        default:
            s_dbg_i2c_cb_other++;
            return;   /* START / BYTE_ACK 绛変腑闂翠簨浠朵笉缁撴潫浼犺緭 */
    }

    s_i2c_state = 0U;
    if (NULL != s_i2c_sem)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(s_i2c_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

bool camera_i2c_init(void)
{
    fsp_err_t err;

    if (NULL == s_i2c_init_mutex)
    {
        taskENTER_CRITICAL();
        if (NULL == s_i2c_init_mutex)
        {
            s_i2c_init_mutex = xSemaphoreCreateMutex();
        }
        taskEXIT_CRITICAL();
    }

    if ((NULL == s_i2c_init_mutex) ||
        (pdTRUE != xSemaphoreTake(s_i2c_init_mutex, pdMS_TO_TICKS(500U))))
    {
        s_dbg_i2c_init_mutex_timeout++;
        return false;
    }

    if (s_i2c_opened)
    {
        (void) xSemaphoreGive(s_i2c_init_mutex);
        return s_i2c_opened;
    }

    if (NULL == s_i2c_sem)
    {
        s_i2c_sem = xSemaphoreCreateBinary();
    }

    if (NULL == s_i2c_bus_mutex)
    {
        s_i2c_bus_mutex = xSemaphoreCreateMutex();
    }

    if ((NULL == s_i2c_sem) || (NULL == s_i2c_bus_mutex))
    {
        s_dbg_i2c_open_err = (uint32_t) FSP_ERR_OUT_OF_MEMORY;
        (void) xSemaphoreGive(s_i2c_init_mutex);
        return false;
    }

    err = R_IIC_MASTER_Open(&g_i2c_master0_ctrl, &g_i2c_master0_cfg);
    if (FSP_SUCCESS != err)
    {
        /* Open 澶辫触锛氳褰曠姸鎬侊紝鍚庣画浼犺緭灏嗚秴鏃惰繑鍥?false */
        s_i2c_ok = false;
    }
    else
    {
        s_i2c_opened = true;
    }

    (void) xSemaphoreGive(s_i2c_init_mutex);
    return s_i2c_opened;
}

bool camera_i2c_bus_is_idle(void)
{
    if (!s_i2c_opened || (NULL == g_i2c_master0_ctrl.p_reg))
    {
        return false;
    }

    return (0U != g_i2c_master0_ctrl.p_reg->ICCR1_b.SDAI) &&
           (0U != g_i2c_master0_ctrl.p_reg->ICCR1_b.SCLI) &&
           (0U == g_i2c_master0_ctrl.p_reg->ICCR2_b.BBSY);
}

bool camera_i2c_recover(void)
{
    fsp_err_t err;
    bool idle = false;

    if ((NULL == s_i2c_bus_mutex) ||
        (pdTRUE != xSemaphoreTake(s_i2c_bus_mutex, pdMS_TO_TICKS(500U))))
    {
        s_dbg_i2c_mutex_timeout++;
        return false;
    }

    i2c_abort_transaction();
    (void) R_IIC_MASTER_Close(&g_i2c_master0_ctrl);
    s_i2c_opened = false;
    vTaskDelay(pdMS_TO_TICKS(2U));
    (void) i2c_gpio_recover_bus();
    err = R_IIC_MASTER_Open(&g_i2c_master0_ctrl, &g_i2c_master0_cfg);
    if (FSP_SUCCESS == err)
    {
        s_i2c_opened = true;
        idle = camera_i2c_bus_is_idle();
    }
    else
    {
        s_dbg_i2c_last_err = (uint32_t) err;
        s_dbg_i2c_open_err = (uint32_t) err;
    }

    (void) xSemaphoreGive(s_i2c_bus_mutex);
    return idle;
}

/* 绛夊緟涓€娆′紶杈撳畬鎴愶紙5 绉掕秴鏃朵繚鎶わ紝杩斿洖鏄惁鎴愬姛锛?*/
static void i2c_drain_completion(void)
{
    if (NULL == s_i2c_sem)
    {
        return;
    }

    while (pdTRUE == xSemaphoreTake(s_i2c_sem, 0U))
    {
        /* Remove a completion left by an aborted or late transaction. */
    }
}

static void i2c_abort_transaction(void)
{
    if (s_i2c_state != 0U)
    {
        (void) R_IIC_MASTER_Abort(&g_i2c_master0_ctrl);
        s_dbg_i2c_abort_calls++;
    }

    s_i2c_state = 0U;
    s_i2c_ok = false;
    i2c_drain_completion();
}

static bool i2c_reopen_locked(void)
{
    fsp_err_t err;

    i2c_abort_transaction();
    (void) R_IIC_MASTER_Close(&g_i2c_master0_ctrl);
    vTaskDelay(pdMS_TO_TICKS(1U));
    err = R_IIC_MASTER_Open(&g_i2c_master0_ctrl, &g_i2c_master0_cfg);
    if (FSP_SUCCESS != err)
    {
        s_dbg_i2c_last_err = (uint32_t) err;
        s_dbg_i2c_open_err = (uint32_t) err;
        return false;
    }
    return true;
}

static bool i2c_wait_done(void)
{
    if (pdTRUE == xSemaphoreTake(s_i2c_sem, pdMS_TO_TICKS(5000U)))
    {
        return s_i2c_ok;
    }

    s_dbg_i2c_timeout++;
    i2c_abort_transaction();
    return false;
}

static bool camera_i2c_write_locked(uint8_t reg, uint8_t data)
{
    uint8_t buf[2];
    fsp_err_t err;

    if (NULL == s_i2c_sem)
    {
        return false;
    }

    /* 纭繚涓婁竴娆′紶杈撳凡缁撴潫锛堣秴鏃朵繚鎶わ紝闃叉鐘舵€佹満閿欎贡锛?*/
    if (s_i2c_state != 0U)
    {
        return false;
    }

    i2c_drain_completion();
    s_dbg_i2c_write_calls++;
    buf[0] = reg;
    buf[1] = data;
    s_i2c_state = 1U;
    s_i2c_ok = false;

    err = R_IIC_MASTER_Write(&g_i2c_master0_ctrl, buf, 2U, false);
    if (FSP_SUCCESS != err)
    {
        s_dbg_i2c_write_fail++;
        i2c_abort_transaction();
        return false;
    }

    if (false == i2c_wait_done())
    {
        s_dbg_i2c_write_fail++;
        return false;
    }
    return true;
}

static bool camera_i2c_read_locked(uint8_t reg,
                                   uint8_t * data,
                                   uint32_t length,
                                   bool repeated_start)
{
    fsp_err_t err;

    s_dbg_i2c_read_calls++;

    if ((NULL == s_i2c_sem) || (NULL == data) || (0U == length))
    {
        return false;
    }

    i2c_drain_completion();

    if (s_i2c_state != 0U)
    {
        s_dbg_i2c_read_fail++;
        return false;
    }

    /* Phase 1: write register address.  CST816S requires a repeated START
     * before the read; OV7725 keeps the historical STOP-separated path. */
    s_i2c_state = 1U;
    s_i2c_ok = false;

    err = R_IIC_MASTER_Write(&g_i2c_master0_ctrl, &reg, 1U, repeated_start);
    if (FSP_ERR_IN_USE == err)
    {
        /* A camera timeout can leave the IIC peripheral busy even after the
         * application semaphore has been released. Reinitialize the FSP
         * instance while the bus mutex is held, then retry this transaction. */
        /* Abort unconditionally as well: the FSP control block can retain
         * its busy flag after a transfer that never reached an IRQ callback,
         * while the application-side state is already zero. */
        (void) R_IIC_MASTER_Abort(&g_i2c_master0_ctrl);
        if (i2c_reopen_locked())
        {
            s_i2c_state = 1U;
            s_i2c_ok = false;
            err = R_IIC_MASTER_Write(&g_i2c_master0_ctrl, &reg, 1U, repeated_start);
        }
    }
    if (FSP_SUCCESS != err)
    {
        s_dbg_i2c_last_err = (uint32_t) err;
        s_dbg_i2c_read_fail++;
        i2c_abort_transaction();
        return false;
    }

    if (false == i2c_wait_done())
    {
        s_dbg_i2c_read_fail++;
        return false;
    }

    /* Phase 2: independent read transaction (FSP sends slave addr+read bit, then STOP) */
    s_i2c_state = 2U;
    s_i2c_ok = false;

    err = R_IIC_MASTER_Read(&g_i2c_master0_ctrl, data, length, false);
    if (FSP_SUCCESS != err)
    {
        s_dbg_i2c_last_err = (uint32_t) err;
        s_dbg_i2c_read_fail++;
        i2c_abort_transaction();
        return false;
    }

    if (!i2c_wait_done())
    {
        s_dbg_i2c_read_fail++;
        return false;
    }
    return true;
}

static bool camera_i2c_select_address(uint8_t address)
{
    if (!s_i2c_opened)
    {
        return false;
    }

#if defined(CAMERA_RTT_ONLY)
    /* RTT-only is the isolated OV7725 baseline: RASC already opens IIC0 with
     * the sensor's fixed 0x21 address.  Avoid the per-transaction
     * slaveAddressSet() call so this path is identical to the historical
     * frame-producing firmware.  The normal GUI path still switches between
     * OV7725 and CST816S under the shared-bus mutex. */
    if (OV7725_ADDR == address)
    {
        return true;
    }
#endif

    return FSP_SUCCESS == g_i2c_master0.p_api->slaveAddressSet(
        &g_i2c_master0_ctrl, address, I2C_MASTER_ADDR_MODE_7BIT);
}

bool camera_i2c_write_at(uint8_t address, uint8_t reg, uint8_t data)
{
    bool result = false;

    if (!s_i2c_opened)
    {
        return false;
    }

    if ((NULL == s_i2c_bus_mutex) ||
        (pdTRUE != xSemaphoreTake(s_i2c_bus_mutex, pdMS_TO_TICKS(500U))))
    {
        s_dbg_i2c_mutex_timeout++;
        return false;
    }

    if (camera_i2c_select_address(address))
    {
        result = camera_i2c_write_locked(reg, data);
        (void) camera_i2c_select_address(OV7725_ADDR);
    }
    else
    {
        s_dbg_i2c_addr_fail++;
    }

    (void) xSemaphoreGive(s_i2c_bus_mutex);
    return result;
}

bool camera_i2c_read_at(uint8_t address, uint8_t reg, uint8_t * data)
{
    bool result = false;

    if ((NULL == data) || !s_i2c_opened || (NULL == s_i2c_bus_mutex) ||
        (pdTRUE != xSemaphoreTake(s_i2c_bus_mutex, pdMS_TO_TICKS(500U))))
    {
        if (NULL != data)
        {
            s_dbg_i2c_mutex_timeout++;
        }
        return false;
    }

    if (camera_i2c_select_address(address))
    {
        result = camera_i2c_read_locked(reg, data, 1U, false);
        (void) camera_i2c_select_address(OV7725_ADDR);
    }
    else
    {
        s_dbg_i2c_addr_fail++;
    }

    (void) xSemaphoreGive(s_i2c_bus_mutex);
    return result;
}

bool camera_i2c_read_block_at(uint8_t address, uint8_t reg, uint8_t * data, uint32_t length)
{
    bool result = false;

    if ((NULL == data) || (0U == length) || !s_i2c_opened || (NULL == s_i2c_bus_mutex) ||
        (pdTRUE != xSemaphoreTake(s_i2c_bus_mutex, pdMS_TO_TICKS(500U))))
    {
        if (NULL != data)
        {
            s_dbg_i2c_mutex_timeout++;
        }
        return false;
    }

    if (camera_i2c_select_address(address))
    {
        result = camera_i2c_read_locked(reg, data, length, true);
        (void) camera_i2c_select_address(OV7725_ADDR);
    }
    else
    {
        s_dbg_i2c_addr_fail++;
    }

    (void) xSemaphoreGive(s_i2c_bus_mutex);
    return result;
}

bool camera_i2c_read_block_stop_at(uint8_t address, uint8_t reg, uint8_t * data, uint32_t length)
{
    bool result = false;

    if ((NULL == data) || (0U == length) || !s_i2c_opened || (NULL == s_i2c_bus_mutex) ||
        (pdTRUE != xSemaphoreTake(s_i2c_bus_mutex, pdMS_TO_TICKS(500U))))
    {
        if (NULL != data)
        {
            s_dbg_i2c_mutex_timeout++;
        }
        return false;
    }

    if (camera_i2c_select_address(address))
    {
        result = camera_i2c_read_locked(reg, data, length, false);
        (void) camera_i2c_select_address(OV7725_ADDR);
    }
    else
    {
        s_dbg_i2c_addr_fail++;
    }

    (void) xSemaphoreGive(s_i2c_bus_mutex);
    return result;
}

bool camera_i2c_probe_at(uint8_t address)
{
    bool result = false;
    uint8_t probe_byte = 0U;
    fsp_err_t err;

    if (!s_i2c_opened || (NULL == s_i2c_bus_mutex) ||
        (pdTRUE != xSemaphoreTake(s_i2c_bus_mutex, pdMS_TO_TICKS(500U))))
    {
        s_dbg_i2c_mutex_timeout++;
        return false;
    }

    if (camera_i2c_select_address(address))
    {
        i2c_drain_completion();
        if (0U == s_i2c_state)
        {
            s_i2c_state = 1U;
            s_i2c_ok = false;
            err = R_IIC_MASTER_Write(&g_i2c_master0_ctrl, &probe_byte, 1U, false);
            if (FSP_SUCCESS == err)
            {
                result = i2c_wait_done();
            }
            else
            {
                i2c_abort_transaction();
            }
        }
        (void) camera_i2c_select_address(OV7725_ADDR);
    }
    else
    {
        s_dbg_i2c_addr_fail++;
    }

    (void) xSemaphoreGive(s_i2c_bus_mutex);
    return result;
}

bool camera_i2c_write(uint8_t reg, uint8_t data)
{
#if defined(CAMERA_RTT_ONLY)
    /*
     * Always select the OV7725 address explicitly, even in the isolated RTT
     * build.  This removes hidden dependence on the IIC driver's previous
     * SlaveAddressSet value and keeps the RTT and shared-bus paths identical.
     */
    return camera_i2c_write_at(OV7725_ADDR, reg, data);
#else
    return camera_i2c_write_at(OV7725_ADDR, reg, data);
#endif
}

bool camera_i2c_read(uint8_t reg, uint8_t * data)
{
#if defined(CAMERA_RTT_ONLY)
    /* Keep the historical OV7725 STOP-separated register-read sequence. */
    return camera_i2c_read_at(OV7725_ADDR, reg, data);
#else
    return camera_i2c_read_at(OV7725_ADDR, reg, data);
#endif
}

/***********************************************************************************************************************
 * CEU 采集启动
 *
 * 工作模式：IMAGE_CAPTURE（RASC: capture_format = image_capture）
 *  - HD (HREF) 作为行同步输入（Active High）
 *  - VD (VSYNC) 作为帧起始触发
 *
 * CMCYR 说明（RA8 CEU 硬件手册，与 RZ/A 同 IP）：
 *  - HCYL = 传感器完整行周期（PCLK 数），即 HREF 相邻上升沿间的时钟数
 *  - VCYL = 传感器完整帧周期（HD 数），即 VSYNC 相邻上升沿间的行数
 *  - FSP R_CEU_Open 会写入 HCYL=x_capture_pixels(320)/VCYL=y_capture_pixels(240)，
 *    与实际行周期不匹配 → 触发 IGHS(HD MISMATCH)，CEU 丢弃每行数据（缓冲全 0）。
 *  - 此处覆盖为传感器真实 HTS/VTS（camera_ov7725_init 从 0x32/0x33 读回），
 *    不能用 VGA 默认 784（OV7725 QVGA 缩放后实际行周期不同）。
 *  - 清除 CETCR 用 Renesas 魔数 0x0317F313（Linux sh_mobile_ceu 驱动同款），
 *    确保下一帧的 IGHS 标志不是残留值。
 *
 * 修复点：
 *  1) CMCYR 覆盖为传感器真实行/帧周期（避免 IGHS 导致 CEU 丢弃数据）。
 *  2) 记录 R_CEU_Open / R_CEU_CaptureStart 返回码（此前变量声明后从未写入，
 *     被链接器 GC 丢弃，错误码全部丢失，无法诊断）。
 **********************************************************************************************************************/
#define CEU_CETCR_CLEAR_MAGIC    (0x0317F313UL)   /* Renesas 推荐的 CETCR 全清除魔数 */

/* OV7725 实际行/帧周期（camera_ov7725_init 从 0x32/0x33 读回，用于 CMCYR） */
extern uint16_t g_ov7725_hts;
extern uint16_t g_ov7725_vts;

fsp_err_t camera_ceu_start(uint8_t * p_frame_buffer)
{
    fsp_err_t err;

    /* CEU 软件复位（CPKIL）：确保从干净状态启动。
     * RA8 CEU 手册（RZ/A 同 IP, p.1961）：IGHS/VBP 等错误触发后，
     * CEU 进入"丢弃行"锁定状态，即使 CE=1 也不捕获。
     * 必须 CPKIL 软件复位 + 等待 CSTSR.CPTON=0 才能恢复。
     * Linux sh_mobile_ceu 驱动同样在每次捕获前执行此序列。 */
    R_CEU->CAPSR = R_CEU_CAPSR_CPKIL_Msk;              /* 软件复位 */
    uint32_t timeout = 10000U;
    while ((R_CEU->CSTSR_b.CPTON != 0U) && (timeout-- > 0U))
    {
        /* 等待 CEU 真正停止 */
    }

    err = R_CEU_Open(g_ceu0.p_ctrl, g_ceu0.p_cfg);
    s_dbg_ceu_open_err = (uint32_t) err;   /* 记录错误码（防 GC 丢弃，J-Link 可读） */
    if (FSP_SUCCESS != err) return err;

    s_dbg_ceu_camcr = R_CEU->CAMCR;

    /* ========================================================================
     * CAPWR/CDWDR/CFSZR - trust FSP R_CEU_Open() auto calculation (verified 2026-08-07):
     *   FSP r_ceu.c formula (8-bit data_synchronous mode):
     *     bytes_per_line  = 320px * 2B = 640
     *     cycles_per_line = 640 / 1 = 640   -> CAPWR.HWDTH  = 640 (PCLK cycles)
     *     hfclp           = 640 / 2 = 320   -> CFSZR.HFCLP  = 320 (4-pixel units)
     *     CDWDR           = 640             -> 640B written per line
     *   [FIX] Old code overwrote 160/160/80 based on wrong "4-PCLK unit" assumption,
     *         making HWDTH(160) << actual HREF high (640 PCLK) -> truncated line -> NHD.
     * ======================================================================== */

    /* CMCYR: disable cycle check (=0). FSP default writes HCYL=320/VCYL=240, but OV7725
     * actual HTS ~= 408 PCLK != 320 -> would trigger IGHS (HD_MISMATCH). OV7725 line
     * period varies with PLL/divider combo; disabled is safest. Tune later if needed. */
    R_CEU->CMCYR = 0U;

    /* 采样边沿：保持 RASC 默认（rising）。OV7725 HREF 边沿在 PCLK 下降沿附近(0-5ns)，
     * rising 采样可采到稳态；历史试过 falling 无改善。 */

    /* 强制单帧捕获模式：清除 CAPCR.CTNCP(bit16)。 */
    R_CEU->CAPCR &= (uint32_t) ~R_CEU_CAPCR_CTNCP_Msk;

    /* 中断使能：恢复全使能（CPEIE|VDIE|CDTOFIE|VBPIE = 0x110201）。
     * [修正] 之前只留 CPEIE+VDIE 导致 CDTOF（CRAM 写溢出，硬件会清 CAPSR.CE）
     *        发生时既不触发中断、事件又被下一次 VD 中断读 CETCR 清除 → 完全无感知，
     *        表现为"CE 写入后变 0、frame_cnt=0、错误计数全 0"。
     * 恢复 CDTOFIE/VBPIE 后，错误会触发回调 → evt_cram/evt_vd_err 计数可读，
     * 从而定位 CE 被清的真实原因。
     * [注] 回调中的 recover 已移除（只计数不 CPKIL 复位），不会再有恢复循环。 */
    R_CEU->CEIER = R_CEU_CEIER_CPEIE_Msk | R_CEU_CEIER_VDIE_Msk |
                   R_CEU_CEIER_CDTOFIE_Msk | R_CEU_CEIER_VBPIE_Msk;

    /* 清除所有 CETCR 错误位（魔数，Linux 驱动验证），避免上一帧 IGHS 残留 */
    R_CEU->CETCR = CEU_CETCR_CLEAR_MAGIC;

    err = R_CEU_CaptureStart(g_ceu0.p_ctrl, p_frame_buffer);
    s_dbg_ceu_cap_err = (uint32_t) err;    /* 记录错误码（防 GC 丢弃，J-Link 可读） */
    return err;
}
