# 硬件指导 · 储药搬运（STORAGE_PLACE / PLACE_FINISHED）

> 覆盖云端新增的**储药搬运闭环**，给硬件/主控 MCU + ESP-01S 对接用。
> 场景：药师在网页扫码储药 → 云端分配好放置位置并生成「储药搬运任务」→ 药师在页面**手动下发** →
> 设备（执行机构）**到取药口取药 → 放到云端分配的位置 → 回报 PLACE_FINISHED** → 云端标记搬运完成。

协议权威版见 `硬件对接指南_MCU_LVGL.md`；本文为该子流程的补充。

---

## 一、整体时序

```
 网页储药       云端(Node)             设备(MCU+ESP-01S,执行机构)
   │扫码储药──▶ 分配位置+生成搬运任务(PLACE-001, QUEUED)
   │                                              │
   │页面点「下发」─────────────────────────────▶ STORAGE_PLACE(下行)
   │                                              │ 设备到取药口取药→放到 coord 位
   │                                              │ 放置完成
   │◀──核销 DONE──── 回报 PLACE_FINISHED(上行) ◀───┘
```

---

## 二、下行指令（云端 → 设备）：STORAGE_PLACE

云端下发（TO 透传/WS，每行一个 JSON，`\n` 结尾）：

```json
{
  "cmd": "STORAGE_PLACE",
  "taskId": "PLACE-001",
  "item": {
    "drugId": "DRG-1",
    "drugName": "阿莫西林胶囊",
    "from": "取药口",            // 源位置：去这里取药
    "coord": "A-02",            // 放置目标：层内货位编号
    "layer": 1,                 // 目标层（1=A）
    "x": 122,                   // 目标 x 坐标（mm，药位中心）
    "w": 72,                    // 药品宽度（mm）
    "startX": 102,              // 目标段起点（mm，可选）
    "endX": 174                 // 目标段终点（mm，可选）
  },
  "ts": 1789000000000
}
```

**设备要做**：
1. 解析 `cmd === "STORAGE_PLACE"` 与 `item`。
2. 到 `item.from`（取药口）抓取该药品（按 `item.drugId/drugName` 核对）。
3. 移动到第 `layer` 层的 `x` mm 处，把药放到该位置（段长 `w`）。
4. 放置完成后，回报 `PLACE_FINISHED`（见下）。

---

## 三、上行回报（设备 → 云端）：PLACE_FINISHED

```json
{ "deviceId": "CAB-001", "type": "PLACE_FINISHED", "taskId": "PLACE-001", "coord": "A-02", "status": "SUCCESS" }
```

| 字段 | 说明 |
| --- | --- |
| `deviceId` | 设备唯一标识（必须） |
| `type` | 固定 `PLACE_FINISHED` |
| `taskId` | 原样回传下发的 `taskId`（用于云端匹配） |
| `coord` | 放置完成的货位（可选） |
| `status` | `SUCCESS`（成功）\| `FAILED`（失败）；失败可附 `message` |

云端收到后：匹配该搬运任务 → 置为 `DONE`（或 `FAILED`）→ 网页实时更新。

---

## 四、与取药等其它事件的关系
- 储药搬运与取药（`PICKUP_SCANNED`）、出药（`ACTION_FINISHED`）是**独立指令**，通过 `cmd` / `type` 区分，互不影响。
- 若设备同时支持三种，建议按下行 `cmd` 分派处理。

---

## 五、MCU/ESP-01S 伪代码

```c
// 下行处理：收到平台 { "cmd": ... }
void on_line(const char* line){
    cJSON* r = cJSON_Parse(line); if(!r) return;
    const char* cmd = cJSON_GetObjectItem(r,"cmd")->valuestring;
    if(!cmd) return;

    if(strcmp(cmd,"STORAGE_PLACE")==0){
        cJSON* it = cJSON_GetObjectItem(r,"item");
        const char* from = cJSON_GetObjectItem(it,"from")->valuestring;
        const char* coord= cJSON_GetObjectItem(it,"coord")->valuestring;
        int   layer     = cJSON_GetObjectItem(it,"layer")->valueint;
        double x        = cJSON_GetObjectItem(it,"x")->valuedouble;
        const char* taskId = cJSON_GetObjectItem(r,"taskId")->valuestring;

        pick_from(from);                 // 去取药口取药
        move_to(layer, x);               // 移到目标层/位
        place();                         // 放到目标位

        // 回报放置完成
        char b[160];
        snprintf(b,sizeof b,
          "{\"deviceId\":\"%s\",\"type\":\"PLACE_FINISHED\",\"taskId\":\"%s\",\"coord\":\"%s\",\"status\":\"SUCCESS\"}",
          DEVICE_ID, taskId, coord);
        send_line(b);
    }
    // ... 其它 cmd：CONNECTED / PING / DISPENSE_ACTION
    cJSON_Delete(r);
}
```

---

## 六、排查

| 现象 | 处理 |
| --- | --- |
| 页面下发后设备没反应 | 是否在线、收到 STORAGE_PLACE？下行 `\n` 分帧；解析 `cmd` |
| 放置后网页没变 DONE | 是否回报 `PLACE_FINISHED`，`taskId` 是否原样回传 |
| 找不到放置位坐标 | 以 `layer` + `x` 为准（段起止 `startX/endX` 供参考） |
| 回报后 taskId 匹配失败 | `taskId` 必须与下发完全一致（含前缀 `PLACE-`） |

---

## 七、固件实现对照（RA8P1 设备侧，已并入权威手册 §8.4）

> 协议权威版已并入 `docs/硬件对接手册_MCU_LVGL.md` §8.4；本文为子流程说明存档。
> 设备侧实现（2026-08-21 提交）：

| 项 | 实现 |
| --- | --- |
| 下行解析 | `esp01s_proto.c` `STORAGE_PLACE` 分支：`taskId` + `item{drugId,drugName,coord,layer,x}`；`from` 不消费（固定取暂存区坐标） |
| layer → 层Y | `layer` 1=A → `PickupParams_ShelfY(layer-1)`；越界回报 FAILED（不静默回退第 0 层） |
| x 单位 | mm（药位中心）；越界由 gantry 软限位兜底 → 机械动作失败 → FAILED |
| 执行 | `pickup_test.c` 新增存药模式 `PickupTest_StartPlace(x,y)`：取药口(暂存区)抓 1 盒 → 目标仓放下 → 回零备机 |
| 上行回报 | `PLACE_FINISHED{deviceId,type,taskId,coord,status}`（SUCCESS/FAILED）；失败附 ALARM |
| 忙互斥 | 机械臂忙（取药/出药进行中）→ 新任务直接 FAILED；存药进行中忽略新 STORAGE_PLACE |
| GUI | Store 存药页：任务执行中显示药品/目标仓/「执行中」并禁用「确认存药」；完成 10s 内显示「已完成/已入库」；结束后恢复 |
