/* *****************************************************************
 *
 * SmallDesktopDisplay
 *   小型桌面显示器
 *
 * 原作者: Misaka
 * 修改: 微车游
 * 再次修改: 丘山鹤
 * 讨论群: 811058758、887171863、720661626
 * 创建日期: 2021.07.19
 * 引脚分配: SCK   GPIO14
 *          MOSI  GPIO13
 *          RES   GPIO2
 *          DC    GPIO0
 *          LCDBL GPIO5
 *
 * 感谢群友 @你别失望 提醒发现WiFi保存后无法重置的问题, 目前已解决。详情查看更改说明！
 * *****************************************************************/

/* *****************************************************************
 *  库文件、头文件
 * *****************************************************************/
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <TJpg_Decoder.h>
#include <EEPROM.h>                 //内存
#include <Button2.h>                //按钮库
#include <Thread.h>                 //协程
#include <StaticThreadController.h> //协程控制

#include "config.h"                  //配置文件
#include "weatherIcon/weatherIcon.h" //天气图库
#include "Animate/Animate.h"         //动画模块

#define Version "SDD V1.4.3 - Yarukon"
/* *****************************************************************
 *  配置使能位
 * *****************************************************************/

#if WM_EN
#include <WiFiManager.h>
WiFiManager wm;
#endif

//定义按钮引脚
Button2 btn = Button2(4);

/* *****************************************************************
 *  字库、图片库
 * *****************************************************************/
#include "font/ZdyLwFont_20.h"           //字体库
#include "font/timeClockFont.h"          //字体库
#include "weatherIcon/img/temperature.h" //温度图标
#include "weatherIcon/img/humidity.h"    //湿度图标

//函数声明
void sendNTPpacket(IPAddress &address); //向NTP服务器发送请求
time_t getNtpTime();                    //从NTP获取时间

void printDigits(int digits);
String num2str(int digits);
void refreshLCD();

void saveWifiConfig(); // wifi ssid, psw保存到eeprom
void readWifiConfig(); //从eeprom读取WiFi信息ssid, psw
void delWifiConfig();  //删除原有eeprom中的信息

void getCityCode();  //发送HTTP请求并且将服务器响应通过串口输出
void fetchWeather(); //获取城市天气

void resetWifi(Button2 &btn); // WIFI重设
void resetESP(Button2 &btn);

void saveParamCallback();

void updateBanner();
void weatherData(String *cityDZ, String *dataSK, String *dataFC); // 天气信息写到屏幕上
void refreshAnimatedImg();                                        // 更新右下角图片动画

// 创建时间渲染刷新函数线程
Thread updateTimeThread = Thread();
// 创建副标题切换线程
Thread refreshBannerThread = Thread();
// 创建恢复WIFI链接
Thread refreshWifiThread = Thread();
// 信息更新线程
Thread infoRefreshThread = Thread();
// 创建动画绘制和刷新线程
Thread refreshAnimationThread = Thread();

//创建协程池
StaticThreadController<5> controller(&updateTimeThread, &refreshBannerThread, &infoRefreshThread, &refreshWifiThread, &refreshAnimationThread);

//联网后所有需要更新的数据
Thread WIFI_reflash = Thread();

/* *****************************************************************
 *  参数设置
 * *****************************************************************/
struct config_type
{
  char stassid[32]; //定义配网得到的WIFI名长度(最大32字节)
  char stapsw[64];  //定义配网得到的WIFI密码长度(最大64字节)
};

//---------------修改此处""内的信息--------------------
//如开启WEB配网则可不用设置这里的参数, 前一个为wifi ssid, 后一个为密码
config_type wificonf = {{"ESP8266"}, {"88888888"}};

//天气更新时间  X 分钟
unsigned int updateWeatherInterval = 1;
//----------------------------------------------------

// LCD屏幕相关设置
TFT_eSPI tft = TFT_eSPI(); // 引脚请自行配置tft_espi库中的 User_Setup.h文件
TFT_eSprite clk = TFT_eSprite(&tft);
#define LCD_BL_PIN 5 // LCD背光引脚
uint16_t bgColor = 0x0000;

// 状态标志
int rotationLCD = 0;    // LCD屏幕方向
int brightnessLCD = 50; //屏幕亮度0-100, 默认50
uint8_t wifiState = 1;  // WIFI模块状态  1 - 打开 0 - 关闭

// EEPROM参数存储地址位
int BL_addr = 1;    //被写入数据的EEPROM地址编号  1 亮度
int Ro_addr = 2;    //被写入数据的EEPROM地址编号  2 旋转方向
int CC_addr = 10;   //被写入数据的EEPROM地址编号  10 城市
int wifi_addr = 30; //被写入数据的EEPROM地址编号  20 wifi-ssid-psw

/*** Component objects ***/
WeatherNum wrat;

uint32_t targetTime = 0;
String cityCode = "101090609"; //天气城市代码
int tempnum = 0;               //温度百分比
int huminum = 0;               //湿度百分比
int tempcol = 0xffff;          //温度显示颜色
int humicol = 0xffff;          //湿度显示颜色

// NTP服务器参数
static const char ntpServerName[] = "ntp6.aliyun.com";
const int timeZone = 8; //东八区

// wifi连接UDP设置参数
WiFiUDP Udp;
WiFiClient wificlient;
unsigned int localPort = 8000;
float duty = 0;

//星期
const String wk[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
String week()
{
  return wk[weekday() - 1];
}

//月日
String monthDay()
{
  String result = String(month()) + "月" + day() + "日";
  return result;
}

/* *****************************************************************
 *  函数
 * *****************************************************************/

// wifi ssid, psw保存到eeprom
void saveWifiConfig()
{
  //开始写入
  uint8_t *p = (uint8_t *)(&wificonf);
  for (unsigned int i = 0; i < sizeof(wificonf); i++)
  {
    EEPROM.write(i + wifi_addr, *(p + i)); //在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit(); //执行写入ROM
  delay(10);
}

// TFT屏幕输出函数
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
  if (y >= tft.height())
    return 0;
  tft.pushImage(x, y, w, h, bitmap);
  // Return 1 to decode next block
  return 1;
}

//进度条函数
byte loadNum = 6;
void loading(byte delayTime) //绘制进度条
{
  clk.setColorDepth(8);

  clk.createSprite(200, 100); //创建窗口
  clk.fillSprite(0x0000);     //填充率

  clk.drawRoundRect(0, 0, 200, 16, 8, 0xFFFF);     //空心圆角矩形
  clk.fillRoundRect(3, 3, loadNum, 10, 5, 0xFFFF); //实心圆角矩形
  clk.setTextDatum(CC_DATUM);                      //设置文本数据
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("Connecting to WiFi......", 100, 40, 2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawRightString(Version, 180, 60, 2);
  clk.pushSprite(20, 120); //窗口位置

  clk.deleteSprite();
  loadNum += 1;
  delay(delayTime);
}

//湿度图标显示函数
void drawHumidity()
{
  clk.setColorDepth(8);

  huminum = huminum / 2;
  clk.createSprite(52, 6);                         //创建窗口
  clk.fillSprite(0x0000);                          //填充率
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);       //空心圆角矩形  起始位x,y,长度, 宽度, 圆弧半径, 颜色
  clk.fillRoundRect(1, 1, huminum, 4, 2, humicol); //实心圆角矩形
  clk.pushSprite(45, 222);                         //窗口位置
  clk.deleteSprite();
}

//温度图标显示函数
void drawTemp()
{
  clk.setColorDepth(8);

  clk.createSprite(52, 6);                         //创建窗口
  clk.fillSprite(0x0000);                          //填充率
  clk.drawRoundRect(0, 0, 52, 6, 3, 0xFFFF);       //空心圆角矩形  起始位x,y,长度, 宽度, 圆弧半径, 颜色
  clk.fillRoundRect(1, 1, tempnum, 4, 2, tempcol); //实心圆角矩形
  clk.pushSprite(45, 192);                         //窗口位置
  clk.deleteSprite();
}

#if !WM_EN
//微信配网函数
void SmartConfig(void)
{
  WiFi.mode(WIFI_STA); //设置STA模式
  // tft.pushImage(0, 0, 240, 240, qr);
  tft.pushImage(0, 0, 240, 240, qr);
  Serial.println("\r\nWait for Smartconfig..."); //打印log信息
  WiFi.beginSmartConfig();                       //开始SmartConfig, 等待手机端发出用户名和密码
  while (1)
  {
    Serial.print(".");
    delay(100);                 // wait for a second
    if (WiFi.smartConfigDone()) //配网成功, 接收到SSID和密码
    {
      Serial.println("SmartConfig Success");
      Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
      Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
      break;
    }
  }
  loadNum = 194;
}
#endif

String SMOD = ""; // 亮度

// 串口调试设置函数
void serialListenerUpdate()
{
  String incomingByte = "";
  if (Serial.available() > 0)
  {
    while (Serial.available() > 0) //监测串口缓存, 当有数据输入时, 循环赋值给incomingByte
    {
      incomingByte += char(Serial.read()); //读取单个字符值, 转换为字符, 并按顺序一个个赋值给incomingByte
      delay(2);                            //不能省略, 因为读取缓冲区数据需要时间
    }
    if (SMOD == "0x01") //设置1亮度设置
    {
      int LCDBL = atoi(incomingByte.c_str()); // int n = atoi(xxx.c_str());//String转int
      if (LCDBL >= 0 && LCDBL <= 100)
      {
        EEPROM.write(BL_addr, LCDBL); //亮度地址写入亮度值
        EEPROM.commit();              //保存更改的数据
        delay(5);
        brightnessLCD = EEPROM.read(BL_addr);
        delay(5);
        SMOD = "";
        Serial.printf("亮度调整为: ");
        analogWrite(LCD_BL_PIN, 1023 - (brightnessLCD * 10));
        Serial.println(brightnessLCD);
        Serial.println("");
      }
      else
        Serial.println("亮度调整错误, 请输入0 - 100之间的数");
    }
    if (SMOD == "0x02") //设置2地址设置
    {
      int CityCODE = 0;
      int CityC = atoi(incomingByte.c_str()); // int n = atoi(xxx.c_str());//String转int
      if (((CityC >= 101000000) && (CityC <= 102000000)) || (CityC == 0))
      {
        for (int cnum = 0; cnum < 5; cnum++)
        {
          EEPROM.write(CC_addr + cnum, CityC % 100); //城市地址写入城市代码
          EEPROM.commit();                           //保存更改的数据
          CityC = CityC / 100;
          delay(5);
        }
        for (int cnum = 5; cnum > 0; cnum--)
        {
          CityCODE = CityCODE * 100;
          CityCODE += EEPROM.read(CC_addr + cnum - 1);
          delay(5);
        }

        if (cityCode == "0")
        {
          Serial.println("城市代码调整为: 自动");
          getCityCode(); //获取城市代码
        }
        else
        {
          Serial.printf("城市代码调整为: ");
          Serial.println(cityCode);
        }
        Serial.println("");
        fetchWeather(); //更新城市天气
        SMOD = "";
      }
      else
        Serial.println("城市调整错误, 请输入9位城市代码, 自动获取请输入0");
    }
    if (SMOD == "0x03") //设置3屏幕显示方向
    {
      int RoSet = atoi(incomingByte.c_str());
      if (RoSet >= 0 && RoSet <= 3)
      {
        EEPROM.write(Ro_addr, RoSet); //屏幕方向地址写入方向值
        EEPROM.commit();              //保存更改的数据
        SMOD = "";
        //设置屏幕方向后重新刷屏并显示
        tft.setRotation(RoSet);
        tft.fillScreen(0x0000);
        refreshLCD();                                               //屏幕刷新程序
        TJpgDec.drawJpg(15, 183, temperature, sizeof(temperature)); //温度图标
        TJpgDec.drawJpg(15, 213, humidity, sizeof(humidity));       //湿度图标

        Serial.print("屏幕方向设置为: ");
        Serial.println(RoSet);
      }
      else
      {
        Serial.println("屏幕方向值错误, 请输入0-3内的值");
      }
    }
    if (SMOD == "0x04") //设置天气更新时间
    {
      int wtup = atoi(incomingByte.c_str()); // int n = atoi(xxx.c_str());//String转int
      if (wtup >= 1 && wtup <= 60)
      {
        updateWeatherInterval = wtup;
        SMOD = "";
        Serial.printf("天气更新时间更改为: ");
        Serial.print(updateWeatherInterval);
        Serial.println("分钟");
      }
      else
        Serial.println("更新时间太长, 请重新设置 (1-60)");
    }
    else
    {
      SMOD = incomingByte;
      delay(2);
      if (SMOD == "0x01")
        Serial.println("请输入亮度值, 范围0-100");
      else if (SMOD == "0x02")
        Serial.println("请输入9位城市代码, 自动获取请输入0");
      else if (SMOD == "0x03")
      {
        Serial.println("请输入屏幕方向值, ");
        Serial.println("0-USB接口朝下");
        Serial.println("1-USB接口朝右");
        Serial.println("2-USB接口朝上");
        Serial.println("3-USB接口朝左");
      }
      else if (SMOD == "0x04")
      {
        Serial.print("当前天气更新时间: ");
        Serial.print(updateWeatherInterval);
        Serial.println("分钟");
        Serial.println("请输入天气更新时间 (分钟)");
      }
      else if (SMOD == "0x05")
      {
        Serial.println("重置WiFi设置中......");
        delay(10);
        wm.resetSettings();
        delWifiConfig();
        delay(10);
        Serial.println("重置WiFi成功");
        SMOD = "";
        ESP.restart();
      }
      else
      {
        Serial.println("");
        Serial.println("请输入需要修改的代码: ");
        Serial.println("亮度设置输入        0x01");
        Serial.println("地址设置输入        0x02");
        Serial.println("屏幕方向设置输入    0x03");
        Serial.println("更改天气更新时间    0x04");
        Serial.println("重置WiFi(会重启)    0x05");
        Serial.println("");
      }
    }
  }
}

#if WM_EN
// WEB配网LCD显示函数
void Web_win()
{
  clk.setColorDepth(8);

  clk.createSprite(200, 60); //创建窗口
  clk.fillSprite(0x0000);    //填充率

  clk.setTextDatum(CC_DATUM); //设置文本数据
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("Failed to connect WiFi!", 100, 10, 2);
  clk.drawString("SSID: ", 45, 40, 2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawString("SDD-CONF", 125, 40, 2);
  clk.pushSprite(20, 50); //窗口位置

  clk.deleteSprite();
}

// WEB配网函数
void Webconfig()
{
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  delay(3000);
  wm.resetSettings(); // wipe settings

  // add a custom input field
  // int customFieldLength = 40;

  // new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\"");

  // test custom html input type(checkbox)
  //  new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\" type=\"checkbox\""); // custom html type

  // test custom html(radio)
  // const char* custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  // new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input

  const char *set_rotation = "<br/><label for='set_rotation'>显示方向设置</label>\
                              <input type='radio' name='set_rotation' value='0' checked> USB接口朝下<br>\
                              <input type='radio' name='set_rotation' value='1'> USB接口朝右<br>\
                              <input type='radio' name='set_rotation' value='2'> USB接口朝上<br>\
                              <input type='radio' name='set_rotation' value='3'> USB接口朝左<br>";
  WiFiManagerParameter custom_rot(set_rotation); // custom html input
  WiFiManagerParameter custom_bl("LCDBL", "屏幕亮度 (1-100)", "10", 3);
  WiFiManagerParameter custom_weatertime("WeaterUpdateTime", "天气刷新时间 ( 分钟)", "10", 3);
  WiFiManagerParameter custom_cc("CityCode", "城市代码", "0", 9);
  WiFiManagerParameter p_lineBreak_notext("<p></p>");

  // wm.addParameter(&p_lineBreak_notext);
  // wm.addParameter(&custom_field);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_cc);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_bl);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_weatertime);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_rot);
  wm.setSaveParamsCallback(saveParamCallback);

  // custom menu via array or vector
  //
  // menu tokens, "wifi","wifinoscan","info","param","close","sep","erase","restart","exit" (sep is seperator) (if param is in menu, params will not show up in wifi page!)
  // const char* menu[] = {"wifi","info","param","sep","restart","exit"};
  // wm.setMenu(menu,6);
  std::vector<const char *> menu = {"wifi", "restart"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");

  // set static ip
  //  wm.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0)); // set static ip,gw,sn
  //  wm.setShowStaticFields(true); // force show static ip fields
  //  wm.setShowDnsFields(true);    // force show dns field always

  // wm.setConnectTimeout(20); // how long to try to connect for before continuing
  //  wm.setConfigPortalTimeout(30); // auto close configportal after n seconds
  // wm.setCaptivePortalEnable(false); // disable captive portal redirection
  // wm.setAPClientCheck(true); // avoid timeout if client connected to softap

  // wifi scan settings
  // wm.setRemoveDuplicateAPs(false); // do not remove duplicate ap names (true)
  wm.setMinimumSignalQuality(20); // set min RSSI (percentage) to show in scans, null = 8%
  // wm.setShowInfoErase(false);      // do not show erase button on info page
  // wm.setScanDispPerc(true);       // show RSSI as percentage not graph icons

  // wm.setBreakAfterConfig(true);   // always exit configportal even if wifi save fails

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  res = wm.autoConnect("SDD-CONF"); // anonymous ap
  // res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  while (!res)
    ;
}

String getParam(String name)
{
  // read parameter from server, for customhmtl input
  String value;
  if (wm.server->hasArg(name))
  {
    value = wm.server->arg(name);
  }
  return value;
}

//删除原有eeprom中的信息
void delWifiConfig()
{
  config_type deletewifi = {{""}, {""}};
  uint8_t *p = (uint8_t *)(&deletewifi);
  for (unsigned int i = 0; i < sizeof(deletewifi); i++)
  {
    EEPROM.write(i + wifi_addr, *(p + i)); //在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit(); //执行写入ROM
  delay(10);
}

//从eeprom读取WiFi信息ssid, psw
void readWifiConfig()
{
  uint8_t *p = (uint8_t *)(&wificonf);
  for (unsigned int i = 0; i < sizeof(wificonf); i++)
  {
    *(p + i) = EEPROM.read(i + wifi_addr);
  }
  // EEPROM.commit();
  // ssid = wificonf.stassid;
  // pass = wificonf.stapsw;
  Serial.printf("Read WiFi Config.....\r\n");
  Serial.printf("SSID:%s\r\n", wificonf.stassid);
  Serial.printf("PSW:%s\r\n", wificonf.stapsw);
  Serial.printf("Connecting.....\r\n");
}

void saveParamCallback()
{
  int CCODE = 0, cc;

  Serial.println("[CALLBACK] saveParamCallback fired");
  // Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
  // Serial.println("PARAM CityCode = " + getParam("CityCode"));
  // Serial.println("PARAM LCD BackLight = " + getParam("LCDBL"));
  // Serial.println("PARAM WeaterUpdateTime = " + getParam("WeaterUpdateTime"));
  // Serial.println("PARAM Rotation = " + getParam("set_rotation"));

  //将从页面中获取的数据保存
  updateWeatherInterval = getParam("WeaterUpdateTime").toInt();
  cc = getParam("CityCode").toInt();
  rotationLCD = getParam("set_rotation").toInt();
  brightnessLCD = getParam("LCDBL").toInt();

  //对获取的数据进行处理
  //城市代码
  Serial.print("CityCode = ");
  Serial.println(cc);
  if (((cc >= 101000000) && (cc <= 102000000)) || (cc == 0))
  {
    for (int cnum = 0; cnum < 5; cnum++)
    {
      EEPROM.write(CC_addr + cnum, cc % 100); //城市地址写入城市代码
      EEPROM.commit();                        //保存更改的数据
      cc = cc / 100;
      delay(5);
    }
    for (int cnum = 5; cnum > 0; cnum--)
    {
      CCODE = CCODE * 100;
      CCODE += EEPROM.read(CC_addr + cnum - 1);
      delay(5);
    }
    cityCode = CCODE;
  }

  //屏幕方向
  Serial.print("rotationLCD = ");
  Serial.println(rotationLCD);

  if (EEPROM.read(Ro_addr) != rotationLCD)
  {
    EEPROM.write(Ro_addr, rotationLCD);
    EEPROM.commit();
    delay(5);
  }

  tft.setRotation(rotationLCD);
  tft.fillScreen(0x0000);
  Web_win();
  loadNum--;
  loading(1);
  if (EEPROM.read(BL_addr) != brightnessLCD)
  {
    EEPROM.write(BL_addr, brightnessLCD);
    EEPROM.commit();
    delay(5);
  }

  // 屏幕亮度
  Serial.printf("亮度调整为: ");
  analogWrite(LCD_BL_PIN, 1023 - (brightnessLCD * 10));
  Serial.println(brightnessLCD);

  // 天气更新时间
  Serial.printf("天气更新时间调整为: ");
  Serial.println(updateWeatherInterval);
}
#endif

// 发送HTTP请求并且将服务器响应通过串口输出
void getCityCode()
{
  String URL = "http://wgeo.weather.com.cn/ip/?_=" + String(now());
  //创建 HTTPClient 对象
  HTTPClient httpClient;

  //配置请求地址。此处也可以不使用端口号和PATH而单纯的
  httpClient.begin(wificlient, URL);

  //设置请求头中的User-Agent
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");

  //启动连接并发送HTTP请求
  int httpCode = httpClient.GET();
  Serial.print("Send GET request to URL: ");
  Serial.println(URL);

  //如果服务器响应OK则从服务器获取响应体信息并通过串口输出
  if (httpCode == HTTP_CODE_OK)
  {
    String str = httpClient.getString();

    int aa = str.indexOf("id=");
    if (aa > -1)
    {
      cityCode = str.substring(aa + 4, aa + 4 + 9);
      Serial.println(cityCode);
      fetchWeather();
    }
    else
    {
      Serial.println("获取城市代码失败");
    }
  }
  else
  {
    Serial.println("请求城市代码错误: ");
    Serial.println(httpCode);
  }

  //关闭ESP8266与服务器连接
  httpClient.end();
}

// 获取城市天气
void fetchWeather()
{
  // String URL = "http://d1.weather.com.cn/dingzhi/" + cityCode + ".html?_="+String(now());//新
  String URL = "http://d1.weather.com.cn/weather_index/" + cityCode + ".html?_=" + String(now()); //原来
  //创建 HTTPClient 对象
  HTTPClient httpClient;

  // httpClient.begin(URL);
  httpClient.begin(wificlient, URL); //使用新方法

  //设置请求头中的User-Agent
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");

  //启动连接并发送HTTP请求
  int httpCode = httpClient.GET();
  Serial.println("正在获取天气数据");
  // Serial.println(URL);

  //如果服务器响应OK则从服务器获取响应体信息并通过串口输出
  if (httpCode == HTTP_CODE_OK)
  {

    String str = httpClient.getString();
    int indexStart = str.indexOf("weatherinfo\":");
    int indexEnd = str.indexOf("};var alarmDZ");

    String jsonCityDZ = str.substring(indexStart + 13, indexEnd);
    // Serial.println(jsonCityDZ);

    indexStart = str.indexOf("dataSK =");
    indexEnd = str.indexOf(";var dataZS");
    String jsonDataSK = str.substring(indexStart + 8, indexEnd);
    // Serial.println(jsonDataSK);

    indexStart = str.indexOf("\"f\":[");
    indexEnd = str.indexOf(",{\"fa");
    String jsonFC = str.substring(indexStart + 5, indexEnd);
    // Serial.println(jsonFC);

    weatherData(&jsonCityDZ, &jsonDataSK, &jsonFC);
    Serial.println("获取成功");
  }
  else
  {
    Serial.println("请求城市天气错误: ");
    Serial.print(httpCode);
  }

  //关闭ESP8266与服务器连接
  httpClient.end();
}

String scrollText[7];
// int scrollTextWidth = 0;

// 天气信息写到屏幕上
void weatherData(String *cityDZ, String *dataSK, String *dataFC)
{
  // 解析第一段JSON
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, *dataSK);
  JsonObject sk = doc.as<JsonObject>();

  // TFT_eSprite clkb = TFT_eSprite(&tft);

  /***绘制相关文字***/
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);

  // 温度
  clk.createSprite(58, 24);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(sk["temp"].as<String>() + "℃", 28, 13);
  clk.pushSprite(100, 184);
  clk.deleteSprite();
  tempnum = sk["temp"].as<int>();
  tempnum = tempnum + 10;
  if (tempnum < 10)
    tempcol = 0x00FF;
  else if (tempnum < 28)
    tempcol = 0x0AFF;
  else if (tempnum < 34)
    tempcol = 0x0F0F;
  else if (tempnum < 41)
    tempcol = 0xFF0F;
  else if (tempnum < 49)
    tempcol = 0xF00F;
  else
  {
    tempcol = 0xF00F;
    tempnum = 50;
  }
  drawTemp();

  // 湿度
  clk.createSprite(58, 24);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(sk["SD"].as<String>(), 28, 13);
  clk.pushSprite(100, 214);
  clk.deleteSprite();
  huminum = atoi((sk["SD"].as<String>()).substring(0, 2).c_str());

  if (huminum > 90)
    humicol = 0x00FF;
  else if (huminum > 70)
    humicol = 0x0AFF;
  else if (huminum > 40)
    humicol = 0x0F0F;
  else if (huminum > 20)
    humicol = 0xFF0F;
  else
    humicol = 0xF00F;
  drawHumidity();

  // 城市名称
  clk.createSprite(94, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(sk["cityname"].as<String>(), 44, 16);
  clk.pushSprite(15, 15);
  clk.deleteSprite();

  // PM2.5空气指数
  uint16_t pm25BgColor = tft.color565(156, 202, 127); //优
  String aqiTxt = "优";
  uint16_t pm25TextCol = 0x3186;

  int pm25V = sk["aqi"];
  if (pm25V > 200)
  {
    pm25BgColor = tft.color565(136, 11, 32); //重度
    pm25TextCol = 0xffff;
    aqiTxt = "重度";
  }
  else if (pm25V > 150)
  {
    pm25BgColor = tft.color565(186, 55, 121); //中度
    pm25TextCol = 0xffff;
    aqiTxt = "中度";
  }
  else if (pm25V > 100)
  {
    pm25BgColor = tft.color565(242, 159, 57); //轻
    pm25TextCol = 0xffff;
    aqiTxt = "轻度";
  }
  else if (pm25V > 50)
  {
    pm25BgColor = tft.color565(247, 219, 100); //良
    aqiTxt = "良";
  }
  clk.createSprite(56, 24);
  clk.fillSprite(bgColor);
  clk.fillRoundRect(0, 0, 50, 24, 4, pm25BgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(pm25TextCol);
  clk.drawString(aqiTxt, 25, 13);
  clk.pushSprite(104, 18);
  clk.deleteSprite();

  //天气图标
  wrat.printfweather(170, 15, atoi((sk["weathercode"].as<String>()).substring(1, 3).c_str()));

  //左上角滚动字幕
  scrollText[0] = "实时天气 " + sk["weather"].as<String>();
  scrollText[1] = "风向 " + sk["WD"].as<String>() + sk["WS"].as<String>();

  //解析第二段JSON
  deserializeJson(doc, *cityDZ);
  JsonObject dz = doc.as<JsonObject>();
  scrollText[2] = "今日" + dz["weather"].as<String>();

  deserializeJson(doc, *dataFC);
  JsonObject fc = doc.as<JsonObject>();

  scrollText[3] = "温度" + fc["fd"].as<String>() + "℃ - " + fc["fc"].as<String>() + "℃";

  clk.unloadFont();
}

int currentIndex = 0;
TFT_eSprite clkb = TFT_eSprite(&tft);

void updateBanner()
{
  if (scrollText[currentIndex])
  {
    clkb.setColorDepth(8);
    clkb.loadFont(ZdyLwFont_20);
    clkb.createSprite(150, 30);
    clkb.fillSprite(bgColor);
    clkb.setTextWrap(false);
    clkb.setTextDatum(CC_DATUM);
    clkb.setTextColor(TFT_WHITE, bgColor);
    clkb.drawString(scrollText[currentIndex], 74, 16);
    clkb.pushSprite(10, 45);

    clkb.deleteSprite();
    clkb.unloadFont();

    if (currentIndex >= 3)
      currentIndex = 0; //回第一个
    else
      currentIndex += 1; //准备切换到下一个
  }
}

// 用快速线方法绘制数字
void drawLineFont(uint32_t _x, uint32_t _y, uint32_t _num, uint32_t _size, uint32_t _color)
{
  uint32_t fontSize;
  const LineAtom *fontOne;
  if (_size == 1) // 小号(9 * 14)
  {
    fontOne = smallLineFont[_num];
    fontSize = smallLineFont_size[_num];
    // 绘制前清理字体绘制区域
    tft.fillRect(_x, _y, 9, 14, TFT_BLACK);
  }
  else if (_size == 2) // 中号(18 * 30)
  {
    fontOne = middleLineFont[_num];
    fontSize = middleLineFont_size[_num];
    // 绘制前清理字体绘制区域
    tft.fillRect(_x, _y, 18, 30, TFT_BLACK);
  }
  else if (_size == 3) // 大号(36 * 90)
  {
    fontOne = largeLineFont[_num];
    fontSize = largeLineFont_size[_num];
    // 绘制前清理字体绘制区域
    tft.fillRect(_x, _y, 36, 90, TFT_BLACK);
  }
  else
    return;

  for (uint32_t i = 0; i < fontSize; i++)
  {
    tft.drawFastHLine(fontOne[i].xValue + _x, fontOne[i].yValue + _y, fontOne[i].lValue, _color);
  }
}

int hourSign = 60;
int minSign = 60;
int secSign = 60;

// 日期刷新
void digitalClockDisplay(int forceRefresh = 0)
{
  // 时钟刷新,输入1强制刷新
  int hourNow = hour();  // 获取小时
  int minNow = minute(); // 获取分钟
  int secNow = second(); // 获取秒针

  //小时刷新
  if ((hourNow != hourSign) || (forceRefresh == 1))
  {
    drawLineFont(20, timeY, hourNow / 10, 3, SD_FONT_WHITE);
    drawLineFont(60, timeY, hourNow % 10, 3, SD_FONT_WHITE);
    hourSign = hourNow;
  }

  //分钟刷新
  if ((minNow != minSign) || (forceRefresh == 1))
  {
    drawLineFont(101, timeY, minNow / 10, 3, SD_FONT_YELLOW);
    drawLineFont(141, timeY, minNow % 10, 3, SD_FONT_YELLOW);
    minSign = minNow;
  }

  //秒钟刷新
  if ((secNow != secSign) || (forceRefresh == 1))
  {
    drawLineFont(182, timeY + 30, secNow / 10, 2, SD_FONT_WHITE);
    drawLineFont(202, timeY + 30, secNow % 10, 2, SD_FONT_WHITE);
    secSign = secNow;
  }

  if (forceRefresh == 1)
    forceRefresh = 0;

  /*** 日期 ***/
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);

  //星期
  clk.createSprite(58, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(week(), 29, 16);
  clk.pushSprite(102, 150);
  clk.deleteSprite();

  //月日
  clk.createSprite(95, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(monthDay(), 49, 16);
  clk.pushSprite(5, 150);
  clk.deleteSprite();

  clk.unloadFont();
  /*** 日期 End ***/
}

/*-------- NTP code ----------*/
const int NTP_PACKET_SIZE = 48;     // NTP时间在消息的前48字节中
byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0)
    ; // discard any previously received packets
  // Serial.println("Transmit NTP Request");
  //  get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  // Serial.print(ntpServerName);
  // Serial.print(": ");
  // Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500)
  {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE)
    {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE); // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 = (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      // Serial.println(secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // 无法获取时间时返回0
}

// 向NTP服务器发送请求
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;          // Stratum, or type of clock
  packetBuffer[2] = 6;          // Polling Interval
  packetBuffer[3] = 0xEC;       // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); // NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void resetESP(Button2 &btn)
{
  ESP.reset();
}

void resetWifi(Button2 &btn)
{
  wm.resetSettings();
  delWifiConfig();
  delay(10);
  Serial.println("重置WiFi成功");
  ESP.restart();
}

// 更新时间
void updateTime()
{
  digitalClockDisplay();
}

// 所有需要联网后更新的方法都放在这里
void refreshAll()
{
  if (wifiState == 1)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("WIFI Connected");

      fetchWeather();
      // 其他需要联网的方法写在后面

      WiFi.forceSleepBegin();
      Serial.println("WIFI STATE - SLEEP");
      wifiState = 0;
    }
  }
}

// 打开WIFI
void openWifi()
{
  Serial.println("WIFI STATE - ACTIVE");
  WiFi.forceSleepWake();
  wifiState = 1;
}

// 强制屏幕刷新
void refreshLCD()
{
  updateTime();
  updateBanner();
  openWifi();
}

// 守护线程池
void threadUpdate()
{
  if (controller.shouldRun())
  {
    controller.run();
  }
}

void setup()
{
  btn.setClickHandler(resetESP);
  btn.setLongClickHandler(resetWifi);
  Serial.begin(115200);
  EEPROM.begin(1024);
  // WiFi.forceSleepWake();
  // wm.resetSettings();    //在初始化中使wifi重置, 需重新配置WiFi

  //从eeprom读取背光亮度设置
  if (EEPROM.read(BL_addr) > 0 && EEPROM.read(BL_addr) < 100)
    brightnessLCD = EEPROM.read(BL_addr);
  //从eeprom读取屏幕方向设置
  if (EEPROM.read(Ro_addr) >= 0 && EEPROM.read(Ro_addr) <= 3)
    rotationLCD = EEPROM.read(Ro_addr);

  pinMode(LCD_BL_PIN, OUTPUT);
  analogWrite(LCD_BL_PIN, 1023 - (brightnessLCD * 10));

  tft.begin();          /* TFT init */
  tft.invertDisplay(1); //反转所有显示颜色: 1反转, 0正常
  tft.setRotation(rotationLCD);
  tft.fillScreen(0x0000);
  tft.setTextColor(TFT_BLACK, bgColor);

  targetTime = millis() + 1000;
  readWifiConfig(); //读取存储的wifi信息
  Serial.print("连接至WIFI ");
  Serial.println(wificonf.stassid);
  WiFi.begin(wificonf.stassid, wificonf.stapsw);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  while (WiFi.status() != WL_CONNECTED)
  {
    loading(30);

    if (loadNum >= 194)
    {
//使能web配网后自动将smartconfig配网失效
#if WM_EN
      Web_win();
      Webconfig();
#endif

#if !WM_EN
      SmartConfig();
#endif
      break;
    }
  }
  delay(10);
  while (loadNum < 194) //让动画走完
  {
    loading(1);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID().c_str());
    Serial.print("PSW: ");
    Serial.println(WiFi.psk().c_str());
    strcpy(wificonf.stassid, WiFi.SSID().c_str()); //名称复制
    strcpy(wificonf.stapsw, WiFi.psk().c_str());   //密码复制
    saveWifiConfig();
    readWifiConfig();
  }

  Serial.print("本地IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("启动UDP...");
  Udp.begin(localPort);
  Serial.println("NTP同步...");
  setSyncProvider(getNtpTime);
  setSyncInterval(800);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  int CityCODE = 0;
  for (int cnum = 5; cnum > 0; cnum--)
  {
    CityCODE = CityCODE * 100;
    CityCODE += EEPROM.read(CC_addr + cnum - 1);
    delay(5);
  }
  if (CityCODE >= 101000000 && CityCODE <= 102000000)
    cityCode = CityCODE;
  else
    getCityCode(); //获取城市代码

  tft.fillScreen(TFT_BLACK); //清屏

  TJpgDec.drawJpg(15, 183, temperature, sizeof(temperature)); //温度图标
  TJpgDec.drawJpg(15, 213, humidity, sizeof(humidity));       //湿度图标

  fetchWeather();

  WiFi.forceSleepBegin(); // wifi off
  Serial.println("WIFI休眠...");
  wifiState = 0;

  updateTimeThread.setInterval(300); // 设置执行间隔
  updateTimeThread.onRun(updateTime);

  refreshBannerThread.setInterval(3 * TMS); // 设置刷新间隔
  refreshBannerThread.onRun(updateBanner);

  refreshWifiThread.setInterval(updateWeatherInterval * 60 * TMS); //设置所需间隔 10分钟
  refreshWifiThread.onRun(openWifi);

  refreshAnimationThread.setInterval(TMS / 25); //设置帧率
  refreshAnimationThread.onRun(refreshAnimatedImg);

  infoRefreshThread.setInterval(300);
  infoRefreshThread.onRun(refreshAll);

  controller.run();
}

const uint8_t *animationImg; // 指向关键帧的指针
uint32_t animationSize;      // 指向关键帧大小的指针
void refreshAnimatedImg()
{
#if Animate_Choice
  imgAnim(&animationImg, &animationSize);
  TJpgDec.drawJpg(160, 160, animationImg, animationSize);
#endif
}

void loop()
{
  threadUpdate();         // 线程池守护
  serialListenerUpdate(); // 串口响应
  btn.loop();             // 按钮轮询
}