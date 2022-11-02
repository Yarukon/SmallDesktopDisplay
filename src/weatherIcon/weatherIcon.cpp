#include "WeatherIcon.h"

#include <TJpg_Decoder.h>

//显示天气图标
void WeatherNum::printfweather(int x, int y, int index)
{
  switch (index)
  {
  case 0:
    TJpgDec.drawJpg(x, y, t0, sizeof(t0));
    break;

  case 1:
    TJpgDec.drawJpg(x, y, t1, sizeof(t1));
    break;

  case 2:
    TJpgDec.drawJpg(x, y, t2, sizeof(t2));
    break;

  case 3:
    TJpgDec.drawJpg(x, y, t3, sizeof(t3));
    break;

  case 4:
    TJpgDec.drawJpg(x, y, t4, sizeof(t4));
    break;

  case 5:
    TJpgDec.drawJpg(x, y, t5, sizeof(t5));
    break;

  case 6:
    TJpgDec.drawJpg(x, y, t6, sizeof(t6));
    break;

  case 7:
  case 21:
    TJpgDec.drawJpg(x, y, t7, sizeof(t7));
    break;

  case 8:
  case 22:
    TJpgDec.drawJpg(x, y, t8, sizeof(t8));
    break;

  case 9:
  case 23:
    TJpgDec.drawJpg(x, y, t9, sizeof(t9));
    break;

  case 10:
  case 24:
    TJpgDec.drawJpg(x, y, t10, sizeof(t10));
    break;

  case 11:
  case 25:
    TJpgDec.drawJpg(x, y, t11, sizeof(t11));
    break;

  case 12:
  case 301:
    TJpgDec.drawJpg(x, y, t12, sizeof(t12));
    break;

  case 13:
    TJpgDec.drawJpg(x, y, t13, sizeof(t13));
    break;

  case 14:
    TJpgDec.drawJpg(x, y, t14, sizeof(t14));
    break;

  case 15:
    TJpgDec.drawJpg(x, y, t15, sizeof(t15));
    break;

  case 16:
  case 17:
    TJpgDec.drawJpg(x, y, t16, sizeof(t16));
    break;

  case 18:
    TJpgDec.drawJpg(x, y, t18, sizeof(t18));
    break;

  case 19:
    TJpgDec.drawJpg(x, y, t19, sizeof(t19));
    break;

  case 20:
    TJpgDec.drawJpg(x, y, t20, sizeof(t20));
    break;

  case 26:
    TJpgDec.drawJpg(x, y, t26, sizeof(t26));
    break;

  case 27:
    TJpgDec.drawJpg(x, y, t27, sizeof(t27));
    break;

  case 28:
  case 302:
    TJpgDec.drawJpg(x, y, t28, sizeof(t28));
    break;

  case 29:
    TJpgDec.drawJpg(x, y, t29, sizeof(t29));
    break;

  case 30:
    TJpgDec.drawJpg(x, y, t30, sizeof(t30));
    break;

  case 31:
    TJpgDec.drawJpg(x, y, t31, sizeof(t31));
    break;

  default:
    TJpgDec.drawJpg(x, y, t99, sizeof(t99));
  }
}
