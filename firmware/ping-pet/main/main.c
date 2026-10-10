#include "main.h"




void app_main(void)
{
  bspInit();

  hwInit();
  apInit();
  apMain();
}
