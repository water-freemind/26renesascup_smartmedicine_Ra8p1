# XYZ龙门机械臂实施进度

## 2026-08-12

- 创建独立持久化规划目录 `.planning/xyz_gantry_api/`。
- 开始审计ZDT驱动、应用层、Motor线程和CMake关系。
- 明确只新增应用接口，不修改底层指令、报文及到位应答。
- 完成现有代码审计：确认旧队列未实现、旧流程会阻塞、底层位置变量不能作为可靠三轴坐标。
- 新增 `gantry_robot.h/.c`：静态命令队列、XYZ配置、回零顺序、同步移动、软限位、超时、急停、状态与到位桥接口。
- Motor线程接入 `Gantry_Service()`，正式模式默认关闭CANgaroo周期测试帧。
- 增加 `Gantry_MoveSafeTo()`，按Z退回、XY横移、Z下降的龙门防碰路径自动排队。
- 根据CAN IPL12约束，将到位桥改成中断仅写轴标志、Motor线程消费，避免高优先级中断调用FreeRTOS API。
- ARM单独编译 `gantry_robot.c` 和 `Motor_thread_entry.c` 成功且无新警告；全工程仍被既有 `gui_app.c` 的旧Copy页面引用阻塞。
- `git diff --check` 通过；确认 `ZDT_drv.c/.h` 零差异。
- 设备页新增“机械臂坐标”后重建13个字号中文裁剪字库，Simulator完整编译通过，缺字乱码已修复。
- 将XYZ龙门接口、Motor线程接入、到位桥、标定要求和字库修复同步归档到工程总览、根README与GUI README。
