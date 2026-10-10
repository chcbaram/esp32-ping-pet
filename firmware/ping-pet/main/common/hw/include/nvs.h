#ifndef NVS_H_
#define NVS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_NVS


/* 작은 설정 값 저장 (ESP-IDF NVS, nvs 파티션). 이름은 HG-T113-S3 의 nvsGet / nvsSet 과 같다.
 * 플래시에 쓰는 동안 화면이 잠깐 번쩍일 수 있다 (docs/22 알려진 문제 #4). 설정을 바꿀 때만 쓴다. */

bool nvsInit(void);
bool nvsGet(const char *key, void *p_data, uint32_t length);
bool nvsSet(const char *key, const void *p_data, uint32_t length);


#endif

#ifdef __cplusplus
}
#endif

#endif
