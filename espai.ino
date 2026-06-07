#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

#include "ST7305_U8g2.h"
#include "adc_bsp.h"
#include "board_pins.h"
#include "i2c_bsp.h"
#include "i2c_equipment.h"

struct AppConfig {
  String ssid;
  String password;
  // 这里只保存 Mac 的 IP/主机名，不保存 Codex 凭据。
  // ESP32 只访问本机 Bun 服务暴露的只读 /api/status，真正读取账号用量的工作留在 Mac 上完成。
  String macHost;
};

struct QuotaWindow {
  String label = "--";
  int usedPercent = 0;
  int remainingPercent = -1;
  long resetsAt = 0;
};

struct QuotaState {
  bool ok = false;
  String error = "等待中";
  String source = "--";
  String planType = "--";
  String limitName = "--";
  String requestUrl = "--";
  int lastHttpCode = 0;
  QuotaWindow primary;
  QuotaWindow secondary;
  long updatedAt = 0;
  uint32_t localUpdatedMs = 0;
};

enum AppPage : uint8_t {
  PAGE_QUOTA = 0,
  PAGE_ENV,
  PAGE_POWER,
  PAGE_DEBUG,
  PAGE_COUNT
};

struct ButtonState {
  uint8_t pin;
  bool stablePressed = false;
  bool lastReading = false;
  bool longReported = false;
  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;
};

static ST7305_U8g2 lcd(
    BoardPins::RLCD_SCK,
    BoardPins::RLCD_MOSI,
    BoardPins::RLCD_DC,
    BoardPins::RLCD_CS,
    BoardPins::RLCD_RST);
static U8G2 *u8g2 = nullptr;

static I2cMasterBus i2cBus(BoardPins::I2C_SCL, BoardPins::I2C_SDA, BoardPins::I2C_PORT);
static Shtc3Port *shtc3 = nullptr;
static Preferences prefs;
static WebServer setupServer(80);
static DNSServer dnsServer;

static AppConfig config;
static QuotaState quota;
static rtcTimeStruct_t lastRtc = {};
static float lastTemp = NAN;
static float lastRh = NAN;
static float batteryVoltage = 0.0f;
static uint8_t batteryLevel = 0;
static int batteryRaw = 0;
static bool setupMode = false;
static AppPage currentPage = PAGE_QUOTA;
static ButtonState bootButton = {BoardPins::BOOT_KEY};
static ButtonState gp18Button = {BoardPins::GP18_KEY};

static uint32_t lastSensorReadMs = 0;
static uint32_t lastClockReadMs = 0;
static uint32_t lastTimeSyncMs = 0;
static uint32_t lastQuotaFetchMs = 0;
static uint32_t lastDrawMs = 0;
static bool timeSynced = false;

// 配置版本用来识别旧固件保存过的 URL/token。
// 这版配置页只让用户填 Mac IP，版本不一致时会清空旧配置，避免继续访问上一次写错的地址。
constexpr uint8_t CONFIG_VERSION = 2;
constexpr uint32_t SENSOR_INTERVAL_MS = 5000;
constexpr uint32_t CLOCK_INTERVAL_MS = 1000;
constexpr uint32_t TIME_SYNC_RETRY_MS = 60000;
constexpr uint32_t TIME_SYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t QUOTA_INTERVAL_MS = 60000;
constexpr uint32_t DRAW_INTERVAL_MS = 1000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;
constexpr uint32_t BUTTON_LONG_MS = 1200;
constexpr uint32_t RESET_HOLD_MS = 5000;
constexpr byte DNS_PORT = 53;

static const char *pageTitle(AppPage page)
{
  switch (page) {
    case PAGE_QUOTA: return "用量";
    case PAGE_ENV: return "环境";
    case PAGE_POWER: return "电源";
    case PAGE_DEBUG: return "诊断";
    default: return "espai";
  }
}

static const uint32_t LUNAR_INFO[] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,
    0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5d0, 0x14573, 0x052d0, 0x0a9a8, 0x0e950, 0x06aa0,
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6,
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x055c0, 0x0ab60, 0x096d5, 0x092e0,
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
    0x05aa0, 0x076a3, 0x096d0, 0x04bd7, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,
    0x0a2e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4,
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160,
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252};

static int daysFromCivil(int y, int m, int d)
{
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}

static int leapMonth(int lunarYear)
{
  if (lunarYear < 1900 || lunarYear > 2099) return 0;
  return LUNAR_INFO[lunarYear - 1900] & 0x0F;
}

static int leapDays(int lunarYear)
{
  int month = leapMonth(lunarYear);
  if (month == 0) return 0;
  return (LUNAR_INFO[lunarYear - 1900] & 0x10000) ? 30 : 29;
}

static int lunarMonthDays(int lunarYear, int lunarMonth)
{
  return (LUNAR_INFO[lunarYear - 1900] & (0x10000 >> lunarMonth)) ? 30 : 29;
}

static int lunarYearDays(int lunarYear)
{
  int days = 348;
  uint32_t info = LUNAR_INFO[lunarYear - 1900];
  for (uint32_t mask = 0x8000; mask > 0x8; mask >>= 1) {
    if (info & mask) days++;
  }
  return days + leapDays(lunarYear);
}

static bool gregorianToLunar(const rtcTimeStruct_t &date, int *lunarYear, int *lunarMonth, int *lunarDay, bool *isLeap)
{
  if (date.year < 1900 || date.year > 2099 || date.month < 1 || date.month > 12 || date.day < 1) {
    return false;
  }

  int offset = daysFromCivil(date.year, date.month, date.day) - daysFromCivil(1900, 1, 31);
  if (offset < 0) {
    return false;
  }

  int year = 1900;
  while (year <= 2099) {
    int yearDays = lunarYearDays(year);
    if (offset < yearDays) break;
    offset -= yearDays;
    year++;
  }
  if (year > 2099) {
    return false;
  }

  int leap = leapMonth(year);
  bool inLeap = false;
  int month = 1;
  while (month <= 12) {
    int monthDays = inLeap ? leapDays(year) : lunarMonthDays(year, month);
    if (offset < monthDays) break;
    offset -= monthDays;
    if (leap == month && !inLeap) {
      inLeap = true;
    } else {
      inLeap = false;
      month++;
    }
  }

  *lunarYear = year;
  *lunarMonth = month;
  *lunarDay = offset + 1;
  *isLeap = inLeap;
  return true;
}

static String lunarDayName(int day)
{
  static const char *nums[] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};
  if (day <= 0 || day > 30) return "--";
  if (day == 10) return "初十";
  if (day == 20) return "二十";
  if (day == 30) return "三十";
  String value;
  if (day < 10) value = "初";
  else if (day < 20) value = "十";
  else value = "廿";
  value += nums[day % 10];
  return value;
}

static String lunarText(const rtcTimeStruct_t &date)
{
  static const char *months[] = {"", "正月", "二月", "三月", "四月", "五月", "六月", "七月", "八月", "九月", "十月", "冬月", "腊月"};
  int lunarYear = 0;
  int lunarMonth = 0;
  int lunarDay = 0;
  bool isLeap = false;
  if (!gregorianToLunar(date, &lunarYear, &lunarMonth, &lunarDay, &isLeap)) {
    return "老历 --";
  }
  String value = "老历 ";
  if (isLeap) value += "闰";
  value += months[lunarMonth];
  value += lunarDayName(lunarDay);
  return value;
}

static void readClock()
{
  Rtc_GetTime(&lastRtc);
}

static void setCnFont()
{
  // 字体存在 Flash 里，不会整包吃掉 RAM；GB2312 体积更大，但能避免中文子集缺字。
  u8g2->setFont(u8g2_font_wqy14_t_gb2312);
}

static void setBlack()
{
  u8g2->setDrawColor(1);
}

static void clearWhite()
{
  // U8g2 的缓冲区清空后就是“纸张”的空白像素；随后只用 drawColor=1 画黑色内容。
  // 不反色、不铺黑底，所以屏幕视觉就是白底黑字。
  u8g2->clearBuffer();
  setBlack();
}

static void drawText(int x, int y, const char *text)
{
  setBlack();
  u8g2->drawUTF8(x, y, text);
}

static void drawCenteredText(int y, const char *text)
{
  int width = u8g2->getUTF8Width(text);
  int x = (BoardPins::LCD_WIDTH - width) / 2;
  drawText(x > 0 ? x : 0, y, text);
}

static void copyTextFit(char *out, size_t outLen, const String &value, int maxWidth)
{
  if (outLen == 0) {
    return;
  }

  const char *src = value.c_str();
  size_t copied = 0;
  out[0] = '\0';
  while (*src != '\0' && copied + 1 < outLen) {
    size_t charLen = 1;
    uint8_t first = (uint8_t)*src;
    if ((first & 0xE0) == 0xC0) charLen = 2;
    else if ((first & 0xF0) == 0xE0) charLen = 3;
    else if ((first & 0xF8) == 0xF0) charLen = 4;

    if (copied + charLen >= outLen) {
      break;
    }

    char next[128];
    memcpy(next, out, copied);
    memcpy(next + copied, src, charLen);
    next[copied + charLen] = '\0';
    if (u8g2->getUTF8Width(next) > maxWidth) {
      break;
    }

    memcpy(out + copied, src, charLen);
    copied += charLen;
    out[copied] = '\0';
    src += charLen;
  }
}

static void drawCard(int x, int y, int w, int h, const char *title)
{
  (void)title;
  setBlack();
  u8g2->drawFrame(x, y, w, h);
}

static void drawChip(int x, int y, const char *text)
{
  int width = u8g2->getUTF8Width(text) + 14;
  setBlack();
  u8g2->drawFrame(x, y, width, 22);
  u8g2->drawUTF8(x + 7, y + 16, text);
}

static void drawProgress(int x, int y, int w, int h, int percent)
{
  // 黑白屏没有颜色层级，所以进度条用细边框 + 实心填充表达比例，保持网页仪表盘的干净感。
  int value = constrain(percent, 0, 100);
  setBlack();
  u8g2->drawFrame(x, y, w, h);
  int fill = ((w - 4) * value) / 100;
  if (fill > 0) {
    u8g2->drawBox(x + 2, y + 2, fill, h - 4);
  }
}

static void drawMetric(int x, int y, const char *label, const char *value, const char *unit)
{
  if (label && label[0] != '\0') {
    setCnFont();
    drawText(x, y, label);
  }
  u8g2->setFont(u8g2_font_logisoso20_tn);
  drawText(x, y + 31, value);
  int valueWidth = u8g2->getUTF8Width(value);
  if (unit && unit[0] != '\0') {
    u8g2->setFont(u8g2_font_9x18_tf);
    drawText(x + valueWidth + 14, y + 28, unit);
  }
}

static void drawHeader(const char *title)
{
  char page[32];
  snprintf(page, sizeof(page), "%s", title);
  u8g2->setFont(u8g2_font_9x18B_tf);
  drawText(18, 27, "espai");
  setCnFont();
  drawText(90, 27, page);
  drawChip(306, 11, WiFi.status() == WL_CONNECTED ? "在线" : "离线");
  u8g2->drawHLine(16, 45, 368);
}

static void drawFooter()
{
  char dateTime[32];
  u8g2->drawHLine(16, 276, 368);
  snprintf(dateTime, sizeof(dateTime), "%04d-%02d-%02d %02d:%02d:%02d",
           lastRtc.year, lastRtc.month, lastRtc.day,
           lastRtc.hour, lastRtc.minute, lastRtc.second);
  u8g2->setFont(u8g2_font_6x13B_tf);
  drawText(18, 294, dateTime);
  setCnFont();
  drawText(252, 294, "左刷");
  drawText(304, 294, "左右翻");
}

static void renderPageShell(const char *title)
{
  // 所有页面共用同一个白底、标题栏、卡片系统，切页时视觉不会跳。
  clearWhite();
  drawHeader(title);
}

static bool loadConfig()
{
  prefs.begin("espai", true);
  uint8_t version = prefs.getUChar("version", 0);
  config.ssid = prefs.getString("ssid", "");
  config.password = prefs.getString("pass", "");
  config.macHost = prefs.getString("host", "");
  prefs.end();

  // 旧版本曾经保存完整 URL 和 token；现在只保存 Mac host。
  // 如果继续沿用旧配置，板子可能还在请求旧地址，所以版本不一致时直接清空，重新进配网页。
  if (version != CONFIG_VERSION) {
    clearConfig();
    config = AppConfig();
    return false;
  }
  return config.ssid.length() > 0 && config.macHost.length() > 0;
}

static void saveConfig(const AppConfig &next)
{
  // Preferences 是 ESP32 的 NVS 存储，掉电后仍然保留。
  // 保存时顺手删除旧版 url/token 字段，避免调试时看到过期数据误判。
  prefs.begin("espai", false);
  prefs.putUChar("version", CONFIG_VERSION);
  prefs.putString("ssid", next.ssid);
  prefs.putString("pass", next.password);
  prefs.putString("host", next.macHost);
  prefs.remove("url");
  prefs.remove("token");
  prefs.end();
}

static void clearConfig()
{
  // 右键/BOOT 长按会调用这里。清空后重启，设备就会重新进入 espai-setup 热点配网。
  prefs.begin("espai", false);
  prefs.clear();
  prefs.end();
}

static void readSensors()
{
  // 温湿度和电池都是低频状态，不需要每次重绘都读。
  // 主循环每隔 SENSOR_INTERVAL_MS 更新一次，然后 UI 只读缓存值，减少 I2C 占用。
  if (shtc3 != nullptr) {
    float temp = NAN;
    float rh = NAN;
    if (shtc3->Shtc3_ReadTempHumi(&temp, &rh) == 0) {
      lastTemp = temp;
      lastRh = rh;
    }
  }
  batteryVoltage = Adc_GetBatteryVoltage(&batteryRaw);
  batteryLevel = Adc_GetBatteryLevel();
}

static bool syncRtcFromNtp(uint32_t timeoutMs)
{
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  lastTimeSyncMs = millis();
  configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");

  struct tm localTime;
  if (!getLocalTime(&localTime, timeoutMs)) {
    Serial.println("NTP sync failed");
    return false;
  }

  Rtc_SetTime(
      localTime.tm_year + 1900,
      localTime.tm_mon + 1,
      localTime.tm_mday,
      localTime.tm_hour,
      localTime.tm_min,
      localTime.tm_sec);
  readClock();
  timeSynced = true;
  Serial.printf("RTC synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                lastRtc.year,
                lastRtc.month,
                lastRtc.day,
                lastRtc.hour,
                lastRtc.minute,
                lastRtc.second);
  return true;
}

static String htmlEscape(const String &value)
{
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("\"", "&quot;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  return escaped;
}

static String configPage()
{
  String page;
  page.reserve(3600);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>espai 配置</title><style>");
  page += F("body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;margin:0;background:#f5f7fa;color:#17202a}");
  page += F("main{max-width:520px;margin:0 auto;padding:28px 20px}");
  page += F("h1{font-size:28px;margin:0 0 8px}.hint{color:#5f6b7a;margin-bottom:22px;line-height:1.5}");
  page += F("label{display:block;font-weight:700;margin:16px 0 6px}");
  page += F("input{box-sizing:border-box;width:100%;font-size:16px;padding:12px;border:1px solid #c8d0d9;border-radius:8px;background:white}");
  page += F("button{width:100%;margin-top:24px;padding:13px 16px;border:0;border-radius:8px;background:#111827;color:white;font-size:17px;font-weight:700}");
  page += F(".foot{font-size:13px;color:#657282;margin-top:18px;line-height:1.5}</style></head><body><main>");
  page += F("<h1>espai 配置</h1><p class='hint'>填写 Wi-Fi 和 Mac IP 即可，设备会自动拼出 http://MacIP:8787/api/status。</p>");
  page += F("<form method='post' action='/save'>");
  page += F("<label>Wi-Fi 名称</label><input name='ssid' required value=\"");
  page += htmlEscape(config.ssid);
  page += F("\">");
  page += F("<label>Wi-Fi 密码</label><input name='password' type='password' value=\"");
  page += htmlEscape(config.password);
  page += F("\">");
  page += F("<label>Mac IP 或主机名</label><input name='host' required placeholder='192.168.1.6' value=\"");
  page += htmlEscape(config.macHost);
  page += F("\">");
  page += F("<button type='submit'>保存并重启</button></form>");
  page += F("<p class='foot'>任何时候长按右键/BOOT 5 秒都会清空配置，重新进入配网模式。</p></main></body></html>");
  return page;
}

static void handleSetupRoot()
{
  setupServer.send(200, "text/html; charset=utf-8", configPage());
}

static void handleSave()
{
  AppConfig next;
  next.ssid = setupServer.arg("ssid");
  next.password = setupServer.arg("password");
  next.macHost = setupServer.arg("host");
  next.ssid.trim();
  next.macHost.trim();

  if (next.ssid.length() == 0 || next.macHost.length() == 0) {
    setupServer.send(400, "text/plain; charset=utf-8", "缺少必填项");
    return;
  }

  saveConfig(next);
  setupServer.send(200, "text/html; charset=utf-8",
                   "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                   "<body style='font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;padding:28px'>"
                   "<h2>已保存</h2><p>espai 正在重启，现在可以关闭这个页面。</p></body>");
  delay(800);
  ESP.restart();
}

static String serviceUrl()
{
  // 配网页只要求填 Mac IP，是为了避免用户手动拼 /api/status 出错。
  // 这里兼容三种输入：192.168.1.6、192.168.1.6:8787、http://192.168.1.6:8787。
  String host = config.macHost;
  host.trim();
  if (host.startsWith("http://") || host.startsWith("https://")) {
    if (host.endsWith("/api/status")) {
      return host;
    }
    int schemeEnd = host.indexOf("://");
    int pathStart = host.indexOf("/", schemeEnd + 3);
    if (pathStart >= 0) {
      host = host.substring(0, pathStart);
    }
    return host.endsWith(":8787") ? host + "/api/status" : host + ":8787/api/status";
  }
  int slash = host.indexOf("/");
  if (slash >= 0) {
    host = host.substring(0, slash);
  }
  return host.indexOf(":") >= 0 ? "http://" + host + "/api/status" : "http://" + host + ":8787/api/status";
}

static void drawSetupScreen()
{
  clearWhite();
  drawHeader("配网");
  drawCard(34, 68, 332, 158, "手机配置");
  u8g2->setFont(u8g2_font_10x20_tf);
  drawCenteredText(124, "espai-setup");
  setCnFont();
  drawCenteredText(162, "打开 http://192.168.4.1");
  drawCenteredText(196, "长按右键 5 秒清空配置");
  drawFooter();
  u8g2->sendBuffer();
}

static void startSetupMode()
{
  // 无配置或 Wi-Fi 连接失败时启动热点。
  // 手机连上 espai-setup 后，DNS 会把任意域名都引到 192.168.4.1，方便打开配置页。
  setupMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("espai-setup");
  delay(200);

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  setupServer.on("/", HTTP_GET, handleSetupRoot);
  setupServer.on("/save", HTTP_POST, handleSave);
  setupServer.onNotFound(handleSetupRoot);
  setupServer.begin();

  Serial.println("Setup AP started: espai-setup");
  Serial.println(WiFi.softAPIP());
  drawSetupScreen();
}

static void drawMessage(const char *title, const char *body)
{
  clearWhite();
  drawHeader("状态");
  drawCard(36, 92, 328, 96, title);
  setCnFont();
  drawCenteredText(150, body);
  drawFooter();
  u8g2->sendBuffer();
}

static bool connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  drawMessage("正在连接 Wi-Fi", config.ssid.c_str());

  uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connect failed");
    drawMessage("Wi-Fi 连接失败", "进入配网模式");
    delay(1000);
    return false;
  }

  Serial.print("Wi-Fi connected: ");
  Serial.println(WiFi.localIP());
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("Mac host: ");
  Serial.println(config.macHost);
  return true;
}

static QuotaWindow parseQuotaWindow(JsonVariantConst value)
{
  // Bun 服务已经把 Codex 返回值整理成 ESP32 容易消费的结构。
  // 这里保持解析很薄，只拿 label、百分比和 reset 时间，避免板子端理解复杂 JSON-RPC。
  QuotaWindow window;
  if (!value.is<JsonObjectConst>()) {
    return window;
  }
  JsonObjectConst object = value.as<JsonObjectConst>();
  window.label = object["label"] | "--";
  window.usedPercent = object["usedPercent"] | 0;
  window.remainingPercent = object["remainingPercent"] | -1;
  window.resetsAt = object["resetsAt"] | 0;
  return window;
}

static bool fetchQuota()
{
  if (WiFi.status() != WL_CONNECTED) {
    quota.ok = false;
    quota.error = "Wi-Fi 离线";
    quota.lastHttpCode = 0;
    return false;
  }

  HTTPClient http;
  http.setTimeout(6000);
  String url = serviceUrl();
  quota.requestUrl = url;
  Serial.print("Fetching quota: ");
  Serial.println(url);

  if (!http.begin(url)) {
    quota.ok = false;
    quota.error = "地址错误";
    quota.lastHttpCode = 0;
    return false;
  }

  int code = http.GET();
  String payload = http.getString();
  http.end();
  quota.lastHttpCode = code;

  if (code != 200) {
    quota.ok = false;
    quota.error = code < 0 ? "连接 " + String(code) : "HTTP " + String(code);
    Serial.printf("Quota HTTP failed: %d %s\n", code, payload.c_str());
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    quota.ok = false;
    quota.error = "JSON 错误";
    Serial.println(error.c_str());
    return false;
  }

  if (!doc["ok"].as<bool>()) {
    quota.ok = false;
    quota.error = doc["error"].as<const char *>() ? doc["error"].as<const char *>() : "服务错误";
    return false;
  }

  quota.ok = true;
  quota.error = "";
  quota.source = doc["source"].as<const char *>() ? doc["source"].as<const char *>() : "--";
  quota.planType = doc["planType"].as<const char *>() ? doc["planType"].as<const char *>() : "--";
  quota.limitName = doc["limitName"].as<const char *>() ? doc["limitName"].as<const char *>() : "--";
  quota.primary = parseQuotaWindow(doc["primary"]);
  quota.secondary = parseQuotaWindow(doc["secondary"]);
  quota.updatedAt = doc["updatedAt"] | 0;
  quota.localUpdatedMs = millis();

  Serial.printf("Quota: %s %d%% remaining, %s %d%% remaining\n",
                quota.primary.label.c_str(),
                quota.primary.remainingPercent,
                quota.secondary.label.c_str(),
                quota.secondary.remainingPercent);
  return true;
}

static long secondsUntil(long epoch)
{
  if (epoch <= 0 || quota.updatedAt <= 0 || quota.localUpdatedMs == 0) {
    return -1;
  }
  long elapsed = (long)((millis() - quota.localUpdatedMs) / 1000);
  return epoch - (quota.updatedAt + elapsed);
}

static void formatDuration(long seconds, char *buffer, size_t len)
{
  if (seconds < 0) {
    snprintf(buffer, len, "--");
    return;
  }
  long days = seconds / 86400;
  long hours = (seconds % 86400) / 3600;
  long minutes = (seconds % 3600) / 60;
  if (days > 0) {
    snprintf(buffer, len, "%ld天%ld时", days, hours);
  } else if (hours > 0) {
    snprintf(buffer, len, "%ld时%ld分", hours, minutes);
  } else {
    snprintf(buffer, len, "%ld分", minutes);
  }
}

static void drawQuotaMini(int x, int y, int w, const QuotaWindow &window, const char *fallbackLabel)
{
  char line[72];
  char resetText[20];
  int remaining = window.remainingPercent >= 0 ? window.remainingPercent : 0;
  const char *label = window.label.length() > 0 ? window.label.c_str() : fallbackLabel;
  formatDuration(secondsUntil(window.resetsAt), resetText, sizeof(resetText));

  setCnFont();
  snprintf(line, sizeof(line), "%s %d%%", label, remaining);
  drawText(x, y, line);
  snprintf(line, sizeof(line), "重置 %s", resetText);
  drawText(x, y + 18, line);
  drawProgress(x, y + 26, w, 9, remaining);
}

static void drawQuotaPage()
{
  char line[96];
  int primaryRemaining = quota.primary.remainingPercent >= 0 ? quota.primary.remainingPercent : 0;

  renderPageShell("用量");
  drawCard(18, 54, 364, 124, "Codex 剩余额度");

  u8g2->setFont(u8g2_font_logisoso50_tn);
  snprintf(line, sizeof(line), "%d", primaryRemaining);
  int numberWidth = u8g2->getStrWidth(line);
  int numberX = 194 - (numberWidth / 2);
  drawText(numberX, 127, line);
  u8g2->setFont(u8g2_font_9x18B_tf);
  drawText(numberX + numberWidth + 8, 125, "%");

  setCnFont();
  snprintf(line, sizeof(line), "%s 窗口  计划 %s", quota.primary.label.c_str(), quota.planType.c_str());
  drawCenteredText(160, line);

  drawCard(18, 190, 174, 76, "5 小时");
  drawQuotaMini(32, 212, 146, quota.primary, "5小时");
  drawCard(208, 190, 174, 76, "7 天");
  drawQuotaMini(222, 212, 146, quota.secondary, "7天");
  drawFooter();
  u8g2->sendBuffer();
}

static void drawEnvPage()
{
  char value[48];
  char dateLine[40];
  char timeLine[16];
  char lunarLine[40];
  renderPageShell("环境");

  drawCard(18, 54, 174, 82, "温度");
  if (isnan(lastTemp)) snprintf(value, sizeof(value), "--");
  else snprintf(value, sizeof(value), "%.1f", lastTemp);
  drawMetric(34, 80, "SHTC3", value, "C");

  drawCard(208, 54, 174, 82, "湿度");
  if (isnan(lastRh)) snprintf(value, sizeof(value), "--");
  else snprintf(value, sizeof(value), "%.1f", lastRh);
  drawMetric(224, 80, "相对湿度", value, "%");

  drawCard(18, 144, 364, 122, "时间");
  snprintf(dateLine, sizeof(dateLine), "%04d年%02d月%02d日", lastRtc.year, lastRtc.month, lastRtc.day);
  snprintf(timeLine, sizeof(timeLine), "%02d:%02d:%02d", lastRtc.hour, lastRtc.minute, lastRtc.second);
  copyTextFit(lunarLine, sizeof(lunarLine), lunarText(lastRtc), 320);
  setCnFont();
  drawText(34, 171, dateLine);
  u8g2->setFont(u8g2_font_logisoso24_tn);
  drawText(34, 207, timeLine);
  setCnFont();
  drawText(34, 238, lunarLine);

  drawFooter();
  u8g2->sendBuffer();
}

static void drawPowerPage()
{
  char value[48];
  renderPageShell("电源");

  drawCard(18, 58, 364, 96, "电池估算");
  snprintf(value, sizeof(value), "%u", batteryLevel);
  drawMetric(34, 94, "剩余", value, "%");
  drawProgress(160, 108, 190, 18, batteryLevel);

  drawCard(18, 174, 174, 70, "电压");
  snprintf(value, sizeof(value), "%.2f", batteryVoltage);
  drawMetric(34, 204, "", value, "V");

  drawCard(208, 174, 174, 70, "ADC 原始值");
  snprintf(value, sizeof(value), "%d", batteryRaw);
  drawMetric(224, 204, "", value, "");

  drawFooter();
  u8g2->sendBuffer();
}

static void drawDebugLine(int y, const char *label, const String &value)
{
  char text[88];
  setCnFont();
  drawText(30, y, label);
  setCnFont();
  copyTextFit(text, sizeof(text), value, 238);
  drawText(116, y, text);
}

static void drawDebugPage()
{
  char text[48];
  renderPageShell("诊断");
  drawCard(18, 58, 364, 198, "请求状态");
  drawDebugLine(96, "Mac", config.macHost);
  drawDebugLine(118, "URL", quota.requestUrl);
  drawDebugLine(140, "来源", quota.source);
  drawDebugLine(162, "错误", quota.ok ? String("无") : quota.error);
  snprintf(text, sizeof(text), "%d", quota.lastHttpCode);
  drawDebugLine(184, "HTTP", String(text));
  snprintf(text, sizeof(text), "%ld 秒", quota.localUpdatedMs == 0 ? -1L : (long)((millis() - quota.localUpdatedMs) / 1000));
  drawDebugLine(206, "刷新", String(text));
  drawDebugLine(228, "本机IP", WiFi.localIP().toString());
  drawFooter();
  u8g2->sendBuffer();
}

static void drawCurrentPage()
{
  switch (currentPage) {
    case PAGE_QUOTA: drawQuotaPage(); break;
    case PAGE_ENV: drawEnvPage(); break;
    case PAGE_POWER: drawPowerPage(); break;
    case PAGE_DEBUG: drawDebugPage(); break;
    default: drawQuotaPage(); break;
  }
}

static void changePage(int delta)
{
  int next = (int)currentPage + delta;
  if (next < 0) next = PAGE_COUNT - 1;
  if (next >= PAGE_COUNT) next = 0;
  currentPage = (AppPage)next;
  drawCurrentPage();
}

static void handleShortPress(uint8_t pin)
{
  // 短按只负责翻页：GP18 是左键，BOOT 是右键。
  // 配网模式下网页服务更重要，所以不响应普通翻页。
  if (setupMode) {
    return;
  }
  if (pin == BoardPins::BOOT_KEY) {
    changePage(1);
  } else if (pin == BoardPins::GP18_KEY) {
    changePage(-1);
  }
}

static void handleLongPress(uint8_t pin)
{
  if (pin == BoardPins::BOOT_KEY) {
    clearConfig();
    drawMessage("配置已清空", "正在重启配网");
    delay(700);
    ESP.restart();
  }
  if (!setupMode && pin == BoardPins::GP18_KEY) {
    drawMessage("正在刷新", "请求 Mac 服务");
    lastQuotaFetchMs = millis();
    fetchQuota();
    drawCurrentPage();
  }
}

static void pollButton(ButtonState &button)
{
  // 两个按键都是低电平按下。这里先做 35ms 防抖，再区分短按和长按。
  // BOOT/右键的长按阈值更长，因为它会清空配置；GP18/左键长按只触发立即刷新。
  bool reading = digitalRead(button.pin) == LOW;
  uint32_t now = millis();

  if (reading != button.lastReading) {
    button.lastReading = reading;
    button.changedAt = now;
  }

  if (now - button.changedAt < BUTTON_DEBOUNCE_MS || reading == button.stablePressed) {
    if (button.stablePressed && !button.longReported) {
      uint32_t holdMs = button.pin == BoardPins::BOOT_KEY ? RESET_HOLD_MS : BUTTON_LONG_MS;
      if (now - button.pressedAt >= holdMs) {
        button.longReported = true;
        handleLongPress(button.pin);
      }
    }
    return;
  }

  button.stablePressed = reading;
  if (button.stablePressed) {
    button.pressedAt = now;
    button.longReported = false;
  } else if (!button.longReported) {
    handleShortPress(button.pin);
  }
}

void setup()
{
  // 启动顺序：
  // 1. 初始化串口、按键、屏幕、RTC、ADC、温湿度传感器。
  // 2. 读取 NVS 配置；没有配置就开热点配网。
  // 3. 有配置则连 Wi-Fi，并向 Mac 上的 Bun 服务拉取 Codex 用量。
  Serial.begin(115200);
  delay(500);
  Serial.println("\nespai Codex quota dashboard");

  pinMode(BoardPins::BOOT_KEY, INPUT_PULLUP);
  pinMode(BoardPins::GP18_KEY, INPUT_PULLUP);

  lcd.begin(0, U8G2_R1);
  u8g2 = lcd.getU8g2();

  Rtc_Setup(&i2cBus, BoardPins::RTC_ADDR);
  readClock();
  Adc_PortInit();
  shtc3 = new Shtc3Port(i2cBus);
  readSensors();

  // If there is no current config, stay in AP mode so setup can be done from a phone.
  if (!loadConfig()) {
    startSetupMode();
    return;
  }

  if (!connectWiFi()) {
    startSetupMode();
    return;
  }

  drawMessage("校准时间", "正在同步网络时间");
  syncRtcFromNtp(5000);

  drawMessage("加载用量", "请求 Mac 服务");
  fetchQuota();
  drawCurrentPage();
}

void loop()
{
  // loop 保持“短任务轮询”：按键、网页配网、传感器、Wi-Fi、quota、重绘分开计时。
  // 这样即使某一次 HTTP 失败，页面和按键仍然能继续响应。
  pollButton(bootButton);
  pollButton(gp18Button);

  if (setupMode) {
    dnsServer.processNextRequest();
    setupServer.handleClient();
    delay(10);
    return;
  }

  uint32_t now = millis();
  if (now - lastClockReadMs >= CLOCK_INTERVAL_MS) {
    lastClockReadMs = now;
    readClock();
  }

  if (now - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = now;
    readSensors();
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    quota.ok = false;
    quota.error = "Wi-Fi 重连中";
    timeSynced = false;
  } else if ((!timeSynced && now - lastTimeSyncMs >= TIME_SYNC_RETRY_MS) ||
             (timeSynced && now - lastTimeSyncMs >= TIME_SYNC_INTERVAL_MS)) {
    syncRtcFromNtp(timeSynced ? 1500 : 3000);
  }

  if (now - lastQuotaFetchMs >= QUOTA_INTERVAL_MS || lastQuotaFetchMs == 0) {
    lastQuotaFetchMs = now;
    fetchQuota();
  }

  if (now - lastDrawMs >= DRAW_INTERVAL_MS) {
    lastDrawMs = now;
    drawCurrentPage();
  }

  delay(20);
}
