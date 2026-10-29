#include "data_ingest.h"

#if defined(FIRMWARE_MODE_APP)

#include "bme_sensor.h"
#include "wifi_config.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

#if defined(INGEST_ENV_LOCAL)
#include "ingest_config_local.h"
#elif defined(INGEST_ENV_PROD)
#include "ingest_config_prod.h"
#else
#error "Build app with INGEST_ENV_LOCAL or INGEST_ENV_PROD"
#endif

static const uint32_t INTERVAL_MS = 10UL * 60UL * 1000UL;  // 10min
static const uint32_t RETRY_MS = 60UL * 1000UL; // 60s

static uint32_t nextPostMs = 0;
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

void dataIngestLoop(bool allowRadioOff) {
  if ((int32_t)(millis() - nextPostMs) < 0) {
    return;
  }

  if (!wifiConfigIsConnected()) {
    // During boot config, SoftAP may be up — do not tear it down for ingest.
    if (!allowRadioOff) {
      scheduleNext(true);
      return;
    }
    if (!wifiConfigWakeStaForSync()) {
      Serial.println("data ingest: WiFi wake failed; retry once or wait 10 min");
      scheduleNext(true);
      return;
    }
  }

  float t = 0, h = 0, p = 0;
  const bool sensorOk = bmeSensorRead(&t, &h, &p);
  const bool postOk = postReading(sensorOk, t, h);

  if (postOk && sensorOk) {
    scheduleNext(false);
    if (isnan(h))
      Serial.printf("data ingest ok  T=%.2f RH=null (BMP280)\n", t);
    else
      Serial.printf("data ingest ok  T=%.2f RH=%.1f\n", t, h);
  } else if (postOk && !sensorOk) {
    // Server sees null T/RH → sensor failed/missing.
    scheduleNext(true);
    Serial.println("data ingest ok (sensor null); one 30s retry then 10 min");
  } else {
    scheduleNext(true);
    if (!sensorOk)
      Serial.println("data ingest HTTP failed (sensor null); retry soon");
    else
      Serial.println("data ingest HTTP failed; retry soon");
  }

  if (allowRadioOff) {
    wifiConfigRadioOff();
  }
}

#else

void dataIngestBegin() {}
void dataIngestLoop(bool /*allowRadioOff*/) {}

#endif
