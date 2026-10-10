#ifndef LV_PORT_DISP_H_
#define LV_PORT_DISP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"


void lv_port_disp_init(void);
void disp_enable_update(void);
void disp_disable_update(void);
void disp_set_flush_full(bool enable);
bool disp_get_flush_full(void);


#ifdef __cplusplus
}
#endif

#endif
