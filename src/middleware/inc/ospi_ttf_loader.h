/*
 * ospi_ttf_loader.h
 *
 * 通过 RTT Down 通道把 TTF 字体烧录到 OSPI Flash 的接口。
 */
#ifndef OSPI_TTF_LOADER_H_
#define OSPI_TTF_LOADER_H_

#include <stdbool.h>
#include <stdint.h>

/** 是否正在烧录（有未完成的数据块）。 */
bool ospi_ttf_loader_is_busy(void);

/** 已写入字节数（诊断用，J-Link 可读）。 */
uint32_t ospi_ttf_loader_written(void);

/** 整片擦除是否已完成（0x03 擦除命令执行成功）。 */
bool ospi_ttf_loader_chip_erased(void);

/** 烧录模式标志（.noinit，复位不清零）：为 true 时 GUI 必须跳过 tiny_ttf
 *  字体创建/应用（残缺字体渲染会触发 stbtt 断言 → HardFault → loader 停摆）。 */
bool ospi_ttf_loader_burn_mode(void);

/** 轮询 RTT down 通道处理烧录帧。应在低优先级循环调用。 */
void ospi_ttf_loader_poll(void);

#endif /* OSPI_TTF_LOADER_H_ */
