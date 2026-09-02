/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = ceu_isr, /* CEU CEUI (CEU interrupt) */
            [1] = usbfs_interrupt_handler, /* USBFS INT (USBFS interrupt) */
            [2] = usbfs_resume_handler, /* USBFS RESUME (USBFS resume interrupt) */
            [3] = usbfs_d0fifo_handler, /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [4] = usbfs_d1fifo_handler, /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
            [5] = usbhs_interrupt_handler, /* USBHS USB INT RESUME (USBHS interrupt) */
            [6] = usbhs_d0fifo_handler, /* USBHS FIFO 0 (DMA transfer request 0) */
            [7] = usbhs_d1fifo_handler, /* USBHS FIFO 1 (DMA transfer request 1) */
            [8] = iic_master_rxi_isr, /* IIC0 RXI (Receive data full) */
            [9] = iic_master_txi_isr, /* IIC0 TXI (Transmit data empty) */
            [10] = iic_master_tei_isr, /* IIC0 TEI (Transmit end) */
            [11] = iic_master_eri_isr, /* IIC0 ERI (Transfer error) */
            [12] = glcdc_line_detect_isr, /* GLCDC LINE DETECT (Specified line) */
            [13] = mipi_dsi_seq0_isr, /* MIPIDSI SEQ0 (Sequence operation channel 0 interrupt) */
            [14] = mipi_dsi_seq1_isr, /* MIPIDSI SEQ1 (Sequence operation channel 1 interrupt) */
            [15] = mipi_dsi_vin1_isr, /* MIPIDSI VIN1 (Video-Input operation channel1 interrupt) */
            [16] = mipi_dsi_rcv_isr, /* MIPIDSI RCV (DSI packet receive interrupt) */
            [17] = mipi_dsi_ferr_isr, /* MIPIDSI FERR (DSI fatal error interrupt) */
            [18] = mipi_dsi_ppi_isr, /* MIPIDSI PPI (DSI D-PHY PPI interrupt) */
            [19] = canfd_error_isr, /* CAN0 CHERR (Channel  error) */
            [20] = canfd_channel_tx_isr, /* CAN0 TX (Transmit interrupt) */
            [21] = canfd_common_fifo_rx_isr, /* CAN0 COMFRX (Common FIFO receive interrupt) */
            [22] = canfd_error_isr, /* CAN GLERR (Global error) */
            [23] = canfd_rx_fifo_isr, /* CAN RXF (Global receive FIFO interrupt) */
            [24] = sci_b_uart_rxi_isr, /* SCI8 RXI (Receive data full) */
            [25] = sci_b_uart_txi_isr, /* SCI8 TXI (Transmit data empty) */
            [26] = sci_b_uart_tei_isr, /* SCI8 TEI (Transmit end) */
            [27] = sci_b_uart_eri_isr, /* SCI8 ERI (Receive error) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_CEU_CEUI,GROUP0), /* CEU CEUI (CEU interrupt) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_USBFS_INT,GROUP1), /* USBFS INT (USBFS interrupt) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_USBFS_RESUME,GROUP2), /* USBFS RESUME (USBFS resume interrupt) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_0,GROUP3), /* USBFS FIFO 0 (DMA/DTC transfer request 0) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_USBFS_FIFO_1,GROUP4), /* USBFS FIFO 1 (DMA/DTC transfer request 1) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_USBHS_USB_INT_RESUME,GROUP5), /* USBHS USB INT RESUME (USBHS interrupt) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_USBHS_FIFO_0,GROUP6), /* USBHS FIFO 0 (DMA transfer request 0) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_USBHS_FIFO_1,GROUP7), /* USBHS FIFO 1 (DMA transfer request 1) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_IIC0_RXI,GROUP0), /* IIC0 RXI (Receive data full) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_IIC0_TXI,GROUP1), /* IIC0 TXI (Transmit data empty) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_IIC0_TEI,GROUP2), /* IIC0 TEI (Transmit end) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_IIC0_ERI,GROUP3), /* IIC0 ERI (Transfer error) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_GLCDC_LINE_DETECT,GROUP4), /* GLCDC LINE DETECT (Specified line) */
            [13] = BSP_PRV_VECT_ENUM(EVENT_MIPIDSI_SEQ0,GROUP5), /* MIPIDSI SEQ0 (Sequence operation channel 0 interrupt) */
            [14] = BSP_PRV_VECT_ENUM(EVENT_MIPIDSI_SEQ1,GROUP6), /* MIPIDSI SEQ1 (Sequence operation channel 1 interrupt) */
            [15] = BSP_PRV_VECT_ENUM(EVENT_MIPIDSI_VIN1,GROUP7), /* MIPIDSI VIN1 (Video-Input operation channel1 interrupt) */
            [16] = BSP_PRV_VECT_ENUM(EVENT_MIPIDSI_RCV,GROUP0), /* MIPIDSI RCV (DSI packet receive interrupt) */
            [17] = BSP_PRV_VECT_ENUM(EVENT_MIPIDSI_FERR,GROUP1), /* MIPIDSI FERR (DSI fatal error interrupt) */
            [18] = BSP_PRV_VECT_ENUM(EVENT_MIPIDSI_PPI,GROUP2), /* MIPIDSI PPI (DSI D-PHY PPI interrupt) */
            [19] = BSP_PRV_VECT_ENUM(EVENT_CAN0_CHERR,GROUP3), /* CAN0 CHERR (Channel  error) */
            [20] = BSP_PRV_VECT_ENUM(EVENT_CAN0_TX,GROUP4), /* CAN0 TX (Transmit interrupt) */
            [21] = BSP_PRV_VECT_ENUM(EVENT_CAN0_COMFRX,GROUP5), /* CAN0 COMFRX (Common FIFO receive interrupt) */
            [22] = BSP_PRV_VECT_ENUM(EVENT_CAN_GLERR,GROUP6), /* CAN GLERR (Global error) */
            [23] = BSP_PRV_VECT_ENUM(EVENT_CAN_RXF,GROUP7), /* CAN RXF (Global receive FIFO interrupt) */
            [24] = BSP_PRV_VECT_ENUM(EVENT_SCI8_RXI,GROUP0), /* SCI8 RXI (Receive data full) */
            [25] = BSP_PRV_VECT_ENUM(EVENT_SCI8_TXI,GROUP1), /* SCI8 TXI (Transmit data empty) */
            [26] = BSP_PRV_VECT_ENUM(EVENT_SCI8_TEI,GROUP2), /* SCI8 TEI (Transmit end) */
            [27] = BSP_PRV_VECT_ENUM(EVENT_SCI8_ERI,GROUP3), /* SCI8 ERI (Receive error) */
        };
        #endif
        #endif