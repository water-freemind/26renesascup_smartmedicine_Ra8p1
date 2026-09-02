#include "hal_data.h"
#include "rtt_preview.h"

#include <string.h>

#define RTT_PREVIEW_BUFFER_SIZE (196608U)
#define RTT_PREVIEW_FLAG_SKIP   (1U)
#define RTT_PREVIEW_FRAME_BYTES (320U * 240U * sizeof(uint16_t))
#define RTT_PREVIEW_PACKET_BYTES (8U + RTT_PREVIEW_FRAME_BYTES)

typedef struct
{
    const char       * p_name;
    char             * p_buffer;
    uint32_t           size;
    volatile uint32_t  write_offset;
    volatile uint32_t  read_offset;
    uint32_t           flags;
} rtt_buffer_t;

typedef struct
{
    char         id[16];
    int32_t      max_up_buffers;
    int32_t      max_down_buffers;
    rtt_buffer_t up[1];
    rtt_buffer_t down[1];
} rtt_control_block_t;

static char s_rtt_name[] = "camera";
static uint8_t s_rtt_buffer[RTT_PREVIEW_BUFFER_SIZE] __attribute__((aligned(32)));
static uint8_t s_rtt_packet[RTT_PREVIEW_PACKET_BYTES] __attribute__((aligned(32)));
/* Down channel（PC→MCU）：TTF 烧录等接收用。2KB 环形缓冲。 */
#define RTT_DOWN_BUFFER_SIZE (2048U)
static char s_rtt_down_name[] = "cmd";
static uint8_t s_rtt_down_buffer[RTT_DOWN_BUFFER_SIZE] __attribute__((aligned(32)));

/* The control block format and ID are discovered directly by J-Link RTT. */
rtt_control_block_t _SEGGER_RTT __attribute__((used, aligned(32))) =
{
    .id               = "SEGGER RTT",
    .max_up_buffers   = 1,
    .max_down_buffers = 1,
    .up = {{
        .p_name       = s_rtt_name,
        .p_buffer     = (char *) s_rtt_buffer,
        .size         = RTT_PREVIEW_BUFFER_SIZE,
        .write_offset = 0,
        .read_offset  = 0,
        .flags        = RTT_PREVIEW_FLAG_SKIP,
    }},
    .down = {{
        .p_name       = s_rtt_down_name,
        .p_buffer     = (char *) s_rtt_down_buffer,
        .size         = RTT_DOWN_BUFFER_SIZE,
        .write_offset = 0,
        .read_offset  = 0,
        .flags        = 0,
    }},
};

static void rtt_cache_clean(void const * p_data, uint32_t length)
{
    uintptr_t start = ((uintptr_t) p_data) & ~(uintptr_t) 31U;
    uintptr_t end   = (((uintptr_t) p_data) + length + 31U) & ~(uintptr_t) 31U;

    SCB_CleanDCache_by_Addr((uint32_t *) start, (int32_t) (end - start));
}

static void rtt_cache_invalidate(void const * p_data, uint32_t length)
{
    uintptr_t start = ((uintptr_t) p_data) & ~(uintptr_t) 31U;
    uintptr_t end   = (((uintptr_t) p_data) + length + 31U) & ~(uintptr_t) 31U;

    SCB_InvalidateDCache_by_Addr((uint32_t *) start, (int32_t) (end - start));
}

static uint32_t rtt_free_space(rtt_buffer_t const * p_up)
{
    uint32_t write_offset = p_up->write_offset;
    uint32_t read_offset  = p_up->read_offset;

    if (write_offset >= read_offset)
    {
        return p_up->size - write_offset + read_offset - 1U;
    }

    return read_offset - write_offset - 1U;
}

static bool rtt_write(void const * p_data, uint32_t length)
{
    rtt_buffer_t * p_up = &_SEGGER_RTT.up[0];
    uint32_t write_offset;
    uint32_t first;

    /* J-Link updates read_offset through SWD, bypassing the CPU cache. */
    rtt_cache_invalidate(&_SEGGER_RTT, sizeof(_SEGGER_RTT));

    if ((NULL == p_data) || (0U == length) || (length > rtt_free_space(p_up)))
    {
        return false;
    }

    write_offset = p_up->write_offset;
    first = p_up->size - write_offset;
    if (first > length)
    {
        first = length;
    }

    memcpy(&p_up->p_buffer[write_offset], p_data, first);
    if (length > first)
    {
        memcpy(p_up->p_buffer, ((uint8_t const *) p_data) + first, length - first);
        rtt_cache_clean(p_up->p_buffer, length - first);
    }
    rtt_cache_clean(&p_up->p_buffer[write_offset], first);

    write_offset += length;
    if (write_offset >= p_up->size)
    {
        write_offset -= p_up->size;
    }

    __DMB();
    p_up->write_offset = write_offset;
    rtt_cache_clean(&_SEGGER_RTT, sizeof(_SEGGER_RTT));
    return true;
}

void rtt_preview_init(void)
{
    _SEGGER_RTT.up[0].write_offset = 0U;
    _SEGGER_RTT.up[0].read_offset  = 0U;
    rtt_cache_clean(s_rtt_buffer, sizeof(s_rtt_buffer));
    rtt_cache_clean(&_SEGGER_RTT, sizeof(_SEGGER_RTT));
}

bool rtt_preview_send_rgb565(const uint16_t * p_rgb565, uint16_t width, uint16_t height)
{
    uint32_t payload_size = (uint32_t) width * height * sizeof(uint16_t);
    static uint16_t sequence;

    if ((NULL == p_rgb565) || (0U == payload_size) || (payload_size > RTT_PREVIEW_FRAME_BYTES))
    {
        return false;
    }

    /* Publish one contiguous packet so the PC can never observe a header without its payload. */
    s_rtt_packet[0] = 0x55U;
    s_rtt_packet[1] = 0xAAU;
    s_rtt_packet[2] = (uint8_t) (width >> 8U);
    s_rtt_packet[3] = (uint8_t) width;
    s_rtt_packet[4] = (uint8_t) (height >> 8U);
    s_rtt_packet[5] = (uint8_t) height;
    s_rtt_packet[6] = (uint8_t) (sequence >> 8U);
    s_rtt_packet[7] = (uint8_t) sequence++;
    memcpy(&s_rtt_packet[8], p_rgb565, payload_size);

    return rtt_write(s_rtt_packet, payload_size + 8U);
}

/* ============================================================================
 * RTT Down 通道（PC→MCU）：供 TTF 烧录等接收数据。
 * J-Link 通过 SWD 写 down buffer 的 write_offset 与数据（绕过 CPU cache，
 * 读前需 invalidate；MCU 消费后更新 read_offset，写回前 clean）。
 * ==========================================================================*/
uint32_t rtt_down_available(void)
{
    rtt_buffer_t * p_down = &_SEGGER_RTT.down[0];
    uint32_t read_offset;
    uint32_t write_offset;

    rtt_cache_invalidate(&_SEGGER_RTT, sizeof(_SEGGER_RTT));

    read_offset  = p_down->read_offset;
    write_offset = p_down->write_offset;

    if (write_offset >= read_offset)
    {
        return write_offset - read_offset;
    }
    return p_down->size - read_offset + write_offset;
}

/* 从 down 通道读 up to length 字节，返回实际读到的字节数（不跨环）。 */
uint32_t rtt_down_read(uint8_t * p_dest, uint32_t length)
{
    rtt_buffer_t * p_down = &_SEGGER_RTT.down[0];
    uint32_t read_offset;
    uint32_t write_offset;
    uint32_t available;
    uint32_t first;

    if (NULL == p_dest)
    {
        return 0U;
    }

    rtt_cache_invalidate(&_SEGGER_RTT, sizeof(_SEGGER_RTT));

    read_offset  = p_down->read_offset;
    write_offset = p_down->write_offset;

    if (write_offset >= read_offset)
    {
        available = write_offset - read_offset;
    }
    else
    {
        available = p_down->size - read_offset + write_offset;
    }
    if (available > length)
    {
        available = length;
    }
    if (0U == available)
    {
        return 0U;
    }

    first = p_down->size - read_offset;
    if (first > available)
    {
        first = available;
    }
    memcpy(p_dest, &p_down->p_buffer[read_offset], first);
    if (available > first)
    {
        memcpy(p_dest + first, p_down->p_buffer, available - first);
    }
    rtt_cache_invalidate(&p_down->p_buffer[read_offset], first);
    if (available > first)
    {
        rtt_cache_invalidate(p_down->p_buffer, available - first);
    }

    read_offset += available;
    if (read_offset >= p_down->size)
    {
        read_offset -= p_down->size;
    }

    __DMB();
    p_down->read_offset = read_offset;
    rtt_cache_clean(&_SEGGER_RTT, sizeof(_SEGGER_RTT));
    return available;
}

void rtt_down_init(void)
{
    _SEGGER_RTT.down[0].write_offset = 0U;
    _SEGGER_RTT.down[0].read_offset  = 0U;
    rtt_cache_clean(&_SEGGER_RTT, sizeof(_SEGGER_RTT));
}
