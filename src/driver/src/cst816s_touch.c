#include "cst816s_touch.h"

#include "camera_drv.h"

volatile uint32_t g_cst816s_probe_ok;
volatile uint32_t g_cst816s_probe_fail;
volatile uint32_t g_cst816s_read_ok;
volatile uint32_t g_cst816s_read_fail;
volatile uint32_t g_cst816s_chip_id;
volatile uint32_t g_cst816s_fw_version;
volatile uint32_t g_cst816s_touch_x;
volatile uint32_t g_cst816s_touch_y;
volatile uint32_t g_cst816s_touch_pressed;
volatile uint32_t g_cst816s_touch_fingers;
volatile uint32_t g_cst816s_last_reg;
volatile uint32_t g_cst816s_error_reg;
volatile uint32_t g_cst816s_detected_address;
volatile uint32_t g_cst816s_ack_address;

static bool s_cst816s_ready;
static uint8_t s_cst816s_address = CST816S_I2C_ADDRESS;

static bool cst816s_read_reg(uint8_t reg, uint8_t * value)
{
    /* The reference CST816S driver uses a combined register write/read with
     * repeated START.  Do not replace this with two STOP-separated transfers. */
    /* CST816S register reads require a write of the register index followed
     * by a repeated START and read phase.  The STOP-separated helper is for
     * OV7725/SCCB and is not used for this controller. */
    bool const ok = camera_i2c_read_block_at(s_cst816s_address, reg, value, 1U);
    g_cst816s_last_reg = reg;
    if (!ok)
    {
        g_cst816s_error_reg = reg;
    }
    return ok;
}

bool cst816s_touch_init(void)
{
    uint8_t chip_id = 0U;
    uint8_t fw_version = 0U;
    static const uint8_t candidate_addresses[] = { CST816S_I2C_ADDRESS, 0x15U, 0x14U, 0x5DU };

    s_cst816s_ready = false;
    s_cst816s_address = CST816S_I2C_ADDRESS;
    g_cst816s_detected_address = 0U;
    g_cst816s_ack_address = 0U;
    if (!camera_i2c_init())
    {
        g_cst816s_probe_fail++;
        return false;
    }
    for (uint32_t i = 0U; i < (sizeof(candidate_addresses) / sizeof(candidate_addresses[0])); i++)
    {
        uint8_t address = candidate_addresses[i];
        s_cst816s_address = address;

        /* The screen reference firmware identifies this controller as
         * CST816S.  Its ID/FW registers are 0xA7/0xA9; the ID value varies
         * by controller revision, so ACK plus successful reads is the probe
         * criterion rather than one hard-coded ID byte. */
        if (cst816s_read_reg(CST816S_CHIP_ID_REGISTER, &chip_id) &&
            cst816s_read_reg(CST816S_FW_VERSION_REGISTER, &fw_version))
        {
            g_cst816s_ack_address = address;
            g_cst816s_detected_address = address;
            break;
        }
    }

    if (0U == g_cst816s_detected_address)
    {
        g_cst816s_probe_fail++;
        return false;
    }

    g_cst816s_chip_id = chip_id;
    g_cst816s_fw_version = fw_version;
    g_cst816s_probe_ok++;
    s_cst816s_ready = true;
    return true;
}

bool cst816s_touch_read(cst816s_touch_state_t * state)
{
    uint8_t data[6];

    if ((NULL == state) || !s_cst816s_ready)
    {
        return false;
    }

    /* CST816S data is 0x01 gesture, 0x02 finger count, then X/Y at 0x03..0x06. */
    g_cst816s_last_reg = CST816S_DATA_REGISTER;
    if (!camera_i2c_read_block_at(s_cst816s_address, CST816S_DATA_REGISTER, data, 6U))
    {
        g_cst816s_error_reg = CST816S_DATA_REGISTER;
        g_cst816s_read_fail++;
        /* Bus-safety net: an interrupted shared IIC0 transaction (e.g. the
         * preview copy previously ran with interrupts masked) can leave the
         * controller busy with SDA held low.  Rebuilding the IIC instance
         * plus the 9-pulse GPIO recovery clears the MCU-side state; if an
         * external device still holds SDA low the recovery counters stay at
         * zero success and the read keeps failing (hardware latch). */
        if ((g_cst816s_read_fail & 0x0FU) == 0U)
        {
            (void) camera_i2c_recover();
        }
        return false;
    }

    state->gesture = data[0];
    state->finger_count = (uint8_t) (data[1] & 0x0FU);
    state->x = (uint16_t) (((uint16_t) (data[2] & 0x0FU) << 8U) | data[3]);
    state->y = (uint16_t) (((uint16_t) (data[4] & 0x0FU) << 8U) | data[5]);
    state->pressed = (state->finger_count != 0U);
    state->present = true;
    g_cst816s_touch_x = state->x;
    g_cst816s_touch_y = state->y;
    g_cst816s_touch_pressed = state->pressed ? 1U : 0U;
    g_cst816s_touch_fingers = state->finger_count;
    g_cst816s_read_ok++;
    return true;
}
