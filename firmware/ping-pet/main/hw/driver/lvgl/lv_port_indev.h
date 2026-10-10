#ifndef LV_PORT_INDEV_H_
#define LV_PORT_INDEV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"


void        lv_port_indev_init(void);
lv_indev_t *lv_port_indev_get_touch(void);


#ifdef __cplusplus
}
#endif

#endif
