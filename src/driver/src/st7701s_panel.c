#include "hal_data.h"
#include "st7701s_panel.h"

volatile uint32_t g_st7701s_init_error;
volatile uint32_t g_st7701s_init_step;
volatile uint32_t g_st7701s_last_command_error;
volatile uint32_t g_st7701s_sequence0_count;
volatile uint32_t g_st7701s_sequence1_count;
volatile uint32_t g_st7701s_last_mipi_event;
volatile uint32_t g_st7701s_last_mipi_status;

/* Each item includes the DCS command byte followed by its parameters.
 * The sequence is taken from "初始化代码 ST7701S+28-480640_INIT.txt"
 * supplied with the WLK2802MIPI-15P V2 panel. */
static const uint8_t s_cmd_ff_13[] = {0xFF, 0x77, 0x01, 0x00, 0x00, 0x13};
static const uint8_t s_cmd_soft_reset[] = {0x01};
static const uint8_t s_cmd_ef_08[] = {0xEF, 0x08};
static const uint8_t s_cmd_ff_10[] = {0xFF, 0x77, 0x01, 0x00, 0x00, 0x10};
static const uint8_t s_cmd_c0[]    = {0xC0, 0x4F, 0x00};
static const uint8_t s_cmd_c1[]    = {0xC1, 0x10, 0x0C};
static const uint8_t s_cmd_c2[]    = {0xC2, 0x07, 0x14};
static const uint8_t s_cmd_cc[]    = {0xCC, 0x10};
static const uint8_t s_cmd_b0[]    = {0xB0, 0x0A, 0x18, 0x1E, 0x12, 0x16, 0x0C, 0x0E, 0x0D,
                                      0x0C, 0x29, 0x06, 0x14, 0x13, 0x29, 0x33, 0x1C};
static const uint8_t s_cmd_b1[]    = {0xB1, 0x0A, 0x19, 0x21, 0x0A, 0x0C, 0x00, 0x0C, 0x03, 0x03,
                                      0x23, 0x01, 0x0E, 0x0C, 0x27, 0x2B, 0x1C};
static const uint8_t s_cmd_ff_11[] = {0xFF, 0x77, 0x01, 0x00, 0x00, 0x11};
static const uint8_t s_cmd_b0_1[]  = {0xB0, 0x5D};
static const uint8_t s_cmd_b1_1[]  = {0xB1, 0x61};
static const uint8_t s_cmd_b2[]    = {0xB2, 0x84};
static const uint8_t s_cmd_b3[]    = {0xB3, 0x80};
static const uint8_t s_cmd_b5[]    = {0xB5, 0x4D};
static const uint8_t s_cmd_b7[]    = {0xB7, 0x85};
static const uint8_t s_cmd_b8[]    = {0xB8, 0x20};
static const uint8_t s_cmd_c1_1[]  = {0xC1, 0x78};
static const uint8_t s_cmd_c2_1[]  = {0xC2, 0x78};
static const uint8_t s_cmd_d0[]    = {0xD0, 0x88};
static const uint8_t s_cmd_e0[]    = {0xE0, 0x00, 0x00, 0x02};
static const uint8_t s_cmd_e1[]    = {0xE1, 0x06, 0xA0, 0x08, 0xA0, 0x05, 0xA0, 0x07, 0xA0, 0x00, 0x44, 0x44};
static const uint8_t s_cmd_e2[]    = {0xE2, 0x20, 0x20, 0x44, 0x44, 0x96, 0xA0, 0x00, 0x00, 0x96, 0xA0, 0x00, 0x00};
static const uint8_t s_cmd_e3[]    = {0xE3, 0x00, 0x00, 0x22, 0x22};
static const uint8_t s_cmd_e4[]    = {0xE4, 0x44, 0x44};
static const uint8_t s_cmd_e5[]    = {0xE5, 0x0D, 0x91, 0xA0, 0xA0, 0x0F, 0x93, 0xA0, 0xA0, 0x09, 0x8D, 0xA0, 0xA0, 0x0B, 0x8F, 0xA0, 0xA0};
static const uint8_t s_cmd_e6[]    = {0xE6, 0x00, 0x00, 0x22, 0x22};
static const uint8_t s_cmd_e7[]    = {0xE7, 0x44, 0x44};
static const uint8_t s_cmd_e8[]    = {0xE8, 0x0C, 0x90, 0xA0, 0xA0, 0x0E, 0x92, 0xA0, 0xA0, 0x08, 0x8C, 0xA0, 0xA0, 0x0A, 0x8E, 0xA0, 0xA0};
static const uint8_t s_cmd_e9[]    = {0xE9, 0x36, 0x00};
static const uint8_t s_cmd_eb[]    = {0xEB, 0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40};
static const uint8_t s_cmd_ed[]    = {0xED, 0xFF, 0x45, 0x67, 0xFA, 0x01, 0x2B, 0xCF, 0xFF,
                                      0xFF, 0xFC, 0xB2, 0x10, 0xAF, 0x76, 0x54, 0xFF};
static const uint8_t s_cmd_ef[]    = {0xEF, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F};
static const uint8_t s_cmd_ff_00[] = {0xFF, 0x77, 0x01, 0x00, 0x00, 0x00};
static const uint8_t s_cmd_sleep_out[] = {0x11};
static const uint8_t s_cmd_display_on[] = {0x29};
static const uint8_t s_cmd_tear_on[] = {0x35, 0x00};

typedef struct st_st7701s_cmd
{
    uint8_t const * p_data;
    uint16_t        length;
    uint16_t        delay_ms;
} st7701s_cmd_t;

#define ST7701S_CMD(data, delay) { data, (uint16_t) sizeof(data), delay }
static const st7701s_cmd_t s_init_sequence[] =
{
    ST7701S_CMD(s_cmd_soft_reset, 120),
    ST7701S_CMD(s_cmd_ff_13, 0), ST7701S_CMD(s_cmd_ef_08, 0), ST7701S_CMD(s_cmd_ff_10, 0),
    ST7701S_CMD(s_cmd_c0, 0), ST7701S_CMD(s_cmd_c1, 0), ST7701S_CMD(s_cmd_c2, 0), ST7701S_CMD(s_cmd_cc, 0),
    ST7701S_CMD(s_cmd_b0, 0), ST7701S_CMD(s_cmd_b1, 0), ST7701S_CMD(s_cmd_ff_11, 0),
    ST7701S_CMD(s_cmd_b0_1, 0), ST7701S_CMD(s_cmd_b1_1, 0), ST7701S_CMD(s_cmd_b2, 0),
    ST7701S_CMD(s_cmd_b3, 0), ST7701S_CMD(s_cmd_b5, 0), ST7701S_CMD(s_cmd_b7, 0), ST7701S_CMD(s_cmd_b8, 0),
    ST7701S_CMD(s_cmd_c1_1, 0), ST7701S_CMD(s_cmd_c2_1, 0), ST7701S_CMD(s_cmd_d0, 0),
    ST7701S_CMD(s_cmd_e0, 0), ST7701S_CMD(s_cmd_e1, 0), ST7701S_CMD(s_cmd_e2, 0), ST7701S_CMD(s_cmd_e3, 0),
    ST7701S_CMD(s_cmd_e4, 0), ST7701S_CMD(s_cmd_e5, 0), ST7701S_CMD(s_cmd_e6, 0), ST7701S_CMD(s_cmd_e7, 0),
    ST7701S_CMD(s_cmd_e8, 0), ST7701S_CMD(s_cmd_e9, 0), ST7701S_CMD(s_cmd_eb, 0), ST7701S_CMD(s_cmd_ed, 0),
    ST7701S_CMD(s_cmd_ef, 0), ST7701S_CMD(s_cmd_ff_00, 0), ST7701S_CMD(s_cmd_sleep_out, 120),
    ST7701S_CMD(s_cmd_display_on, 20), ST7701S_CMD(s_cmd_tear_on, 0),
};

bool st7701s_panel_mipi_config_is_valid(void)
{
    mipi_dsi_cfg_t const * p_cfg = g_mipi_dsi0.p_cfg;

    return (NULL != p_cfg) &&
           (ST7701S_PANEL_WIDTH == p_cfg->horizontal_active_lines) &&
           (ST7701S_PANEL_HEIGHT == p_cfg->vertical_active_lines) &&
           (1U == p_cfg->num_lanes) &&
           (MIPI_DSI_VIDEO_DATA_24RGB_PIXEL_STREAM == p_cfg->data_type) &&
           (0U == p_cfg->virtual_channel_id) &&
           (true == p_cfg->continuous_clock);
}

void st7701s_panel_board_reset(void)
{
    /* Optional fallback hook.  DCS Software Reset is always sent below. */
}

void st7701s_panel_board_backlight(bool enable)
{
    FSP_PARAMETER_NOT_USED(enable);
    /* Optional fallback hook when the adapter does not auto-enable BLC. */
}

static fsp_err_t st7701s_send(uint8_t const * p_data, uint16_t length)
{
    if ((NULL == p_data) || (0U == length))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    /* The supplier's W280BF036I Linux driver uses DCS writes for the whole
     * ST7701S vendor table, including FF/EF/Bx pages.  Generic packet types
     * are accepted by the host but can be ignored by this panel. */
    mipi_cmd_id_t packet_type = MIPI_CMD_ID_DCS_LONG_WRITE;
    if (1U == length)
    {
        packet_type = MIPI_CMD_ID_DCS_SHORT_WRITE_0_PARAM;
    }
    else if (2U == length)
    {
        packet_type = MIPI_CMD_ID_DCS_SHORT_WRITE_1_PARAM;
    }

    mipi_dsi_cmd_t cmd =
    {
        .channel     = 0U,
        .cmd_id      = packet_type,
        .flags       = MIPI_DSI_CMD_FLAG_LOW_POWER,
        .tx_len      = length,
        .p_tx_buffer = p_data,
        .p_rx_buffer = NULL,
    };

    /* R_MIPI_DSI_Open() may leave its initial sequence channel busy when the
     * POST_OPEN callback is entered.  Do not mistake that transient state for
     * a panel failure: wait for the channel and retry the command. */
    fsp_err_t err = FSP_ERR_IN_USE;
    uint32_t retry = 2000U;
    while ((FSP_ERR_IN_USE == err) && (retry > 0U))
    {
        err = g_mipi_dsi0.p_api->command(g_mipi_dsi0.p_ctrl, &cmd);
        if (FSP_ERR_IN_USE == err)
        {
            R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
            retry--;
        }
    }
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* command() starts the DSI sequence asynchronously.  The link RUN bit
     * can still be zero for a short window immediately after command(), so a
     * pure RUN-bit poll can falsely report completion.  The application
     * callback counts the SQ0 completion interrupt; wait for that event first
     * and only then wait for both channels to become idle. */
    uint32_t sequence0_before = g_st7701s_sequence0_count;
    uint32_t timeout_ms = 2000U;
    while (g_st7701s_sequence0_count == sequence0_before)
    {
        if (0U == timeout_ms)
        {
            return FSP_ERR_TIMEOUT;
        }

        R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
        timeout_ms--;
    }

    timeout_ms = 2000U;
    while ((R_MIPI_DSI->LINKSR_b.SQ0RUN != 0U) ||
           (R_MIPI_DSI->LINKSR_b.SQ1RUN != 0U))
    {
        if (0U == timeout_ms)
        {
            return FSP_ERR_TIMEOUT;
        }

        R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
        timeout_ms--;
    }

    return FSP_SUCCESS;
}

fsp_err_t st7701s_panel_init(void)
{
    g_st7701s_init_error = (uint32_t) FSP_SUCCESS;
    g_st7701s_init_step = 0U;
    g_st7701s_last_command_error = (uint32_t) FSP_SUCCESS;
    g_st7701s_sequence0_count = 0U;
    g_st7701s_sequence1_count = 0U;
    g_st7701s_last_mipi_event = (uint32_t) MIPI_DSI_EVENT_POST_OPEN;
    g_st7701s_last_mipi_status = 0U;

    if (!st7701s_panel_mipi_config_is_valid())
    {
        g_st7701s_init_error = ST7701S_PANEL_ERROR_RASC_CONFIGURATION;
        return FSP_ERR_INVALID_ARGUMENT;
    }

    st7701s_panel_board_reset();

    for (uint32_t i = 0U; i < (sizeof(s_init_sequence) / sizeof(s_init_sequence[0])); i++)
    {
        g_st7701s_init_step = i + 1U;
        fsp_err_t err = st7701s_send(s_init_sequence[i].p_data, s_init_sequence[i].length);
        g_st7701s_last_command_error = (uint32_t) err;
        if (FSP_SUCCESS != err)
        {
            g_st7701s_init_error = (uint32_t) err;
            return err;
        }

        if (0U != s_init_sequence[i].delay_ms)
        {
            R_BSP_SoftwareDelay(s_init_sequence[i].delay_ms, BSP_DELAY_UNITS_MILLISECONDS);
        }
    }

    st7701s_panel_board_backlight(true);
    return FSP_SUCCESS;
}
