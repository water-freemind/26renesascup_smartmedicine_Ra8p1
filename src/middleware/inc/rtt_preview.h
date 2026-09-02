#ifndef RTT_PREVIEW_TRANSPORT_H
#define RTT_PREVIEW_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

/* RTT preview frames: 55 AA, big-endian width/height/sequence, grayscale payload. */
void rtt_preview_init(void);
bool rtt_preview_send_rgb565(const uint16_t * p_rgb565, uint16_t width, uint16_t height);

/* RTT Down 通道（PC→MCU）：TTF 烧录等接收用 */
void rtt_down_init(void);
uint32_t rtt_down_available(void);
uint32_t rtt_down_read(uint8_t * p_dest, uint32_t length);

#endif /* RTT_PREVIEW_TRANSPORT_H */
