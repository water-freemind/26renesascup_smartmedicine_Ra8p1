/*
 * Copyright (c) 2025 Renesas Electronics Corporation and/or its affiliates
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Camera_thread.h"
#include "r_ceu.h"

/*******************************************************************************************************************//**
 * Macros
 **********************************************************************************************************************/

/* OV7725 SCCB (I2C) slave address (7-bit) */
#define OV7725_ADDR                 (0x21U)

/* RIIC0 I2C timing: target ~100kHz with PCLKB=62.5MHz, CKS=4 (fIIC=3.90625MHz).
 * ICBRL/ICBRH are 5-bit fields (max 31).
 * fSCL = fIIC / (BRL + BRH + 2) ≈ 3.90625 / (18 + 19 + 2) ≈ 100.16kHz */
#define RIIC0_CKS                   (4U)
#define RIIC0_BRL                   (18U)
#define RIIC0_BRH                   (19U)

/* GPT10 XCLK: target ~25MHz with PCLKD=250MHz.
 * Sawtooth PWM: output period = 2*(GTPR+1) PCLKD cycles → 25MHz */
#define GPT10_XCLK_PERIOD           (4U)    /* PCLKD / (2*(GTPR+1)) = 250/10 = 25MHz */

/* CEU capture buffer size: 640 x 480 x 2 bytes (YCbCr422) */
#define CAMERA_IMAGE_SIZE           (640U * 480U * 2U)

/* Pins — already configured as GPIO outputs by g_bsp_pin_cfg */
#define PIN_PWDN                    BSP_IO_PORT_07_PIN_09   /* P709: CAM_PWDN (active high: 1=power down) */
#define PIN_RST                     BSP_IO_PORT_07_PIN_10   /* P710: CAM_RST  (active low:  0=reset)     */

/*******************************************************************************************************************//**
 * Private global variables
 **********************************************************************************************************************/
static uint8_t g_camera_frame_buffer[CAMERA_IMAGE_SIZE]  __attribute__((aligned(16)));
static volatile bool g_frame_ready = false;

/*******************************************************************************************************************//**
 * Local function prototypes
 **********************************************************************************************************************/
static void camera_xclk_init(void);
static void camera_power_on(void);
static void camera_i2c_init(void);
static bool camera_i2c_write(uint8_t reg, uint8_t data);
static bool camera_i2c_read(uint8_t reg, uint8_t * data);
static bool camera_ov7725_init(void);
static void camera_ceu_start(void);

/*******************************************************************************************************************//**
 * @brief  OV7725 register table for basic initialization
 **********************************************************************************************************************/
typedef struct
{
    uint8_t reg;
    uint8_t val;
} ov7725_reg_t;

static const ov7725_reg_t g_ov7725_config[] =
{
    /* Register       Value      Description */
    { 0x12,           0x80 },              /* COM7: Reset all registers */
    { 0x0D,           0x41 },              /* COM4: PLL enable, /1 divider */
    { 0x11,           0x01 },              /* CLKRC: Internal clock = XCLK / 2 (no prescaler) */
    { 0x12,           0x0C },              /* COM7: VGA, YCbCr422 output format */
    { 0x40,           0xC1 },              /* COM15: Full output range, RGB565 */
    { 0x3D,           0x03 },              /* COM12: Enable DCW, enable scaling */
    { 0x1E,           0x00 },              /* MVFP: No mirror/flip */
    { 0x0C,           0x0A },              /* COM3: Enable DCW, default settings */
    { 0x2A,           0x00 },              /* FR58, Frame rate fine tuning */
    { 0x2B,           0x00 },              /* FR67, Frame rate fine tuning */
    { 0x13,           0xFF },              /* BLCn: Auto BLC on */
    { 0x14,           0x48 },              /* BLCn: Auto BLC on */
    { 0x15,           0x20 },              /* BLCn: Auto BLC on */
    { 0x92,           0x00 },              /* DM_LNL: Set default */
    { 0x93,           0x00 },              /* DM_LNH: Set default */
    { 0x94,           0x00 },              /* DM_PUL: Set default */
    { 0x95,           0x00 },              /* DM_PUH: Set default */
    { 0x96,           0x00 },              /* DM_PUL: Set default */
    { 0x97,           0x00 },              /* DM_PUH: Set default */
    { 0xFF,           0xFF },              /* Terminator */
};

/*******************************************************************************************************************//**
 * @brief  Initialize GPT10 to output ~25MHz XCLK on P109 (GTIOC10A)
 **********************************************************************************************************************/
static void camera_xclk_init(void)
{
    /* Enable GPT10 module clock */
    R_BSP_MODULE_START(FSP_IP_GPT, 10U);

    /* Unprotect GPT registers */
    R_GPT10->GTWP = 0xA50B;               /* Write GTWP unlock key */
    R_GPT10->GTWP_b.WP = 0;               /* Clear write protect */

    /* Configure GTCR:
     *   MD  = 0x4 (PWM mode 1 - sawtooth single-shot PWM)
     *   TPCS= 0   (count source = PCLKD, no prescaler)
     *   CKEG= 0   (count on rising edge)
     *   CST = 0   (stop counter) */
    R_GPT10->GTCR = 0;
    R_GPT10->GTCR_b.MD   = 4;
    R_GPT10->GTCR_b.TPCS = 0;
    R_GPT10->GTCR_b.CKEG = 0;

    /* Set GTPR for period: output period = 2*(GTPR+1) PCLKD cycles */
    R_GPT10->GTPR = GPT10_XCLK_PERIOD;

    /* Configure GTIOR: GTIOCA toggle output on compare match A (GTIOA = 0x01) */
    R_GPT10->GTIOR = 0;
    R_GPT10->GTIOR_b.GTIOA = 0x01;

    /* Enable GTIOCA output */
    R_GPT10->GTIOR_b.OAE = 1;

    /* P109 is already configured as GPT10 GTIOC10A by g_bsp_pin_cfg */

    /* Start counter */
    R_GPT10->GTCR_b.CST = 1;
}

/*******************************************************************************************************************//**
 * @brief  OV7725 power-up sequence.
 *         P709 (PWDN) and P710 (RST) are already configured as GPIO outputs by g_bsp_pin_cfg.
 *         Sequence: PWDN=0 (enable), RST=0 (hold) → delay 1ms → RST=1 (release) → delay 20ms.
 **********************************************************************************************************************/
static void camera_power_on(void)
{
    /* PWDN = 0 (enable sensor), RST = 0 (hold reset) */
    R_PFS->PORT[7].PIN[9].PmnPFS_b.PODR  = 0;   /* CAM_PWDN: PWDN=0 = sensor active  */
    R_PFS->PORT[7].PIN[10].PmnPFS_b.PODR = 0;   /* CAM_RST:  RST=0  = hold reset     */

    vTaskDelay(pdMS_TO_TICKS(1));

    /* Release reset */
    R_PFS->PORT[7].PIN[10].PmnPFS_b.PODR = 1;   /* CAM_RST: high = release reset */

    /* Wait ~20ms for sensor internal stabilization */
    vTaskDelay(pdMS_TO_TICKS(20));
}

/*******************************************************************************************************************//**
 * @brief  Initialize RIIC0 for I2C master communication (~100kHz)
 **********************************************************************************************************************/
static void camera_i2c_init(void)
{
    /* Enable IIC0 module clock */
    R_BSP_MODULE_START(FSP_IP_IIC, 0U);

    /* Reset and enable IIC0 */
    R_IIC0->ICCR1_b.IICRST = 1;           /* Internal reset */
    R_IIC0->ICCR1_b.ICE    = 0;           /* Disable */

    /* Configure ICFER: enable functions */
    R_IIC0->ICFER_b.TMOE  = 1;            /* Timeout enabled */
    R_IIC0->ICFER_b.MALE  = 1;            /* Master arbitration lost */
    R_IIC0->ICFER_b.NACKE = 0;            /* NACK reception don't suspend */
    R_IIC0->ICFER_b.NFE   = 1;            /* Digital noise filter enabled */
    R_IIC0->ICFER_b.SCLE  = 1;            /* SCL sync enabled */
    R_IIC0->ICFER_b.FMPE  = 0;            /* Fast-mode Plus disabled */

    /* Configure ICMR1: bit counter and clock */
    R_IIC0->ICMR1 = 0;
    R_IIC0->ICMR1_b.CKS = RIIC0_CKS;     /* fIIC = PCLKB / 2^CKS */

    /* Set bit rate: ICBRL and ICBRH */
    R_IIC0->ICBRL_b.BRL = RIIC0_BRL;
    R_IIC0->ICBRH_b.BRH = RIIC0_BRH;

    /* Configure ICMR3 */
    R_IIC0->ICMR3_b.NF   = 0;             /* Noise filter 1 stage */
    R_IIC0->ICMR3_b.SMBS = 0;             /* I2C mode (not SMBus) */
    R_IIC0->ICMR3_b.WAIT = 0;

    /* Clear status flags */
    R_IIC0->ICSR2 = 0;

    /* Enable IIC0 */
    R_IIC0->ICCR1_b.IICRST = 0;
    R_IIC0->ICCR1_b.ICE    = 1;

    /* Wait for module to stabilize */
    volatile uint32_t delay;
    for (delay = 0; delay < 100; delay++);
}

/*******************************************************************************************************************//**
 * @brief  Write a single byte to OV7725 register via I2C
 * @param  reg   Register address
 * @param  data  Data to write
 * @return true if successful
 **********************************************************************************************************************/
static bool camera_i2c_write(uint8_t reg, uint8_t data)
{
    uint32_t timeout;

    /* Wait for bus free */
    timeout = 10000;
    while (R_IIC0->ICCR2_b.BBSY)
    {
        if (--timeout == 0) return false;
    }

    /* Set master transmit mode */
    R_IIC0->ICCR2_b.MST = 1;
    R_IIC0->ICCR2_b.TRS = 1;

    /* Issue start condition */
    R_IIC0->ICCR2_b.ST = 1;

    /* Wait for TDRE (transmit data empty) */
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TDRE)
    {
        if (--timeout == 0) return false;
    }

    /* Send slave address + write */
    R_IIC0->ICDRT = (OV7725_ADDR << 1) | 0U;

    /* Wait for TEND */
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TEND)
    {
        if (R_IIC0->ICSR2_b.NACKF) { R_IIC0->ICSR2_b.NACKF = 0; return false; }
        if (--timeout == 0) return false;
    }
    R_IIC0->ICSR2_b.TEND = 0;

    /* Send register address */
    R_IIC0->ICDRT = reg;
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TEND)
    {
        if (R_IIC0->ICSR2_b.NACKF) { R_IIC0->ICSR2_b.NACKF = 0; return false; }
        if (--timeout == 0) return false;
    }
    R_IIC0->ICSR2_b.TEND = 0;

    /* Send data */
    R_IIC0->ICDRT = data;
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TEND)
    {
        if (R_IIC0->ICSR2_b.NACKF) { R_IIC0->ICSR2_b.NACKF = 0; return false; }
        if (--timeout == 0) return false;
    }
    R_IIC0->ICSR2_b.TEND = 0;

    /* Issue stop condition */
    R_IIC0->ICCR2_b.SP = 1;
    timeout = 10000;
    while (R_IIC0->ICCR2_b.SP)
    {
        if (--timeout == 0) return false;
    }

    /* Release master mode */
    R_IIC0->ICCR2_b.MST = 0;
    R_IIC0->ICCR2_b.TRS = 0;

    return true;
}

/*******************************************************************************************************************//**
 * @brief  Read a single byte from OV7725 register via I2C (SCCB protocol)
 * @param  reg   Register address
 * @param  data  Pointer to store read data
 * @return true if successful
 **********************************************************************************************************************/
static bool camera_i2c_read(uint8_t reg, uint8_t * data)
{
    uint32_t timeout;

    if (NULL == data) return false;

    /* Wait for bus free */
    timeout = 10000;
    while (R_IIC0->ICCR2_b.BBSY)
    {
        if (--timeout == 0) return false;
    }

    /* -- Phase 1: Write register address -- */
    R_IIC0->ICCR2_b.MST = 1;
    R_IIC0->ICCR2_b.TRS = 1;

    R_IIC0->ICCR2_b.ST = 1;
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TDRE)
    {
        if (--timeout == 0) return false;
    }

    /* Send slave address + write */
    R_IIC0->ICDRT = (OV7725_ADDR << 1) | 0U;
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TEND)
    {
        if (R_IIC0->ICSR2_b.NACKF) { R_IIC0->ICSR2_b.NACKF = 0; return false; }
        if (--timeout == 0) return false;
    }
    R_IIC0->ICSR2_b.TEND = 0;

    /* Send register address */
    R_IIC0->ICDRT = reg;
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TEND)
    {
        if (R_IIC0->ICSR2_b.NACKF) { R_IIC0->ICSR2_b.NACKF = 0; return false; }
        if (--timeout == 0) return false;
    }
    R_IIC0->ICSR2_b.TEND = 0;

    /* -- Phase 2: Restart and read data -- */
    /* Issue restart condition */
    R_IIC0->ICCR2_b.RS = 1;

    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TDRE)
    {
        if (--timeout == 0) return false;
    }

    /* Send slave address + read */
    R_IIC0->ICDRT = (OV7725_ADDR << 1) | 1U;
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TEND)
    {
        if (R_IIC0->ICSR2_b.NACKF) { R_IIC0->ICSR2_b.NACKF = 0; return false; }
        if (--timeout == 0) return false;
    }
    R_IIC0->ICSR2_b.TEND = 0;

    /* Switch to receive mode */
    R_IIC0->ICCR2_b.TRS = 0;

    /* Wait for data to arrive */
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.RDRF)
    {
        if (--timeout == 0) return false;
    }

    /* Read received data (reading ICDRR automatically clears RDRF) */
    *data = R_IIC0->ICDRR;

    /* Issue stop condition */
    R_IIC0->ICCR2_b.SP = 1;
    timeout = 10000;
    while (R_IIC0->ICCR2_b.SP)
    {
        if (--timeout == 0) return false;
    }

    /* Release master mode */
    R_IIC0->ICCR2_b.MST = 0;
    R_IIC0->ICCR2_b.TRS = 0;

    return true;
}

/*******************************************************************************************************************//**
 * @brief  Initialize OV7725 camera sensor via SCCB
 * @return true if successful
 **********************************************************************************************************************/
static bool camera_ov7725_init(void)
{
    const ov7725_reg_t * p_entry = g_ov7725_config;

    /* Apply register configuration */
    while ((p_entry->reg != 0xFF) || (p_entry->val != 0xFF))
    {
        if (false == camera_i2c_write(p_entry->reg, p_entry->val))
        {
            return false;
        }
        p_entry++;

        /* After reset, wait extra time */
        if ((p_entry - 1)->reg == 0x12 && (p_entry - 1)->val == 0x80)
        {
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }

    /* Verify sensor ID (optional) */
    uint8_t pid = 0, ver = 0;
    if (camera_i2c_read(0x0A, &pid) && camera_i2c_read(0x0B, &ver))
    {
        /* OV7725 PID=0x77, VER=0x21 */
        if (pid != 0x77)
        {
            return false;   /* Sensor not detected */
        }
    }

    return true;
}

/*******************************************************************************************************************//**
 * @brief  Open CEU and start capture
 **********************************************************************************************************************/
static void camera_ceu_start(void)
{
    fsp_err_t err;

    /* Open CEU driver */
    err = R_CEU_Open(g_ceu0.p_ctrl, g_ceu0.p_cfg);
    if (FSP_SUCCESS != err)
    {
        while (1)
        {
            vTaskDelay(1);
        }
    }

    /* Start capture into the frame buffer */
    err = R_CEU_CaptureStart(g_ceu0.p_ctrl, g_camera_frame_buffer);
    if (FSP_SUCCESS != err)
    {
        while (1)
        {
            vTaskDelay(1);
        }
    }
}

/*******************************************************************************************************************//**
 * @brief  CEU capture callback (called from ISR context)
 **********************************************************************************************************************/
void g_ceu0_user_callback(capture_callback_args_t * p_args)
{
    if (NULL == p_args) return;

    switch (p_args->event)
    {
        case CEU_EVENT_FRAME_END:
            /* A frame has been captured */
            g_frame_ready = true;
            break;

        case CEU_EVENT_CRAM_OVERFLOW:
            /* CRAM overflow error */
            break;

        case CEU_EVENT_VD_ERROR:
        case CEU_EVENT_HD_MISSING:
        case CEU_EVENT_VD_MISSING:
            /* Sync error */
            break;

        default:
            break;
    }
}

/*******************************************************************************************************************//**
 * @brief  Camera thread entry function
 **********************************************************************************************************************/
void Camera_thread_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* 1. Initialize XCLK (GPT10 PWM on P109) */
    camera_xclk_init();

    /* 2. Power-up sequence: PWDN enable → RST release (P709/P710 pre-configured by g_bsp_pin_cfg) */
    camera_power_on();

    /* 3. Initialize I2C (RIIC0, P409/P410) */
    camera_i2c_init();

    /* 4. Initialize OV7725 via SCCB */
    if (false == camera_ov7725_init())
    {
        /* Camera init failed — trap here */
        while (1)
        {
            vTaskDelay(1);
        }
    }

    /* 5. Start CEU capture */
    camera_ceu_start();

    /* 6. Main loop: process frames */
    while (1)
    {
        if (g_frame_ready)
        {
            g_frame_ready = false;

            /* TODO: Process g_camera_frame_buffer (640x480 YCbCr422) */
            /* e.g., display on LCD, send over network, etc. */

            /* Restart capture for next frame */
            R_CEU_CaptureStart(g_ceu0.p_ctrl, g_camera_frame_buffer);
        }

        vTaskDelay(1);
    }
}
