#include "inbox.h"


/* 데모 모드 — 가상의 메시지로 여유 → 배고픔 → 울음 → 냠냠 을 돌려 보여 준다.
 *
 * 맥 중계와 같은 함수(inboxSync / Add / Done / Tick)로 모델에 넣으므로 화면은 실제와 같은 경로로 그려진다.
 * 이름과 메시지는 목업의 가상 예시다.
 *
 *   1  여유     8 s   목록 비움, 오늘 통계
 *   2  배고픔   8 s   DM 12 분 전, 멘션 4 분 전
 *   3  울음     8 s   가상 시계 +26 분, 2 건 더 (최장 38 분)
 *   4  냠냠     5 s   가장 오래된 DM 에 답장 → 남은 요청
 *   5  냠냠     6 s   나머지에 차례로 답장 → 여유, 처음으로
 */


#define DEMO_TZ           (9 * 3600)


typedef struct
{
  uint32_t    ms;           // 이 단계를 보여 주는 시간
  void      (*run)(void);
} demo_step_t;


static void demoStepIdle(void);
static void demoStepHungry(void);
static void demoStepCry(void);
static void demoStepEat(void);
static void demoStepDone(void);


static const demo_step_t step_tbl[] =
{
  {8000, demoStepIdle},
  {8000, demoStepHungry},
  {8000, demoStepCry},
  {5000, demoStepEat},
  {6000, demoStepDone},
};
#define STEP_CNT    (sizeof(step_tbl) / sizeof(step_tbl[0]))

static bool          is_on = false;
static volatile int  demo_req = -1;      // 다른 태스크(ui)의 켜기 · 끄기 요청. 메인 태스크(inboxDemoUpdate)가 처리한다
static int           step = 0;
static uint32_t      step_ms = 0;
static uint32_t      sub_ms = 0;
static int           sub_cnt = 0;
static uint32_t      demo_now = 0;       // 가상 시각 (유닉스 초)
static uint32_t      demo_now_ms = 0;
static inbox_stats_t demo_stats;




static uint32_t demoNow(void)
{
  return demo_now + (millis() - demo_now_ms) / 1000;
}

static void demoSetNow(uint32_t now)
{
  demo_now    = now;
  demo_now_ms = millis();
}

static void demoItem(inbox_item_t *p, const char *id, uint8_t kind, const char *from,
                     const char *where, const char *text, uint32_t ago_sec)
{
  memset(p, 0, sizeof(*p));
  strncpy(p->id,    id,    sizeof(p->id) - 1);
  strncpy(p->from,  from,  sizeof(p->from) - 1);
  strncpy(p->where, where, sizeof(p->where) - 1);
  strncpy(p->text,  text,  sizeof(p->text) - 1);
  p->kind = kind;
  p->ts   = demoNow() - ago_sec;
}

static void demoRecent(uint8_t kind, const char *from, const char *where, uint32_t waited, uint32_t ago_sec)
{
  inbox_recent_t r;

  memset(&r, 0, sizeof(r));
  r.kind = kind;
  strncpy(r.from,  from,  sizeof(r.from) - 1);
  strncpy(r.where, where, sizeof(r.where) - 1);
  r.waited = waited;
  r.at     = demoNow() - ago_sec;

  // 최근 것이 앞
  for (int i=INBOX_RECENT_MAX-1; i>0; i--)
  {
    demo_stats.recent[i] = demo_stats.recent[i-1];
  }
  demo_stats.recent[0] = r;
  if (demo_stats.recent_cnt < INBOX_RECENT_MAX)
    demo_stats.recent_cnt++;
}

static void demoAnswer(const char *id, uint8_t kind, const char *from, const char *where, uint32_t waited)
{
  // 맥처럼 : done 을 보내고 통계를 갱신한 tick 을 보낸다
  inboxDone(id, waited);
  demo_stats.handled++;
  demo_stats.avg_wait_min = (demo_stats.avg_wait_min * (demo_stats.handled - 1) + waited / 60) / demo_stats.handled;
  demoRecent(kind, from, where, waited, 0);
  inboxTick(demoNow(), DEMO_TZ, &demo_stats);
}


//-- 단계
//
void demoStepIdle(void)
{
  memset(&demo_stats, 0, sizeof(demo_stats));
  demo_stats.handled      = 7;
  demo_stats.avg_wait_min = 6;
  demoRecent(INBOX_KIND_DM,      "박준호", "DM",       5 * 60,  138 * 60);
  demoRecent(INBOX_KIND_MENTION, "이지은", "#hw-team", 9 * 60,  45 * 60);
  demoRecent(INBOX_KIND_DM,      "김민수", "DM",       4 * 60,  13 * 60);

  inboxSync(NULL, 0, demoNow(), DEMO_TZ, &demo_stats);
}

void demoStepHungry(void)
{
  inbox_item_t item;

  demoItem(&item, "D01", INBOX_KIND_DM, "김민수", "DM",
           "샘플 보드 펌웨어 오늘 중으로 올려줄 수 있어요?", 12 * 60);
  inboxAdd(&item);
  demoItem(&item, "C01:1", INBOX_KIND_MENTION, "이지은", "#hw-team",
           "@나 커넥터 핀맵 한 번만 확인 부탁드려요", 4 * 60);
  inboxAdd(&item);
}

void demoStepCry(void)
{
  inbox_item_t item;

  // 26 분이 지난 것으로 : 김민수 38 분, 이지은 30 분
  demoSetNow(demoNow() + 26 * 60);
  inboxTick(demoNow(), DEMO_TZ, &demo_stats);

  demoItem(&item, "D02", INBOX_KIND_DM, "박준호", "DM",
           "라이다 로그 공유드렸어요, 시간 될 때 봐주세요", 15 * 60);
  inboxAdd(&item);
  demoItem(&item, "C02:1", INBOX_KIND_MENTION, "최유나", "#robot-ops",
           "@나 내일 회의 시간 괜찮으세요?", 3 * 60);
  inboxAdd(&item);
}

void demoStepEat(void)
{
  demoAnswer("D01", INBOX_KIND_DM, "김민수", "DM", 38 * 60);
}

/* 나머지 셋에 1.5 초 간격으로 답장한다 (demoUpdate 가 sub_cnt 로 나눠 부른다) */
void demoStepDone(void)
{
  sub_cnt = 0;
  sub_ms  = millis();
}

static void demoStepDoneSub(void)
{
  if (millis() - sub_ms < 1500 || sub_cnt >= 3)
    return;
  sub_ms = millis();

  switch (sub_cnt++)
  {
    case 0: demoAnswer("D02",   INBOX_KIND_DM,      "박준호", "DM",         16 * 60); break;
    case 1: demoAnswer("C01:1", INBOX_KIND_MENTION, "이지은", "#hw-team",   31 * 60); break;
    case 2: demoAnswer("C02:1", INBOX_KIND_MENTION, "최유나", "#robot-ops", 5 * 60);  break;
  }
}


//-- inbox 모듈이 부른다
//
bool inboxDemoIsOn(void)
{
  return is_on;
}

static void demoStart(void)
{
  inbox_view_t *p_view = malloc(sizeof(inbox_view_t));

  // 맥이 시각을 줬으면 그 시각, 아니면 2026-10-10 14:05 (KST)
  demoSetNow(utilEpochFromCivil(2026, 10, 10, 14, 5, 0) - DEMO_TZ);
  if (p_view != NULL)
  {
    inboxGetView(p_view);
    if (p_view->time_valid && p_view->link == INBOX_LINK_OK)
      demoSetNow(p_view->now);
    free(p_view);
  }

  is_on   = true;
  step    = 0;
  step_ms = millis();
  inboxSetDemo(true);
  step_tbl[step].run();
}

static void demoStop(void)
{
  is_on = false;
  inboxSetDemo(false);
  inboxClear();
}

void inboxDemoUpdate(void)
{
  int req = demo_req;

  if (req >= 0)
  {
    demo_req = -1;
    if (req == 1 && is_on == false) demoStart();
    if (req == 0 && is_on == true)  demoStop();
  }

  if (is_on != true)
    return;

  // 1 분마다 맥이 보내는 tick 처럼 시계를 맞춘다 (연결 끊김 표시가 뜨지 않게)
  static uint32_t tick_ms = 0;
  if (millis() - tick_ms >= 10000)
  {
    tick_ms = millis();
    inboxTick(demoNow(), DEMO_TZ, &demo_stats);
  }

  if (step_tbl[step].run == demoStepDone)
  {
    demoStepDoneSub();
  }

  if (millis() - step_ms >= step_tbl[step].ms)
  {
    step    = (step + 1) % STEP_CNT;
    step_ms = millis();
    step_tbl[step].run();
  }
}

/* 어느 태스크에서든 부를 수 있다. 실제 전환은 메인 태스크의 inboxDemoUpdate() 가 한다 */
void inboxDemoSet(bool enable)
{
  demo_req = enable ? 1 : 0;
}

void inboxDemoCli(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "on"))
  {
    inboxDemoSet(true);
    ret = true;
  }
  if (args->argc == 1 && args->isStr(0, "off"))
  {
    inboxDemoSet(false);
    ret = true;
  }
  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("demo : %s, step %d / %d\n", is_on ? "on" : "off", step + 1, (int)STEP_CNT);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("demo on:off:info\n");
  }
}
