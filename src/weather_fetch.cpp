#include "weather_fetch.h"

#if defined(FIRMWARE_MODE_APP) || defined(FIRMWARE_MODE_CHECKUP)

#include "weather_config.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool hasData = false;
static bool updatedFlag = false;
static float cachedTempC = NAN;
static float cachedHumidityPct = NAN;
static char cachedMain[16] = "";

void weatherFetchBegin(void) {
  hasData = false;
  updatedFlag = false;
  cachedTempC = NAN;
  cachedHumidityPct = NAN;
  cachedMain[0] = '\0';
#if defined(FIRMWARE_MODE_CHECKUP)
  Serial.println("weather fetch: OpenWeather 2.5 /weather (checkup)");
#else
  Serial.println("weather fetch: OpenWeather 2.5 /weather (with ingest wake)");
#endif
}

bool weatherFetchHasData(void) { return hasData; }

bool weatherFetchTakeUpdated(void) {
  if (!updatedFlag)
    return false;
  updatedFlag = false;
  return true;
}

bool weatherFetchGet(float *tempC, float *humidityPct, char *mainOut,
                     size_t mainOutLen) {
  if (!hasData)
    return false;
  if (tempC)
    *tempC = cachedTempC;
  if (humidityPct)
    *humidityPct = cachedHumidityPct;
  if (mainOut && mainOutLen > 0) {
    strncpy(mainOut, cachedMain, mainOutLen - 1);
    mainOut[mainOutLen - 1] = '\0';
  }
  return true;
}

bool weatherFetchOnce(void) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("weather fetch: not connected");
    return false;
  }

  if (!OPENWEATHER_API_KEY || !OPENWEATHER_API_KEY[0] ||
      strcmp(OPENWEATHER_API_KEY, "YOUR_OPENWEATHER_API_KEY") == 0 ||
      strcmp(OPENWEATHER_API_KEY, "tbd") == 0) {
    Serial.println("weather fetch: set OPENWEATHER_API_KEY in weather_config.h");
    return false;
  }

  // Free-tier Current Weather API (One Call 3.0/4.0 need a paid subscription).
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.openweathermap.org/data/2.5/weather"
           "?lat=%.5f&lon=%.5f&units=metric&lang=en&appid=%s",
           (double)OPENWEATHER_LAT, (double)OPENWEATHER_LON,
           OPENWEATHER_API_KEY);

  WiFiClientSecure client;
  client.setInsecure(); // v1: no CA pin; tighten later if needed

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    Serial.println("weather fetch: http.begin failed");
    return false;
  }

  yield();
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    String body = http.getString();
    http.end();
    Serial.printf("weather fetch: HTTP %d\n", code);
    if (body.length()) {
      body.replace('\n', ' ');
      if (body.length() > 160)
        body = body.substring(0, 160);
      Serial.printf("weather fetch: %s\n", body.c_str());
    }
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  yield();

  if (err) {
    Serial.printf("weather fetch: JSON %s\n", err.c_str());
    return false;
  }

  // 2.5 shape: main.temp, main.humidity, weather[0].main
  JsonObject mainObj = doc["main"].as<JsonObject>();
  if (mainObj.isNull() ||
      (!mainObj["temp"].is<float>() && !mainObj["temp"].is<int>())) {
    Serial.println("weather fetch: missing main.temp");
    return false;
  }

  const float tempC = mainObj["temp"].as<float>();
  const float humidity = mainObj["humidity"] | NAN;
  const char *mainStr = doc["weather"][0]["main"] | "";

  cachedTempC = tempC;
  cachedHumidityPct = humidity;
  strncpy(cachedMain, mainStr, sizeof(cachedMain) - 1);
  cachedMain[sizeof(cachedMain) - 1] = '\0';
  hasData = true;
  updatedFlag = true;

  Serial.printf("weather fetch ok  T=%.1f RH=%.0f main=%s\n", cachedTempC,
                cachedHumidityPct, cachedMain);
  return true;
}

#else

void weatherFetchBegin(void) {}
bool weatherFetchOnce(void) { return false; }
bool weatherFetchHasData(void) { return false; }
bool weatherFetchTakeUpdated(void) { return false; }
bool weatherFetchGet(float *, float *, char *, size_t) { return false; }

#endif
