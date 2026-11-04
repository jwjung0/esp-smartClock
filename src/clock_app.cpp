#include "clock_app.h"

#include <Arduino.h>

#if defined(FIRMWARE_MODE_APP)

#include "bl_ctrl.h"
#include "bme_sensor.h"
#include "board_pins.h"
#include "data_ingest.h"
#include "light_sensor.h"
#include "market_fetch.h"
#include "power_policy.h"
#include "time_sync.h"
#include "weather_fetch.h"
#include "wifi_config.h"

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ui.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const uint16_t kScreenW = 240;
static const uint16_t kScreenH = 240;
static const uint32_t kNtpWaitMs = 15000;
static const uint32_t kBootConfigMs = 10UL * 60UL * 1000UL; // portal window
static const uint32_t kDailySyncMs = 24UL * 60UL * 60UL * 1000UL;
static const uint32_t kClimateUiMs = 5000; // refresh ui_temp / ui_humid
static const uint32_t kLuxBlMs = 3000; // scan frequency for ambient sensor (VEML7700)
static const uint32_t kProgressAnimMs = 500;
static const uint32_t kCreepIntervalMs = 60;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[kScreenW * kScreenH / 10];
static TFT_eSPI tft = TFT_eSPI(kScreenW, kScreenH);
static bool onHomeScreen = false;

static uint32_t bootConfigUntilMs = 0;
static uint32_t nextNtpSyncMs = 0;
static uint32_t nextClimateUiMs = 0;
static uint32_t nextLuxBlMs = 0;
static bool bootConfigOpen = false;
static int loadingCreepCeiling = 0;
static uint32_t lastCreepMs = 0;
static bool idleSleepActive = false;

static void setBacklight(bool on) { blCtrlSetOn(on); }

// Cast before | — LV_PART_* and LV_STATE_* are different enums (GCC warning).
static const lv_style_selector_t kMain =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) |
    static_cast<lv_style_selector_t>(LV_STATE_DEFAULT);
static const lv_style_selector_t kIndicator =
    static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
    static_cast<lv_style_selector_t>(LV_STATE_DEFAULT);

// HH/MM/SS as two fixed-width digit cells (proportional fonts won't shift).
struct DigitPair {
  lv_obj_t *tens;
  lv_obj_t *ones;
};
static DigitPair hhPair = {};
static DigitPair mmPair = {};
static DigitPair ssPair = {};
static lv_obj_t *colonDotTop = nullptr;
static lv_obj_t *colonDotBot = nullptr;

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area,
                          lv_color_t *color_p) {
  const uint32_t w = (area->x2 - area->x1 + 1);
  const uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

static void my_touchpad_read(lv_indev_drv_t * /*indev_driver*/,
                             lv_indev_data_t *data) {
  data->state = LV_INDEV_STATE_REL;
}

static void applyClockColors() {
  // Portal "clock" color → HH / MM / colon dots; "date" → date + weekday.
  const lv_color_t clock = lv_color_hex(wifiConfigClockColor());
  const lv_color_t date = lv_color_hex(wifiConfigDateColor());
  if (ui_date)
    lv_obj_set_style_text_color(ui_date, date, kMain);
  if (ui_wkday)
    lv_obj_set_style_text_color(ui_wkday, date, kMain);
  for (lv_obj_t *d : {hhPair.tens, hhPair.ones, mmPair.tens, mmPair.ones}) {
    if (d)
      lv_obj_set_style_text_color(d, clock, kMain);
  }
  for (lv_obj_t *dot : {colonDotTop, colonDotBot}) {
    if (dot)
      lv_obj_set_style_bg_color(dot, clock, kMain);
  }
}

static void styleSolidBlack(lv_obj_t *obj) {
  if (!obj)
    return;
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), kMain);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, kMain);
}

static lv_coord_t maxDigitGlyphWidth(const lv_font_t *font,
                                     lv_coord_t letterSpace) {
  lv_coord_t maxW = 0;
  for (char d = '0'; d <= '9'; ++d) {
    char s[2] = {d, '\0'};
    const lv_coord_t w =
        lv_txt_get_width(s, 1, font, letterSpace, LV_TEXT_FLAG_NONE);
    if (w > maxW)
      maxW = w;
  }
  return maxW;
}

// Replace a 2-digit SquareLine label with two fixed digit slots (no
// proportional shift).
static void setupDigitPair(DigitPair *pair, lv_obj_t *host) {
  if (!pair || !host || !ui_home)
    return;
  if (pair->tens) // already built
    return;

  const lv_font_t *font = lv_obj_get_style_text_font(host, LV_PART_MAIN);
  if (!font)
    return;

  const lv_color_t color = lv_obj_get_style_text_color(host, LV_PART_MAIN);
  const lv_coord_t letterSpace =
      lv_obj_get_style_text_letter_space(host, LV_PART_MAIN);
  const lv_coord_t digitW = maxDigitGlyphWidth(font, letterSpace) + 2;
  const lv_coord_t pairW = digitW * 2;
  const lv_coord_t lineH = lv_font_get_line_height(font) + 2;
  // Use style offsets (SquareLine ALIGN_CENTER), NOT lv_obj_get_x/y (absolute
  // after layout).
  const lv_coord_t x = lv_obj_get_style_x(host, LV_PART_MAIN);
  const lv_coord_t y = lv_obj_get_style_y(host, LV_PART_MAIN);

  lv_obj_add_flag(host, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *box = lv_obj_create(ui_home);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, pairW, lineH);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(box, LV_ALIGN_CENTER, x, y);

  pair->tens = lv_label_create(box);
  pair->ones = lv_label_create(box);
  for (lv_obj_t *dig : {pair->tens, pair->ones}) {
    lv_obj_set_width(dig, digitW);
    lv_label_set_long_mode(dig, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(dig, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(dig, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(dig, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(dig, letterSpace, LV_PART_MAIN);
    lv_label_set_text(dig, "0");
  }
  lv_obj_align(pair->tens, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(pair->ones, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void setDigitPair(const DigitPair &pair, int value) {
  if (!pair.tens || !pair.ones)
    return;
  if (value < 0)
    value = 0;
  char t[2] = {static_cast<char>('0' + ((value / 10) % 10)), '\0'};
  char o[2] = {static_cast<char>('0' + (value % 10)), '\0'};
  if (strcmp(lv_label_get_text(pair.tens), t) != 0)
    lv_label_set_text(pair.tens, t);
  if (strcmp(lv_label_get_text(pair.ones), o) != 0)
    lv_label_set_text(pair.ones, o);
}

// Blink two 6px dots between HH and MM (ui_colon1 removed from SquareLine).
static void setupColonDots() {
  if (!ui_home || !ui_hh1 || !ui_mm1 || colonDotTop)
    return;

  // Midpoint between HH/MM style offsets (ALIGN_CENTER).
  const lv_coord_t hx = lv_obj_get_style_x(ui_hh1, LV_PART_MAIN);
  const lv_coord_t mx = lv_obj_get_style_x(ui_mm1, LV_PART_MAIN);
  const lv_coord_t hy = lv_obj_get_style_y(ui_hh1, LV_PART_MAIN);
  const lv_coord_t cx = (hx + mx) / 2;
  const lv_coord_t cy = hy;
  const lv_color_t color = lv_color_hex(wifiConfigClockColor());

  for (int i = 0; i < 2; ++i) {
    lv_obj_t *dot = lv_obj_create(ui_home);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dot, LV_ALIGN_CENTER, cx, cy + (i == 0 ? -10 : 10));
    if (i == 0)
      colonDotTop = dot;
    else
      colonDotBot = dot;
  }
}

// Lock label width to widest glyphs so "1"→"8" etc. does not shift neighbors.
static void fixDigitLabelWidth(lv_obj_t *label, uint8_t glyphCount,
                               const char *glyphs, lv_text_align_t align) {
  if (!label || glyphCount == 0)
    return;

  const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  if (!font)
    return;

  const lv_coord_t letterSpace =
      lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);

  lv_coord_t maxW = 0;
  for (const char *p = glyphs; *p; ++p) {
    char s[2] = {*p, '\0'};
    const lv_coord_t w =
        lv_txt_get_width(s, 1, font, letterSpace, LV_TEXT_FLAG_NONE);
    if (w > maxW)
      maxW = w;
  }

  const lv_coord_t width =
      (lv_coord_t)(maxW * glyphCount + letterSpace * (glyphCount - 1) + 4);

  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_min_width(label, width, LV_PART_MAIN);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
}

static void fixNumericLabelWidths() {
  static const char kDigits[] = "0123456789";

  // Time: true per-digit slots (fixes seconds shift with Orbitron).
  // Must run before layout update so we read SquareLine style offsets.
  setupDigitPair(&hhPair, ui_hh1);
  setupDigitPair(&mmPair, ui_mm1);
  setupDigitPair(&ssPair, ui_ss1);
  setupColonDots();

  // Keep ui_temp / ui_current_temp as SIZE_CONTENT — forcing a wide box with
  // LV_ALIGN_CENTER grows both edges and overlaps the fixed °C labels.
  fixDigitLabelWidth(ui_humid, 2, kDigits, LV_TEXT_ALIGN_CENTER);
  fixDigitLabelWidth(ui_current_humid, 2, kDigits, LV_TEXT_ALIGN_CENTER);

  if (ui_date) {
    const lv_font_t *font = lv_obj_get_style_text_font(ui_date, LV_PART_MAIN);
    if (font) {
      const char *sample = "00.00.00";
      const lv_coord_t w =
          lv_txt_get_width(sample, strlen(sample), font, 0, LV_TEXT_FLAG_NONE) +
          4;
      lv_label_set_long_mode(ui_date, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_min_width(ui_date, w, LV_PART_MAIN);
      lv_obj_set_width(ui_date, w);
      lv_obj_set_style_text_align(ui_date, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
  }
  if (ui_wkday) {
    const lv_font_t *font = lv_obj_get_style_text_font(ui_wkday, LV_PART_MAIN);
    if (font) {
      const char *sample = "WWW";
      const lv_coord_t w =
          lv_txt_get_width(sample, strlen(sample), font, 0, LV_TEXT_FLAG_NONE) +
          4;
      lv_label_set_long_mode(ui_wkday, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_min_width(ui_wkday, w, LV_PART_MAIN);
      lv_obj_set_width(ui_wkday, w);
      lv_obj_set_style_text_align(ui_wkday, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
  }
}

static void applyDarkTheme() {
  const lv_color_t white = lv_color_hex(0xFFFFFF);

  styleSolidBlack(ui_home);
  styleSolidBlack(ui_loading);
  lv_disp_set_bg_color(NULL, lv_color_hex(0x000000));
  lv_disp_set_bg_opa(NULL, LV_OPA_COVER);

  if (ui_box1) {
    lv_obj_set_style_border_color(ui_box1, lv_color_hex(0x555555), kMain);
    lv_obj_set_style_radius(ui_box1, 4, kMain);
  }

  if (ui_connect)
    lv_obj_set_style_text_color(ui_connect, white, kMain);
  if (ui_progress) {
    lv_obj_set_style_bg_color(ui_progress, lv_color_hex(0x333333), kMain);
    lv_obj_set_style_bg_opa(ui_progress, LV_OPA_COVER, kMain);
    lv_obj_set_style_radius(ui_progress, 4, kMain);
    lv_obj_set_style_anim_time(ui_progress, kProgressAnimMs, kMain);
    lv_obj_set_style_bg_color(ui_progress, lv_color_hex(0xFDDF03), kIndicator);
    lv_obj_set_style_bg_opa(ui_progress, LV_OPA_COVER, kIndicator);
    lv_obj_set_style_radius(ui_progress, 4, kIndicator);
    lv_bar_set_range(ui_progress, 0, 100);
    lv_bar_set_value(ui_progress, 0, LV_ANIM_OFF);
  }
  if (ui_temp)
    lv_obj_set_style_text_color(ui_temp, white, kMain);
  if (ui_humid)
    lv_obj_set_style_text_color(ui_humid, white, kMain);
  if (ui_celsus)
    lv_obj_set_style_text_color(ui_celsus, white, kMain);
  if (ui_pct)
    lv_obj_set_style_text_color(ui_pct, white, kMain);
  if (ui_current_temp)
    lv_obj_set_style_text_color(ui_current_temp, white, kMain);
  if (ui_current_humid)
    lv_obj_set_style_text_color(ui_current_humid, white, kMain);
  if (ui_current_celcus)
    lv_obj_set_style_text_color(ui_current_celcus, white, kMain);
  if (ui_current_pct)
    lv_obj_set_style_text_color(ui_current_pct, white, kMain);

  fixNumericLabelWidths();
}

static void lvglPump() {
  lv_timer_handler();
  delay(5);
}

static void setLoadingProgress(int value, bool anim) {
  if (!ui_progress)
    return;
  if (value < 0)
    value = 0;
  if (value > 100)
    value = 100;
  lv_bar_set_value(ui_progress, value, anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void setLoadingCreepCeiling(int ceiling) {
  loadingCreepCeiling = ceiling;
  lastCreepMs = 0;
}

static void loadingCreepTick() {
  if (!ui_progress || loadingCreepCeiling <= 0)
    return;
  const uint32_t now = millis();
  if (lastCreepMs != 0 && (now - lastCreepMs) < kCreepIntervalMs)
    return;
  lastCreepMs = now;

  const int cur = lv_bar_get_value(ui_progress);
  if (cur >= loadingCreepCeiling)
    return;

  int next = cur + 1;
  if (next > loadingCreepCeiling)
    next = loadingCreepCeiling;
  // Hold under 100 until boot finishes.
  if (loadingCreepCeiling < 100 && next > 99)
    next = 99;
  lv_bar_set_value(ui_progress, next, LV_ANIM_ON);
}

static void loadingPump() {
  loadingCreepTick();
  lvglPump();
}

static void updateLoadingIp() {
  if (!ui_ipaddr)
    return;
  // Prefer SoftAP IP on loading — that's the config portal address.
  IPAddress ip = wifiConfigSoftApIp();
  if (ip == IPAddress())
    ip = wifiConfigLocalIp();
  if (ip == IPAddress()) {
    lv_label_set_text(ui_ipaddr, "");
    return;
  }
  lv_label_set_text(ui_ipaddr, ip.toString().c_str());
}

static void showLoading(const char *status) {
  if (ui_connect)
    lv_label_set_text(ui_connect, status);
  updateLoadingIp();
  lv_disp_load_scr(ui_loading);
  onHomeScreen = false;
  loadingPump();
  lv_refr_now(NULL); // full flush before backlight may turn on
}

static bool isLocalNight() {
  time_t sunrise = 0;
  time_t sunset = 0;
  if (weatherFetchGetSunTimes(&sunrise, &sunset)) {
    const time_t now = time(nullptr);
    if (now > 0)
      return now < sunrise || now >= sunset;
  }
  // Fallback before first weather fetch (or missing sys.*).
  struct tm t;
  if (!timeSyncGet(&t))
    return false;
  return t.tm_hour < 6 || t.tm_hour >= 20;
}

// Preferred weather main/id → SquareLine assets on ui_weather.
// Naver: id is wetrCd (5/6 구름많음 → sun|moon+clouds; 7 흐림 → clouds).
// OpenWeather: Clouds 801–802 → sun|moon+clouds; 803–804 → clouds.
static void setWeatherIcon(const char *main) {
  if (!ui_weather)
    return;
  const bool night = isLocalNight();
  const bool fromNaver = weatherFetchIsNaver();
  int weatherId = 0;
  weatherFetchGetId(&weatherId);

  const lv_img_dsc_t *src = &ui_img_575321948; // weather-clouds
  if (main && main[0]) {
    if (strcmp(main, "Clear") == 0)
      src = night ? &ui_img_1018541966 // weather-clear-night
                  : &ui_img_weather_clear_png;
    else if (strcmp(main, "Clouds") == 0) {
      const bool partly =
          fromNaver ? (weatherId == 2 || weatherId == 3 || weatherId == 5 ||
                       weatherId == 6)
                    : (weatherId == 801 || weatherId == 802);
      if (partly)
        src = night ? &ui_img_icon_cloudy_night_png
                    : &ui_img_icon_cloudy_sun_png;
      else
        src = &ui_img_575321948; // overcast / broken / unknown
    } else if (strcmp(main, "Rain") == 0 || strcmp(main, "Drizzle") == 0)
      src = &ui_img_579096672; // weather-rain
    else if (strcmp(main, "Thunderstorm") == 0)
      src = &ui_img_1449682034; // weather-thunder
    else if (strcmp(main, "Snow") == 0)
      src = &ui_img_2047004501; // weather-snow
    else if (strcmp(main, "Tornado") == 0)
      src = &ui_img_1449682034; // no extreme asset — reuse thunder
    else if (strcmp(main, "Mist") == 0 || strcmp(main, "Smoke") == 0 ||
             strcmp(main, "Haze") == 0 || strcmp(main, "Dust") == 0 ||
             strcmp(main, "Fog") == 0 || strcmp(main, "Sand") == 0 ||
             strcmp(main, "Ash") == 0 || strcmp(main, "Squall") == 0)
      src = &ui_img_959432778; // weather-atmosphere
  }
  if (lv_img_get_src(ui_weather) != src)
    lv_img_set_src(ui_weather, src);
}

static void refreshWeatherIconFromCache() {
  char mainBuf[16];
  if (!weatherFetchGet(nullptr, nullptr, mainBuf, sizeof(mainBuf)))
    return;
  setWeatherIcon(mainBuf);
}

static void updateWifiIcon() {
  if (!ui_wifi)
    return;
  // Always visible: wifi asset while radio is on, calendar while radio sleeps.
  const lv_img_dsc_t *src = wifiConfigIsRadioOn()
                                ? &ui_img_539372687  // icons-wifi
                                : &ui_img_537441319; // icon-calendar
  if (lv_img_get_src(ui_wifi) != src)
    lv_img_set_src(ui_wifi, src);
  lv_obj_clear_flag(ui_wifi, LV_OBJ_FLAG_HIDDEN);
}

static void updateOutdoorWeatherUi() {
  float t = 0, h = 0;
  char mainBuf[16];
  if (!weatherFetchGet(&t, &h, mainBuf, sizeof(mainBuf))) {
    if (ui_current_temp)
      lv_label_set_text(ui_current_temp, "--");
    if (ui_current_humid)
      lv_label_set_text(ui_current_humid, "--");
    setWeatherIcon(nullptr);
    return;
  }

  if (ui_current_temp) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", (double)t);
    lv_label_set_text(ui_current_temp, buf);
  }
  if (ui_current_humid) {
    if (isnan(h))
      lv_label_set_text(ui_current_humid, "--");
    else {
      char buf[16];
      snprintf(buf, sizeof(buf), "%.0f", (double)h);
      lv_label_set_text(ui_current_humid, buf);
    }
  }
  setWeatherIcon(mainBuf);
}

// Korean market colors: up=red, down=blue, flat=white (LVGL #RRGGBB recolor).
static uint32_t marketColorForPct(float pct) {
  if (pct > 0.0f)
    return 0xFF3333;
  if (pct < 0.0f)
    return 0x3399FF;
  return 0xFFFFFF;
}

// Format with thousands separators, e.g. 25437.00 → "25,437.00"
static void formatPriceWithCommas(float price, char *out, size_t outLen) {
  if (!out || outLen < 8) {
    if (out && outLen)
      out[0] = '\0';
    return;
  }

  const bool neg = price < 0.0f;
  if (neg)
    price = -price;

  const long whole = (long)price;
  int cents = (int)lroundf((price - (float)whole) * 100.0f);
  long adjWhole = whole;
  if (cents >= 100) {
    cents -= 100;
    adjWhole += 1;
  } else if (cents < 0) {
    cents += 100;
    adjWhole -= 1;
  }

  char digits[16];
  snprintf(digits, sizeof(digits), "%ld", adjWhole < 0 ? -adjWhole : adjWhole);

  size_t used = 0;
  if (neg && used + 1 < outLen)
    out[used++] = '-';

  const size_t len = strlen(digits);
  for (size_t i = 0; i < len; ++i) {
    if (i > 0 && (len - i) % 3 == 0) {
      if (used + 1 >= outLen)
        break;
      out[used++] = ',';
    }
    if (used + 1 >= outLen)
      break;
    out[used++] = digits[i];
  }

  if (used + 4 < outLen)
    snprintf(out + used, outLen - used, ".%02d", cents);
  else
    out[used < outLen ? used : outLen - 1] = '\0';
}

static bool marketBannerVisible(void) {
  return onHomeScreen && marketConfigIsVisible() && ui_market &&
         !lv_obj_has_flag(ui_market, LV_OBJ_FLAG_HIDDEN);
}

static void applyMarketVisibility() {
  if (!ui_market)
    return;
  if (marketConfigIsVisible())
    lv_obj_clear_flag(ui_market, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(ui_market, LV_OBJ_FLAG_HIDDEN);
}

static void updateMarketUi() {
  if (!ui_market)
    return;

  applyMarketVisibility();
  if (!marketConfigIsVisible())
    return;

  lv_label_set_recolor(ui_market, true);
  lv_label_set_long_mode(ui_market, LV_LABEL_LONG_SCROLL_CIRCULAR);

  if (!marketFetchHasData() || marketFetchCount() == 0) {
    lv_label_set_text(ui_market, "KS --.-- --.--%  KQ --.-- --.--%");
    return;
  }

  char line[288];
  size_t used = 0;
  line[0] = '\0';

  for (size_t i = 0; i < marketFetchCount(); ++i) {
    MarketQuote q;
    if (!marketFetchGetAt(i, &q) || !q.valid)
      continue;

    const uint32_t color = marketColorForPct(q.pct);
    char priceBuf[24];
    formatPriceWithCommas(q.price, priceBuf, sizeof(priceBuf));

    char seg[80];
    const int n =
        snprintf(seg, sizeof(seg), "%s#%06X %s %s %+0.2f%%#",
                 used > 0 ? "  " : "", (unsigned)color, q.tag, priceBuf,
                 (double)q.pct);
    if (n <= 0 || (size_t)n >= sizeof(seg))
      continue;
    if (used + (size_t)n + 1 > sizeof(line))
      break;
    memcpy(line + used, seg, (size_t)n);
    used += (size_t)n;
    line[used] = '\0';
  }

  if (used == 0)
    lv_label_set_text(ui_market, "KS --.-- --.--%  KQ --.-- --.--%");
  else
    lv_label_set_text(ui_market, line);
}

enum class ClimateLevel { Low, Norm, High };

static const uint32_t kBarDim = 0x222222;
static const uint32_t kTempLowColor = 0x270CF1;
static const uint32_t kTempNormColor = 0x07AAF6;
static const uint32_t kTempHighColor = 0xF8381E;
static const uint32_t kHumidLowColor = 0xFE9F00;
static const uint32_t kHumidNormColor = 0x04FBA9;
static const uint32_t kHumidHighColor = 0x0490F9;

static ClimateLevel levelFromTemp(float t) {
  if (t < 18.0f)
    return ClimateLevel::Low;
  if (t > 28.0f)
    return ClimateLevel::High;
  return ClimateLevel::Norm;
}

static ClimateLevel levelFromHumid(float h) {
  if (h < 40.0f)
    return ClimateLevel::Low;
  if (h > 60.0f)
    return ClimateLevel::High;
  return ClimateLevel::Norm;
}

static void paintLevelBars(lv_obj_t *low, lv_obj_t *norm, lv_obj_t *high,
                           ClimateLevel active, uint32_t cLow, uint32_t cNorm,
                           uint32_t cHigh, bool hasValue) {
  const uint32_t colors[3] = {
      hasValue && active == ClimateLevel::Low ? cLow : kBarDim,
      hasValue && active == ClimateLevel::Norm ? cNorm : kBarDim,
      hasValue && active == ClimateLevel::High ? cHigh : kBarDim};
  lv_obj_t *bars[3] = {low, norm, high};
  for (int i = 0; i < 3; ++i) {
    if (!bars[i])
      continue;
    lv_obj_set_style_bg_color(bars[i], lv_color_hex(colors[i]), kMain);
    lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, kMain);
  }
}

static void paintClimateUi(bool ok, float t, float h) {
  if (!ok) {
    if (ui_temp)
      lv_label_set_text(ui_temp, "--.-");
    if (ui_humid)
      lv_label_set_text(ui_humid, "--");
    paintLevelBars(ui_temp_low, ui_temp_norm, ui_temp_high, ClimateLevel::Norm,
                   kTempLowColor, kTempNormColor, kTempHighColor, false);
    paintLevelBars(ui_humid_low, ui_humid_norm, ui_humid_high,
                   ClimateLevel::Norm, kHumidLowColor, kHumidNormColor,
                   kHumidHighColor, false);
    return;
  }

  if (ui_temp) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", (double)t);
    lv_label_set_text(ui_temp, buf);
  }
  paintLevelBars(ui_temp_low, ui_temp_norm, ui_temp_high, levelFromTemp(t),
                 kTempLowColor, kTempNormColor, kTempHighColor, true);

  const bool humidOk = bmeSensorHasHumidity() && !isnan(h);
  if (ui_humid) {
    if (!humidOk)
      lv_label_set_text(ui_humid, "--");
    else {
      char humBuf[8];
      snprintf(humBuf, sizeof(humBuf), "%d", (int)lroundf(h));
      lv_label_set_text(ui_humid, humBuf);
    }
  }
  paintLevelBars(ui_humid_low, ui_humid_norm, ui_humid_high,
                 humidOk ? levelFromHumid(h) : ClimateLevel::Norm,
                 kHumidLowColor, kHumidNormColor, kHumidHighColor, humidOk);
}

static void updateClimateUi() {
  float t = 0, h = 0, p = 0;
  paintClimateUi(bmeSensorRead(&t, &h, &p), t, h);
}

static void blinkColon(const struct tm &localTm) {
  if (!colonDotTop || !colonDotBot)
    return;
  const bool on = (localTm.tm_sec & 1) == 0;
  for (lv_obj_t *dot : {colonDotTop, colonDotBot}) {
    if (on)
      lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
  }
}

static void paintTime(const struct tm &localTm) {
  static int lastYday = -1;
  static int lastNight = -1; // -1 unset, 0 day, 1 night

  setDigitPair(hhPair, localTm.tm_hour);
  setDigitPair(mmPair, localTm.tm_min);
  setDigitPair(ssPair, localTm.tm_sec);

  if (localTm.tm_yday != lastYday) {
    if (ui_date) {
      char dateBuf[16];
      timeSyncFormatDate(localTm, dateBuf, sizeof(dateBuf));
      lv_label_set_text(ui_date, dateBuf);
    }
    if (ui_wkday) {
      char wdBuf[8];
      timeSyncFormatWeekday(localTm, wdBuf, sizeof(wdBuf));
      lv_label_set_text(ui_wkday, wdBuf);
    }
    lastYday = localTm.tm_yday;
  }
  {
    // Swap Clear / partly-cloudy day|night icon at sunrise/sunset.
    const int night = isLocalNight() ? 1 : 0;
    if (night != lastNight) {
      lastNight = night;
      refreshWeatherIconFromCache();
    }
  }
  blinkColon(localTm);
}

static void homeUiPump() {
  if (onHomeScreen) {
    struct tm localTm;
    if (timeSyncTick(&localTm)) {
      paintTime(localTm);
      // Full flush stalls SPI with the marquee strip; skip while banner is on.
      if (!marketBannerVisible()) {
        lv_timer_handler();
        lv_refr_now(NULL);
      }
    }
  }
  lvglPump();
}

static void showHome() {
  applyClockColors();
  struct tm localTm;
  if (timeSyncGet(&localTm))
    paintTime(localTm);
  updateClimateUi();
  updateOutdoorWeatherUi();
  updateWifiIcon();
  nextClimateUiMs = millis() + kClimateUiMs;
  lv_disp_load_scr(ui_home);
  onHomeScreen = true;
  // During portal Connect / scan waits, wifiIdle() keeps the clock moving.
  wifiConfigSetIdleCallback(homeUiPump);
  // Apply ticker after home is the active screen so circular scroll starts.
  applyMarketVisibility();
  updateMarketUi();
  (void)marketFetchTakeUpdated();
  (void)marketConfigTakeVisibilityChanged();
  lvglPump();
}

static void waitForNtp() {
  showLoading("Syncing time...");
  setLoadingProgress(40, true);
  setLoadingCreepCeiling(70);
  const uint32_t start = millis();
  while (!timeSyncIsReady() && (millis() - start) < kNtpWaitMs) {
    loadingPump();
  }
  setLoadingProgress(75, true);
  setLoadingCreepCeiling(0);
  if (timeSyncIsReady())
    Serial.println("NTP sync OK");
  else
    Serial.println("NTP sync timeout");
}

static void closeBootConfigWindow() {
  if (!bootConfigOpen)
    return;
  bootConfigOpen = false;
  wifiConfigRadioOff();
  updateWifiIcon();
  Serial.println("Boot config window closed (10 min)");
}

enum class NtpPhase : uint8_t { Idle, Waking, Waiting };

static NtpPhase ntpPhase = NtpPhase::Idle;
static uint32_t ntpWaitStartMs = 0;
static time_t ntpEpochBefore = 0;
static bool ntpHadTime = false;

static bool canIdleLightSleep(void) {
  // Market circular scroll needs frequent LVGL pumps — never deep-idle then.
  return onHomeScreen && !bootConfigOpen && !wifiConfigIsRadioOn() &&
         wifiConfigCmdIdle() && !marketFetchBusy() &&
         ntpPhase == NtpPhase::Idle && !marketConfigIsVisible();
}

static void serviceDailyNtp() {
  if (ntpPhase == NtpPhase::Idle) {
    if ((int32_t)(millis() - nextNtpSyncMs) < 0)
      return;
    Serial.println("Daily NTP sync…");
    powerSetRadioActive(true);
    if (!wifiConfigRequestWake()) {
      powerSetRadioActive(false);
      nextNtpSyncMs = millis() + kDailySyncMs;
      return;
    }
    ntpPhase = NtpPhase::Waking;
    return;
  }

  if (ntpPhase == NtpPhase::Waking) {
    if (!wifiConfigCmdIdle())
      return;
    if (!wifiConfigIsConnected()) {
      ntpPhase = NtpPhase::Idle;
      nextNtpSyncMs = millis() + kDailySyncMs;
      return;
    }
    timeSyncRequestResync();
    struct tm before;
    ntpHadTime = getLocalTime(&before, 0);
    ntpEpochBefore = ntpHadTime ? mktime(&before) : 0;
    ntpWaitStartMs = millis();
    ntpPhase = NtpPhase::Waiting;
    return;
  }

  struct tm nowTm;
  bool done = (millis() - ntpWaitStartMs) >= kNtpWaitMs;
  if (!done && getLocalTime(&nowTm, 0)) {
    const time_t t1 = mktime(&nowTm);
    if (!ntpHadTime || t1 != ntpEpochBefore ||
        (millis() - ntpWaitStartMs) > 3000)
      done = true;
  }
  if (!done)
    return;

  wifiConfigRadioOff();
  ntpPhase = NtpPhase::Idle;
  nextNtpSyncMs = millis() + kDailySyncMs;
  Serial.println("Daily NTP sync done; WiFi off");
}

void clockAppSetup() {
  // UI preempts the portal task (priority 1), especially on unicore S2 Mini.
  vTaskPrioritySet(NULL, 3);

  // Hold backlight off before anything else — TFT_eSPI used to force it on
  // during ST7789 init while GRAM was still white.
  blCtrlLoadPrefs();
  setBacklight(false);

  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.printf("smartClock app + LVGL %u.%u.%u\n", lv_version_major(),
                lv_version_minor(), lv_version_patch());

  lv_init();

  tft.begin();
  // 270° clockwise (90° CCW) on 240x240 ST7789.
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  Serial.printf("TFT setup %s  MOSI=%d SCLK=%d CS=%d DC=%d RST=%d BL=%d\n",
                USER_SETUP_INFO, PIN_TFT_MOSI, PIN_TFT_SCLK, PIN_TFT_CS,
                PIN_TFT_DC, PIN_TFT_RST, PIN_TFT_BL);

  lv_disp_draw_buf_init(&draw_buf, buf, NULL, kScreenW * kScreenH / 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = kScreenW;
  disp_drv.ver_res = kScreenH;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  ui_init();
  applyDarkTheme();
  // Creep progress during STA connect waits inside wifiConfigBegin.
  wifiConfigSetIdleCallback(loadingPump);

  showLoading("Connecting WiFi...");
  setLoadingProgress(5, false);
  setLoadingCreepCeiling(35);
  setBacklight(true);
  Serial.println("UI init done (loading)");

  wifiConfigBegin();
  updateLoadingIp(); // STA or SoftAP IP once WiFi is up
  setLoadingProgress(40, true);
  setLoadingCreepCeiling(0);
  loadingPump();

  if (wifiConfigIsConnected()) {
    timeSyncBegin();
    waitForNtp();
  } else {
    showLoading("WiFi setup mode");
    setLoadingProgress(40, true);
    setLoadingCreepCeiling(40);
    const uint32_t t0 = millis();
    while (millis() - t0 < 1500)
      loadingPump();
    setLoadingCreepCeiling(0);
  }

  bootConfigOpen = wifiConfigIsRadioOn();
  bootConfigUntilMs = millis() + kBootConfigMs;
  nextNtpSyncMs = millis() + kDailySyncMs;
  if (bootConfigOpen)
    Serial.println("Boot config window: 10 min (then WiFi off)");

  bmeSensorBegin();
  lightSensorBegin();
  nextLuxBlMs = millis() + kLuxBlMs;
  // Apply lux-based duty once sensor is up (BL already on from loading).
  blCtrlUpdateFromLux();
  weatherFetchBegin();
  marketFetchBegin();
  // Startup only: stay on the loading screen until quotes are in hand.
  // Later refreshes stay on the market task and paint when ready.
  if (wifiConfigIsConnected() && marketConfigIsVisible()) {
    showLoading("Fetching market...");
    setLoadingProgress(80, true);
    setLoadingCreepCeiling(95);
    marketFetchRequestRefresh();
    const uint32_t marketWaitStart = millis();
    const uint32_t kBootMarketWaitMs = 30000;
    while (!marketFetchHasData() &&
           (millis() - marketWaitStart) < kBootMarketWaitMs) {
      marketFetchService();
      loadingPump();
    }
  }
  dataIngestBegin();

  setLoadingProgress(100, true);
  setLoadingCreepCeiling(0);
  const uint32_t doneStart = millis();
  while (millis() - doneStart < 350)
    loadingPump();

  showHome();
  powerSetRadioActive(wifiConfigIsRadioOn());
  Serial.println("Home screen");
}

void clockAppLoop() {
  bool didTimeTick = false;
  bool uiDirty = false;

  const bool radioOn = wifiConfigIsRadioOn();
  const bool marketScroll = marketBannerVisible();
  // 240 MHz only while Wi‑Fi is up; idle / market scroll run at 80 MHz.
  powerSetRadioActive(radioOn);
  powerSetUiBoost(false);
  const bool needFastLvgl = bootConfigOpen || radioOn || marketScroll;

  // Keep UI alive first — WiFi/HTTP/I2C must not starve the clock.
  if (onHomeScreen) {
    struct tm localTm;
    if (timeSyncTick(&localTm)) {
      paintTime(localTm);
      didTimeTick = true;
      // Never lv_refr_now while the market is scrolling — it force-flushes
      // the whole dirty marquee and freezes banner + seconds together.
      // With market off, a one-shot flush keeps digits from fading.
      if (!marketScroll) {
        lv_timer_handler();
        lv_refr_now(NULL);
      }
    }
    // Climate every 5 s — AHT20 needs ~80 ms; Request/Take avoid delay().
    if ((int32_t)(millis() - nextClimateUiMs) >= 0) {
      bmeSensorRequest();
      nextClimateUiMs = millis() + kClimateUiMs;
    }
    {
      float t = 0, h = 0, p = 0;
      if (bmeSensorTake(&t, &h, &p)) {
        paintClimateUi(true, t, h);
        uiDirty = true;
      }
    }
    if ((int32_t)(millis() - nextLuxBlMs) >= 0) {
      blCtrlUpdateFromLux();
      nextLuxBlMs = millis() + kLuxBlMs;
    }
  }

  if (wifiConfigTakeColorsChanged()) {
    applyClockColors();
    uiDirty = true;
  }
  if (marketConfigTakeVisibilityChanged()) {
    applyMarketVisibility();
    if (marketConfigIsVisible())
      updateMarketUi();
    uiDirty = true;
  }

  if (needFastLvgl || didTimeTick || uiDirty)
    lv_timer_handler();

  // Portal DNS/HTTP runs on its own task — do not call wifiConfigLoop here.
  if (bootConfigOpen) {
    if ((int32_t)(millis() - bootConfigUntilMs) >= 0)
      closeBootConfigWindow();
  } else {
    serviceDailyNtp();
  }

  marketFetchService();

  dataIngestLoop(!bootConfigOpen);
  if (dataIngestTakeRadioOff()) {
    wifiConfigRadioOff();
    updateWifiIcon();
  }
  if (onHomeScreen && weatherFetchTakeUpdated()) {
    updateOutdoorWeatherUi();
    lv_timer_handler();
  }
  if (onHomeScreen && marketFetchTakeUpdated()) {
    updateMarketUi();
    lv_timer_handler();
  }
  if (onHomeScreen)
    updateWifiIcon();

  if (canIdleLightSleep()) {
    if (!idleSleepActive) {
      idleSleepActive = true;
      Serial.println("Power: idle (CPU 80 MHz, poll ≤100 ms)");
    }
    // Short slices so HH/MM/SS keep ticking; long delay(1s) looked frozen.
    uint64_t sleepUs = powerUsUntilNextSecond();
    if (sleepUs > 100000ULL)
      sleepUs = 100000ULL;
    const uint32_t now = millis();
    auto clampWakeMs = [&](uint32_t deadlineMs) {
      const int32_t left = (int32_t)(deadlineMs - now);
      if (left <= 0)
        return;
      uint64_t us = (uint64_t)left * 1000ULL;
      if (us < 20000ULL)
        us = 20000ULL;
      if (us < sleepUs)
        sleepUs = us;
    };
    clampWakeMs(nextClimateUiMs);
    clampWakeMs(nextLuxBlMs);
    clampWakeMs(nextNtpSyncMs);

    if (!blCtrlAutoEnabled() && blCtrlWantOn()) {
      pinMode(PIN_TFT_BL, OUTPUT);
      digitalWrite(PIN_TFT_BL, HIGH);
      esp_sleep_enable_timer_wakeup(sleepUs);
      esp_light_sleep_start();
      blCtrlReapplyAfterSleep();
    } else {
      delay((uint32_t)(sleepUs / 1000ULL));
    }
  } else {
    if (idleSleepActive) {
      idleSleepActive = false;
      Serial.println("Power: leave idle");
    }
    // Market marquee / portal: ~5 ms LVGL cadence.
    delay(needFastLvgl ? 5 : 30);
  }
}

#else

void clockAppSetup() {}
void clockAppLoop() {}

#endif
