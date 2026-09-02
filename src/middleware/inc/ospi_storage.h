/*
 * ospi_storage.h
 *
 * 板载串行 Flash（OSPI0 / CS0）驱动接口：初始化、JEDEC ID、擦写读、内存映射。
 */
#ifndef OSPI_STORAGE_H_
#define OSPI_STORAGE_H_

#include <stdint.h>
#include <stdbool.h>
#include "bsp_api.h"    /* fsp_err_t（必须最先包含 FSP 基础头） */

/** 初始化 OSPI0（g_ospi0），读 JEDEC ID 确认芯片。返回 FSP_SUCCESS 或错误码。 */
fsp_err_t ospi_storage_init(void);

/** OSPI 是否初始化完成（读 JEDEC ID 成功）。 */
bool ospi_storage_get_ready(void);

/** 最近一次初始化错误码（0=成功）。 */
uint32_t ospi_storage_get_error(void);

/** 读取 JEDEC ID（3 字节：制造商/类型/密度）。 */
void ospi_storage_get_jedec(uint8_t jedec[3]);

/** 擦除 4KB 扇区（地址为 OSPI 内部偏移，0 ~ 32MB）。 */
fsp_err_t ospi_storage_erase_sector(uint32_t address);

/** 擦除 64KB 块（0xD8，每块 ~1.5s）。 */
fsp_err_t ospi_storage_erase_block(uint32_t address);

/** 整片擦除（0xC7，W25Q256 约 40~100s）。烧录大数据前调用。 */
fsp_err_t ospi_storage_chip_erase(void);

/** 等待 Flash 空闲（WIP=0），最长 timeout_ms。
 *  上次烧录被中断时芯片可能仍在后台擦除，写使能会被忽略，需先等它完成。 */
fsp_err_t ospi_storage_wait_idle(uint32_t timeout_ms);

/** 页编程（内部按 256B 页切分），要求 address 8 字节对齐、length 为 8 的倍数。 */
fsp_err_t ospi_storage_write(uint32_t address, const uint8_t * p_src, uint32_t length);

/** 内存映射读（1S-1S-1S，Open 后 CS0 区域直接可读）。 */
fsp_err_t ospi_storage_read(uint32_t address, uint8_t * p_dest, uint32_t length);

/** 自检：擦除 -> 页编程 -> 映射读回 -> 比对（flash 末尾 4KB 扇区）。
 *  返回 FSP_SUCCESS 表示四条路径全部验证通过。 */
fsp_err_t ospi_storage_selftest(void);

/** 自检结果（0=通过；高位为阶段错误码，低 8 位为失配字节偏移）。 */
uint32_t ospi_storage_selftest_result(void);

/** 自检失配的首个字节偏移。 */
uint32_t ospi_storage_selftest_mismatch(void);

/** 状态寄存器-2（35h）：bit4=ADS（4 字节地址模式生效标志）。 */
uint8_t ospi_storage_sr2(void);

/** OSPI CS0 内存映射基址（0x80000000），可零拷贝访问整片 32MB。 */
const uint8_t * ospi_storage_mmap_base(void);

#endif /* OSPI_STORAGE_H_ */
