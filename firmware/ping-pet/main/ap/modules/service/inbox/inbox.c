#include "inbox.h"
#include "module.h"


#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY)
#define unLock()    xSemaphoreGive(mutex_lock)


static bool inboxModuleInit(void);
static void inboxModuleUpdate(void const *arg);
static void cliInbox(cli_args_t *args);
void        inboxDemoUpdate(void);
void        inboxDemoCli(cli_args_t *args);


static SemaphoreHandle_t mutex_lock = NULL;

static inbox_item_t  items[INBOX_ITEM_MAX];
static uint8_t       item_cnt = 0;
static inbox_stats_t stats;
static uint32_t      version = 0;

static bool          time_valid = false;
static uint32_t      time_base = 0;        // 마지막으로 받은 유닉스 초
static uint32_t      time_base_ms = 0;     // 그때의 millis()
static int32_t       time_tz = 9 * 3600;   // 맥이 보내지 않으면 KST

static bool          link_ever = false;
static uint32_t      link_ms = 0;          // 마지막 sync / tick
static bool          demo_on = false;

static bool          eat_on = false;
static uint32_t      eat_ms = 0;
static inbox_item_t  eat_item;
static uint32_t      eat_waited;
static uint8_t       eat_full_from;
static uint8_t       eat_full_to;

static void        (*request_func)(const char *type, const char *id) = NULL;


MODULE_DEF(inbox)
{
  .name     = "inbox",
  .priority = MODULE_PRI_4,          // ui(NORMAL) 보다 먼저
  .init     = inboxModuleInit,
  .update   = inboxModuleUpdate,
};




bool inboxModuleInit(void)
{
  return inboxInit();
}

void inboxModuleUpdate(void const *arg)
{
  (void)arg;
  inboxDemoUpdate();
}

bool inboxInit(void)
{
  if (mutex_lock == NULL)
  {
    mutex_lock = xSemaphoreCreateMutex();
  }
  memset(&stats, 0, sizeof(stats));
  item_cnt = 0;

  cliAdd("inbox", cliInbox);
  cliAdd("demo", inboxDemoCli);
  return true;
}


//-- 내부 (잠근 상태에서 부른다)
//
static uint32_t nowLocked(void)
{
  if (time_valid != true)
    return 0;
  return time_base + (millis() - time_base_ms) / 1000;
}

static int findLocked(const char *id)
{
  for (int i=0; i<item_cnt; i++)
  {
    if (strcmp(items[i].id, id) == 0)
      return i;
  }
  return -1;
}

static void removeAtLocked(int idx)
{
  for (int i=idx; i<item_cnt-1; i++)
  {
    items[i] = items[i+1];
  }
  item_cnt--;
}

/* 받은 순(오래된 것이 앞)으로 넣는다. 꽉 차면 가장 최근 것을 버린다 (보드는 16 개까지, 맥이 원본을 갖는다) */
static void insertLocked(const inbox_item_t *p_item)
{
  int pos = item_cnt;

  if (item_cnt >= INBOX_ITEM_MAX)
  {
    if (p_item->ts >= items[item_cnt-1].ts)
      return;
    item_cnt--;
    pos = item_cnt;
  }
  while (pos > 0 && items[pos-1].ts > p_item->ts)
  {
    items[pos] = items[pos-1];
    pos--;
  }
  items[pos] = *p_item;
  item_cnt++;
}

static int activeCountLocked(void)
{
  int cnt = 0;

  for (int i=0; i<item_cnt; i++)
  {
    if (items[i].wip == 0)
      cnt++;
  }
  return cnt;
}

/* 처리 중이 아닌 것 중 가장 오래된 것 (목록은 받은 순) */
static uint32_t oldestSecLocked(uint32_t now)
{
  for (int i=0; i<item_cnt; i++)
  {
    if (items[i].wip != 0)
      continue;
    if (now == 0 || now < items[i].ts)
      return 0;
    return now - items[i].ts;
  }
  return 0;
}

static uint8_t fullnessLocked(uint32_t now)
{
  int32_t full = INBOX_FULL_MAX - 2 * activeCountLocked() - (int32_t)(oldestSecLocked(now) / INBOX_WARN_SEC);

  return (uint8_t)constrain(full, 0, INBOX_FULL_MAX);
}


//-- 맥 중계 / 데모
//
void inboxSync(const inbox_item_t *p_items, uint8_t count, uint32_t now, int32_t tz, const inbox_stats_t *p_stats)
{
  lock();
  item_cnt = 0;
  for (int i=0; i<count; i++)
  {
    insertLocked(&p_items[i]);
  }
  if (p_stats != NULL) stats = *p_stats;
  if (now > 0)
  {
    time_valid   = true;
    time_base    = now;
    time_base_ms = millis();
    time_tz      = tz;
  }
  link_ever = true;
  link_ms   = millis();
  version++;
  unLock();
}

void inboxAdd(const inbox_item_t *p_item)
{
  int idx;

  lock();
  idx = findLocked(p_item->id);
  if (idx >= 0)
  {
    // 같은 대화의 후속 메시지 : 기다리기 시작한 시각은 그대로, 내용만 바꾼다
    uint32_t ts = items[idx].ts;

    items[idx]    = *p_item;
    items[idx].ts = ts;
  }
  else
  {
    insertLocked(p_item);
  }
  version++;
  unLock();
}

void inboxDone(const char *id, uint32_t waited)
{
  int idx;

  lock();
  idx = findLocked(id);
  if (idx >= 0)
  {
    uint32_t now = nowLocked();

    eat_full_from = fullnessLocked(now);
    eat_item      = items[idx];
    eat_waited    = waited;
    removeAtLocked(idx);
    eat_full_to   = fullnessLocked(now);
    eat_on        = true;
    eat_ms        = millis();
    version++;
  }
  unLock();
}

void inboxRemove(const char *id)
{
  int idx;

  lock();
  idx = findLocked(id);
  if (idx >= 0)
  {
    removeAtLocked(idx);
    version++;
  }
  unLock();
}

void inboxTick(uint32_t now, int32_t tz, const inbox_stats_t *p_stats)
{
  lock();
  if (now > 0)
  {
    time_valid   = true;
    time_base    = now;
    time_base_ms = millis();
    time_tz      = tz;
  }
  if (p_stats != NULL && memcmp(&stats, p_stats, sizeof(stats)) != 0)
  {
    stats = *p_stats;
    version++;
  }
  link_ever = true;
  link_ms   = millis();
  unLock();
}

void inboxClear(void)
{
  lock();
  item_cnt = 0;
  eat_on   = false;
  memset(&stats, 0, sizeof(stats));
  version++;
  unLock();
}

void inboxSetDemo(bool enable)
{
  lock();
  demo_on = enable;
  version++;
  unLock();
}


//-- 화면
//
void inboxGetView(inbox_view_t *p_view)
{
  uint32_t now;

  lock();
  now = nowLocked();

  p_view->time_valid = time_valid;
  p_view->now        = now;
  p_view->tz         = time_tz;

  if (demo_on)
    p_view->link = INBOX_LINK_DEMO;
  else if (link_ever == false)
    p_view->link = INBOX_LINK_NONE;
  else if (millis() - link_ms >= INBOX_LINK_TIMEOUT_MS)
    p_view->link = INBOX_LINK_LOST;
  else
    p_view->link = INBOX_LINK_OK;

  // 처리 중이 아닌 것을 앞에, 처리 중인 것을 뒤에 (각각 받은 순)
  p_view->count = 0;
  for (int pass=0; pass<2; pass++)
  {
    for (int i=0; i<item_cnt; i++)
    {
      if ((items[i].wip != 0) == (pass == 1))
        p_view->item[p_view->count++] = items[i];
    }
  }
  p_view->active = activeCountLocked();
  p_view->wip    = item_cnt - p_view->active;
  p_view->stats  = stats;

  p_view->oldest_sec = oldestSecLocked(now);
  p_view->fullness   = fullnessLocked(now);

  if (eat_on && millis() - eat_ms >= INBOX_EAT_MS)
  {
    eat_on = false;
    version++;
  }
  if (eat_on)
    p_view->state = INBOX_STATE_EAT;
  else if (item_cnt == 0)
    p_view->state = INBOX_STATE_IDLE;
  else if (p_view->active == 0)
    p_view->state = INBOX_STATE_WORK;
  else if (p_view->oldest_sec > INBOX_CRY_SEC)
    p_view->state = INBOX_STATE_CRY;
  else
    p_view->state = INBOX_STATE_HUNGRY;

  p_view->eat_item      = eat_item;
  p_view->eat_waited    = eat_waited;
  p_view->eat_full_from = eat_full_from;
  p_view->eat_full_to   = eat_full_to;
  p_view->version       = version;
  unLock();
}

uint32_t inboxGetLinkMs(void)
{
  return link_ms;
}

bool inboxIsLinkAlive(void)
{
  return link_ever && millis() - link_ms < INBOX_LINK_TIMEOUT_MS;
}

void inboxSetRequestFunc(void (*func)(const char *type, const char *id))
{
  request_func = func;
}

void inboxRequestOpen(const char *id)
{
  logPrintf("[  ] inbox open %s\n", id);
  if (request_func != NULL)
    request_func("open", id);
}

void inboxRequestWip(const char *id, bool on)
{
  int idx;

  logPrintf("[  ] inbox wip %s %s\n", id, on ? "on" : "off");
  if (request_func != NULL)
    request_func(on ? "wip" : "unwip", id);

  // 맥이 add 로 다시 보내기 전에 화면에 먼저 반영한다 (맥이 없거나 데모일 때도 동작한다)
  lock();
  idx = findLocked(id);
  if (idx >= 0)
  {
    items[idx].wip = on ? 1 : 0;
    version++;
  }
  unLock();
}

void inboxRequestSync(void)
{
  if (request_func != NULL)
    request_func("sync", "");
}

void inboxRequestAck(const char *id)
{
  logPrintf("[  ] inbox ack %s\n", id);
  if (request_func != NULL)
    request_func("ack", id);

  // 맥이 remove 를 보내기 전에 화면에서 먼저 뺀다 (맥이 없거나 데모일 때도 동작한다)
  inboxRemove(id);
}


void cliInbox(cli_args_t *args)
{
  bool ret = false;
  static inbox_view_t view;     // 큰 구조체라 스택에 두지 않는다

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const char *state_str[] = {"idle", "hungry", "cry", "eat", "work"};
    const char *link_str[]  = {"none", "ok", "lost", "demo"};

    inboxGetView(&view);
    cliPrintf("state    : %s\n", state_str[view.state]);
    cliPrintf("link     : %s\n", link_str[view.link]);
    cliPrintf("time     : %s %lu (tz %ld)\n", view.time_valid ? "valid" : "-", view.now, view.tz);
    cliPrintf("fullness : %d / %d\n", view.fullness, INBOX_FULL_MAX);
    cliPrintf("oldest   : %lu s\n", view.oldest_sec);
    cliPrintf("stats    : handled %d, avg %d min, recent %d\n",
              view.stats.handled, view.stats.avg_wait_min, view.stats.recent_cnt);
    cliPrintf("items    : %d\n", view.count);
    for (int i=0; i<view.count; i++)
    {
      cliPrintf("  %-20s %-8s %-12s %4lu s  %s\n",
                view.item[i].id, view.item[i].from, view.item[i].where,
                view.now > view.item[i].ts ? view.now - view.item[i].ts : 0, view.item[i].text);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "clear"))
  {
    inboxClear();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("inbox info\n");
    cliPrintf("inbox clear\n");
  }
}
