#include "ap.h"
#include "module.h"




void apInit(void)
{
  //-- 각 모듈의 init() 이 우선순위 순으로 실행된다 (MODULE_DEF 로 자기 등록)
  moduleInit();
}

void apMain(void)
{
  logBoot(false);

  while (1)
  {
    moduleUpdate();
    delay(1);
  }
}
