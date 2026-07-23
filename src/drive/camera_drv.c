#include "camera_drv.h"
#include "Camera_thread.h"

/***********************************************************************************************************************
 * XCLK (GPT10 PWM ~25MHz)
 **********************************************************************************************************************/
void camera_xclk_init(void)
{
    R_BSP_MODULE_START(FSP_IP_GPT, 10U);

    R_GPT10->GTWP = 0xA50B;
    R_GPT10->GTWP_b.WP = 0;

    R_GPT10->GTCR = 0;
    R_GPT10->GTCR_b.MD   = 4;       /* PWM mode 1 - sawtooth single-shot PWM */
    R_GPT10->GTCR_b.TPCS = 0;       /* count source = PCLKD */
    R_GPT10->GTCR_b.CKEG = 0;

    R_GPT10->GTPR = GPT10_XCLK_PERIOD;

    R_GPT10->GTIOR = 0;
    R_GPT10->GTIOR_b.GTIOA = 0x01;  /* GTIOCA toggle on compare match A */
    R_GPT10->GTIOR_b.OAE = 1;        /* Enable GTIOCA output */

    R_GPT10->GTCR_b.CST = 1;         /* Start counter */
}

/***********************************************************************************************************************
 * Power-on sequence (PWDN/RST GPIO)
 **********************************************************************************************************************/
void camera_power_on(void)
{
    /* PWDN = 0 (enable), RST = 0 (hold reset) */
    R_PFS->PORT[7].PIN[9].PmnPFS_b.PODR  = 0;
    R_PFS->PORT[7].PIN[10].PmnPFS_b.PODR = 0;

    vTaskDelay(pdMS_TO_TICKS(1));

    /* Release reset */
    R_PFS->PORT[7].PIN[10].PmnPFS_b.PODR = 1;

    /* Wait for sensor stabilization */
    vTaskDelay(pdMS_TO_TICKS(20));
}

/***********************************************************************************************************************
 * I2C (RIIC0) - ~100kHz master
 **********************************************************************************************************************/
void camera_i2c_init(void)
{
    R_BSP_MODULE_START(FSP_IP_IIC, 0U);

    /* Reset and enable IIC0 */
    R_IIC0->ICCR1_b.IICRST = 1;
    R_IIC0->ICCR1_b.ICE    = 0;

    /* Configure ICFER */
    R_IIC0->ICFER_b.TMOE  = 1;
    R_IIC0->ICFER_b.MALE  = 1;
    R_IIC0->ICFER_b.NACKE = 0;
    R_IIC0->ICFER_b.NFE   = 1;
    R_IIC0->ICFER_b.SCLE  = 1;
    R_IIC0->ICFER_b.FMPE  = 0;

    /* Configure ICMR1 */
    R_IIC0->ICMR1 = 0;
    R_IIC0->ICMR1_b.CKS = RIIC0_CKS;

    /* Set bit rate */
    R_IIC0->ICBRL_b.BRL = RIIC0_BRL;
    R_IIC0->ICBRH_b.BRH = RIIC0_BRH;

    /* Configure ICMR3 */
    R_IIC0->ICMR3_b.NF   = 0;
    R_IIC0->ICMR3_b.SMBS = 0;
    R_IIC0->ICMR3_b.WAIT = 0;

    R_IIC0->ICSR2 = 0;

    /* Enable IIC0 */
    R_IIC0->ICCR1_b.IICRST = 0;
    R_IIC0->ICCR1_b.ICE    = 1;

    volatile uint32_t delay;
    for (delay = 0; delay < 100; delay++);
}

static bool i2c_wait_bb(void)
{
    uint32_t timeout = 10000;
    while (R_IIC0->ICCR2_b.BBSY) { if (--timeout == 0) return false; }
    return true;
}

static bool i2c_send_byte(uint8_t byte)
{
    uint32_t timeout = 10000;
    while (!R_IIC0->ICSR2_b.TDRE) { if (--timeout == 0) return false; }
    R_IIC0->ICDRT = byte;
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TEND)
    {
        if (R_IIC0->ICSR2_b.NACKF) { R_IIC0->ICSR2_b.NACKF = 0; return false; }
        if (--timeout == 0) return false;
    }
    R_IIC0->ICSR2_b.TEND = 0;
    return true;
}

bool camera_i2c_write(uint8_t reg, uint8_t data)
{
    if (!i2c_wait_bb()) return false;

    /* Master transmit mode + start */
    R_IIC0->ICCR2_b.MST = 1;
    R_IIC0->ICCR2_b.TRS = 1;
    R_IIC0->ICCR2_b.ST  = 1;

    if (!i2c_send_byte((OV7725_ADDR << 1) | 0U)) return false;  /* addr + write */
    if (!i2c_send_byte(reg))  return false;                       /* register */
    if (!i2c_send_byte(data)) return false;                       /* data */

    /* Stop condition */
    R_IIC0->ICCR2_b.SP = 1;
    uint32_t timeout = 10000;
    while (R_IIC0->ICCR2_b.SP) { if (--timeout == 0) return false; }

    R_IIC0->ICCR2_b.MST = 0;
    R_IIC0->ICCR2_b.TRS = 0;
    return true;
}

bool camera_i2c_read(uint8_t reg, uint8_t *data)
{
    if (NULL == data) return false;

    /* Phase 1: Write register address */
    if (!i2c_wait_bb()) return false;

    R_IIC0->ICCR2_b.MST = 1;
    R_IIC0->ICCR2_b.TRS = 1;
    R_IIC0->ICCR2_b.ST  = 1;

    if (!i2c_send_byte((OV7725_ADDR << 1) | 0U)) return false;
    if (!i2c_send_byte(reg)) return false;

    /* Phase 2: Restart and read */
    R_IIC0->ICCR2_b.RS = 1;
    uint32_t timeout = 10000;
    while (!R_IIC0->ICSR2_b.TDRE) { if (--timeout == 0) return false; }

    R_IIC0->ICDRT = (OV7725_ADDR << 1) | 1U;
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.TEND)
    {
        if (R_IIC0->ICSR2_b.NACKF) { R_IIC0->ICSR2_b.NACKF = 0; return false; }
        if (--timeout == 0) return false;
    }
    R_IIC0->ICSR2_b.TEND = 0;

    /* Receive mode */
    R_IIC0->ICCR2_b.TRS = 0;
    timeout = 10000;
    while (!R_IIC0->ICSR2_b.RDRF) { if (--timeout == 0) return false; }

    *data = R_IIC0->ICDRR;

    /* Stop */
    R_IIC0->ICCR2_b.SP = 1;
    timeout = 10000;
    while (R_IIC0->ICCR2_b.SP) { if (--timeout == 0) return false; }

    R_IIC0->ICCR2_b.MST = 0;
    R_IIC0->ICCR2_b.TRS = 0;
    return true;
}

/***********************************************************************************************************************
 * CEU capture start
 **********************************************************************************************************************/
fsp_err_t camera_ceu_start(uint8_t *p_frame_buffer)
{
    fsp_err_t err;

    err = R_CEU_Open(g_ceu0.p_ctrl, g_ceu0.p_cfg);
    if (FSP_SUCCESS != err) return err;

    err = R_CEU_CaptureStart(g_ceu0.p_ctrl, p_frame_buffer);
    return err;
}