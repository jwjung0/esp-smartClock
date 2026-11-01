#include "data_ingest.h"

#if defined(FIRMWARE_MODE_APP)

#include "bme_sensor.h"
#include "market_fetch.h"
#include "wifi_config.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

#if defined(INGEST_ENV_LOCAL)
#include "ingest_config_local.h"
#elif defined(INGEST_ENV_PROD)
#include "ingest_config_prod.h"
#else
#error "Build app with INGEST_ENV_LOCAL or INGEST_ENV_PROD"
#endif

static const uint32_t INTERVAL_MS = 10UL * 60UL * 1000UL; // 10min
static const uint32_t RETRY_MS = 60UL * 1000UL;           // 60s

static uint32_t nextPostMs = 0;
static volatile bool deferRadioOff = false;
static volatile uint32_t deferRadioOffSinceMs = 0;
static volatile bool postBusy = false;
static bool ingestWaking = false;
static bool postSensorOk = false;
static float postTempC = 0;
static float postHumidity = 0;
static bool postAllowRadioOff = false;
// After a failed read/post, one 30s retry then back to 10 min.
static bool awaitingQuickRetry = false;

static String iso8601UtcNow() {
  time_t now = time(nullptr);
  struct tm tmUtc;
  gmtime_r(&now, &tmUtc);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
  return String(buf);
}

// Always POST. sensorOk=false → temperature_c / humidity_pct are JSON null.
static bool postReading(bool sensorOk, float tempC, float humidityPct) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  http.setTimeout(5000);
  http.begin(INGEST_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_INGEST_TOKEN);

  char body[256];
  const String measuredAt = iso8601UtcNow();
  if (sensorOk && !isnan(humidityPct)) {
    snprintf(body, sizeof(body),
             "{\"device_id\":\"%s\",\"temperature_c\":%.2f,\"humidity_pct\":%.2f,"
             "\"measured_at\":\"%s\"}",
             DEVICE_ID, tempC, humidityPct, measuredAt.c_str());
  } else if (sensorOk) {
    snprintf(body, sizeof(body),
             "{\"device_id\":\"%s\",\"temperature_c\":%.2f,\"humidity_pct\":null,"
             "\"measured_at\":\"%s\"}",
             DEVICE_ID, tempC, measuredAt.c_str());
  } else {
    snprintf(body, sizeof(body),
             "{\"device_id\":\"%s\",\"temperature_c\":null,\"humidity_pct\":null,"
             "\"measured_at\":\"%s\"}",
             DEVICE_ID, measuredAt.c_str());
  }

  yield();
  const int code = http.POST(body);
  http.end();
  yield();
  return code >= 200 && code < 300;
}

static void scheduleNext(bool wantQuickRetry) {
  if (wantQuickRetry && !awaitingQuickRetry) {
    awaitingQuickRetry = true;
    nextPostMs = millis() + RETRY_MS;
  } else {
    awaitingQuickRetry = false;
    nextPostMs = millis() + INTERVAL_MS;
  }
}

void dataIngestBegin() {
  // Let the home clock paint for a few seconds before the first HTTP POST.
  nextPostMs = millis() + 15000;
  awaitingQuickRetry = false;
#if defined(INGEST_ENV_LOCAL)
  Serial.println("data ingest LOCAL: every 10 min (WiFi wake → POST → off)");
#else
  Serial.println("data ingest PROD: every 10 min (WiFi wake → POST → off)");
#endif
}

static void ingestNetTask(void * /*arg*/) {
  const bool sensorOk = postSensorOk;
  const float t = postTempC;
  const float h = postHumidity;
  const bool postOk = postReading(sensorOk, t, h);

  if (postOk && sensorOk) {
    scheduleNext(false);
    if (isnan(h))
      Serial.printf("data ingest ok  T=%.2f RH=null (BMP280)\n", t);
    else
      Serial.printf("data ingest ok  T=%.2f RH=%.1f\n", t, h);
  } else if (postOk && !sensorOk) {
    scheduleNext(true);
    Serial.println("data ingest ok (sensor null); one 30s retry then 10 min");
  } else {
    scheduleNext(true);
    if (!sensorOk)
      Serial.println("data ingest HTTP failed (sensor null); retry soon");
    else
      Serial.println("data ingest HTTP failed; retry soon");
  }

  // Weather + quotes on the market task (not this one, not the UI).
  marketFetchRequestRefresh();
  if (postAllowRadioOff) {
    deferRadioOff = true;
    deferRadioOffSinceMs = millis();
  }
  postBusy = false;
  vTaskDelete(nullptr);
}

void dataIngestLoop(bool allowRadioOff) {
  // Boot config window: leave the radio to the portal. No HTTP here.
  if (!allowRadioOff) {
    ingestWaking = false;
    if ((int32_t)(millis() - nextPostMs) >= 0)
      nextPostMs = millis() + 5000;
    return;
  }
  if (postBusy)
    return;

  if (ingestWaking) {
    if (!wifiConfigCmdIdle())
      return;
    ingestWaking = false;
    if (!wifiConfigIsConnected()) {
      Serial.println("data ingest: WiFi wake failed; retry once or wait 10 min");
      scheduleNext(true);
      return;
    }
  } else if ((int32_t)(millis() - nextPostMs) < 0) {
    return;
  } else if (!wifiConfigIsConnected()) {
    if (!wifiConfigRequestWake()) {
      Serial.println("data ingest: WiFi wake skipped; retry later");
      scheduleNext(true);
      return;
    }
    ingestWaking = true;
    return;
  }

  float t = 0, h = 0, p = 0;
  postSensorOk = bmeSensorRead(&t, &h, &p);
  postTempC = t;
  postHumidity = h;
  postAllowRadioOff = true;
  (void)p;
  postBusy = true;
  // Hold the slot until the task schedules the real next time.
  nextPostMs = millis() + INTERVAL_MS;

  if (xTaskCreatePinnedToCore(ingestNetTask, "ingest", 10240, nullptr, 1,
                              nullptr, 0) != pdPASS) {
    postBusy = false;
    scheduleNext(true);
    Serial.println("data ingest: task create failed");
  }
}

bool dataIngestTakeRadioOff(void) {
  if (!deferRadioOff)
    return false;
  if (marketFetchBusy() && (millis() - deferRadioOffSinceMs) < 30000)
    return false;
  deferRadioOff = false;
  return true;
}

#else

void dataIngestBegin() {}
void dataIngestLoop(bool /*allowRadioOff*/) {}
bool dataIngestTakeRadioOff(void) { return false; }

#endif
