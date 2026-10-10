// IDF 헤더를 먼저 : esp_rom 의 ets_sys.h 가 enum 에 OK 를 쓰는데 common/err_code.h 가 OK 를 매크로로 둔다
#include "esp_sleep.h"
#include "module.h"
#include "power/power.h"
#include "inbox.h"
#include "ota/ota.h"


/* 화면 끄기 · 딥슬립 (power.h) */


static bool powerInit(void);
static void powerUpdate(void const *arg);
static void cliPower(cli_args_t *args);


static volatile uint32_t touch_ms = 0;        // 마지막 터치 (ui 태스크가 쓴다)
static uint32_t          busy_ms  = 0;        // 마지막으로 PC 연결 · 데모 · OTA 가 있던 때
static volatile bool     is_off   = false;
static uint8_t           saved_bl = 100;      // 끄기 전 밝기
static volatile uint32_t wake_touch_ms = 0;   // 터치로 켠 때 (0 = 아님)
static bool              is_forced = false;   // CLI power off : 중계가 "살아 있음" 이어도 끈 채로 둔다
static uint32_t          forced_ms = 0;


MODULE_DEF(power)
{
  .name     = "power",
  .priority = MODULE_PRI_NORMAL,
  .init     = powerInit,
  .update   = powerUpdate,
};




bool powerInit(void)
{
  busy_ms  = millis();
  touch_ms = millis();
  cliAdd("power", cliPower);
  return true;
}

void powerTouch(void)
{
  touch_ms = millis();
}

bool powerIsScreenOff(void)
{
  return is_off;
}

bool powerIsWakeTouch(void)
{
  return wake_touch_ms != 0 && millis() - wake_touch_ms < POWER_WAKE_GUARD_MS;
}

void powerScreenOff(void)
{
  if (is_off)
    return;
  saved_bl = lcdGetBackLight();
  is_off   = true;                        // 먼저 : 화면 쪽이 백라이트를 다시 켜지 않게
  lcdSetBackLight(0);
  lcdDisplayOff();
  logPrintf("[  ] power : screen off\n");
}

static void powerScreenOn(bool by_touch)
{
  if (is_off != true)
    return;
  lcdDisplayOn();
  lcdSetBackLight(saved_bl);
  wake_touch_ms = by_touch ? millis() : 0;
  is_off    = false;
  is_forced = false;
  logPrintf("[  ] power : screen on (%s)\n", by_touch ? "touch" : "pc link");
}

void powerSleep(void)
{
  logPrintf("[  ] power : deep sleep (RST 로 깨운다)\n");
  lcdSetBackLight(0);
  lcdDisplayOff();
  delay(100);                             // 로그 · LCD 명령이 나갈 시간
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();
}

static uint32_t idleMs(void)
{
  uint32_t now = millis();
  uint32_t a   = now - busy_ms;
  uint32_t b   = now - touch_ms;

  return (a < b) ? a : b;
}

void powerUpdate(void const *arg)
{
  bool busy;

  (void)arg;

  // 중계가 살아 있다 (sync · tick), 데모 중, OTA 중이면 깨어 있다.
  // cdcIsConnect()(DTR) 는 쓰지 않는다 : 맥은 포트를 닫아도 DTR 을 내리지 않아 계속 연결로 보인다
  busy = inboxIsLinkAlive() || inboxDemoIsOn() || otaIsActive();
  if (busy)
    busy_ms = millis();

  // CLI 로 끈 것은 중계가 새로 메시지를 보낼 때만 깬다 (그 전부터 살아 있던 것은 무시)
  if (is_forced && inboxIsLinkAlive() && (int32_t)(inboxGetLinkMs() - forced_ms) <= 0)
    busy = inboxDemoIsOn() || otaIsActive();

  if (is_off)
  {
    if (busy)
      powerScreenOn(false);
    else if (millis() - touch_ms < 500)
      powerScreenOn(true);
  }
  else if (idleMs() >= POWER_SCREEN_OFF_MS)
  {
    powerScreenOff();
  }
}


void cliPower(cli_args_t *args)
{
  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("screen   : %s\n", is_off ? "off" : "on");
    cliPrintf("relay    : %s (cdc dtr %s), demo %s, ota %s\n", inboxIsLinkAlive() ? "alive" : "none",
              cdcIsConnect() ? "on" : "off",
              inboxDemoIsOn() ? "on" : "off", otaIsActive() ? "on" : "off");
    cliPrintf("idle     : %d s / %d s (touch %d s ago)\n", (int)(idleMs() / 1000),
              (int)(POWER_SCREEN_OFF_MS / 1000), (int)((millis() - touch_ms) / 1000));
    cliPrintf("wakeup   : %d (0 = 처음 켬 · RST)\n", (int)esp_sleep_get_wakeup_cause());
    return;
  }
  if (args->argc == 1 && args->isStr(0, "off"))
  {
    // 시험용 : 바로 끈다. 터치하거나 중계가 새로 메시지를 보내면 켜진다
    busy_ms = touch_ms = millis() - POWER_SCREEN_OFF_MS;
    is_forced = true;
    forced_ms = millis();
    powerScreenOff();
    return;
  }
  if (args->argc == 1 && args->isStr(0, "sleep"))
  {
    cliPrintf("deep sleep. RST 버튼으로 깨운다\n");
    delay(50);
    powerSleep();
  }
  cliPrintf("power info\n");
  cliPrintf("power off    : 바로 화면 끄기 (시험)\n");
  cliPrintf("power sleep  : 딥슬립\n");
}
