#ifndef CAMERA_DRV_H
#define CAMERA_DRV_H

#include "hal_data.h"
#include <stdbool.h>

/**********************************************************************************************************************
 * Macros
 **********************************************************************************************************************/

/* OV7725 SCCB (I2C) slave address (7-bit) */
#define OV7725_ADDR                 (0x21U)

/* RIIC0 I2C timing: target ~100kHz with PCLKB=62.5MHz, CKS=4 (fIIC=3.90625MHz) */
#define RIIC0_CKS                   (4U)
#define RIIC0_BRL                   (18U)
#define RIIC0_BRH                   (19U)

/* GPT10 XCLK: target ~25MHz with PCLKD=250MHz */
#define GPT10_XCLK_PERIOD           (4U)

/* Pins */
#define PIN_PWDN                    BSP_IO_PORT_07_PIN_09   /* P709: CAM_PWDN */
#define PIN_RST                     BSP_IO_PORT_07_PIN_10   /* P710: CAM_RST  */

/**********************************************************************************************************************
 * Function Declarations
 **********************************************************************************************************************/

void     camera_xclk_init(void);
void     camera_power_on(void);
void     camera_i2c_init(void);
bool     camera_i2c_write(uint8_t reg, uint8_t data);
bool     camera_i2c_read(uint8_t reg, uint8_t *data);
fsp_err_t camera_ceu_start(uint8_t *p_frame_buffer);

#endif /* CAMERA_DRV_H */