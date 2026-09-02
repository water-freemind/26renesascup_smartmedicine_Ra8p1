#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 * USB PCDC 帧传输模块（OV7725 → USBHS → PC）
 *
 * 依赖：RASC 添加 USB PCDC (r_usb_pcdc + r_usb_basic, USB_IP1 / Hi Speed) 并生成代码后自动激活。
 *       RASC 未完成时本模块为空实现，不影响其他代码编译。
 **********************************************************************************************************************/

/* 帧协议：RGB565 数据 + 8 字节帧头 */
#define USB_CDC_FRAME_HEADER_SIZE   (8U)
#define USB_CDC_MAGIC_0             (0x55U)
#define USB_CDC_MAGIC_1             (0xAAU)

/* 分包大小（HS bulk 端点建议 512B） */
#define USB_CDC_CHUNK_SIZE          (512U)

/**********************************************************************************************************************
 * 函数声明
 **********************************************************************************************************************/

/* 初始化 USB PCDC（R_USB_Open）。成功返回 true。 */
bool usb_cdc_init(void);

/* 发送一帧 RGB565 图像（w*h 像素）。阻塞直到整帧发完或失败。返回 true 表示已排队。 */
bool usb_cdc_send_frame(const uint16_t * p_rgb565, uint16_t w, uint16_t h);

/* 异步提交 RGB565 预览帧。调用方在 usb_cdc_preview_busy() 为真期间
 * 必须保持 p_rgb565 内容不变；usb_cdc_preview_poll() 应在任务循环调用。 */
bool usb_cdc_preview_submit(const uint16_t * p_rgb565, uint16_t w, uint16_t h);
bool usb_cdc_preview_busy(void);
void usb_cdc_preview_poll(void);

/* 发送一帧原始灰度字节流（w*h 字节，无帧头）。阻塞直到发完或失败。 */
bool usb_cdc_send_raw(const uint8_t * p_gray, uint32_t bytes);

/* 是否已枚举成功（PC 已连接且识别为虚拟串口） */
bool usb_cdc_is_connected(void);

/* SerialState 通知轮询（任务上下文，主循环每轮调用一次）：
 * Windows usbser 打开端口后等待 DSR/DCD 上报，否则 Open() 永久阻塞。 */
void usb_cdc_poll_serial_notify(void);

#endif /* USB_CDC_H */
