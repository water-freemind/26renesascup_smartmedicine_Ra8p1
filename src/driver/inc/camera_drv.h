#ifndef CAMERA_DRV_H
#define CAMERA_DRV_H

#include "hal_data.h"
#include <stdbool.h>

/**********************************************************************************************************************
 * Macros
 **********************************************************************************************************************/

/* OV7725 SCCB（I2C）从机地址（7位） */
#define OV7725_ADDR                 (0x21U)

/* Pins */
#define PIN_PWDN                    BSP_IO_PORT_07_PIN_09   /* P709：摄像头电源 */
#define PIN_RST                     BSP_IO_PORT_07_PIN_10   /* P710：摄像头复位 */

/**********************************************************************************************************************
 * Function Declarations
 **********************************************************************************************************************/

void     camera_xclk_init(void);
void     camera_xclk_slow(void);
void     camera_power_on(void);
void     camera_power_off(void);
void     camera_diag_sample_sync_pins(uint32_t sample_ms);
bool     camera_i2c_init(void);
bool     camera_i2c_bus_is_idle(void);
bool     camera_i2c_recover(void);
bool     camera_i2c_write(uint8_t reg, uint8_t data);
bool     camera_i2c_read(uint8_t reg, uint8_t *data);
bool     camera_i2c_write_at(uint8_t address, uint8_t reg, uint8_t data);
bool     camera_i2c_read_at(uint8_t address, uint8_t reg, uint8_t *data);
bool     camera_i2c_read_block_at(uint8_t address, uint8_t reg, uint8_t *data, uint32_t length);
bool     camera_i2c_read_block_stop_at(uint8_t address, uint8_t reg, uint8_t *data, uint32_t length);
bool     camera_i2c_probe_at(uint8_t address);
fsp_err_t camera_ceu_start(uint8_t *p_frame_buffer);

#endif /* CAMERA_DRV_H */
