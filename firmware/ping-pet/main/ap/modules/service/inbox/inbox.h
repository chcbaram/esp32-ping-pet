#ifndef INBOX_H_
#define INBOX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


/* 미응답 요청 목록 (인계 문서 "보드 펌웨어 사양")
 *
 * 맥 중계(USB JSON) 와 데모 모드가 데이터를 넣고, 화면은 inboxGetView() 스냅숏만 읽는다.
 * 여러 태스크(cli, cmd, ui)에서 부르므로 안에서 잠근다.
 *
 *   상태    : 미응답 0 건 = 여유, 1 건 이상 = 배고픔, 가장 오래된 요청 30 분 초과 = 울음,
 *             done 을 받으면 4 초 냠냠 뒤 다시 계산
 *   배부름  : 10 에서 미응답 1 건마다 -2, 가장 오래된 요청이 10 분을 넘을 때마다 -1, 최소 0
 *   연결    : 90 초 동안 tick 이 없으면 끊김
 */

#define INBOX_ITEM_MAX        16
#define INBOX_RECENT_MAX      3
#define INBOX_FULL_MAX        10

#define INBOX_CRY_SEC         (30 * 60)
#define INBOX_WARN_SEC        (10 * 60)
#define INBOX_EAT_MS          4000
#define INBOX_LINK_TIMEOUT_MS 90000


typedef enum
{
  INBOX_KIND_DM = 0,
  INBOX_KIND_MPIM,
  INBOX_KIND_MENTION,
} inbox_kind_t;

typedef enum
{
  INBOX_STATE_IDLE = 0,
  INBOX_STATE_HUNGRY,
  INBOX_STATE_CRY,
  INBOX_STATE_EAT,
} inbox_state_t;

typedef enum
{
  INBOX_LINK_NONE = 0,    // 한 번도 연결된 적 없다
  INBOX_LINK_OK,
  INBOX_LINK_LOST,        // 90 초 동안 tick 이 없다
  INBOX_LINK_DEMO,        // 데모 모드
} inbox_link_t;

typedef struct
{
  char     id[48];
  uint8_t  kind;          // inbox_kind_t
  char     from[32];      // 보낸 사람 표시 이름
  char     where[48];     // "DM", "그룹 DM", "#hw-team"
  char     text[160];     // 맥이 80 자로 자른 본문
  uint32_t ts;            // 받은 시각 (유닉스 초)
} inbox_item_t;

typedef struct
{
  uint8_t  kind;
  char     from[32];
  char     where[48];
  uint32_t waited;        // 기다린 시간 (초)
  uint32_t at;            // 처리한 시각 (유닉스 초)
} inbox_recent_t;

typedef struct
{
  uint16_t       handled;         // 오늘 답장한 요청
  uint16_t       avg_wait_min;    // 평균 응답 시간 (분)
  uint8_t        recent_cnt;
  inbox_recent_t recent[INBOX_RECENT_MAX];
} inbox_stats_t;

typedef struct
{
  // 시각
  bool          time_valid;
  uint32_t      now;              // 유닉스 초 (맥이 보낸 시각 + 경과)
  int32_t       tz;               // 초 (KST = 32400)

  inbox_link_t  link;

  // 목록 (받은 순, 오래된 것이 앞)
  uint8_t       count;
  inbox_item_t  item[INBOX_ITEM_MAX];
  inbox_stats_t stats;

  // 계산 값
  inbox_state_t state;
  uint8_t       fullness;         // 0 ~ 10
  uint32_t      oldest_sec;       // 가장 오래된 요청이 기다린 시간

  // 냠냠 (done 뒤 4 초)
  inbox_item_t  eat_item;
  uint32_t      eat_waited;
  uint8_t       eat_full_from;    // 먹기 전 배부름
  uint8_t       eat_full_to;      // 먹은 뒤 배부름

  uint32_t      version;          // 목록 · 통계가 바뀔 때마다 늘어난다 (화면 다시 그리기 판단)
} inbox_view_t;


bool     inboxInit(void);

// 맥 중계 / 데모가 부른다
void     inboxSync(const inbox_item_t *p_items, uint8_t count, uint32_t now, int32_t tz, const inbox_stats_t *p_stats);
void     inboxAdd(const inbox_item_t *p_item);
void     inboxDone(const char *id, uint32_t waited);
void     inboxRemove(const char *id);
void     inboxTick(uint32_t now, int32_t tz, const inbox_stats_t *p_stats);
void     inboxClear(void);
void     inboxSetDemo(bool enable);

// 데모 모드 (inbox_demo.c)
void     inboxDemoSet(bool enable);
bool     inboxDemoIsOn(void);

// 화면이 부른다
void     inboxGetView(inbox_view_t *p_view);
void     inboxRequestOpen(const char *id);   // 카드 탭 : 맥 슬랙에서 대화 열기
void     inboxRequestAck(const char *id);    // 카드 길게 누름 : 답장 없이 처리

// 보드 → 맥 (cmd 채널이 붙으면 그쪽에서 등록한다)
void     inboxSetRequestFunc(void (*func)(const char *type, const char *id));


#ifdef __cplusplus
}
#endif

#endif
