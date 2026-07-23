#ifndef ZDT_DRV_H
#define ZDT_DRV_H

#include "hal_data.h"

/* --- 张大头 57 电机常用指令集 (默认ID 0x01) --- */
#define ZDT_ID_X  0x0100  
#define ZDT_ID_Y  0x0200  
#define ZDT_ID_Z  0x0300  
#define ZDT_ID_CATCH 0x0400 // 抓手电机
#define ZDT_ID_ALL  0x0000// 广播 ID

#define ZDT_CMD_ENABLE    0xF3  // 使能/脱机
#define ZDT_CMD_ZERO      0x93  // 设置当前位置为零点
#define ZDT_CMD_POS_MOVE    0xFD  // 梯形加减速位置模式
#define ZDT_CMD_PASS_LIMIT  0xFB  // 直通限速位置模式
#define ZDT_CMD_SYSTEM    0xFE  // 系统控制 (如复位)
#define ZDT_SUB_STOP      0x98  // 紧急停止
#define ZDT_CMD_GOZERO    0x9A  // 回零运动

//脉冲转长度 (单位: mm/pulse, 以细分后每转 16384 脉冲为基准)
#define PULSE_PER_REV  16384.0f //一圈是 16384 脉冲

/*外部变量声明*/
extern volatile uint16_t x_position;
extern volatile uint16_t y_position;
extern volatile uint16_t z_position;
extern volatile uint16_t catch_position;

/* --- 驱动函数声明 --- */

// 初始化 CAN 模块
fsp_err_t ZDT_Driver_Init(void);


void ZDT_Enable(uint32_t id, bool enable);

// 将当前位置设为零点
void ZDT_SetZero(uint32_t id);


void ZDT_MovePosition(uint32_t id, int32_t pos, uint16_t speed, uint8_t acc, bool sync);
void ZDT_SyncTrigger(void);
void ZDT_Gozero(uint32_t id, bool sync);//回零运动
void ZDT_Stop(uint32_t id);// 紧急停止
#endif