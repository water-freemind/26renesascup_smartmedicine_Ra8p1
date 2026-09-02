/*
 * ospi_storage.c
 *
 * 板载串行 Flash（OSPI0 / CS0）驱动封装：
 *  - 初始化 R_OSPI_B（g_ospi0，W25Q256 / AT25SF256 通用命令集）
 *  - 读 JEDEC ID 确认芯片型号
 *  - 扇区擦除 / 页编程 / 读
 *  - 内存映射读（OSPI CS0 区域 0x80000000，1S-1S-1S 模式直接读）
 *
 * 硬件：RA8P1 OSPI0，CS0=P107，SCLK=P808，SIO0~3=P100/P803/P103/P101
 * 芯片：板载 32MB 串行 NOR（W25Q256JVEIQ 或 AT25SF2561C，型号见 JEDEC ID）
 */
#include "ospi_storage.h"

#include <string.h>
#include "ospi_storage.h"
#include "bsp_api.h"
#include "r_ospi_b.h"           /* R_OSPI_B_* 函数 */
#include "r_spi_flash_api.h"    /* spi_flash_* 类型 / 实例 */

/* RASC 生成的 OSPI 实例在 Camera 线程（ra_gen/Camera_thread.c）。
 * 这里直接外部声明，避免 include 线程头引发的 FSP 头顺序问题。 */
extern const spi_flash_instance_t g_ospi0;
extern ospi_b_instance_ctrl_t g_ospi0_ctrl;
extern const spi_flash_cfg_t g_ospi0_cfg;

/* 内存映射区域：CS0 起始地址（bsp_feature.h） */
#define OSPI_MMAP_BASE         (0x80000000U)
#define OSPI_MMAP_SIZE         (0x02000000U)   /* 32 MiB */

/* JEDEC ID 命令：9Fh（无地址） */
#define OSPI_CMD_JEDEC_ID      (0x9F)
#define JEDEC_ID_LEN           (3U)

/* W25Q256 JEDEC: MFG 0xEF, Type 0x40, Density 0x19 */
#define JEDEC_MFG_WINBOND      (0xEF)
#define JEDEC_TYPE_W25Q256     (0x40)
#define JEDEC_DENSITY_W25Q256  (0x19)
/* AT25SF256 JEDEC: MFG 0x1F, Type 0x85, Density 0x19 */
#define JEDEC_MFG_DIALOG       (0x1F)
#define JEDEC_TYPE_AT25SF256   (0x85)
#define JEDEC_DENSITY_AT25SF256 (0x19)

static volatile bool s_ospi_ready = false;
static volatile uint32_t s_ospi_init_error = 0U;
static volatile uint8_t  s_ospi_jedec[3] = {0U, 0U, 0U};
static volatile uint32_t s_ospi_page_size = 256U;

/* 诊断：4 字节地址模式状态寄存器-2（35h，bit4=ADS）；自检结果。 */
static volatile uint8_t  s_ospi_sr2 = 0U;
static volatile uint32_t s_ospi_selftest_result = 0xFFFFFFFFU;  /* 0=通过 */
static volatile uint32_t s_ospi_selftest_mismatch = 0U;         /* 首个失配字节偏移 */
/* 自检读回缓冲（J-Link 直读 RAM 诊断） */
static volatile uint8_t s_ospi_selftest_got[16];
static volatile uint8_t s_ospi_selftest_exp[16];

/* 自检扇区：flash 末尾 4KB（TTF 占 0 ~ 4MB，不冲突） */
#define OSPI_SELFTEST_ADDR (0x01FFF000U)

bool ospi_storage_get_ready(void)
{
    return s_ospi_ready;
}

uint32_t ospi_storage_get_error(void)
{
    return s_ospi_init_error;
}

void ospi_storage_get_jedec(uint8_t jedec[3])
{
    if (jedec != NULL)
    {
        jedec[0] = s_ospi_jedec[0];
        jedec[1] = s_ospi_jedec[1];
        jedec[2] = s_ospi_jedec[2];
    }
}

/* 通过 DirectTransfer 发命令并读回数据（R_OSPI_B_DirectRead 返回 UNSUPPORTED，
 * 必须用 DirectTransfer：READ 方向数据写入 p_transfer->data / data_u64）。
 * 注意：DirectTransfer 的 data_length 最大 8 字节（CDD0/CDD1），适合 JEDEC ID/状态。 */
static fsp_err_t ospi_send_cmd_no_addr(uint8_t command, uint8_t * p_rx, uint32_t rx_len)
{
    if ((p_rx == NULL) || (rx_len == 0U) || (rx_len > 8U))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    spi_flash_direct_transfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.command        = command;
    xfer.command_length = 1U;
    xfer.address_length = 0U;
    xfer.dummy_cycles   = 0U;
    xfer.data_length    = (uint8_t) rx_len;

    fsp_err_t err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &xfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* 小端拷贝：CDD0 低位在前 */
    for (uint32_t i = 0U; i < rx_len; i++)
    {
        p_rx[i] = (uint8_t) (xfer.data_u64 >> (i * 8U));
    }
    return FSP_SUCCESS;
}

/* 进入 4 字节地址模式（B7h，易失，上电即失效）。
 * W25Q256 上电默认 3 字节地址模式；OSPI 配置为 4 字节地址（address_bytes_4），
 * 必须每次上电发 B7h，否则映射读 / 页编程的地址会错位。 */
static fsp_err_t ospi_enter_4byte_mode(void)
{
    spi_flash_direct_transfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.command        = 0xB7;   /* ENTER 4-BYTE ADDRESS MODE */
    xfer.command_length = 1U;
    xfer.address_length = 0U;
    xfer.dummy_cycles   = 0U;
    xfer.data_length    = 0U;
    return R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &xfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
}

fsp_err_t ospi_storage_init(void)
{
    fsp_err_t err = R_OSPI_B_Open(&g_ospi0_ctrl, &g_ospi0_cfg);
    if (FSP_SUCCESS != err)
    {
        s_ospi_init_error = (uint32_t) err;
        return err;
    }

    /* 读 JEDEC ID：9Fh */
    err = ospi_send_cmd_no_addr(OSPI_CMD_JEDEC_ID, (uint8_t *) s_ospi_jedec, JEDEC_ID_LEN);
    if (FSP_SUCCESS != err)
    {
        s_ospi_init_error = (uint32_t) err;
        return err;
    }

    /* 进入 4 字节地址模式（必须，见上） */
    err = ospi_enter_4byte_mode();
    if (FSP_SUCCESS != err)
    {
        s_ospi_init_error = (uint32_t) err;
        return err;
    }

    /* 读状态寄存器-2（35h）观察 ADS 位（bit4=1 表示 4 字节地址模式生效） */
    (void) ospi_send_cmd_no_addr(0x35, (uint8_t *) &s_ospi_sr2, 1U);

    /* 按型号确认页大小（都是 256B，保留判断逻辑便于扩展） */
    s_ospi_page_size = 256U;
    s_ospi_ready = true;
    s_ospi_init_error = 0U;
    return FSP_SUCCESS;
}

/* 前置声明（chip_erase 定义在 write_enable 之前调用它们） */
static fsp_err_t ospi_write_enable_and_wait(void);
static fsp_err_t ospi_wait_not_busy_timeout(uint32_t timeout_ms);
static fsp_err_t ospi_wait_not_busy(void);

/* 轮询状态寄存器（05h）bit0 = WIP，直到空闲。
 * R_OSPI_B_Erase / R_OSPI_B_Write 发出命令后**不会**等芯片忙完，
 * 必须在此等待，否则紧接的读回会读到芯片忙时的垃圾数据。 */
static fsp_err_t ospi_wait_not_busy_timeout(uint32_t timeout_ms)
{
    while (timeout_ms-- > 0U)
    {
        uint8_t status = 0U;
        fsp_err_t err = ospi_send_cmd_no_addr(0x05, &status, 1U);
        if ((FSP_SUCCESS == err) && (0U == (status & 0x01U)))
        {
            return FSP_SUCCESS;
        }
        R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    }
    return FSP_ERR_TIMEOUT;
}

static fsp_err_t ospi_wait_not_busy(void)
{
    return ospi_wait_not_busy_timeout(2000U);
}

/* 等待 Flash 空闲（WIP=0），最长 timeout_ms。
 * 用途：上次烧录被中断时，芯片可能仍在后台执行擦除（WIP=1），
 * 此时任何写使能（06h）都会被忽略；先等它擦完再继续。 */
fsp_err_t ospi_storage_wait_idle(uint32_t timeout_ms)
{
    if (!s_ospi_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }
    return ospi_wait_not_busy_timeout(timeout_ms);
}

/* 整片擦除（0xC7）：烧录 TTF 前一次性擦除，避免逐 4KB 扇区擦除（约 7 分钟）。
 * W25Q256 chip erase 典型 40~100s、最坏可达 200s+，等待超时放宽到 300s。 */
fsp_err_t ospi_storage_chip_erase(void)
{
    if (!s_ospi_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }
    fsp_err_t err = ospi_write_enable_and_wait();
    if (FSP_SUCCESS != err)
    {
        return err;
    }
    spi_flash_direct_transfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.command        = 0xC7;   /* CHIP ERASE（无地址） */
    xfer.command_length = 1U;
    xfer.address_length = 0U;
    xfer.dummy_cycles   = 0U;
    xfer.data_length    = 0U;
    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &xfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        return err;
    }
    return ospi_wait_not_busy_timeout(300000U);
}

/* 写使能（06h）+ 等待 WIP 清除（05h bit0） */
static fsp_err_t ospi_write_enable_and_wait(void)
{
    spi_flash_direct_transfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.command        = 0x06;   /* WRITE ENABLE */
    xfer.command_length = 1U;
    xfer.address_length = 0U;
    xfer.dummy_cycles   = 0U;
    xfer.data_length    = 0U;
    fsp_err_t err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &xfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* 轮询状态寄存器（05h）bit0 = WIP，最多 ~2s */
    uint32_t timeout_ms = 2000U;
    while (timeout_ms-- > 0U)
    {
        uint8_t status = 0U;
        err = ospi_send_cmd_no_addr(0x05, &status, 1U);
        if ((FSP_SUCCESS == err) && (0U == (status & 0x01U)))
        {
            return FSP_SUCCESS;
        }
        R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
    }
    return FSP_ERR_TIMEOUT;
}

fsp_err_t ospi_storage_erase_sector(uint32_t address)
{
    if (!s_ospi_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }
    fsp_err_t err = ospi_write_enable_and_wait();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* 扇区擦除 20h，4KB 扇区（命令集已按 W25Q256 配 0x20/4096）。
     * R_OSPI_B_Erase 的 p_device_address 需为内存映射地址（内部按地址掩码提取芯片地址）。 */
    err = R_OSPI_B_Erase(&g_ospi0_ctrl, (uint8_t *) (OSPI_MMAP_BASE + address), 4096U);
    if (FSP_SUCCESS != err)
    {
        return err;
    }
    /* 擦除命令发出后芯片立即 BUSY（4KB 约 400ms），必须等 WIP 清除 */
    return ospi_wait_not_busy();
}

/* 64KB 块擦除（0xD8）：TTF 烧录按块擦（每块 ~1.5s），
 * 4MB 数据 64 次块擦除 ≈ 96s，远快于 1024 次扇区擦除（~7 分钟）。 */
fsp_err_t ospi_storage_erase_block(uint32_t address)
{
    if (!s_ospi_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }
    fsp_err_t err = ospi_write_enable_and_wait();
    if (FSP_SUCCESS != err)
    {
        return err;
    }
    err = R_OSPI_B_Erase(&g_ospi0_ctrl, (uint8_t *) (OSPI_MMAP_BASE + address), 65536U);
    if (FSP_SUCCESS != err)
    {
        return err;
    }
    return ospi_wait_not_busy_timeout(5000U);
}

/* 注意：R_OSPI_B_Write 要求 p_dest 为内存映射地址（CPU 写映射区触发页编程）、
 * 8 字节对齐、长度 8 的倍数、单页内。本函数按 256B 页切分并维护 D-Cache：
 *  - 写前 invalidate：丢弃映射区缓存的陈旧数据
 *  - 写后 clean：把 CPU 写入的缓存行强制回写到 OSPI 总线（映射区为 WB cacheable，
 *    不回写则页编程事务不会真正发出）
 *
 * 关键：OSPI 映射写以 64B（组合功能大小）为事务粒度，**每个事务消耗一次 WEL
 * （写使能）**。R_OSPI_B_Write 内部只在开头发一次 06h，若一次写入超过 64B，
 * clean 回写会拆成多个事务，只有第一个事务（WEL 置位）成功，其余数据丢失
 * （实测 64B 之后全为 0xFF）。因此这里按 64B 段写入，每段独立写使能。
 * 调用方需保证 address 8 对齐、length 8 的倍数。 */
fsp_err_t ospi_storage_write(uint32_t address, const uint8_t * p_src, uint32_t length)
{
    if (!s_ospi_ready || (NULL == p_src) || (length == 0U))
    {
        return FSP_ERR_ASSERTION;
    }
    if ((address & 7U) != 0U)
    {
        return FSP_ERR_INVALID_ADDRESS;
    }
    if ((length & 7U) != 0U)
    {
        return FSP_ERR_INVALID_SIZE;
    }

    while (length > 0U)
    {
        /* 64B 段：OSPI 组合事务粒度（8 字节对齐保证段长为 8 的倍数） */
        uint32_t chunk = 64U - (address & 63U);
        if (chunk > length)
        {
            chunk = length;
        }

        /* R_OSPI_B_Write 不自动发写使能，必须先 06h + 等 WIP 清除 */
        fsp_err_t err = ospi_write_enable_and_wait();
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        uint8_t * p_map_dest = (uint8_t *) (OSPI_MMAP_BASE + address);
        SCB_InvalidateDCache_by_Addr((uint32_t *) p_map_dest, (int32_t) chunk);
        err = R_OSPI_B_Write(&g_ospi0_ctrl, (uint8_t *) p_src, p_map_dest, chunk);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
        SCB_CleanDCache_by_Addr((uint32_t *) p_map_dest, (int32_t) chunk);
        /* 页编程由映射写异步触发，等 WIP 清除后再写下一段 */
        err = ospi_wait_not_busy();
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        address += chunk;
        p_src   += chunk;
        length  -= chunk;
    }
    return FSP_SUCCESS;
}

fsp_err_t ospi_storage_read(uint32_t address, uint8_t * p_dest, uint32_t length)
{
    if (!s_ospi_ready || (NULL == p_dest) || (length == 0U))
    {
        return FSP_ERR_ASSERTION;
    }

    /* 内存映射读：OSPI CS0 区域按 1S-1S-1S 直接可读（Open 后即映射）。
     * OSPI 映射区可被 D-Cache 缓存，而 flash 内容可能已被本机写入
     * （DirectTransfer 直写，不经过缓存），读回前必须失效缓存行。 */
    uint8_t const * p_map = (uint8_t const *) (OSPI_MMAP_BASE + address);
    SCB_InvalidateDCache_by_Addr((uint32_t *) p_map, (int32_t) length);
    memcpy(p_dest, p_map, length);
    return FSP_SUCCESS;
}

/* 通过 DirectTransfer 页编程（0x02 + 4 字节地址 + 数据），len<=8。
 * 绕开 R_OSPI_B_Write 的 CPU 映射写路径，用于自检对照。 */
static fsp_err_t ospi_direct_program(uint32_t address, const uint8_t * p_src, uint32_t len)
{
    if ((NULL == p_src) || (len == 0U) || (len > 8U))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }
    fsp_err_t err = ospi_write_enable_and_wait();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    spi_flash_direct_transfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.command        = 0x02;   /* PAGE PROGRAM */
    xfer.command_length = 1U;
    xfer.address        = address;
    xfer.address_length = 4U;     /* 4 字节地址模式 */
    xfer.dummy_cycles   = 0U;
    xfer.data_length    = (uint8_t) len;
    uint64_t d = 0U;
    for (uint32_t i = 0U; i < len; i++)
    {
        d |= ((uint64_t) p_src[i]) << (i * 8U);
    }
    xfer.data_u64 = d;
    err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &xfer, SPI_FLASH_DIRECT_TRANSFER_DIR_WRITE);
    if (FSP_SUCCESS != err)
    {
        return err;
    }
    /* 页编程 BUSY 等待（命令发出后芯片立即忙） */
    return ospi_wait_not_busy();
}

/* 通过 DirectTransfer 读 flash（0x03 + 4 字节地址 + 数据）。
 * 与内存映射读对照，用于定位是"写失败"还是"映射读失败"。len<=8。 */
static fsp_err_t ospi_direct_read(uint32_t address, uint8_t * p_dest, uint32_t len)
{
    if ((NULL == p_dest) || (len == 0U) || (len > 8U))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }
    spi_flash_direct_transfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.command        = 0x03;   /* READ */
    xfer.command_length = 1U;
    xfer.address        = address;
    xfer.address_length = 4U;     /* 4 字节地址模式 */
    xfer.dummy_cycles   = 0U;
    xfer.data_length    = (uint8_t) len;
    fsp_err_t err = R_OSPI_B_DirectTransfer(&g_ospi0_ctrl, &xfer, SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        return err;
    }
    for (uint32_t i = 0U; i < len; i++)
    {
        p_dest[i] = (uint8_t) (xfer.data_u64 >> (i * 8U));
    }
    return FSP_SUCCESS;
}

/* ============================================================================
 * 自检：擦除一个扇区 -> 页编程 -> 内存映射读回 + DirectTransfer 读回 -> 比对。
 * 一次性验证 4 字节地址模式 / 擦除 / 编程 / 映射读 四条路径。
 * ==========================================================================*/
fsp_err_t ospi_storage_selftest(void)
{
    /* 256B 测试数据（0x00..0xFF 循环 ×2），验证 64B 段写（4 段）与映射读 */
    static uint8_t pattern[256];
    static bool s_pattern_init = false;
    if (!s_pattern_init)
    {
        for (uint32_t i = 0U; i < sizeof(pattern); i++)
        {
            pattern[i] = (uint8_t) i;
        }
        s_pattern_init = true;
    }
    uint8_t buf[256];
    uint8_t direct_buf[256];
    fsp_err_t err;

    if (!s_ospi_ready)
    {
        s_ospi_selftest_result = 0x0F000000U;
        return FSP_ERR_NOT_OPEN;
    }

    /* 1. 擦除自检扇区（4KB） */
    err = ospi_storage_erase_sector(OSPI_SELFTEST_ADDR);
    if (FSP_SUCCESS != err)
    {
        s_ospi_selftest_result = 0x10000000U | (uint32_t) err;
        return err;
    }

    /* 1b. 擦除后 direct 读回：期望全 0xFF（验证擦除 + direct 读 + 地址模式） */
    for (uint32_t i = 0U; i < 8U; i++)
    {
        err = ospi_direct_read(OSPI_SELFTEST_ADDR + i, &direct_buf[i], 1U);
        if (FSP_SUCCESS != err)
        {
            s_ospi_selftest_result = 0x11000000U | (uint32_t) err;
            return err;
        }
        if (direct_buf[i] != 0xFFU)
        {
            s_ospi_selftest_mismatch = i;
            s_ospi_selftest_result = 0x12000000U | i;   /* 擦除后非 0xFF */
            s_ospi_selftest_got[0] = direct_buf[i];
            return FSP_ERR_WRITE_FAILED;
        }
    }

    /* 2a. DirectTransfer 页编程（0x02 + 4B 地址，逐 8 字节），绕开映射写 */
    for (uint32_t i = 0U; i < sizeof(pattern); i += 8U)
    {
        err = ospi_direct_program(OSPI_SELFTEST_ADDR + i, &pattern[i], 8U);
        if (FSP_SUCCESS != err)
        {
            s_ospi_selftest_result = 0x21000000U | (uint32_t) err;
            return err;
        }
    }

    /* 2b. DirectTransfer 读回，验证 direct 写路径 */
    for (uint32_t i = 0U; i < sizeof(buf); i += 8U)
    {
        err = ospi_direct_read(OSPI_SELFTEST_ADDR + i, &direct_buf[i], 8U);
        if (FSP_SUCCESS != err)
        {
            s_ospi_selftest_result = 0x31000000U | (uint32_t) err;
            return err;
        }
    }
    for (uint32_t i = 0U; i < sizeof(pattern); i++)
    {
        if (direct_buf[i] != pattern[i])
        {
            s_ospi_selftest_mismatch = i;
            s_ospi_selftest_result = 0x50000000U | i;   /* direct 写/读失配 */
            for (uint32_t j = 0U; j < 16U; j++)
            {
                s_ospi_selftest_got[j] = direct_buf[j];
                s_ospi_selftest_exp[j] = pattern[j];
            }
            return FSP_ERR_WRITE_FAILED;
        }
    }

    /* 2c. 再擦除（恢复 0xFF），然后走映射写路径验证 64B 段写 */
    err = ospi_storage_erase_sector(OSPI_SELFTEST_ADDR);
    if (FSP_SUCCESS != err)
    {
        s_ospi_selftest_result = 0x22000000U | (uint32_t) err;
        return err;
    }
    err = ospi_storage_write(OSPI_SELFTEST_ADDR, pattern, sizeof(pattern));
    if (FSP_SUCCESS != err)
    {
        s_ospi_selftest_result = 0x23000000U | (uint32_t) err;
        return err;
    }

    /* 3. 内存映射读回（验证映射读 + 64B 段写 + 4 字节地址模式） */
    err = ospi_storage_read(OSPI_SELFTEST_ADDR, buf, sizeof(buf));
    if (FSP_SUCCESS != err)
    {
        s_ospi_selftest_result = 0x30000000U | (uint32_t) err;
        return err;
    }
    for (uint32_t i = 0U; i < sizeof(pattern); i++)
    {
        if (buf[i] != pattern[i])
        {
            s_ospi_selftest_mismatch = i;
            s_ospi_selftest_result = 0x40000000U | i;   /* 映射读回失配 */
            for (uint32_t j = 0U; j < 16U; j++)
            {
                s_ospi_selftest_got[j] = buf[j];
                s_ospi_selftest_exp[j] = pattern[j];
            }
            return FSP_ERR_WRITE_FAILED;
        }
    }

    s_ospi_selftest_mismatch = 0U;
    s_ospi_selftest_result = 0U;
    return FSP_SUCCESS;
}

uint32_t ospi_storage_selftest_result(void)
{
    return s_ospi_selftest_result;
}

uint32_t ospi_storage_selftest_mismatch(void)
{
    return s_ospi_selftest_mismatch;
}

uint8_t ospi_storage_sr2(void)
{
    return s_ospi_sr2;
}

/* 返回内存映射基址（tiny_ttf 等可零拷贝读 TTF 数据） */
const uint8_t * ospi_storage_mmap_base(void)
{
    return (const uint8_t *) OSPI_MMAP_BASE;
}
