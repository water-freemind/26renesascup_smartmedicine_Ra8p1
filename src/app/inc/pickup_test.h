#ifndef PICKUP_TEST_H
#define PICKUP_TEST_H

/*
 * 取药流程模块（完整取药动作，非阻塞状态机）
 * ---------------------------------------------------
 * 由 LVGL 线程周期调用 PickupTest_Service() 推进；命令全部走 gantry_robot
 * 命令队列（Motor 线程独占执行 ZDT/CAN），本模块只"入队 + 等待空闲"。
 *
 * 两种启动方式：
 *   1. PickupTest_Start()          —— 单目标测试（取药调试页点格子），目标 = 参数 test_shelf/test_slot；
 *   2. PickupTest_StartOrder()     —— 取药单多药流程（扫码后"开始取药"），逐个药品取：
 *        每种药：XY→该药仓位(X脉冲,Y脉冲) → Z伸出抓药 → 夹爪闭合 → Z收回（先收Z！）
 *              → XY→取药区(store) → Z伸出放下 → 夹爪张开 → Z收回 → 下一种药
 *      硬性约束：一种药放到取药区并放下收回 Z 之后，才能移动去下一个药柜。
 *
 * 坐标系零点 = 第一个药柜（用户实机设零确定）；位置单位换算见 drug_db position。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PICKUP_ORDER_MAX_ITEMS (16U)   /* 一单最多药品数（与取药单 JSON 解析一致） */

typedef enum
{
    PICKUP_TEST_IDLE = 0,    /* 未运行 */
    PICKUP_TEST_RUNNING,     /* 运行中 */
    PICKUP_TEST_DONE_OK,     /* 完成 */
    PICKUP_TEST_DONE_FAIL    /* 失败（text 含原因） */
} pickup_test_state_t;

/** 启动取药流程测试（若上次失败遗留 FAULT，先清故障）。 */
void PickupTest_Start(void);

/** 启动取药单多药取药流程：items 为药品 code 列表（每种取 1 盒，逐药取放）。 */
void PickupTest_StartOrder(const char (*p_items)[32], uint32_t count);

/** 启动取药单多药取药流程（显式坐标版）：p_x_mm/p_y_mm 为每味药的机械
 *  目标坐标（mm，来自云端二维码 items[].x/layer → y），逐药取放。
 *  与 StartOrder 的区别：不查本地 drug_db，坐标完全由调用方给出
 *  （手册 §6：二维码 layer/x/w 供机械定位）。p_xy 为 NULL 时等价 StartOrder。
 *  p_w_mm 为每味药宽度（mm，items[].w，可为 NULL）：非 NULL 时夹爪按
 *  该宽度自动闭合（查表，留 1mm 夹紧），否则回退参数 grip_pulses。 */
void PickupTest_StartOrderXY(const char (*p_items)[32],
                             const float * p_x_mm, const float * p_y_mm,
                             const float * p_w_mm,
                             uint32_t count);

/** 启动云端出药模式：显式目标坐标（X/Y mm）+ 盒数 qty，同一格取 qty 盒
 *  放到取药区（逐盒取放，全部完成后状态 DONE_OK）。
 *  供 DISPENSE_ACTION 执行（esp01s_proto 驱动）；其余流程不受影响。 */
void PickupTest_StartCloud(float x_mm, float y_mm, uint32_t qty);

/** 启动云端存药模式：到取药口（暂存区 store_x/store_y）抓 1 盒，放到
 *  目标货位（X/Y mm，STORAGE_PLACE 下发 layer/x → 层Y/x），完成后回零。
 *  w_mm 为药品宽度（mm，item.w，≤0 时回退参数 grip_pulses）：夹爪按该
 *  宽度自动闭合（查表，留 1mm 夹紧）。
 *  供 STORAGE_PLACE 执行（esp01s_proto 驱动）；其余流程不受影响。 */
void PickupTest_StartPlace(float x_mm, float y_mm, float w_mm);

/** 当前是否存药模式（运行中；供 GUI 区分取药/存药状态机消费）。 */
bool PickupTest_IsPlaceMode(void);

/** 当前是否云端出药模式（DISPENSE_ACTION 运行中；DONE 由协议层消费，
 *  GUI 取药轮询应让位，防误扣库存/误发核销/漏发 ACTION_FINISHED）。 */
bool PickupTest_IsCloudMode(void);

/** 周期调用（LVGL 500ms 定时器）：推进状态机。 */
void PickupTest_Service(void);

/** 复位到空闲（完成/失败消费后调用；清订单/云端出药上下文）。
 *  用途：gui_app 处理完 DONE_OK/DONE_FAIL 后调用，避免残留 DONE 态被
 *  下次轮询重复消费（表现为按钮/行状态被反复改写）。 */
void PickupTest_Reset(void);

/** 读取当前状态与说明文本（文本为静态缓冲，下次调用可能被改写）。 */
void PickupTest_GetStatus(pickup_test_state_t * state, const char ** text);

/** 多药取药进度：当前处理到第几味药 / 总共几味药（非订单模式返回 0/0）。 */
void PickupTest_GetProgress(uint32_t * p_index, uint32_t * p_count);

bool PickupTest_IsRunning(void);

#ifdef __cplusplus
}
#endif
#endif /* PICKUP_TEST_H */
