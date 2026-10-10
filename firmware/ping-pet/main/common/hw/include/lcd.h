#ifndef LCD_H_
#define LCD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_LCD


// 화면 좌표 (회전한 뒤, 가로 800x480). 패널 원래 방향은 HW_LCD_PANEL_WIDTH x HW_LCD_PANEL_HEIGHT (세로 480x800)
#define LCD_WIDTH         HW_LCD_WIDTH
#define LCD_HEIGHT        HW_LCD_HEIGHT

#define GETR(c) (((uint16_t)(c)) >> 11)
#define GETG(c) (((c) & 0x07E0)>>5)
#define GETB(c) ((c) & 0x1F)
#define RGB2COLOR(r, g, b) ((((r>>3)<<11) | ((g>>2)<<5) | (b>>3)))


enum class_color {
 white     = 0xFFFF,
 gray      = 0x8410,
 darkgray  = 0xAD55,
 black     = 0x0000,
 purple    = 0x8010,
 pink      = 0xFE19,
 red       = 0xF800,
 orange    = 0xFD20,
 brown     = 0xA145,
 beige     = 0xF7BB,
 yellow    = 0xFFE0,
 lightgreen= 0x9772,
 green     = 0x07E0,
 darkblue  = 0x0011,
 blue      = 0x001F,
 lightblue = 0xAEDC,
};


bool lcdInit(void);
bool lcdIsInit(void);

uint8_t lcdGetBackLight(void);
void    lcdSetBackLight(uint8_t value);      // 0 ~ 100 %

void lcdDisplayOff(void);
void lcdDisplayOn(void);

int32_t lcdGetWidth(void);
int32_t lcdGetHeight(void);

uint16_t *lcdGetFrameBuffer(void);           // 다음에 그릴 버퍼 (패널 방향)
uint16_t *lcdGetCurrentFrameBuffer(void);    // 지금 표시 중인 버퍼 (패널 방향)
void      lcdUpdateDraw(void);               // 그린 버퍼를 표시하고 다음 버퍼를 준비한다
bool      lcdDrawScreen(const uint16_t *p_screen);  // 화면 버퍼(가로)를 PPA 로 돌려 표시한다 (LVGL)
bool      lcdDrawScreenArea(const uint16_t *p_screen, int16_t x, int16_t y, int16_t w, int16_t h);

void lcdClear(uint32_t rgb_code);
void lcdDrawPixel(int16_t x_pos, int16_t y_pos, uint32_t rgb_code);
void lcdDrawFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void lcdDrawFillScreen(uint16_t color);
void lcdDrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

#endif

#ifdef __cplusplus
}
#endif

#endif
