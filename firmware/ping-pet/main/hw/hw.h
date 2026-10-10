#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#include "uart.h"
#include "cli.h"
#include "log.h"
#include "usb.h"
#include "cdc.h"
#include "i2c.h"
#include "lcd.h"
#include "touch.h"
#include "lvgl.h"
#include "qbuffer.h"
#include "util_core.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif
