#include "usb.h"


#ifdef _USE_HW_USB
#include "cli.h"
#include "log.h"
#include "tinyusb_default_config.h"
#include "soc/lp_system_reg.h"
#ifdef _USE_HW_CDC
#include "cdc.h"
#endif


/* USB (esp_tinyusb, P4 USB 2.0 HS OTG 포트).

   esp_tinyusb 가 tud_task() 를 자기 태스크에서 돌린다. 그래서 usbUpdate() 는 할 일이 없다.
   디스크립터는 usb_desc.c 가 채운다. */


static void usbJumpToBootTimer(void *arg);
#if CLI_USE(HW_USB)
static void cliUsb(cli_args_t *args);
static void cliBoot(cli_args_t *args);
#endif


static bool is_init = false;
static esp_timer_handle_t boot_timer = NULL;



bool usbInit(void)
{
  esp_err_t esp_ret;
  tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();


  usbDescConfig(&tusb_cfg);

  esp_ret = tinyusb_driver_install(&tusb_cfg);
  if (esp_ret != ESP_OK)
  {
    logPrintf("[E_] usbInit() %s\n", esp_err_to_name(esp_ret));
    return false;
  }

  const esp_timer_create_args_t timer_args =
  {
    .callback = usbJumpToBootTimer,
    .name     = "usb_boot",
  };
  esp_timer_create(&timer_args, &boot_timer);

  is_init = true;
  logPrintf("[OK] usbInit()\n");
  logPrintf("     CDC %d / HID %d\n", CFG_TUD_CDC, CFG_TUD_HID);
  logPrintf("     SN  %s\n", usbGetSerial());

#if CLI_USE(HW_USB)
  cliAdd("usb", cliUsb);
  cliAdd("boot", cliBoot);
#endif
  return true;
}

void usbDeInit(void)
{
  if (is_init != true)
    return;

  tinyusb_driver_uninstall();
  is_init = false;
}

bool usbIsInit(void)
{
  return is_init;
}

bool usbUpdate(void)
{
  // tud_task() 는 esp_tinyusb 태스크가 돌린다
  return is_init;
}

bool usbIsConnect(void)
{
  if (is_init != true)
    return false;

  return (tud_connected() && !tud_suspended());
}

bool usbIsOpen(void)
{
#ifdef _USE_HW_CDC
  return cdcIsConnect();
#else
  return usbIsConnect();
#endif
}

void usbConnect(void)
{
  if (is_init)
    tud_connect();
}

void usbDisconnect(void)
{
  if (is_init)
    tud_disconnect();
}

UsbMode_t usbGetMode(void)
{
  return is_init ? USB_CDC_MODE : USB_NON_MODE;
}

//   호스트가 CDC 를 연 보율로 갈린다. cdc.c 의 cdcGetType() 을 본다.
UsbType_t usbGetType(void)
{
#ifdef _USE_HW_CDC
  return (UsbType_t)cdcGetType();
#else
  return USB_CON_CDC;
#endif
}

void usbJumpToBoot(void)
{
  // USB 콜백 문맥에서 불릴 수 있으므로 타이머로 넘겨서 재시작한다
  if (boot_timer != NULL)
  {
    esp_timer_stop(boot_timer);
    esp_timer_start_once(boot_timer, 50 * 1000);
  }
  else
  {
    usbJumpToBootTimer(NULL);
  }
}

void usbJumpToBootTimer(void *arg)
{
  (void)arg;

  // ROM 다운로드 모드로 재시작. LP 영역 레지스터라 소프트웨어 리셋 뒤에도 남는다.
  REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG, LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
  esp_restart();
}


#if CLI_USE(HW_USB)
void cliUsb(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("serial    : %s\n", usbGetSerial());
    while (cliKeepLoop())
    {
      cliPrintf("init      : %d\n", is_init);
      cliPrintf("speed     : %-4s\n", tud_speed_get() == TUSB_SPEED_HIGH ? "HS" : "FS");
      cliPrintf("mounted   : %d\n", tud_mounted());
      cliPrintf("connected : %d\n", tud_connected());
      cliPrintf("suspended : %d\n", tud_suspended());
#if CFG_TUD_CDC
      cliPrintf("cdc conn  : %d\n", tud_cdc_n_connected(0));
      // 호스트가 연 보율이 CDC 스트림의 주인을 정한다. 115200 이면 CLI, 그 외는 cmd.
      cliPrintf("cdc baud  : %-8d\n", (int)cdcGetBaud());
      cliPrintf("cdc owner : %-4s\n", usbGetType() == USB_CON_CLI ? "CLI" : "CMD");
      cliMoveUp(8);
#else
      cliMoveUp(5);
#endif
      delay(100);
    }
#if CFG_TUD_CDC
    cliMoveDown(8);
#else
    cliMoveDown(5);
#endif
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usb info\n");
  }
}

void cliBoot(cli_args_t *args)
{
  (void)args;

  cliPrintf("jump to download mode\n");
  delay(50);
  usbJumpToBoot();
}
#endif

#endif
