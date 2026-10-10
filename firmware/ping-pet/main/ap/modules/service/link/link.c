#include "module.h"
#include "inbox.h"
#include "ota/ota.h"
#include "cJSON.h"
#include "mbedtls/base64.h"


/* 맥 중계 링크 — USB CDC 위의 줄 단위 JSON (인계 문서 "USB 프로토콜")
 *
 * 호스트가 CDC 를 115200 이 아닌 보율로 열면 이 모듈이 CDC 를 읽는다 (CDC 주인 판정, docs/27 3 절).
 * 그때 CLI 와 로그는 UART0 에 남는다.
 *
 *   맥 → 보드   sync {items[], now, tz, stats}   목록 전체 교체
 *               add  {item}                       추가 · 갱신 (같은 id)
 *               done {id, waited}                 답장 감지 → 냠냠
 *               remove {id}                       조용히 제거
 *               tick {now, tz, stats}             시계 · 하트비트 (60 초)
 *               hello {}                          연결 직후. 보드는 hello 로 답한다
 *                                                 (보드가 먼저 보내는 hello 는 맥 OS 가 포트를 여는 사이에 사라질 수 있다)
 *               ota_begin {target: app | fonts, size, sha256, version}
 *               ota_data {off, data}              data 는 base64 (원본 OTA_CHUNK_MAX 이하)
 *               ota_end {} / ota_abort {}
 *   보드 → 맥   hello {fw, ota: 1}                연결 직후. 맥은 sync 로 답한다. ota : USB OTA 를 받는다
 *               open {id}                         카드 탭
 *               ack {id}                          카드 길게 누름 → 치우기
 *               wip {id} / unwip {id}             카드 길게 누름 → 처리 중 / 해제
 *               sync {}                           목록 전체를 다시 청한다 (데모를 끈 뒤). 맥은 sync 로 답한다
 *               ota_ready {} / ota_ack {off} / ota_done {} / ota_error {msg}
 *                                                 OTA 는 한 조각씩 : 맥은 ota_ack 를 받고 다음 조각을 보낸다
 *
 *   item  : {id, kind: dm | mpim | mention, from, where, text, ts, wip}   wip : 처리 중 (없으면 0)
 *   stats : {handled_today, avg_wait_min, recent: [{kind, from, where, waited, at}]}
 *   인계 문서에 더한 것 : tz (초), recent 의 kind · at
 */


#define LINK_LINE_MAX          4096
#define OTA_CHUNK_MAX          2048     // base64 로 2732 B. 한 줄과 CDC 수신 큐(4 KB)에 들어간다


static bool linkInit(void);
static void linkUpdate(void const *arg);
static void linkSendRequest(const char *type, const char *id);
static void cliLink(cli_args_t *args);


static char     line_buf[LINK_LINE_MAX + 1];
static uint32_t line_len = 0;
static bool     is_active = false;      // CDC 가 cmd 쪽으로 열려 있다
static uint32_t rx_lines = 0;
static uint32_t rx_errors = 0;
static uint32_t tx_lines = 0;
static char     last_type[16] = "-";
static SemaphoreHandle_t tx_lock = NULL;   // open · ack 은 ui 태스크, hello 는 메인 태스크에서 보낸다


MODULE_DEF(link)
{
  .name     = "link",
  .priority = MODULE_PRI_NORMAL,
  .init     = linkInit,
  .update   = linkUpdate,
};




bool linkInit(void)
{
  tx_lock = xSemaphoreCreateMutex();
  inboxSetRequestFunc(linkSendRequest);
  cliAdd("link", cliLink);
  return true;
}


//-- 보내기
//
static void linkSendJson(cJSON *p_json)
{
  char *str;

  if (is_active != true)
    return;

  str = cJSON_PrintUnformatted(p_json);
  if (str != NULL)
  {
    xSemaphoreTake(tx_lock, portMAX_DELAY);     // 한 줄이 다른 줄과 섞이지 않게
    cdcWrite((uint8_t *)str, strlen(str));
    cdcWrite((uint8_t *)"\n", 1);
    tx_lines++;
    xSemaphoreGive(tx_lock);
    cJSON_free(str);
  }
}

static void linkSendHello(void)
{
  cJSON *p_json = cJSON_CreateObject();

  cJSON_AddStringToObject(p_json, "type", "hello");
  cJSON_AddStringToObject(p_json, "fw", _DEF_FIRMWATRE_VERSION);
  cJSON_AddNumberToObject(p_json, "ota", 1);
  linkSendJson(p_json);
  cJSON_Delete(p_json);
}

/* inbox 가 부른다 (카드 탭 · 길게 누름). ui 태스크에서 불리지만 cdcWrite 는 어느 태스크든 된다 */
void linkSendRequest(const char *type, const char *id)
{
  cJSON *p_json = cJSON_CreateObject();

  cJSON_AddStringToObject(p_json, "type", type);
  cJSON_AddStringToObject(p_json, "id", id);
  linkSendJson(p_json);
  cJSON_Delete(p_json);
}


static void linkSendOta(const char *type, int32_t off, const char *msg)
{
  cJSON *p_json = cJSON_CreateObject();

  cJSON_AddStringToObject(p_json, "type", type);
  if (off >= 0)
    cJSON_AddNumberToObject(p_json, "off", off);
  if (msg != NULL)
    cJSON_AddStringToObject(p_json, "msg", msg);
  linkSendJson(p_json);
  cJSON_Delete(p_json);
}


//-- 받기
//
static const char *jsonStr(const cJSON *p_obj, const char *key)
{
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(p_obj, key);

  return cJSON_IsString(p) ? p->valuestring : "";
}

static uint32_t jsonU32(const cJSON *p_obj, const char *key)
{
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(p_obj, key);

  return cJSON_IsNumber(p) && p->valuedouble > 0 ? (uint32_t)p->valuedouble : 0;
}

static int32_t jsonI32(const cJSON *p_obj, const char *key, int32_t def)
{
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(p_obj, key);

  return cJSON_IsNumber(p) ? (int32_t)p->valuedouble : def;
}

static uint8_t jsonKind(const cJSON *p_obj)
{
  const char *kind = jsonStr(p_obj, "kind");

  if (strcmp(kind, "mpim") == 0)    return INBOX_KIND_MPIM;
  if (strcmp(kind, "mention") == 0) return INBOX_KIND_MENTION;
  return INBOX_KIND_DM;
}

static void parseItem(const cJSON *p_obj, inbox_item_t *p_item)
{
  memset(p_item, 0, sizeof(*p_item));
  strncpy(p_item->id,    jsonStr(p_obj, "id"),    sizeof(p_item->id) - 1);
  strncpy(p_item->from,  jsonStr(p_obj, "from"),  sizeof(p_item->from) - 1);
  strncpy(p_item->where, jsonStr(p_obj, "where"), sizeof(p_item->where) - 1);
  strncpy(p_item->text,  jsonStr(p_obj, "text"),  sizeof(p_item->text) - 1);
  p_item->kind = jsonKind(p_obj);
  p_item->ts   = jsonU32(p_obj, "ts");
  p_item->wip  = jsonU32(p_obj, "wip") ? 1 : 0;
}

static bool parseStats(const cJSON *p_obj, inbox_stats_t *p_stats)
{
  const cJSON *p_recent;
  const cJSON *p_ent;

  if (cJSON_IsObject(p_obj) != true)
    return false;

  memset(p_stats, 0, sizeof(*p_stats));
  p_stats->handled      = jsonU32(p_obj, "handled_today");
  p_stats->avg_wait_min = jsonU32(p_obj, "avg_wait_min");

  p_recent = cJSON_GetObjectItemCaseSensitive(p_obj, "recent");
  cJSON_ArrayForEach(p_ent, p_recent)
  {
    inbox_recent_t *p_r;

    if (p_stats->recent_cnt >= INBOX_RECENT_MAX)
      break;
    p_r = &p_stats->recent[p_stats->recent_cnt++];
    p_r->kind = jsonKind(p_ent);
    strncpy(p_r->from,  jsonStr(p_ent, "from"),  sizeof(p_r->from) - 1);
    strncpy(p_r->where, jsonStr(p_ent, "where"), sizeof(p_r->where) - 1);
    p_r->waited = jsonU32(p_ent, "waited");
    p_r->at     = jsonU32(p_ent, "at");
  }
  return true;
}

static void linkHandleOta(const char *type, const cJSON *p_json)
{
  static uint8_t data[OTA_CHUNK_MAX];
  char           err[64];

  if (strcmp(type, "ota_begin") == 0)
  {
    const char  *target = jsonStr(p_json, "target");
    ota_target_t t = strcmp(target, "fonts") == 0 ? OTA_TARGET_FONTS : OTA_TARGET_APP;

    if (otaBegin(t, jsonU32(p_json, "size"), jsonStr(p_json, "sha256"), jsonStr(p_json, "version"), err, sizeof(err)))
      linkSendOta("ota_ready", -1, NULL);
    else
      linkSendOta("ota_error", -1, err);
  }
  else if (strcmp(type, "ota_data") == 0)
  {
    const char *b64 = jsonStr(p_json, "data");
    uint32_t    off = jsonU32(p_json, "off");
    size_t      len = 0;

    if (mbedtls_base64_decode(data, sizeof(data), &len, (const uint8_t *)b64, strlen(b64)) != 0)
    {
      otaAbort("데이터가 깨짐 (base64)");
      linkSendOta("ota_error", -1, "base64");
      return;
    }
    if (otaWrite(off, data, len, err, sizeof(err)))
      linkSendOta("ota_ack", (int32_t)(off + len), NULL);
    else
      linkSendOta("ota_error", -1, err);
  }
  else if (strcmp(type, "ota_end") == 0)
  {
    if (otaEnd(err, sizeof(err)))
      linkSendOta("ota_done", -1, NULL);
    else
      linkSendOta("ota_error", -1, err);
  }
  else if (strcmp(type, "ota_abort") == 0)
  {
    otaAbort("중계가 그만둠");
  }
}

static void linkHandleLine(const char *line)
{
  cJSON         *p_json;
  const char    *type;
  inbox_stats_t  stats;
  bool           has_stats;

  p_json = cJSON_Parse(line);
  if (p_json == NULL)
  {
    rx_errors++;
    return;
  }
  rx_lines++;

  type = jsonStr(p_json, "type");
  strncpy(last_type, type, sizeof(last_type) - 1);
  has_stats = parseStats(cJSON_GetObjectItemCaseSensitive(p_json, "stats"), &stats);

  if (strcmp(type, "sync") == 0)
  {
    // 중계가 진짜 목록을 보내면 데모는 끈다. 포트를 열기만 해서는 끄지 않는다
    // (맥 OS 는 포트를 열 때 잠깐 다른 보율을 거치므로 캡처 · 터미널도 링크로 보일 수 있다)
    static inbox_item_t items[INBOX_ITEM_MAX];     // 큰 배열이라 스택에 두지 않는다
    const cJSON *p_items = cJSON_GetObjectItemCaseSensitive(p_json, "items");
    const cJSON *p_ent;
    uint8_t      cnt = 0;

    cJSON_ArrayForEach(p_ent, p_items)
    {
      if (cnt >= INBOX_ITEM_MAX) break;
      parseItem(p_ent, &items[cnt++]);
    }
    inboxDemoSet(false);
    inboxSync(items, cnt, jsonU32(p_json, "now"), jsonI32(p_json, "tz", 9 * 3600), has_stats ? &stats : NULL);
  }
  else if (strcmp(type, "add") == 0)
  {
    inbox_item_t item;

    parseItem(cJSON_GetObjectItemCaseSensitive(p_json, "item"), &item);
    if (item.id[0] != 0)
      inboxAdd(&item);
  }
  else if (strcmp(type, "done") == 0)
  {
    inboxDone(jsonStr(p_json, "id"), jsonU32(p_json, "waited"));
  }
  else if (strcmp(type, "remove") == 0)
  {
    inboxRemove(jsonStr(p_json, "id"));
  }
  else if (strcmp(type, "hello") == 0)
  {
    linkSendHello();
  }
  else if (strncmp(type, "ota_", 4) == 0)
  {
    linkHandleOta(type, p_json);
  }
  else if (strcmp(type, "tick") == 0)
  {
    inboxTick(jsonU32(p_json, "now"), jsonI32(p_json, "tz", 9 * 3600), has_stats ? &stats : NULL);
  }

  cJSON_Delete(p_json);
}

void linkUpdate(void const *arg)
{
  bool active;

  (void)arg;

  // 호스트가 115200 이 아닌 보율로 열었으면 cmd 쪽이다 (115200 은 CLI)
  active = cdcIsConnect() && usbGetType() == USB_CON_CDC;
  if (active != is_active)
  {
    is_active = active;
    line_len  = 0;
    logPrintf("[  ] link %s\n", active ? "open" : "close");
    if (active)
      linkSendHello();
    else
      otaAbort("링크가 끊김");
  }
  if (is_active != true)
    return;

  while (cdcAvailable() > 0)
  {
    char c = (char)cdcRead();

    if (c == '\n' || c == '\r')
    {
      if (line_len > 0)
      {
        line_buf[line_len] = 0;
        linkHandleLine(line_buf);
        line_len = 0;
      }
    }
    else if (line_len < LINK_LINE_MAX)
    {
      line_buf[line_len++] = c;
    }
    else
    {
      // 4 KB 를 넘는 줄은 버린다 (프로토콜 위반)
      line_len = 0;
      rx_errors++;
    }
  }
}


void cliLink(cli_args_t *args)
{
  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("active    : %s (cdc %s, baud %d)\n", is_active ? "yes" : "no",
              cdcIsConnect() ? "open" : "closed", (int)cdcGetBaud());
    cliPrintf("rx lines  : %d, errors %d, last %s\n", (int)rx_lines, (int)rx_errors, last_type);
    cliPrintf("tx lines  : %d\n", (int)tx_lines);
    return;
  }
  cliPrintf("link info\n");
}
