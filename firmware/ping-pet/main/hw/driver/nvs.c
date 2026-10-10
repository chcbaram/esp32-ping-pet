#include "nvs.h"


#ifdef _USE_HW_NVS
#include "nvs_flash.h"
#include "cli.h"


#define NVS_NAMESPACE     "ping-pet"


#if CLI_USE(HW_NVS)
static void cliNvs(cli_args_t *args);
#endif

static bool is_init = false;
static nvs_handle_t nvs_hd = 0;




bool nvsInit(void)
{
  esp_err_t err;

  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    // 형식이 바뀌었거나 꽉 찼다. 지우고 다시 (설정 값만 들어 있다)
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err == ESP_OK)
  {
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_hd);
  }
  is_init = (err == ESP_OK);

  logPrintf("[%s] nvsInit()\n", is_init ? "OK" : "E_");
#if CLI_USE(HW_NVS)
  cliAdd("nvs", cliNvs);
#endif
  return is_init;
}

bool nvsGet(const char *key, void *p_data, uint32_t length)
{
  size_t len = length;

  if (is_init != true) return false;

  return nvs_get_blob(nvs_hd, key, p_data, &len) == ESP_OK && len == length;
}

bool nvsSet(const char *key, const void *p_data, uint32_t length)
{
  if (is_init != true) return false;

  if (nvs_set_blob(nvs_hd, key, p_data, length) != ESP_OK)
    return false;
  return nvs_commit(nvs_hd) == ESP_OK;
}


#if CLI_USE(HW_NVS)
void cliNvs(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    nvs_stats_t st;

    nvs_get_stats(NULL, &st);
    cliPrintf("is_init : %s\n", is_init ? "True" : "False");
    cliPrintf("entries : used %d, free %d, total %d\n", (int)st.used_entries, (int)st.free_entries, (int)st.total_entries);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "erase"))
  {
    nvs_erase_all(nvs_hd);
    nvs_commit(nvs_hd);
    cliPrintf("erased\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("nvs info\n");
    cliPrintf("nvs erase\n");
  }
}
#endif

#endif
