#include "camera_app.h"
#include "camera_drv.h"
#include "Camera_thread.h"

/***********************************************************************************************************************
 * Frame buffer & ready flag
 **********************************************************************************************************************/
uint8_t g_camera_frame_buffer[CAMERA_IMAGE_SIZE] __attribute__((aligned(16)));
volatile bool g_frame_ready = false;

/***********************************************************************************************************************
 * OV7725 register configuration table
 **********************************************************************************************************************/
static const ov7725_reg_t g_ov7725_config[] =
{
    /* Register       Value      Description */
    { 0x12,           0x80 },              /* COM7: Reset all registers */
    { 0x0D,           0x41 },              /* COM4: PLL enable, /1 divider */
    { 0x11,           0x01 },              /* CLKRC: Internal clock = XCLK / 2 */
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

/***********************************************************************************************************************
 * OV7725 initialization via SCCB
 **********************************************************************************************************************/
bool camera_ov7725_init(void)
{
    const ov7725_reg_t *p_entry = g_ov7725_config;

    while ((p_entry->reg != 0xFF) || (p_entry->val != 0xFF))
    {
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

    /* Verify sensor ID */
    uint8_t pid = 0, ver = 0;
    if (camera_i2c_read(0x0A, &pid) && camera_i2c_read(0x0B, &ver))
    {
        if (pid != 0x77)
        {
            return false;
        }
    }

    return true;
}

/***********************************************************************************************************************
 * CEU capture callback (called from ISR context)
 **********************************************************************************************************************/
void g_ceu0_user_callback(capture_callback_args_t *p_args)
{
    if (NULL == p_args) return;

    switch (p_args->event)
    {
        case CEU_EVENT_FRAME_END:
            g_frame_ready = true;
            break;

        case CEU_EVENT_CRAM_OVERFLOW:
        case CEU_EVENT_VD_ERROR:
        case CEU_EVENT_HD_MISSING:
        case CEU_EVENT_VD_MISSING:
        default:
            break;
    }
}

/***********************************************************************************************************************
 * Full camera initialization sequence
 **********************************************************************************************************************/
void camera_app_init(void)
{
    /* 1. XCLK */
    camera_xclk_init();

    /* 2. Power-on */
    camera_power_on();

    /* 3. I2C */
    camera_i2c_init();

    /* 4. OV7725 init */
    if (false == camera_ov7725_init())
    {
        while (1) { vTaskDelay(1); }
    }

    /* 5. CEU capture */
    fsp_err_t err = camera_ceu_start(g_camera_frame_buffer);
    if (FSP_SUCCESS != err)
    {
        while (1) { vTaskDelay(1); }
    }
}