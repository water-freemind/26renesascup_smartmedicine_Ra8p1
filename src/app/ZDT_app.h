#ifndef ZDT_app_H
#define ZDT_app_H

/***********************************************************************************************************************
 * Includes
 **********************************************************************************************************************/
#include "hal_data.h"
#include "ZDT_drv.h"
#include "Motor_thread.h"
/**********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define cabinet_height 420 //药柜高度，单位mm
#define cabinet_width 420 //药柜宽度，单位mm
#define cabinet_first_floor 5//药柜第一层高度，单位mm,第三层高320mm，第二层
#define cabinet_second_floor 150
#define cabinet_third_floor 299
#define cabinet_per_floor 0//药柜每层高度，单位mm
#define cabinet_shelf_num 3 //药柜层数

#define store_area_x 50
#define store_area_y 299
/* 定义指令类型*/
typedef enum {
    CMD_NONE = 0,
    CMD_ZERO_ALL,       // 全部回零
    CMD_MOVE_XY_MM,     // 移动 XY 坐标
    CMD_CATCH_MEDICINE, // 夹取药盒
    CMD_SCAN,
} MotorCmdType_t;

/*"信件"的格式*/
typedef struct {
    MotorCmdType_t cmd;      // 指令类型
    float target_x;          // X 坐标参数 (毫米)
    float target_y;          // Y 坐标参数 (毫米)
    uint16_t medicine_width; // 药盒宽度 (夹取用)
} MotorMsg_t;

/*外部变量声明*/
extern QueueHandle_t g_motor_queue;
extern volatile uint16_t x_position;
extern volatile uint16_t y_position;
extern volatile uint16_t z_position;
extern volatile uint16_t catch_position;

/***********************************************************************************************************************
 * Exported global functions (to be accessed by other files)
 **********************************************************************************************************************/
void ZDT_Enable_ALL(void);//使能所有电机
void ZDT_Gozero_ALL(void);//所有电机回零，Z 轴先单独回零，X 和 Y 轴同步回零
void Move_XY_To_mm(float x_mm, float y_mm, uint16_t speed, uint8_t acc, bool sync);
void Catch(uint16_t medicine_width_mm,uint16_t speed, uint8_t acc ,uint8_t catch_strength);
void getMedicine(float x,float floor,uint16_t width,uint8_t catch_strength);
void storeMedicine(float store_x,float store_y,uint16_t width,uint8_t catch_strength);
#endif /*APP_TEST_H*/