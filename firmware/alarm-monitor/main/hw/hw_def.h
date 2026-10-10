#ifndef HW_DEF_H_
#define HW_DEF_H_


#include "def.h"
#include "bsp.h"



#define _DEF_FIRMWATRE_VERSION    "V261010R1"
#define _DEF_BOARD_NAME           "ALARM-MONITOR"



#define _USE_HW_RTOS
#define _USE_HW_CDC

#define _USE_HW_UART
#define      HW_UART_MAX_CH         2
#define      HW_UART_CH_DEBUG       _DEF_UART1    // UART0
#define      HW_UART_CH_USB         _DEF_UART2    // USB CDC (HS OTG)
#define      HW_UART_CH_CLI         HW_UART_CH_USB

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    128

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_USB
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    2048


#endif 
