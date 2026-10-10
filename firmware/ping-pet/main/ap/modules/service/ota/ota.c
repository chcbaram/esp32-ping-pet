// IDF 헤더를 먼저 : esp_rom 의 ets_sys.h 가 enum 에 OK 를 쓰는데 common/err_code.h 가 OK 를 매크로로 둔다
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "module.h"
#include "ota/ota.h"


/* USB OTA (ota.h). 링크가 ota_begin → ota_data … → ota_end 를 차례로 부른다. */


#define lock()          xSemaphoreTake(mutex_lock, portMAX_DELAY)
#define unLock()        xSemaphoreGive(mutex_lock)

#define FONTS_SUBTYPE   0x40
#define FONTS_MAGIC_LEN 4               // 'PPFT' (tools/fonts.py)
#define ERASE_BLOCK     (64 * 1024)


static bool otaInit(void);
static void otaUpdate(void const *arg);
static void cliOta(cli_args_t *args);


static SemaphoreHandle_t      mutex_lock = NULL;
static ota_info_t             info;
static const esp_partition_t *part = NULL;
static esp_ota_handle_t       ota_handle = 0;
static mbedtls_sha256_context sha_ctx;
static uint8_t                sha_expect[32];
static uint32_t               erased_to = 0;      // fonts : 여기까지 지웠다
static uint8_t                fonts_magic[FONTS_MAGIC_LEN];
static uint32_t               state_ms = 0;       // 마지막 데이터 / 상태가 바뀐 때
static bool                   is_valid_marked = false;

static const char *state_str[]  = {"idle", "run", "done", "fail"};
static const char *target_str[] = {"app", "fonts"};


MODULE_DEF(ota)
{
  .name     = "ota",
  .priority = MODULE_PRI_NORMAL,
  .init     = otaInit,
  .update   = otaUpdate,
};




bool otaInit(void)
{
  mutex_lock = xSemaphoreCreateMutex();
  memset(&info, 0, sizeof(info));
  cliAdd("ota", cliOta);
  return true;
}

static void setState(ota_state_t state)
{
  lock();
  info.state = state;
  unLock();
  state_ms = millis();
}

static void setError(const char *msg, char *err, size_t err_size)
{
  lock();
  strncpy(info.error, msg, sizeof(info.error) - 1);
  info.error[sizeof(info.error) - 1] = 0;
  unLock();
  if (err != NULL && err_size > 0)
  {
    strncpy(err, msg, err_size - 1);
    err[err_size - 1] = 0;
  }
}

static bool hexToBytes(const char *hex, uint8_t *p_out, size_t len)
{
  if (hex == NULL || strlen(hex) != len * 2)
    return false;
  for (size_t i=0; i<len; i++)
  {
    unsigned int v;
    if (sscanf(&hex[i * 2], "%2x", &v) != 1)
      return false;
    p_out[i] = (uint8_t)v;
  }
  return true;
}


bool otaBegin(ota_target_t target, uint32_t size, const char *sha256_hex, const char *version,
              char *err, size_t err_size)
{
  esp_err_t ret;

  if (info.state == OTA_STATE_RUN)
    otaAbort("새 업데이트로 바뀜");

  lock();
  memset(&info, 0, sizeof(info));
  info.target = target;
  info.size   = size;
  strncpy(info.version, version != NULL ? version : "", sizeof(info.version) - 1);
  unLock();

  if (hexToBytes(sha256_hex, sha_expect, sizeof(sha_expect)) != true)
  {
    setError("sha256 형식이 틀림", err, err_size);
    setState(OTA_STATE_FAIL);
    return false;
  }

  if (target == OTA_TARGET_APP)
    part = esp_ota_get_next_update_partition(NULL);
  else
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, FONTS_SUBTYPE, "fonts");

  if (part == NULL)
  {
    setError(target == OTA_TARGET_APP ? "OTA 앱 자리가 없음 (파티션 표)" : "폰트 파티션이 없음", err, err_size);
    setState(OTA_STATE_FAIL);
    return false;
  }
  if (size == 0 || size > part->size)
  {
    setError("크기가 파티션보다 큼", err, err_size);
    setState(OTA_STATE_FAIL);
    return false;
  }

  if (target == OTA_TARGET_APP)
  {
    // 순서대로 쓰면서 그때그때 지운다 (처음에 몇 MB 를 지우느라 멈추지 않게)
    ret = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (ret != ESP_OK)
    {
      setError(esp_err_to_name(ret), err, err_size);
      setState(OTA_STATE_FAIL);
      return false;
    }
  }
  erased_to = 0;

  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts(&sha_ctx, 0);

  logPrintf("[  ] ota begin %s %d B -> %s (%s)\n", target_str[target], (int)size, part->label, info.version);
  setState(OTA_STATE_RUN);
  return true;
}

bool otaWrite(uint32_t offset, const uint8_t *p_data, uint32_t length, char *err, size_t err_size)
{
  esp_err_t ret = ESP_OK;

  if (info.state != OTA_STATE_RUN)
  {
    setError("업데이트 중이 아님", err, err_size);
    return false;
  }
  if (offset != info.done || offset + length > info.size)
  {
    otaAbort("데이터 순서가 틀림");
    setError("데이터 순서가 틀림", err, err_size);
    return false;
  }

  if (info.target == OTA_TARGET_APP)
  {
    ret = esp_ota_write(ota_handle, p_data, length);
  }
  else
  {
    while (ret == ESP_OK && erased_to < offset + length)
    {
      uint32_t len = MIN(ERASE_BLOCK, part->size - erased_to);
      ret = esp_partition_erase_range(part, erased_to, len);
      erased_to += len;
    }
    // 머리의 매직('PPFT')은 다 받고 확인한 뒤에 쓴다. 중간에 끊기면 매직이 없어
    // 다음 부팅에서 반쯤 쓴 폰트를 쓰지 않고 내장 폰트로 뜬다 (ui_theme.c)
    if (ret == ESP_OK && offset == 0 && length >= FONTS_MAGIC_LEN)
    {
      uint8_t blank[FONTS_MAGIC_LEN];

      memcpy(fonts_magic, p_data, FONTS_MAGIC_LEN);
      memset(blank, 0xFF, sizeof(blank));
      ret = esp_partition_write(part, 0, blank, FONTS_MAGIC_LEN);
      if (ret == ESP_OK)
        ret = esp_partition_write(part, FONTS_MAGIC_LEN, p_data + FONTS_MAGIC_LEN, length - FONTS_MAGIC_LEN);
    }
    else if (ret == ESP_OK)
    {
      ret = esp_partition_write(part, offset, p_data, length);
    }
  }
  if (ret != ESP_OK)
  {
    otaAbort(esp_err_to_name(ret));
    setError(esp_err_to_name(ret), err, err_size);
    return false;
  }

  mbedtls_sha256_update(&sha_ctx, p_data, length);
  lock();
  info.done += length;
  unLock();
  state_ms = millis();
  return true;
}

bool otaEnd(char *err, size_t err_size)
{
  uint8_t   sha[32];
  esp_err_t ret;

  if (info.state != OTA_STATE_RUN)
  {
    setError("업데이트 중이 아님", err, err_size);
    return false;
  }
  if (info.done != info.size)
  {
    otaAbort("덜 받음");
    setError("덜 받음", err, err_size);
    return false;
  }

  mbedtls_sha256_finish(&sha_ctx, sha);
  mbedtls_sha256_free(&sha_ctx);
  if (memcmp(sha, sha_expect, sizeof(sha)) != 0)
  {
    otaAbort("sha256 이 다름");
    setError("sha256 이 다름", err, err_size);
    return false;
  }

  if (info.target == OTA_TARGET_APP)
  {
    ret = esp_ota_end(ota_handle);           // 이미지 머리 · 체크섬 확인
    ota_handle = 0;
    if (ret == ESP_OK)
      ret = esp_ota_set_boot_partition(part);
    if (ret != ESP_OK)
    {
      setError(ret == ESP_ERR_OTA_VALIDATE_FAILED ? "펌웨어 이미지가 아님" : esp_err_to_name(ret), err, err_size);
      setState(OTA_STATE_FAIL);
      return false;
    }
  }
  else
  {
    // 0xFF 로 남겨 둔 자리에 매직을 쓴다 (지우지 않고 1 → 0 으로만 쓰므로 된다)
    ret = esp_partition_write(part, 0, fonts_magic, FONTS_MAGIC_LEN);
    if (ret != ESP_OK)
    {
      setError(esp_err_to_name(ret), err, err_size);
      setState(OTA_STATE_FAIL);
      return false;
    }
  }

  logPrintf("[  ] ota done %s, restart\n", target_str[info.target]);
  setState(OTA_STATE_DONE);
  return true;
}

void otaAbort(const char *reason)
{
  if (info.state != OTA_STATE_RUN)
    return;

  if (info.target == OTA_TARGET_APP && ota_handle != 0)
  {
    esp_ota_abort(ota_handle);
    ota_handle = 0;
  }
  mbedtls_sha256_free(&sha_ctx);
  setError(reason, NULL, 0);
  logPrintf("[E_] ota abort : %s\n", reason);
  setState(OTA_STATE_FAIL);
}

void otaGetInfo(ota_info_t *p_info)
{
  lock();
  *p_info = info;
  unLock();
}

bool otaIsActive(void)
{
  return info.state != OTA_STATE_IDLE;
}


void otaUpdate(void const *arg)
{
  (void)arg;

  // 새 펌웨어가 OTA_VALID_MS 동안 돌았으면 정상으로 표시한다 (롤백 취소)
  if (is_valid_marked != true && millis() >= OTA_VALID_MS)
  {
    esp_ota_img_states_t st;

    is_valid_marked = true;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY)
    {
      esp_ota_mark_app_valid_cancel_rollback();
      logPrintf("[  ] ota : new firmware marked valid\n");
    }
  }

  switch (info.state)
  {
    case OTA_STATE_RUN:
      if (millis() - state_ms >= OTA_DATA_TIMEOUT_MS)
        otaAbort("시간 초과 (중계가 멈춤)");
      break;

    case OTA_STATE_DONE:
      if (millis() - state_ms >= OTA_RESTART_MS)
        esp_restart();
      break;

    case OTA_STATE_FAIL:
      if (millis() - state_ms >= OTA_FAIL_SHOW_MS)
      {
        // 폰트를 지우기 시작했으면 지금 화면이 쓰는 폰트 데이터가 깨졌다. 다시 시작해 내장 폰트로 뜬다
        if (info.target == OTA_TARGET_FONTS && erased_to > 0)
          esp_restart();
        setState(OTA_STATE_IDLE);
      }
      break;

    default:
      break;
  }
}


void cliOta(cli_args_t *args)
{
  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const esp_partition_t *run  = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t   st;
    ota_info_t             i;

    otaGetInfo(&i);
    cliPrintf("running  : %s (0x%06X)", run->label, (int)run->address);
    if (esp_ota_get_state_partition(run, &st) == ESP_OK)
      cliPrintf(", state %d", (int)st);
    cliPrintf("\n");
    cliPrintf("next     : %s\n", next != NULL ? next->label : "-");
    cliPrintf("ota      : %s %s %d / %d B %s\n", state_str[i.state], target_str[i.target],
              (int)i.done, (int)i.size, i.version);
    if (i.error[0] != 0)
      cliPrintf("error    : %s\n", i.error);
    return;
  }
  cliPrintf("ota info\n");
}
