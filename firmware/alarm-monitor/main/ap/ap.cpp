#include "ap.h"




static void cliInfo(cli_args_t *args);




void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);
  cliAdd("info", cliInfo);
}

void apMain(void)
{
  bool is_connected = false;


  while(1)
  {
    // 호스트가 CDC 포트를 열면 프롬프트를 띄운다
    if (cdcIsConnect() != is_connected)
    {
      is_connected = cdcIsConnect();
      if (is_connected)
      {
        delay(100);
        cliPrintf("\n%s %s\n", _DEF_BOARD_NAME, _DEF_FIRMWATRE_VERSION);
        cliLogo();
      }
    }

    cliMain();
    delay(1);
  }
}

void cliInfo(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 0 || (args->argc == 1 && args->isStr(0, "sys")))
  {
    esp_chip_info_t chip_info;

    esp_chip_info(&chip_info);
    cliPrintf("Board     : %s\n", _DEF_BOARD_NAME);
    cliPrintf("Version   : %s\n", _DEF_FIRMWATRE_VERSION);
    cliPrintf("Chip      : %s rev v%d.%d\n", CONFIG_IDF_TARGET, chip_info.revision / 100, chip_info.revision % 100);
    cliPrintf("CPU Freq  : %lu Mhz\n", bspGetCpuFreqMhz());
    cliPrintf("PSRAM     : %d MB\n", esp_psram_get_size()/1024/1024);
    cliPrintf("Free heap : %ld KB\n", esp_get_free_heap_size()/1024);
    cliPrintf("Free Heapi: %d KB\n", esp_get_free_internal_heap_size()/1024);
    cliPrintf("Uptime    : %lu s\n", millis()/1000);
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("info [sys]\n");
  }
}
