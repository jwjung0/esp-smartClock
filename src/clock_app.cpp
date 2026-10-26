#include "clock_app.h"

#include <Arduino.h>

#if defined(FIRMWARE_MODE_APP)

#include "loading_gif.h"
#include "time_sync.h"
#include "wifi_config.h"

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ui.h>

static const uint16_t kScreenW = 240;
static const uint16_t kScreenH = 240;
static const uint32_t kNtpWaitMs = 15000;
static const uint32_t kBootConfigMs = 10UL * 60UL * 1000UL;   // portal window
static const uint32_t kDailySyncMs = 24UL * 60UL * 60UL * 1000UL;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[kScreenW * kScreenH / 10];
static TFT_eSPI tft = TFT_eSPI(kScreenW, kScreenH);
static lv_obj_t *loadingGif = nullptr;
static bool onHomeScreen = false;
static lv_img_dsc_t loading_gif_dsc;

static uint32_t bootConfigUntilMs = 0;
static uint32_t nextNtpSyncMs = 0;
static bool bootConfigOpen = false;

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
  lv_obj_set_style_text_color(ui_date, lv_color_hex(wifiConfigDateColor()),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(ui_time, lv_color_hex(wifiConfigClockColor()),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void lvglPump() {
  lv_timer_handler();
  delay(5);
}

static void showLoading(const char *status) {
  if (ui_connect)
    lv_label_set_text(ui_connect, status);
  lv_disp_load_scr(ui_loading);
  onHomeScreen = false;
  lvglPump();
}

static void showHome() {
  applyClockColors();
  lv_disp_load_scr(ui_home);
  onHomeScreen = true;
  lvglPump();
}

static void setupLoadingGif() {
  if (!ui_loading_img)
    return;

  memset(&loading_gif_dsc, 0, sizeof(loading_gif_dsc));
  loading_gif_dsc.header.always_zero = 0;
  loading_gif_dsc.header.w = 64;
  loading_gif_dsc.header.h = 64;
  loading_gif_dsc.header.cf = LV_IMG_CF_RAW;
  loading_gif_dsc.data_size = kLoadingGifSize;
  loading_gif_dsc.data = loading_gif_data;

  lv_obj_add_flag(ui_loading_img, LV_OBJ_FLAG_HIDDEN);

  loadingGif = lv_gif_create(ui_loading);
  lv_gif_set_src(loadingGif, &loading_gif_dsc);
  lv_obj_set_x(loadingGif, lv_obj_get_x(ui_loading_img));
  lv_obj_set_y(loadingGif, lv_obj_get_y(ui_loading_img));
  lv_obj_set_align(loadingGif, LV_ALIGN_CENTER);
}

static void waitForNtp() {
  showLoading("Syncing time...");
  const uint32_t start = millis();
  while (!timeSyncIsReady() && (millis() - start) < kNtpWaitMs) {
    wifiConfigLoop();
    lvglPump();
  }
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
  Serial.println("Boot config window closed (10 min)");
}

static void runDailyNtpSync() {
  Serial.println("Daily NTP sync…");
  if (!wifiConfigWakeStaForSync()) {
    nextNtpSyncMs = millis() + kDailySyncMs;
    return;
  }

  timeSyncRequestResync();
  const uint32_t start = millis();
  struct tm before;
  const bool hadTime = getLocalTime(&before, 0);
  const time_t t0 = hadTime ? mktime(&before) : 0;

  while ((millis() - start) < kNtpWaitMs) {
    lvglPump();
    struct tm nowTm;
    if (getLocalTime(&nowTm, 0)) {
      const time_t t1 = mktime(&nowTm);
      // Accept any valid time after a short settle, or a changed epoch.
      if (!hadTime || t1 != t0 || (millis() - start) > 3000)
        break;
    }
  }

  wifiConfigRadioOff();
  nextNtpSyncMs = millis() + kDailySyncMs;
  Serial.println("Daily NTP sync done; WiFi off");
}

void clockAppSetup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.printf("smartClock app + LVGL %u.%u.%u\n", lv_version_major(),
                lv_version_minor(), lv_version_patch());

  lv_init();

  tft.begin();
  tft.setRotation(3);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

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
  setupLoadingGif();
  wifiConfigSetIdleCallback(lvglPump);

  showLoading("Connecting WiFi...");
  Serial.println("UI init done (loading)");

  wifiConfigBegin();

  if (wifiConfigIsConnected()) {
    timeSyncBegin();
    waitForNtp();
  } else {
    showLoading("WiFi setup mode");
    const uint32_t t0 = millis();
    while (millis() - t0 < 1500)
      lvglPump();
  }

  bootConfigOpen = wifiConfigIsRadioOn();
  bootConfigUntilMs = millis() + kBootConfigMs;
  nextNtpSyncMs = millis() + kDailySyncMs;
  if (bootConfigOpen)
    Serial.println("Boot config window: 10 min (then WiFi off)");

  showHome();
  Serial.println("Home screen");
}

void clockAppLoop() {
  if (bootConfigOpen) {
    wifiConfigLoop();
    if ((int32_t)(millis() - bootConfigUntilMs) >= 0)
      closeBootConfigWindow();
  } else if ((int32_t)(millis() - nextNtpSyncMs) >= 0) {
    runDailyNtpSync();
  }

  if (wifiConfigTakeColorsChanged())
    applyClockColors();

  if (onHomeScreen) {
    struct tm localTm;
    if (timeSyncTick(&localTm)) {
      char timeBuf[16];
      char dateBuf[32];
      timeSyncFormatTime(localTm, timeBuf, sizeof(timeBuf));
      timeSyncFormatDate(localTm, dateBuf, sizeof(dateBuf));
      lv_label_set_text(ui_time, timeBuf);
      lv_label_set_text(ui_date, dateBuf);
      Serial.printf("time %s  date %s\n", timeBuf, dateBuf);
    }
  }

  lv_timer_handler();
  delay(5);
}

#else

void clockAppSetup() {}
void clockAppLoop() {}

#endif
