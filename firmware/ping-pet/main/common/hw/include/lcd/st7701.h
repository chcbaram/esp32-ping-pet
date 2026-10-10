#ifndef ST7701_H_
#define ST7701_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_ST7701
#include "esp_lcd_panel_io.h"


// MIPI-DSI DBI 채널로 초기화 테이블을 보낸다 (SLPOUT, DISPON 포함)
bool st7701InitRegs(esp_lcd_panel_io_handle_t io);
bool st7701DisplayOn(esp_lcd_panel_io_handle_t io, bool on);


#endif

#ifdef __cplusplus
}
#endif

#endif
