#ifndef CST816S_TOUCH_H
#define CST816S_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#define CST816S_I2C_ADDRESS (0x38U)
#define CST816S_CHIP_ID_REGISTER (0xA7U)
#define CST816S_FW_VERSION_REGISTER (0xA9U)
#define CST816S_DATA_REGISTER (0x01U)

typedef struct
{
    bool     present;
    bool     pressed;
    uint16_t x;
    uint16_t y;
    uint8_t  gesture;
    uint8_t  finger_count;
} cst816s_touch_state_t;

extern volatile uint32_t g_cst816s_probe_ok;
extern volatile uint32_t g_cst816s_probe_fail;
extern volatile uint32_t g_cst816s_read_ok;
extern volatile uint32_t g_cst816s_read_fail;
extern volatile uint32_t g_cst816s_chip_id;
extern volatile uint32_t g_cst816s_fw_version;
extern volatile uint32_t g_cst816s_touch_x;
extern volatile uint32_t g_cst816s_touch_y;
extern volatile uint32_t g_cst816s_touch_pressed;
extern volatile uint32_t g_cst816s_touch_fingers;
extern volatile uint32_t g_cst816s_last_reg;
extern volatile uint32_t g_cst816s_error_reg;
extern volatile uint32_t g_cst816s_detected_address;
extern volatile uint32_t g_cst816s_ack_address;

bool cst816s_touch_init(void);
bool cst816s_touch_read(cst816s_touch_state_t * state);

#endif /* CST816S_TOUCH_H */
