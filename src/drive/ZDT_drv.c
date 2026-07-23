/* --- START OF FILE ZDT_drv.c --- */

#include "ZDT_drv.h"
#include <stdint.h>
#include <string.h> 
#include "r_can_api.h"
#include "Motor_thread.h"

static fsp_err_t ZDT_Send_Raw(uint32_t id, uint8_t *data, uint8_t total_len)
{
    can_frame_t frame;
    fsp_err_t err = FSP_SUCCESS;
    static uint8_t mb_idx = 0; 
    
    uint8_t cmd_byte = data[0];           // 提取第一个字节 (永远是功能码)
    uint8_t payload_len = total_len - 1;  // 除功能码以外，纯数据的长度
    uint8_t processed = 0;                // 已经发送的数据字节数
    uint8_t packNum = 0;                  // 发送的包序号

    // 如果数据长度为空(只有功能码)，或者还有数据没发完，就循环发送
    while (processed < payload_len || total_len == 1)
    {
        // 算出当前这一包还能装几个纯数据 (最多装 7 个)
        uint8_t remaining = payload_len - processed;
        uint8_t chunk_size = (remaining > 7) ? 7 : remaining;

        frame.id = id + packNum; // 拆包发送时，ID 自动加上包号
        frame.id_mode = CAN_ID_MODE_EXTENDED; 
        frame.type    = CAN_FRAME_TYPE_DATA;
        frame.data_length_code = chunk_size + 1; // 纯数据 + 1个功能码
        frame.options = 0;

        memset(frame.data, 0, 8);
        frame.data[0] = cmd_byte; // 无论第几包，首字节必须是功能码！
        
        // 填充这一包的有效数据
        for (uint8_t i = 0; i < chunk_size; i++)
        {
            frame.data[i + 1] = data[1 + processed + i];
        }

        /* 硬件邮箱发送机制 */
        uint32_t timeout = 50000; 
        do 
        {
            err = g_canfd0.p_api->write(g_canfd0.p_ctrl, mb_idx, &frame);
            if (err == FSP_ERR_IN_USE)
            {
                mb_idx++;
                if (mb_idx > 7) mb_idx = 0;
                R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MICROSECONDS);
                timeout--;
            }
        } while ((err == FSP_ERR_IN_USE) && (timeout > 0));

        if (err == FSP_SUCCESS) {
            mb_idx++;
            if (mb_idx > 7) mb_idx = 0;
        } else {
            return err; // 发生硬件死锁直接退出
        }

        processed += chunk_size;
        packNum++;
        if (processed < payload_len)
        {
            R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
        }
        
        if (total_len <= 1) break; // 防死循环
    }

    return err;
}

/* --- 外部接口实现 --- */

fsp_err_t ZDT_Driver_Init(void)
{
    return g_canfd0.p_api->open(g_canfd0.p_ctrl, g_canfd0.p_cfg);
}

void ZDT_Enable(uint32_t id, bool enable)
{
    uint8_t data[5] = {0};
    data[0] = ZDT_CMD_ENABLE;       
    data[1] = 0xAB;                 
    data[2] = (uint8_t)enable;
    data[3] = 0x00; 
    data[4] = 0x6B; 
    
    ZDT_Send_Raw(id, data, 5); 
}

void ZDT_SetZero(uint32_t id)
{
    uint8_t data[2] = {0};
    data[0] = ZDT_CMD_ZERO;         
    data[1] = 0x6B;
    
    ZDT_Send_Raw(id, data, 2); 
}

void ZDT_Gozero(uint32_t id, bool sync)
{
    uint8_t data[4] = {0};
    data[0] = ZDT_CMD_GOZERO; 
    data[1] = 0x02;           
    data[2] = sync ? 0x01 : 0x00; 
    data[3] = 0x6B;            
    ZDT_Send_Raw(id, data, 4); 
    /*修改实时状态*/
    x_position = 0;
    y_position = 0;
    z_position = 0;
    catch_position = 0;
}


/* 
 * 梯形曲线位置模式控制 (0xFD) - 匹配真实 12 字节载荷协议
 * 功能码 + 方向 + 速度(2) + 加速度(1) + 位置(4) + 绝对标志(1) + 同步(1) + 6B
 */
void ZDT_MovePosition(uint32_t id, int32_t pos, uint16_t speed, uint8_t acc, bool sync)
{
    uint8_t payload[12] = {0}; 
    uint32_t abs_pos = (pos >= 0) ? (uint32_t)pos : (uint32_t)(-pos);
    uint8_t dir = (pos >= 0) ? 0x00 : 0x01; 

    payload[0]  = ZDT_CMD_POS_MOVE;               // 0: 功能码 0xFD
    payload[1]  = dir;                            // 1: 符号(方向)
    
    payload[2]  = (uint8_t)((speed >> 8) & 0xFF); // 2: 最大速度 高位
    payload[3]  = (uint8_t)(speed & 0xFF);        // 3: 最大速度 低位
    
    payload[4]  = acc;                            // 4: 加速度 (1个字节)
    
    payload[5]  = (uint8_t)((abs_pos >> 24) & 0xFF); // 5: 位置角度 最高位
    payload[6]  = (uint8_t)((abs_pos >> 16) & 0xFF); // 6: 位置角度 次高位
    payload[7]  = (uint8_t)((abs_pos >> 8) & 0xFF);  // 7: 位置角度 次低位
    payload[8]  = (uint8_t)(abs_pos & 0xFF);         // 8: 位置角度 最低位
    
    payload[9]  = 0x01;                           // 9: 相对/绝对位置标志 (01 = 绝对)
    
    payload[10] = sync ? 0x01 : 0x00;             // 10: 多机同步标志
    
    payload[11] = 0x6B;                           // 11: 校验字节 0x6B

    ZDT_Send_Raw(id, payload, 12);
    /*修改实时位置*/
    switch (id) {
        case ZDT_ID_X:x_position+=pos;break;
        case ZDT_ID_Y:y_position+=pos;break;
        case ZDT_ID_Z:z_position+=pos;break;
        case ZDT_ID_CATCH:catch_position+=pos;break;
        default:break;
    }
}

void ZDT_SyncTrigger(void)
{
    uint8_t data[3] = {0};
    data[0] = 0xFF; 
    data[1] = 0x66; 
    data[2] = 0x6B;
    ZDT_Send_Raw(ZDT_ID_ALL, data, 3); 
    data[0] = 0xFF; 
    data[1] = 0x02; 
    data[2] = 0x6B;
    ZDT_Send_Raw(ZDT_ID_ALL, data, 3); 
}

void ZDT_Stop(uint32_t id)
{
    uint8_t data[4] = {0};
    data[0] = ZDT_CMD_SYSTEM; 
    data[1] = ZDT_SUB_STOP;   
    data[2] = 0x00; 
    data[3] = 0x6B;           
    
    ZDT_Send_Raw(id, data, 4); 
}