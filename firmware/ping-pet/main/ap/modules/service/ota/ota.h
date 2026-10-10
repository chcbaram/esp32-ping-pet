#ifndef OTA_H_
#define OTA_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


/* USB OTA — 중계가 링크(link.c)로 보낸 이미지를 돌고 있는 펌웨어가 받아 쓴다.
 *
 *   app   : 지금 돌지 않는 앱 자리(ota_0 / ota_1)에 쓰고, 다 받으면 그쪽으로 부팅을 바꿔 다시 시작한다.
 *           새 펌웨어가 OTA_VALID_MS 동안 돌면 정상으로 표시한다. 그 전에 리셋되면 부트로더가 이전 펌웨어로 되돌린다.
 *   fonts : 폰트 파티션을 지우며 쓰고 다시 시작한다. 쓰는 동안 화면은 폰트 파티션을 쓰지 않는다 (update 앱).
 *
 * 쓰기는 링크를 읽는 메인 태스크에서만 부른다. 화면(ui 태스크)은 otaGetInfo() 스냅숏만 읽는다.
 */

#define OTA_DATA_TIMEOUT_MS   10000       // 이만큼 데이터가 없으면 그만둔다
#define OTA_RESTART_MS        1500        // 다 받은 뒤 다시 시작하기까지 (화면에 "다시 시작" 을 보여 줄 시간)
#define OTA_FAIL_SHOW_MS      6000        // 실패를 화면에 보여 주는 시간
#define OTA_VALID_MS          10000       // 새 펌웨어를 정상으로 표시하기까지


typedef enum
{
  OTA_STATE_IDLE = 0,
  OTA_STATE_RUN,          // 받는 중
  OTA_STATE_DONE,         // 다 받음 → 곧 다시 시작
  OTA_STATE_FAIL,         // 실패 → 잠시 보여 주고 IDLE (폰트를 지우기 시작했으면 다시 시작)
} ota_state_t;

typedef enum
{
  OTA_TARGET_APP = 0,
  OTA_TARGET_FONTS,
} ota_target_t;

typedef struct
{
  ota_state_t  state;
  ota_target_t target;
  uint32_t     size;
  uint32_t     done;
  char         version[32];
  char         error[64];
} ota_info_t;


bool otaBegin(ota_target_t target, uint32_t size, const char *sha256_hex, const char *version,
              char *err, size_t err_size);
bool otaWrite(uint32_t offset, const uint8_t *p_data, uint32_t length, char *err, size_t err_size);
bool otaEnd(char *err, size_t err_size);
void otaAbort(const char *reason);

void otaGetInfo(ota_info_t *p_info);
bool otaIsActive(void);             // RUN · DONE · FAIL (화면을 update 앱으로)


#ifdef __cplusplus
}
#endif

#endif
