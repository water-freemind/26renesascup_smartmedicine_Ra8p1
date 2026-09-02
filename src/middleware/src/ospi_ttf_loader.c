/*
 * ospi_ttf_loader.c
 *
 * 通过 RTT Down 通道把 TTF 字体文件烧录到板载串行 Flash（OSPI0/CS0）。
 *
 * 协议（PC→MCU，每帧）：
 *   [0..2]  'T','T','F'
 *   [3]    命令：0x01=数据块, 0x02=完成
 *   [4..7]  块序号 BE32
 *   [8..11] Flash 偏移 BE32
 *   [12..15] 数据长度 BE32
 *   [16..]  数据（<=2048B）
 *
 * MCU→PC 应答（可选，通过 up 通道）：'O','K',块号 / 'E','R',错误码
 *
 * 建议在 Camera 线程主循环调用 ospi_ttf_loader_poll() 轮询处理。
 */
#include "ospi_ttf_loader.h"
#include "ospi_storage.h"
#include "rtt_preview.h"

#include <string.h>

#define TTF_LOADER_CHUNK_MAX    (2048U)
#define TTF_LOADER_CMD_DATA     (0x01U)
#define TTF_LOADER_CMD_DONE     (0x02U)
#define TTF_LOADER_CMD_ERASE    (0x03U)   /* 整片擦除（0xC7） */
#define TTF_LOADER_CMD_RESET    (0x00U)   /* 重置烧录状态（不清 flash） */
#define TTF_LOADER_CMD_WAIT_IDLE (0x04U)  /* 等上次中断的擦除结束（WIP=0） */
#define TTF_LOADER_CMD_ICON_DATA (0x10U)  /* 图标数据块：写 OSPI 图标区（偏移相对图标区基址） */

#define TTF_FLASH_BASE_OFFSET   (0x00000000U)   /* TTF 放在 Flash 起始 */
/* 图标区：simhei TTF 占 0x000000..0x0094B000（TINY_TTF_SIZE=9745792），
 * 图标/图片打包区从 0x00950000（64KB 块对齐）起，容量 ~21.7MB。
 * 运行时 mmap 地址 = 0x80000000 + 0x00950000 = 0x80095000。 */
#define ICON_FLASH_BASE_OFFSET  (0x00950000U)

static volatile bool s_ttf_busy = false;
static volatile bool s_ttf_chip_erased = false;
static volatile uint32_t s_ttf_written = 0U;
volatile uint32_t s_ttf_last_err = 0U;   /* 诊断：最近一次写/擦错误码 */
/* 烧录模式标志：放 .noinit（复位不清零）。烧录期间禁止 GUI 使用 tiny_ttf——
 * 残缺字体渲染会触发 stbtt 断言 → HardFault → loader 停摆 → 烧录停滞。
 * PC 复位后直写 1（J-Link 直写 RAM），RESET 帧也置 1，DONE 帧清 0。 */
volatile uint32_t s_ttf_burn_mode __attribute__((section(".noinit")));
/* 上一次擦除的 64KB 块（文件级：RESET 命令可重置） */
static uint32_t s_last_block = 0xFFFFFFFFU;
/* 接收帧缓冲：放 SDRAM，避免占用 Camera 线程 4KB 栈 */
static uint8_t s_ttf_frame[16U + TTF_LOADER_CHUNK_MAX]
    __attribute__((section(".sdram_noinit"), aligned(32)));

/* J-Link 经 SWD 直读内存（绕过 M85 D-Cache），状态变量更新后需 clean。 */
static void publish_progress(void)
{
    SCB_CleanDCache_by_Addr((uint32_t *) &s_ttf_written, (int32_t) sizeof(s_ttf_written));
    SCB_CleanDCache_by_Addr((uint32_t *) &s_ttf_chip_erased, (int32_t) sizeof(s_ttf_chip_erased));
    SCB_CleanDCache_by_Addr((uint32_t *) &s_ttf_busy, (int32_t) sizeof(s_ttf_busy));
    SCB_CleanDCache_by_Addr((uint32_t *) &s_ttf_burn_mode, (int32_t) sizeof(s_ttf_burn_mode));
}

static void ack(uint8_t ok, uint32_t seq)
{
    /* 简单应答（可选实现；当前通过 s_ttf_written 诊断变量观察进度） */
    (void) ok;
    (void) seq;
}

bool ospi_ttf_loader_is_busy(void)
{
    return s_ttf_busy;
}

uint32_t ospi_ttf_loader_written(void)
{
    return s_ttf_written;
}

bool ospi_ttf_loader_chip_erased(void)
{
    return s_ttf_chip_erased;
}

bool ospi_ttf_loader_burn_mode(void)
{
    /* 只有协议写入的精确值 1（RESET 帧置位）才算烧录模式；DONE 帧写 0。
     * .noinit 变量复位不清零：若残留任意其它值（如烧录工具中断/内存垃圾，
     * 实测出现过 0xAD9C0FE4），会把 GUI 永久锁在"跳过 tiny_ttf"状态 → 全界面
     * 缺字。这里对非 1 的残留一律视为正常并清除，避免误锁。 */
    if (1U == s_ttf_burn_mode)
    {
        return true;
    }
    if (0U != s_ttf_burn_mode)
    {
        s_ttf_burn_mode = 0U;   /* 清除异常残留 */
        publish_progress();
    }
    return false;
}

/* 处理一帧：返回 true 表示已消费（可继续读下一帧） */
static bool ospi_ttf_loader_process_frame(uint8_t * p_frame, uint32_t frame_len)
{
    if (frame_len < 16U)
    {
        return true;   /* 丢弃不完整帧头 */
    }
    if ((p_frame[0] != 'T') || (p_frame[1] != 'T') || (p_frame[2] != 'F'))
    {
        return true;   /* 非本协议数据，丢弃 */
    }

    uint8_t  cmd    = p_frame[3];
    uint32_t seq    = ((uint32_t) p_frame[4] << 24) | ((uint32_t) p_frame[5] << 16) |
                      ((uint32_t) p_frame[6] << 8) | (uint32_t) p_frame[7];
    uint32_t offset = ((uint32_t) p_frame[8] << 24) | ((uint32_t) p_frame[9] << 16) |
                      ((uint32_t) p_frame[10] << 8) | (uint32_t) p_frame[11];
    uint32_t dlen   = ((uint32_t) p_frame[12] << 24) | ((uint32_t) p_frame[13] << 16) |
                      ((uint32_t) p_frame[14] << 8) | (uint32_t) p_frame[15];

    if (TTF_LOADER_CMD_RESET == cmd)
    {
        /* 重置烧录状态（不清 flash）：PC 每次烧录前发送，避免上次进度残留
         * （s_ttf_written 未清零会让 PC 等待逻辑失效、帧堆积）。
         * 同时进入烧录模式：GUI 暂停 tiny_ttf，防止残缺字体渲染崩溃。 */
        s_ttf_busy = false;
        s_ttf_chip_erased = false;
        s_ttf_written = 0U;
        s_ttf_burn_mode = 1U;
        s_last_block = 0xFFFFFFFFU;
        publish_progress();
        ack(1U, seq);
        return true;
    }

    if (TTF_LOADER_CMD_DONE == cmd)
    {
        s_ttf_busy = false;
        s_ttf_burn_mode = 0U;   /* 退出烧录模式：GUI 恢复 tiny_ttf */
        publish_progress();
        ack(1U, seq);
        return true;
    }

    if (TTF_LOADER_CMD_WAIT_IDLE == cmd)
    {
        /* 等芯片空闲（上次被中断的擦除可能仍在后台执行，WIP=1）。
         * 期间任何写使能都会被忽略，必须先等它结束。 */
        s_ttf_busy = true;
        publish_progress();
        fsp_err_t err = ospi_storage_wait_idle(300000U);
        s_ttf_last_err = (FSP_SUCCESS == err) ? 0U : ((uint32_t) err | 0x04000000U);
        publish_progress();
        s_ttf_busy = false;
        publish_progress();
        ack((FSP_SUCCESS == err) ? 1U : 0U, seq);
        return true;
    }

    if (TTF_LOADER_CMD_ERASE == cmd)
    {
        /* 整片擦除：烧录前调用（约 100s）。完成后置标志，数据帧跳过扇区擦除。 */
        s_ttf_busy = true;
        s_ttf_chip_erased = false;
        publish_progress();
        fsp_err_t err = ospi_storage_chip_erase();
        s_ttf_chip_erased = (FSP_SUCCESS == err);
        s_ttf_written = 0U;
        publish_progress();
        s_ttf_busy = false;
        publish_progress();
        ack((FSP_SUCCESS == err) ? 1U : 0U, seq);
        return true;
    }

    if ((TTF_LOADER_CMD_DATA != cmd) && (TTF_LOADER_CMD_ICON_DATA != cmd))
    {
        return true;
    }

    if ((0U == dlen) || (dlen > TTF_LOADER_CHUNK_MAX) || (frame_len < (16U + dlen)))
    {
        ack(0U, seq);
        return true;
    }

    s_ttf_busy = true;

    /* 数据区基址：0x01 写字体区（Flash 起始），0x10 写图标区（simhei 之后） */
    uint32_t const base = (TTF_LOADER_CMD_ICON_DATA == cmd) ? ICON_FLASH_BASE_OFFSET : TTF_FLASH_BASE_OFFSET;
    uint32_t const abs_addr = base + offset;

    /* 已整片擦除则跳过擦除；否则按 64KB 块擦除（每块首帧擦一次）。
     * 4MB TTF = 64 块 × ~1.5s ≈ 96s，远快于逐 4KB 扇区擦除（~7 分钟）。 */
    if (!s_ttf_chip_erased)
    {
        uint32_t const block = abs_addr & ~(65536U - 1U);
        if (block != s_last_block)
        {
            fsp_err_t e = ospi_storage_erase_block(block);
            if (FSP_SUCCESS != e)
            {
                s_ttf_last_err = 0xE0000000U | (uint32_t) e;
            }
            s_last_block = block;
        }
    }

    fsp_err_t err = ospi_storage_write(abs_addr, &p_frame[16], dlen);
    if (FSP_SUCCESS == err)
    {
        s_ttf_written += dlen;
        publish_progress();
        ack(1U, seq);
    }
    else
    {
        s_ttf_last_err = (uint32_t) err;
        publish_progress();
        ack(0U, seq);
    }
    return true;
}

/* 轮询处理 RTT down 通道中的数据。应在低优先级循环中调用。 */
void ospi_ttf_loader_poll(void)
{
    while (rtt_down_available() >= 16U)
    {
        uint32_t got = rtt_down_read(s_ttf_frame, sizeof(s_ttf_frame));
        if (got >= 16U)
        {
            if (!ospi_ttf_loader_process_frame(s_ttf_frame, got))
            {
                break;
            }
        }
        else
        {
            break;
        }
    }
}
