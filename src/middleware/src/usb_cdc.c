#include "usb_cdc.h"

#include <string.h>

#include "Camera_thread.h"   /* g_basic0 / g_basic0_ctrl / g_basic0_cfg锛圧ASC 鐢熸垚锛?*/
#include "FreeRTOS.h"
#include "semphr.h"

/* RASC 鐢熸垚 USB 椹卞姩鍚庯紝r_usb_basic_api.h 浼氬嚭鐜板湪 ra/fsp/inc/api/ 涓嬨€? * 鐢?__has_include 鎺㈡祴锛歊ASC 鏈畬鎴愭椂缂栬瘧涓虹┖瀹炵幇锛岀敓鎴愬悗鑷姩婵€娲汇€?*/
#if defined(__has_include)
#  if __has_include("r_usb_basic_api.h")
#    define USB_CDC_AVAILABLE 1
#  endif
#endif

#ifdef USB_CDC_AVAILABLE

/**********************************************************************************************************************
 * 灞€閮ㄧ姸鎬? **********************************************************************************************************************/
static volatile bool     s_connected;      /* 鏋氫妇瀹屾垚 */
static volatile bool     s_port_open;      /* usbser 宸叉墦寮€绔彛锛圖TR/RTS 缃綅锛?*/
static SemaphoreHandle_t s_write_sem;      /* 姣忓寘鍙戦€佸畬鎴愪俊鍙?*/
static SemaphoreHandle_t s_notify_sem;
static uint32_t          s_seq;            /* 甯у簭鍙?*/

/* 铏氭嫙涓插彛閰嶇疆锛圙ET_LINE_CODING 杩斿洖 / SET_LINE_CODING 鏇存柊锛?15200-8N1锛?*/
static volatile usb_pcdc_linecoding_t s_line_coding = {
    .dw_dte_rate   = 115200U,
    .b_char_format = 0U,   /* 1 stop bit */
    .b_parity_type = 0U,   /* none */
    .b_data_bits   = 8U,
    .rsv           = 0U,
};

/* DTR/RTS 鎺у埗绾跨姸鎬侊紙SET_CONTROL_LINE_STATE 鏇存柊锛?*/
static volatile uint16_t s_ctrl_line_state;

/* bulk OUT 鎺ユ敹缂撳啿鍖猴紙PC鈫掕澶囦笅琛屾暟鎹級銆? * 蹇呴』鎸佺画鐢?R_USB_Read 姝﹁ PIPE2锛屽惁鍒?usbser 鎵撳紑鏃跺悜 bulk OUT 鍐欏叆
 * 浼氭案涔?NAK 鈫?Open() 闃诲锛團SP PCDC 瀹樻柟绀轰緥鐨勬爣鍑嗘ā寮忥級銆?*/
static uint8_t s_rx_buf[64] __attribute__((aligned(4)));
static volatile bool   s_rx_armed;      /* PIPE2 鏄惁宸叉寕 R_USB_Read */
static volatile uint32_t s_rx_count;    /* 宸叉帴鏀朵笅琛屽瓧鑺傛暟锛堣瘖鏂敤锛?*/

/* SerialState 閫氱煡锛圗P3 涓柇绔偣锛夛細Windows usbser 鎵撳紑绔彛鍚庣瓑寰?DSR/DCD 涓婃姤锛? * 鍥轰欢涓嶅彂閫氱煡鍒?usbser 姘歌繙绛変笉鍒?modem 灏辩华 鈫?Open() 姘镐箙闃诲銆? * 閫氱煡鐢?R_USB_Write(..., USB_CLASS_PCDCC) 鍙戦€侊紝椹卞姩鑷姩缁勮 10 瀛楄妭 CDC 閫氱煡銆?*/
static volatile bool   s_serial_notify_pending;
static volatile bool   s_serial_notify_inflight;
static volatile TickType_t s_serial_notify_tick;
static volatile uint8_t s_serial_notify_state;
volatile uint32_t s_dbg_notify_ok;    /* R_USB_Write(PCDCC) 杩斿洖 FSP_SUCCESS 娆℃暟 */
volatile uint32_t s_dbg_notify_err;
volatile uint32_t s_dbg_notify_first_err; /* 第一次通知失败的 FSP 错误码 */   /* R_USB_Write(PCDCC) 杩斿洖閿欒娆℃暟 */
volatile uint32_t s_dbg_notify_last_err; /* 鏈€杩戜竴娆?R_USB_Write 鐨勯敊璇爜 */
volatile uint32_t s_dbg_write_cb_cnt;
volatile uint32_t s_dbg_write_bulk_cnt;
volatile uint32_t s_dbg_notify_cb_cnt;
volatile uint32_t s_dbg_notify_timeout;
volatile uint32_t s_dbg_send_timeout;

/* USBHS bulk-IN preview state.  Each 4 KiB transfer is asynchronously chained
 * after its completion.  This stays compatible with usbser while avoiding a
 * camera-task wait for every endpoint packet. */
#define USB_CDC_PREVIEW_TRANSFER_SIZE (4096U)
static uint8_t s_preview_header[USB_CDC_FRAME_HEADER_SIZE] __attribute__((aligned(32)));
static const uint8_t * s_preview_data;
static uint32_t s_preview_bytes;
static uint32_t s_preview_offset;
static volatile bool s_preview_pending;
static volatile bool s_preview_header_pending;
static volatile bool s_preview_inflight;
static volatile TickType_t s_preview_tick;

/* 璇婃柇璁℃暟鍣紙J-Link 鍙锛岄獙璇佺湡瀹炶繍琛屾椂鍥炶皟鏄惁琚皟鐢ㄣ€佽蛋鍒板摢涓€姝ワ級 */
volatile uint32_t s_dbg_req_total;      /* USB_STATUS_REQUEST 鍥炶皟杩涘叆娆℃暟 */
volatile uint32_t s_dbg_req_get;        /* GET_LINE_CODING 娆℃暟 */
volatile uint32_t s_dbg_req_set;        /* SET_LINE_CODING 娆℃暟 */
volatile uint32_t s_dbg_req_ctrl;       /* SET_CONTROL_LINE_STATE 娆℃暟 */
volatile uint32_t s_dbg_req_default;    /* default(STALL) 娆℃暟 */
volatile uint32_t s_dbg_data_get_ok;    /* DataSet 杩斿洖 FSP_SUCCESS 娆℃暟 */
volatile uint32_t s_dbg_data_get_err;   /* DataSet 杩斿洖閿欒娆℃暟 */
volatile uint32_t s_dbg_req_complete;   /* USB_STATUS_REQUEST_COMPLETE(event=8) 娆℃暟 = 鏁版嵁/鐘舵€侀樁娈靛畬鎴?*/

/* 璇锋眰鏃堕棿鎴筹紙SysTick 1ms 绮惧害锛孞-Link 鍙锛夛細
 * 璁板綍姣忔 USB_STATUS_REQUEST 鍒拌揪涓?event=8 瀹屾垚鐨勬椂鍒伙紝
 * 鐢ㄤ簬鍒ゆ柇 usbser 60 绉掑欢杩熺殑鏍瑰洜锛氬浐浠跺搷搴旀參 vs usbser 瓒呮椂閲嶈瘯銆?*/
volatile uint32_t s_dbg_req_tick[16];      /* USB_STATUS_REQUEST 鍒拌揪鏃跺埢(ms) */
volatile uint32_t s_dbg_req_tick_idx;      /* req_tick 鍐欏叆绱㈠紩 */
volatile uint32_t s_dbg_complete_tick[16]; /* event=8 瀹屾垚鏃跺埢(ms) */
volatile uint32_t s_dbg_complete_tick_idx; /* complete_tick 鍐欏叆绱㈠紩 */

/**********************************************************************************************************************
 * USB 浜嬩欢鍥炶皟锛堢敱 R_USB_Open 閰嶇疆鐨?p_usb_apl_callback 璋冪敤锛孖SR 涓婁笅鏂囷級
 * 娉ㄦ剰锛欶reeRTOS 涓嬬鍚?= (usb_event_info_t*, TaskHandle_t, usb_onoff_t)
 **********************************************************************************************************************/
static void usb_cdc_cache_clean(void const * p_data, uint32_t length)
{
    uintptr_t start = ((uintptr_t) p_data) & ~(uintptr_t) 31U;
    uintptr_t end   = (((uintptr_t) p_data) + length + 31U) & ~(uintptr_t) 31U;

    SCB_CleanDCache_by_Addr((uint32_t *) start, (int32_t) (end - start));
}

void usb_pcdc_callback(usb_event_info_t * p_info, TaskHandle_t hdl, usb_onoff_t onoff)
{
    FSP_PARAMETER_NOT_USED(hdl);
    FSP_PARAMETER_NOT_USED(onoff);

    if (NULL == p_info)
    {
        return;
    }

    switch (p_info->event)
    {
        case USB_STATUS_CONFIGURED:
            s_connected = true;
            s_serial_notify_state = 0x03U;   /* DCD | DSR */
            s_serial_notify_pending = true;
            s_serial_notify_inflight = false;
            /* 对齐官方示例：枚举完成立即武装 bulk OUT PIPE2 */
            if (false == s_rx_armed)
            {
                if (FSP_SUCCESS == R_USB_Read(&g_basic0_ctrl, s_rx_buf, sizeof(s_rx_buf), USB_CLASS_PCDC))
                {
                    s_rx_armed = true;
                }
            }
            /* 鏋氫妇瀹屾垚绔嬪嵆涓婃姤 modem 灏辩华锛圖CD|DSR锛夛紝鏌愪簺 usbser 鍦ㄦ墦寮€鍓嶅氨绛夊緟璇ラ€氱煡 */
            break;

        case USB_STATUS_DETACH:
        case USB_STATUS_SUSPEND:
            s_connected = false;
            s_serial_notify_inflight = false;
            s_preview_pending = false;
            s_preview_inflight = false;
            break;

        case USB_STATUS_WRITE_COMPLETE:
            s_dbg_write_cb_cnt++;
            if (USB_CLASS_PCDCC == p_info->type)
            {
                /* EP3 通知完成不能唤醒 BULK 发送等待。 */
                s_dbg_notify_cb_cnt++;
                s_serial_notify_inflight = false;
                s_serial_notify_pending = false;
                xSemaphoreGiveFromISR(s_notify_sem, NULL);
            }
            else
            {
                s_dbg_write_bulk_cnt++;
                s_preview_inflight = false;
                xSemaphoreGiveFromISR(s_write_sem, NULL);
            }
            break;

        case USB_STATUS_READ_COMPLETE:
            /* bulk OUT 涓嬭鏁版嵁鎺ユ敹瀹屾垚锛氱疮璁″瓧鑺傛暟锛屼换鍔′笂涓嬫枃閲嶆柊姝﹁ PIPE2 */
            s_rx_count += p_info->data_size;
            /* 对齐官方示例：回调中立即重新武装 PIPE2，避免后续写入久永 NAK */
            if (s_connected)
            {
                if (FSP_SUCCESS == R_USB_Read(&g_basic0_ctrl, s_rx_buf, sizeof(s_rx_buf), USB_CLASS_PCDC))
                {
                    s_rx_armed = true;
                }
            }
            else
            {
                s_rx_armed = false;
            }
            break;

        case USB_STATUS_REQUEST_COMPLETE:
            /* 鎺у埗浼犺緭鐨?鏁版嵁/鐘舵€侀樁娈?瀹屾垚锛坋vent=8锛夆€斺€旇繖鏄?usbser 鎵撳紑鑳藉惁鎴愬姛鐨勬渶缁堣瘉鎹?*/
            s_dbg_req_complete++;
            s_dbg_complete_tick[s_dbg_complete_tick_idx++ & 0x0F] = xTaskGetTickCountFromISR();
            if (USB_PCDC_GET_LINE_CODING ==
                (uint16_t) (p_info->setup.request_type & USB_BREQUEST))
            {
                s_port_open = true;
            }
            break;

        case USB_STATUS_REQUEST:
        {
            /* CDC 绫昏姹傦紙GET/SET_LINE_CODING銆丼ET_CONTROL_LINE_STATE锛夈€?             * Windows usbser 鎵撳紑铏氭嫙涓插彛鏃跺繀鍙戣繖浜涜姹傦紝涓嶅搷搴斿垯鎵撳紑姘镐箙闃诲銆?             * bRequest 鍦?request_type 楂?8 浣嶏紙USB_BREQUEST=0xFF00锛夈€?*/
            uint16_t brequest = (uint16_t) (p_info->setup.request_type & USB_BREQUEST);

            s_dbg_req_total++;
            s_dbg_req_tick[s_dbg_req_tick_idx++ & 0x0F] = xTaskGetTickCountFromISR();

            switch (brequest)
            {
                case USB_PCDC_GET_LINE_CODING:
                {
                    s_dbg_req_get++;
                    /* 鎺у埗璇伙紙涓绘満鈫愯澶囷級锛欴ataSet 鍐呴儴鏄?usb_pstd_ctrl_read 鈫?鍐?FIFO 鍙戦€佹暟鎹€?                     * 娉ㄦ剰锛歊_USB_PeriControlDataGet 鏄帴鏀舵柟鍚戯紙鍐呴儴 usb_pstd_ctrl_write 鈫?BRDY 璇?FIFO锛夛紝
                     * 鑻ヨ鐢ㄤ細瀵艰嚧涓绘満绛夊緟璁惧鍙戦€佽€岃澶囩瓑寰呮帴鏀?鈫?usbser 鎵撳紑姘镐箙闃诲銆?*/
                    usb_cdc_cache_clean((void const *) &s_line_coding, 7U);
                    fsp_err_t err = R_USB_PeriControlDataSet(&g_basic0_ctrl, (uint8_t *) &s_line_coding, 7U);
                    if (FSP_SUCCESS == err) { s_dbg_data_get_ok++; } else { s_dbg_data_get_err++; }

                    /* usbser 鎵撳紑鎻℃墜鐨勬渶鍚庝竴姝ユ槸 GET_LINE_CODING锛氭敹鍒板畠璇存槑绔彛鍗冲皢鍙敤锛?                     * 姝ゆ椂鎵嶅厑璁?bulk 鍙戦€侊紙鍚﹀垯鍚戞棤浜鸿鍙栫殑 IN 绠￠亾鍐欐暟鎹細寮曞彂 NRDY 椋庢毚锛?                     * 骞叉壈 PIPE0 鎺у埗浼犺緭锛屽鑷?COM 鍙ｆ墦寮€闃诲锛夈€?*/
                    break;
                }

                case USB_PCDC_SET_LINE_CODING:
                {
                    s_dbg_req_set++;
                    /* 鎺у埗鍐欙紙涓绘満鈫掕澶囷級锛欴ataGet 鏄帴鏀舵柟鍚戯紙鍐呴儴 usb_pstd_ctrl_write 鈫?BRDY 璇?FIFO锛?*/
                    (void) R_USB_PeriControlDataGet(&g_basic0_ctrl, (uint8_t *) &s_line_coding, 7U);
                    break;
                }

                case USB_PCDC_SET_CONTROL_LINE_STATE:
                {
                    s_dbg_req_ctrl++;
                    /* 鏃犳暟鎹樁娈碉紝椹卞姩鑷姩 ACK锛涗粎璁板綍 DTR/RTS锛坵Value 浣?2 浣嶏級銆?                     * 娉ㄦ剰锛氫笉鑳藉湪姝ゅ紑闂稿彂閫佲€斺€攗sbser 鎵撳紑搴忓垪涓?                     * SET_LINE_CODING 鈫?SET_CONTROL_LINE_STATE 鈫?GET_LINE_CODING锛?                     * DTR 缃綅鏃╀簬 GET_LINE_CODING 瀹屾垚锛屾鏃跺彂閫佷細骞叉壈鎻℃墜銆?                     * 寮€闂镐俊鍙峰湪 GET_LINE_CODING 鍒嗘敮璁剧疆 s_port_open = true銆?*/
                    s_ctrl_line_state = p_info->setup.request_value;

                    /* 鏃犳潯浠惰Е鍙?SerialState 閫氱煡锛坢odem 灏辩华 DCD|DSR锛夛細
                     * usbser 鎵撳紑铏氭嫙涓插彛鏃躲€愬繀鐒躲€戝彂閫?SET_CONTROL_LINE_STATE 骞剁瓑寰呰澶?                     * 閫氳繃 EP3 閫氱煡绔偣涓婃姤 DSR/DCD锛岃嫢璁惧涓嶄笂鎶ュ垯 Open() 姘镐箙闃诲銆?                     * 瀹炴祴 usbser 鐨?wValue 涓?DTR 浣嶅彲鑳戒负 0锛堝 DTR=0 浠?RTS=1锛夛紝
                     * 鍥犳涓嶈兘鎸?DTR 浣嶈繃婊も€斺€斿彧瑕佹敹鍒拌璇锋眰灏辫Е鍙戯紝纭繚姣忔鎵撳紑灏濊瘯
                     * 閮芥湁閫氱煡銆傚疄闄呭彂閫佸湪浠诲姟涓婁笅鏂囷紙usb_cdc_poll_serial_notify锛夋墽琛屻€?*/
                    s_serial_notify_state = 0x03U;   /* DCD | DSR */
                    s_serial_notify_pending = true;
                    break;
                }

                default:
                    s_dbg_req_default++;
                    (void) R_USB_PeriControlStatusSet(&g_basic0_ctrl, USB_SETUP_STATUS_STALL);
                    /* 鏈敮鎸佺殑绫昏姹?鈫?STALL */
                    break;
            }
            break;
        }

        default:
            break;
    }
}

/**********************************************************************************************************************
 * 鍒濆鍖? **********************************************************************************************************************/
bool usb_cdc_init(void)
{
    fsp_err_t err;

    s_write_sem = xSemaphoreCreateCounting(16U, 0U);
    s_notify_sem = xSemaphoreCreateBinary();
    if ((NULL == s_write_sem) || (NULL == s_notify_sem))
    {
        return false;
    }

    /* 鎵撳紑 USB锛圥eripheral / PCDC / Hi Speed 鐢?RASC 閰嶇疆鍐冲畾锛?*/
    err = R_USB_Open(&g_basic0_ctrl, &g_basic0_cfg);
    return (FSP_SUCCESS == err);
}

/**********************************************************************************************************************
 * 鍙戦€佷竴甯э紙鍒嗗寘锛岄樆濉炲紡锛? **********************************************************************************************************************/
bool usb_cdc_preview_busy(void)
{
    return (s_preview_pending || s_preview_inflight);
}

bool usb_cdc_preview_submit(const uint16_t * p_rgb565, uint16_t w, uint16_t h)
{
    uint32_t bytes = (uint32_t) w * (uint32_t) h * sizeof(uint16_t);

    if ((NULL == p_rgb565) || (0U == bytes) || !usb_cdc_is_connected() || usb_cdc_preview_busy())
    {
        return false;
    }

    s_preview_header[0] = USB_CDC_MAGIC_0;
    s_preview_header[1] = USB_CDC_MAGIC_1;
    s_preview_header[2] = (uint8_t) (w >> 8U);
    s_preview_header[3] = (uint8_t) w;
    s_preview_header[4] = (uint8_t) (h >> 8U);
    s_preview_header[5] = (uint8_t) h;
    s_preview_header[6] = (uint8_t) (s_seq >> 8U);
    s_preview_header[7] = (uint8_t) s_seq;
    s_seq++;

    s_preview_data = (const uint8_t *) p_rgb565;
    s_preview_bytes = bytes;
    s_preview_offset = 0U;
    s_preview_header_pending = true;
    s_preview_pending = true;
    return true;
}

void usb_cdc_preview_poll(void)
{
    void * p_tx;
    uint32_t tx_len;

    if (!s_preview_pending)
    {
        return;
    }

    if (s_preview_inflight)
    {
        if ((xTaskGetTickCount() - s_preview_tick) < pdMS_TO_TICKS(250U))
        {
            return;
        }

        /* The driver still owns the pipe after a host-side timeout.  Clearing
         * only our flag leaves every subsequent write permanently busy. */
        (void) R_USB_Stop(&g_basic0_ctrl, USB_TRANSFER_WRITE, USB_CLASS_PCDC);
        s_preview_inflight = false;
        s_preview_pending = false;
        s_dbg_send_timeout++;
        return;
    }

    if (s_preview_header_pending)
    {
        p_tx = s_preview_header;
        tx_len = USB_CDC_FRAME_HEADER_SIZE;
        s_preview_header_pending = false;
    }
    else
    {
        uint32_t remaining = s_preview_bytes - s_preview_offset;
        tx_len = (remaining > USB_CDC_PREVIEW_TRANSFER_SIZE) ? USB_CDC_PREVIEW_TRANSFER_SIZE : remaining;
        p_tx = (void *) &s_preview_data[s_preview_offset];
        s_preview_offset += tx_len;
        if (s_preview_offset >= s_preview_bytes)
        {
            s_preview_pending = false;
        }
    }

    usb_cdc_cache_clean(p_tx, tx_len);
    if (FSP_SUCCESS != R_USB_Write(&g_basic0_ctrl, p_tx, tx_len, USB_CLASS_PCDC))
    {
        s_preview_pending = false;
        s_dbg_send_timeout++;
        return;
    }

    s_preview_inflight = true;
    s_preview_tick = xTaskGetTickCount();
}

bool usb_cdc_send_frame(const uint16_t * p_rgb565, uint16_t w, uint16_t h)
{
    static uint8_t  s_chunk[USB_CDC_FRAME_HEADER_SIZE + USB_CDC_CHUNK_SIZE];
    uint32_t        pixel_count = (uint32_t) w * (uint32_t) h;
    uint32_t        data_bytes  = pixel_count * 2U;
    const uint8_t * p_data      = (const uint8_t *) p_rgb565;
    uint32_t        offset      = 0U;
    uint32_t        chunk_data;

    if ((NULL == p_rgb565) || (0U == data_bytes))
    {
        return false;
    }

    if (!s_connected)
    {
        return false;   /* 鏈灇涓撅紝涓㈠抚 */
    }

    /* 甯уご锛歮agic(2) + w(2) + h(2) + seq(2) */
    s_chunk[0] = USB_CDC_MAGIC_0;
    s_chunk[1] = USB_CDC_MAGIC_1;
    s_chunk[2] = (uint8_t) (w >> 8U);
    s_chunk[3] = (uint8_t) (w & 0xFFU);
    s_chunk[4] = (uint8_t) (h >> 8U);
    s_chunk[5] = (uint8_t) (h & 0xFFU);
    s_chunk[6] = (uint8_t) (s_seq >> 8U);
    s_chunk[7] = (uint8_t) (s_seq & 0xFFU);
    s_seq++;

    /* 鍒嗗寘鍙戦€侊細姣忔涓€涓?chunk锛堝抚澶翠粎绗竴鍖呮惡甯︼級 */
    while (offset < data_bytes)
    {
        uint32_t remaining = data_bytes - offset;
        uint8_t  chunk_hdr;
        uint8_t * p_tx;
        uint32_t  tx_len;

        if (0U == offset)
        {
            chunk_hdr = USB_CDC_FRAME_HEADER_SIZE;
            chunk_data = remaining < USB_CDC_CHUNK_SIZE ? remaining : USB_CDC_CHUNK_SIZE;
            p_tx = s_chunk;
            tx_len = chunk_hdr + chunk_data;
            memcpy(&s_chunk[USB_CDC_FRAME_HEADER_SIZE], &p_data[offset], chunk_data);
        }
        else
        {
            chunk_hdr = 0U;
            chunk_data = remaining < USB_CDC_CHUNK_SIZE ? remaining : USB_CDC_CHUNK_SIZE;
            p_tx = (uint8_t *) &p_data[offset];
            tx_len = chunk_data;
        }

        usb_cdc_cache_clean(p_tx, tx_len);
        if (FSP_SUCCESS != R_USB_Write(&g_basic0_ctrl, p_tx, tx_len, USB_CLASS_PCDC))
        {
            return false;
        }

        /* USB 异常时快速退出，避免摄像头线程被卡住。 */
        if (pdTRUE != xSemaphoreTake(s_write_sem, pdMS_TO_TICKS(500U)))
        {
            s_dbg_send_timeout++;
            usb_cdc_poll_serial_notify();
            return false;
        }

        offset += chunk_data;
    }

    return true;
}

/**********************************************************************************************************************
 * 杩炴帴鐘舵€? **********************************************************************************************************************/

/**********************************************************************************************************************
 * Send a raw grayscale byte stream (no frame header, chunked, blocking)
 **********************************************************************************************************************/
bool usb_cdc_send_raw(const uint8_t * p_gray, uint32_t bytes)
{
    uint32_t offset = 0U;

    if ((NULL == p_gray) || (0U == bytes))
    {
        return false;
    }

    if (!s_connected)
    {
        return false;   /* Not enumerated, drop frame */
    }

    while (offset < bytes)
    {
        uint32_t chunk = (bytes - offset) < USB_CDC_CHUNK_SIZE ? (bytes - offset) : USB_CDC_CHUNK_SIZE;

        usb_cdc_cache_clean(&p_gray[offset], chunk);
        if (FSP_SUCCESS != R_USB_Write(&g_basic0_ctrl, (void *) &p_gray[offset], chunk, USB_CLASS_PCDC))
        {
            return false;
        }

        /* USB 异常时快速退出，避免灰度串流线程被卡住。 */
        if (pdTRUE != xSemaphoreTake(s_write_sem, pdMS_TO_TICKS(500U)))
        {
            s_dbg_send_timeout++;
            usb_cdc_poll_serial_notify();
            return false;
        }

        offset += chunk;
    }

    return true;
}
bool usb_cdc_is_connected(void)
{
    /* 浠呭綋鏋氫妇瀹屾垚 涓?usbser 宸叉墦寮€铏氭嫙涓插彛锛圖TR/RTS 缃綅锛夋椂鎵嶅厑璁?bulk 鍙戦€併€?     * 鍚﹀垯鍚戞棤浜鸿鍙栫殑 IN 绠￠亾鍐欐暟鎹細寮曞彂 NRDY 椋庢毚锛屽共鎵?PIPE0 鎺у埗浼犺緭锛?     * 瀵艰嚧 usbser 鎵撳紑 COM 鍙ｆ案涔呴樆濉烇紙GDB 鏂偣鍐荤粨鎽勫儚澶寸嚎绋嬫椂鐥囩姸娑堝け鐨勬牴婧愶級銆?*/
    return (s_connected && s_port_open);
}

/**********************************************************************************************************************
 * SerialState 閫氱煡 + bulk OUT 鎺ユ敹姝﹁锛堜换鍔′笂涓嬫枃杞锛屾瘡涓诲惊鐜皟鐢ㄤ竴娆★級
 *
 * 1) SerialState 閫氱煡锛歐indows usbser 鎵撳紑铏氭嫙涓插彛鏃讹紝浼氶€氳繃 EP3 涓柇绔偣(閫氱煡绔偣)
 *    绛夊緟璁惧涓婃姤 SerialState 閫氱煡锛圖CD/DSR 缃綅 = modem 灏辩华锛夈€傝嫢璁惧浠庝笉鍙戦€侊紝
 *    usbser 鐨?Open() 姘镐箙闃诲銆? *
 *    娉ㄦ剰銆愭寜闇€瑙﹀彂銆戯細涓嶈兘鍦ㄦ灇涓惧悗鎸佺画鍛ㄦ湡鎬ц桨鐐?EP3鈥斺€斿疄娴嬫寔缁彂閫?姣?5tick)浼? *    骞叉壈 PIPE0 鎺у埗浼犺緭锛屽鑷?usbser 鎵撳紑搴忓垪鐨勮姹傛棤娉曞畬鎴?event=8=0)銆? *    姝ｇ‘鍋氭硶锛歋ET_CONTROL_LINE_STATE(DTR 缃綅)鏃剁疆 pending锛岃繖閲屽彧鍙戜竴娆°€? *    鎴愬姛鍗虫竻闄ゃ€倁sbser 姣忔鎵撳紑閮戒細鍙?SET_CONTROL_LINE_STATE 鈫?姣忔閮借兘鏀跺埌閫氱煡銆? *
 * 2) bulk OUT 鎺ユ敹姝﹁锛氬繀椤绘寔缁敤 R_USB_Read 鎸傛帴 PIPE2锛圥C鈫掕澶囦笅琛岋級銆? *
 * 娉ㄦ剰锛歊_USB_Read/R_USB_Write 鍐呴儴鍚?vTaskSuspendAll锛岄』鍦ㄤ换鍔′笂涓嬫枃璋冪敤銆? **********************************************************************************************************************/
void usb_cdc_poll_serial_notify(void)
{
    static uint8_t uart_state[2];

    /* --- bulk OUT 鎺ユ敹姝﹁锛圥IPE2锛?-- */
    if ((s_connected) && (false == s_rx_armed))
    {
        if (FSP_SUCCESS == R_USB_Read(&g_basic0_ctrl, s_rx_buf, sizeof(s_rx_buf), USB_CLASS_PCDC))
        {
            s_rx_armed = true;
        }
    }

    /* --- SerialState 通知：端口未打开时周期性重试，防止 usbser 握手死锁。 --- */
    if (!s_connected)
    {
        return;
    }

    if (!s_serial_notify_pending)
    {
        static uint32_t s_notify_force_cnt;
        if (s_port_open)
        {
            return;
        }
        if (0U != (++s_notify_force_cnt % 50U))
        {
            return;
        }
        s_serial_notify_state = 0x03U;   /* DCD | DSR */
        s_serial_notify_pending = true;
    }

    if (s_serial_notify_inflight)
    {
        if ((xTaskGetTickCount() - s_serial_notify_tick) < pdMS_TO_TICKS(1000U))
        {
            return;
        }

        s_serial_notify_inflight = false;
        s_dbg_notify_timeout++;
    }

    /* 閫氱煡绔偣鍙戦€佺敱椹卞姩鑷姩缁勮 10 瀛楄妭閫氱煡锛涗粎鍙戣捣鍙戦€侊紝涓嶇瓑瀹屾垚 */
    uart_state[0] = (uint8_t) s_serial_notify_state;   /* bit0=DCD bit1=DSR ... */
    uart_state[1] = 0U;

    fsp_err_t err_notify = R_USB_Write(&g_basic0_ctrl, uart_state, 2U, USB_CLASS_PCDCC);
    if (FSP_SUCCESS == err_notify)
    {
        s_dbg_notify_ok++;
        s_serial_notify_inflight = true;
        s_serial_notify_tick = xTaskGetTickCount();
    }
    else
    {
        s_dbg_notify_err++;
        s_dbg_notify_last_err = (uint32_t) err_notify;
        if (0U == s_dbg_notify_first_err) { s_dbg_notify_first_err = (uint32_t) err_notify; }
    }
}

#else /* RASC 鏈敓鎴?USB 椹卞姩锛堟棤 r_usb_basic_api.h锛夛細绌哄疄鐜?*/

bool usb_cdc_init(void)
{
    return false;
}

bool usb_cdc_send_frame(const uint16_t * p_rgb565, uint16_t w, uint16_t h)
{
    (void) p_rgb565;
    (void) w;
    (void) h;
    return false;
}

bool usb_cdc_preview_submit(const uint16_t * p_rgb565, uint16_t w, uint16_t h)
{
    (void) p_rgb565;
    (void) w;
    (void) h;
    return false;
}

bool usb_cdc_preview_busy(void)
{
    return false;
}

void usb_cdc_preview_poll(void)
{
}

bool usb_cdc_is_connected(void)
{
    return false;
}

void usb_cdc_poll_serial_notify(void)
{
}

#endif /* USB_CDC_AVAILABLE */
