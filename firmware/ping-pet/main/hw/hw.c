#include "hw.h"



static void bootMsg(void);



bool hwInit(void)
{  
  cliInit();
  logInit();
  uartInit();
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);  
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)bspGetCpuFreqMhz());
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__); 
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__); 
  logPrintf("\n");
  bootMsg();

  i2cInit();
  lcdInit();
  touchInit();

  return true;
}

void bootMsg(void)
{
  esp_chip_info_t chip_info;
  uint32_t flash_size = 0;


  esp_chip_info(&chip_info);
  esp_flash_get_size(NULL, &flash_size);

  logPrintf("[ %s Info ]\n", CONFIG_IDF_TARGET);
  logPrintf("Chip      : %d core(s), rev v%d.%d\n",
            chip_info.cores,
            chip_info.revision / 100,
            chip_info.revision % 100);
  logPrintf("FLASH     : %luMB\n", flash_size / (1024 * 1024));
  logPrintf("PSRAM     : %dMB\n", esp_psram_get_size()/1024/1024);
  logPrintf("Free heap : %ld KB\n", esp_get_free_heap_size()/1024);
  logPrintf("Free Heapi: %d KB\n", esp_get_free_internal_heap_size()/1024);
  logPrintf("\n");
}
