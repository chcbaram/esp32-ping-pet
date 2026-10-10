#ifndef HW_DEF_H_
#define HW_DEF_H_



#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V261010R1"
#define _DEF_BOARD_NAME           "PING-PET"


#define _USE_HW_RTOS
#define _USE_HW_USB
#define _USE_HW_CDC
#define _USE_HW_NVS


//-- LCD : ST7701S 480x800 MIPI-DSI, 화면은 가로 800x480 으로 돌려 쓴다 (docs/22)
//
#define _USE_HW_LCD
#define _USE_HW_ST7701
#define      HW_LCD_PANEL_WIDTH     480
#define      HW_LCD_PANEL_HEIGHT    800
#define      HW_LCD_WIDTH           800       // 화면 (회전 뒤)
#define      HW_LCD_HEIGHT          480
#define      HW_LCD_ROTATION        270       // 0, 90, 270
#define      HW_LCD_DSI_LANES       2
#define      HW_LCD_DSI_MBPS        500
#define      HW_LCD_DPI_MHZ         34
#define      HW_LCD_LDO_CHAN        3         // DSI PHY 전원
#define      HW_LCD_LDO_MV          2500
#define      HW_LCD_PIN_RST         5
#define      HW_LCD_PIN_BL          23        // MP3202 부스트, LEDC PWM
#define      HW_LCD_BL_FREQ_HZ      25000     // 5 kHz 에서 코일 소음 보고 (docs/22 #9)

//-- LVGL 9.6 (컴포넌트 lvgl/lvgl, 설정은 sdkconfig.defaults)
//
#define _USE_HW_LVGL

//-- Touch : GT911
//
#define _USE_HW_TOUCH
#define      HW_TOUCH_MAX_CH        5
#define _USE_HW_GT911
#define      HW_GT911_PIN_RST       3         // INT 는 연결 안 됨 (폴링)

#define _USE_HW_I2C
#define      HW_I2C_MAX_CH          1
#define      HW_I2C_CH_TOUCH        _DEF_I2C1
#define      HW_I2C_CH1_SDA         7         // GT911 + ES8311
#define      HW_I2C_CH1_SCL         8


#define _USE_HW_UART
#define      HW_UART_MAX_CH         2
#define      HW_UART_CH_DEBUG       _DEF_UART1    // UART0 (GPIO37 TX / GPIO38 RX), IDF 콘솔과 같은 포트
#define      HW_UART_CH_USB         _DEF_UART2    // USB CDC (uartSetDriver 로 꽂는다)
#define      HW_UART_CH_CLI         HW_UART_CH_DEBUG

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_DEBUG
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    4096


//-- CLI
//
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_MODULE          1
#define _USE_CLI_HW_USB             1
#define _USE_CLI_HW_LCD             1
#define _USE_CLI_HW_TOUCH           1
#define _USE_CLI_HW_GT911           1
#define _USE_CLI_HW_I2C             1
#define _USE_CLI_HW_LVGL            1
#define _USE_CLI_HW_NVS             1


#endif
