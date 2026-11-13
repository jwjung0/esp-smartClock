#include "weather_fetch.h"

#if defined(FIRMWARE_MODE_APP) || defined(FIRMWARE_MODE_CHECKUP)

#include "weather_config.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

struct WeatherSnap {
  bool valid;
  float tempC;
  float humidityPct;
  char main[16];
  int id;
  bool hasSun;
  time_t sunrise;
  time_t sunset;
  bool hasHiLo;
  float tempHiC;
  float tempLoC;
};

static const char *kPrefsUiNs = "clockui";
static const char *kKeyWxSrc = "wx_src"; // "naver" | "openweather"

static WeatherSnap owSnap;
static WeatherSnap naverSnap;
static bool hasData = false;
static volatile bool updatedFlag = false;
static bool preferredIsNaver = false;
static bool preferNaver = true; // user preference; default Naver
static bool prefsLoaded = false;
static float cachedTempC = NAN;
static float cachedHumidityPct = NAN;
static char cachedMain[16] = "";
static int cachedId = 0;
static bool hasSunTimes = false;
static time_t cachedSunrise = 0;
static time_t cachedSunset = 0;
static bool hasHiLo = false;
static float cachedTempHiC = NAN;
static float cachedTempLoC = NAN;

static void loadSourcePrefs(void) {
  Preferences prefs;
  preferNaver = true;
  if (prefs.begin(kPrefsUiNs, true)) {
    String src = prefs.getString(kKeyWxSrc, "naver");
    prefs.end();
    if (src.equalsIgnoreCase("openweather") || src.equalsIgnoreCase("ow"))
      preferNaver = false;
    else
      preferNaver = true;
  }
  prefsLoaded = true;
}

static void ensureSourcePrefs(void) {
  if (!prefsLoaded)
    loadSourcePrefs();
}

static void clearSnap(WeatherSnap *s) {
  if (!s)
    return;
  s->valid = false;
  s->tempC = NAN;
  s->humidityPct = NAN;
  s->main[0] = '\0';
  s->id = 0;
  s->hasSun = false;
  s->sunrise = 0;
  s->sunset = 0;
  s->hasHiLo = false;
  s->tempHiC = NAN;
  s->tempLoC = NAN;
}

// Normalize hi/lo and expand the band so current temp can sit on the bar.
static void setSnapHiLo(WeatherSnap *s, float hi, float lo) {
  if (!s || isnan(hi) || isnan(lo))
    return;
  if (hi < lo) {
    const float tmp = hi;
    hi = lo;
    lo = tmp;
  }
  if (!isnan(s->tempC)) {
    if (s->tempC > hi)
      hi = s->tempC;
    if (s->tempC < lo)
      lo = s->tempC;
  }
  s->tempHiC = hi;
  s->tempLoC = lo;
  s->hasHiLo = true;
}

static void publishPreferred(void) {
  ensureSourcePrefs();
  const WeatherSnap *src = nullptr;
  bool fromNaver = false;

  if (preferNaver) {
    if (naverSnap.valid) {
      src = &naverSnap;
      fromNaver = true;
    } else if (owSnap.valid) {
      src = &owSnap;
      fromNaver = false;
    }
  } else {
    if (owSnap.valid) {
      src = &owSnap;
      fromNaver = false;
    } else if (naverSnap.valid) {
      src = &naverSnap;
      fromNaver = true;
    }
  }

  if (!src) {
    hasData = false;
    preferredIsNaver = false;
    cachedTempC = NAN;
    cachedHumidityPct = NAN;
    cachedMain[0] = '\0';
    cachedId = 0;
    hasSunTimes = false;
    cachedSunrise = 0;
    cachedSunset = 0;
    hasHiLo = false;
    cachedTempHiC = NAN;
    cachedTempLoC = NAN;
    return;
  }

  preferredIsNaver = fromNaver;
  hasData = true;
  cachedTempC = src->tempC;
  cachedHumidityPct = src->humidityPct;
  strncpy(cachedMain, src->main, sizeof(cachedMain) - 1);
  cachedMain[sizeof(cachedMain) - 1] = '\0';
  cachedId = src->id;
  hasSunTimes = src->hasSun;
  cachedSunrise = src->sunrise;
  cachedSunset = src->sunset;
  hasHiLo = src->hasHiLo;
  cachedTempHiC = src->tempHiC;
  cachedTempLoC = src->tempLoC;
}

// Map Naver wetrCd / wetrTxt → OpenWeather-style main + keep numeric wetrCd as id.
static void classifyNaver(const char *wetrCd, const char *wetrTxt, char *mainOut,
                          size_t mainLen, int *idOut) {
  const int cd = wetrCd && wetrCd[0] ? atoi(wetrCd) : 0;
  if (idOut)
    *idOut = cd;
  if (!mainOut || mainLen == 0)
    return;
  mainOut[0] = '\0';

  if (wetrTxt && wetrTxt[0]) {
    if (strstr(wetrTxt, "낙뢰") || strstr(wetrTxt, "천둥")) {
      strncpy(mainOut, "Thunderstorm", mainLen - 1);
    } else if (strstr(wetrTxt, "눈")) {
      strncpy(mainOut, "Snow", mainLen - 1);
    } else if (strstr(wetrTxt, "비") || strstr(wetrTxt, "소나기") ||
               strstr(wetrTxt, "빗방울")) {
      strncpy(mainOut, "Rain", mainLen - 1);
    } else if (strstr(wetrTxt, "안개") || strstr(wetrTxt, "황사") ||
               strstr(wetrTxt, "연무") || strstr(wetrTxt, "박무") ||
               strstr(wetrTxt, "먼지")) {
      strncpy(mainOut, "Mist", mainLen - 1);
    } else if (strstr(wetrTxt, "맑음")) {
      strncpy(mainOut, "Clear", mainLen - 1);
    } else if (strstr(wetrTxt, "구름")) {
      strncpy(mainOut, "Clouds", mainLen - 1);
    } else if (strstr(wetrTxt, "흐림")) {
      strncpy(mainOut, "Clouds", mainLen - 1);
      if (idOut && *idOut == 0)
        *idOut = 7;
    }
  }

  if (mainOut[0]) {
    mainOut[mainLen - 1] = '\0';
    return;
  }

  // Code-only fallback (common Naver / KMA ico_wt codes).
  switch (cd) {
  case 1:
    strncpy(mainOut, "Clear", mainLen - 1);
    break;
  case 2:
  case 3:
  case 5:
  case 6:
    strncpy(mainOut, "Clouds", mainLen - 1);
    break;
  case 7:
  case 8:
    strncpy(mainOut, "Clouds", mainLen - 1);
    break;
  case 9:
  case 10:
  case 11:
  case 12:
  case 40:
  case 41:
  case 42:
    strncpy(mainOut, "Rain", mainLen - 1);
    break;
  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
    strncpy(mainOut, "Snow", mainLen - 1);
    break;
  case 29:
  case 30:
  case 31:
  case 32:
  case 33:
  case 34:
    strncpy(mainOut, "Thunderstorm", mainLen - 1);
    break;
  default:
    strncpy(mainOut, "Clouds", mainLen - 1);
    break;
  }
  mainOut[mainLen - 1] = '\0';
}

// Naver sun times are local KST wall-clock HHMMSS + aplYmd YYYYMMDD.
static bool naverLocalToUnix(const char *ymd, const char *hhmmss, time_t *out) {
  if (!ymd || !hhmmss || !out || strlen(ymd) < 8 || strlen(hhmmss) < 6)
    return false;
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  if (sscanf(ymd, "%4d%2d%2d", &y, &mo, &d) != 3)
    return false;
  if (sscanf(hhmmss, "%2d%2d%2d", &h, &mi, &s) != 3)
    return false;
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min = mi;
  t.tm_sec = s;
  t.tm_isdst = -1;
  const time_t tt = mktime(&t); // device TZ is KST via configTime(+9)
  if (tt == (time_t)-1)
    return false;
  *out = tt;
  return true;
}

static bool fetchOpenWeather(void) {
  if (!OPENWEATHER_API_KEY || !OPENWEATHER_API_KEY[0] ||
      strcmp(OPENWEATHER_API_KEY, "YOUR_OPENWEATHER_API_KEY") == 0 ||
      strcmp(OPENWEATHER_API_KEY, "tbd") == 0) {
    Serial.println(
        "weather fetch: set OPENWEATHER_API_KEY in weather_config.h");
    return false;
  }

  char url[256];
  snprintf(url, sizeof(url),
           "https://api.openweathermap.org/data/2.5/weather"
           "?lat=%.5f&lon=%.5f&units=metric&lang=en&appid=%s",
           (double)OPENWEATHER_LAT, (double)OPENWEATHER_LON,
           OPENWEATHER_API_KEY);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) {
    Serial.println("weather fetch OW: http.begin failed");
    return false;
  }

  yield();
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    String body = http.getString();
    http.end();
    Serial.printf("weather fetch OW: HTTP %d\n", code);
    if (body.length()) {
      body.replace('\n', ' ');
      if (body.length() > 160)
        body = body.substring(0, 160);
      Serial.printf("weather fetch OW: %s\n", body.c_str());
    }
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  yield();

  if (err) {
    Serial.printf("weather fetch OW: JSON %s\n", err.c_str());
    return false;
  }

  JsonObject mainObj = doc["main"].as<JsonObject>();
  if (mainObj.isNull() ||
      (!mainObj["temp"].is<float>() && !mainObj["temp"].is<int>())) {
    Serial.println("weather fetch OW: missing main.temp");
    return false;
  }

  WeatherSnap snap;
  clearSnap(&snap);
  snap.tempC = mainObj["temp"].as<float>();
  snap.humidityPct = mainObj["humidity"] | NAN;
  const char *mainStr = doc["weather"][0]["main"] | "";
  strncpy(snap.main, mainStr, sizeof(snap.main) - 1);
  snap.main[sizeof(snap.main) - 1] = '\0';
  snap.id = doc["weather"][0]["id"] | 0;

  JsonObject sysObj = doc["sys"].as<JsonObject>();
  if (!sysObj.isNull() &&
      (sysObj["sunrise"].is<long>() || sysObj["sunrise"].is<unsigned long>() ||
       sysObj["sunrise"].is<int>()) &&
      (sysObj["sunset"].is<long>() || sysObj["sunset"].is<unsigned long>() ||
       sysObj["sunset"].is<int>())) {
    const time_t sunrise = (time_t)sysObj["sunrise"].as<unsigned long>();
    const time_t sunset = (time_t)sysObj["sunset"].as<unsigned long>();
    if (sunrise > 0 && sunset > sunrise) {
      snap.sunrise = sunrise;
      snap.sunset = sunset;
      snap.hasSun = true;
    }
  }

  const bool hasMin =
      mainObj["temp_min"].is<float>() || mainObj["temp_min"].is<int>() ||
      mainObj["temp_min"].is<double>();
  const bool hasMax =
      mainObj["temp_max"].is<float>() || mainObj["temp_max"].is<int>() ||
      mainObj["temp_max"].is<double>();
  if (hasMin && hasMax)
    setSnapHiLo(&snap, mainObj["temp_max"].as<float>(),
                mainObj["temp_min"].as<float>());

  snap.valid = true;
  owSnap = snap;
  if (snap.hasHiLo) {
    Serial.printf(
        "weather fetch OW ok  T=%.1f RH=%.0f main=%s id=%d hi=%.1f lo=%.1f\n",
        snap.tempC, snap.humidityPct, snap.main, snap.id, snap.tempHiC,
        snap.tempLoC);
  } else {
    Serial.printf("weather fetch OW ok  T=%.1f RH=%.0f main=%s id=%d\n",
                  snap.tempC, snap.humidityPct, snap.main, snap.id);
  }
  return true;
}

static bool fetchNaver(void) {
  if (!NAVER_WEATHER_REGION_CODE || !NAVER_WEATHER_REGION_CODE[0]) {
    Serial.println("weather fetch Naver: missing region code");
    return false;
  }

  // choiceQuery={"nowSynthesisFcast":{"naverRgnCd":"<code>","cpName":"KMA"}}
  char url[384];
  snprintf(url, sizeof(url),
           "https://weather.naver.com/choiceApi/api?choiceQuery="
           "%%7B%%22nowSynthesisFcast%%22%%3A%%7B%%22naverRgnCd%%22%%3A%%22%s"
           "%%22%%2C%%22cpName%%22%%3A%%22KMA%%22%%7D%%7D",
           NAVER_WEATHER_REGION_CODE);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  http.setUserAgent(
      "Mozilla/5.0 (ESP32) esp-smartClock/1.0");
  if (!http.begin(client, url)) {
    Serial.println("weather fetch Naver: http.begin failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Referer", "https://weather.naver.com/");

  yield();
  const int code = http.GET();
  String body = http.getString();
  http.end();
  yield();

  if (code != HTTP_CODE_OK) {
    Serial.printf("weather fetch Naver: HTTP %d\n", code);
    if (body.length()) {
      body.replace('\n', ' ');
      if (body.length() > 160)
        body = body.substring(0, 160);
      Serial.printf("weather fetch Naver: %s\n", body.c_str());
    }
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("weather fetch Naver: JSON %s\n", err.c_str());
    return false;
  }

  JsonObject nowFcast =
      doc["results"]["choiceResult"]["nowSynthesisFcast"]["nowFcast"]
          .as<JsonObject>();
  if (nowFcast.isNull() ||
      (!nowFcast["tmpr"].is<float>() && !nowFcast["tmpr"].is<int>() &&
       !nowFcast["tmpr"].is<double>())) {
    Serial.println("weather fetch Naver: missing nowFcast.tmpr");
    return false;
  }

  WeatherSnap snap;
  clearSnap(&snap);
  snap.tempC = nowFcast["tmpr"].as<float>();
  snap.humidityPct = nowFcast["humd"] | NAN;
  const char *wetrCd = nowFcast["wetrCd"] | "";
  const char *wetrTxt = nowFcast["wetrTxt"] | "";
  classifyNaver(wetrCd, wetrTxt, snap.main, sizeof(snap.main), &snap.id);

  JsonArray sunList =
      doc["results"]["choiceResult"]["nowSynthesisFcast"]["sunRiseSetList"]
          .as<JsonArray>();
  if (!sunList.isNull() && sunList.size() > 0) {
    JsonObject sun0 = sunList[0].as<JsonObject>();
    const char *ymd = sun0["aplYmd"] | (nowFcast["aplYmd"] | "");
    const char *rise = sun0["sriseTm"] | "";
    const char *set = sun0["ssetTm"] | "";
    time_t sunrise = 0, sunset = 0;
    if (naverLocalToUnix(ymd, rise, &sunrise) &&
        naverLocalToUnix(ymd, set, &sunset) && sunset > sunrise) {
      snap.sunrise = sunrise;
      snap.sunset = sunset;
      snap.hasSun = true;
    }
  }

  // Today's daily hi/lo from weekly forecast (aplYmd = KST YYYYMMDD).
  char todayYmd[16] = "";
  {
    time_t now = time(nullptr);
    struct tm localTm;
    if (now > 0 && localtime_r(&now, &localTm)) {
      const int y = localTm.tm_year + 1900;
      const int m = localTm.tm_mon + 1;
      const int d = localTm.tm_mday;
      if (y >= 2000 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31)
        snprintf(todayYmd, sizeof(todayYmd), "%04d%02d%02d", y, m, d);
    }
  }
  JsonArray weekList =
      doc["results"]["choiceResult"]["nowSynthesisFcast"]["weeklyFcastList"]
          .as<JsonArray>();
  if (!weekList.isNull() && weekList.size() > 0) {
    JsonObject day = weekList[0].as<JsonObject>();
    if (todayYmd[0]) {
      for (JsonObject cand : weekList) {
        const char *ymd = cand["aplYmd"] | "";
        if (ymd[0] && strcmp(ymd, todayYmd) == 0) {
          day = cand;
          break;
        }
      }
    }
    const bool hasMin = day["minTmpr"].is<float>() || day["minTmpr"].is<int>() ||
                        day["minTmpr"].is<double>();
    const bool hasMax = day["maxTmpr"].is<float>() || day["maxTmpr"].is<int>() ||
                        day["maxTmpr"].is<double>();
    if (hasMin && hasMax)
      setSnapHiLo(&snap, day["maxTmpr"].as<float>(), day["minTmpr"].as<float>());
  }

  snap.valid = true;
  naverSnap = snap;
  if (snap.hasHiLo) {
    Serial.printf("weather fetch Naver ok  T=%.1f RH=%.0f main=%s wetrCd=%d "
                  "txt=%s hi=%.1f lo=%.1f\n",
                  snap.tempC, snap.humidityPct, snap.main, snap.id, wetrTxt,
                  snap.tempHiC, snap.tempLoC);
  } else {
    Serial.printf(
        "weather fetch Naver ok  T=%.1f RH=%.0f main=%s wetrCd=%d txt=%s\n",
        snap.tempC, snap.humidityPct, snap.main, snap.id, wetrTxt);
  }
  return true;
}

void weatherFetchBegin(void) {
  clearSnap(&owSnap);
  clearSnap(&naverSnap);
  hasData = false;
  updatedFlag = false;
  preferredIsNaver = false;
  cachedTempC = NAN;
  cachedHumidityPct = NAN;
  cachedMain[0] = '\0';
  cachedId = 0;
  hasSunTimes = false;
  cachedSunrise = 0;
  cachedSunset = 0;
  hasHiLo = false;
  cachedTempHiC = NAN;
  cachedTempLoC = NAN;
  loadSourcePrefs();
#if defined(FIRMWARE_MODE_CHECKUP)
  Serial.printf("weather fetch: Naver + OpenWeather (checkup), prefer=%s\n",
                preferNaver ? "naver" : "openweather");
#else
  Serial.printf(
      "weather fetch: Naver + OpenWeather (ingest wake), prefer=%s\n",
      preferNaver ? "naver" : "openweather");
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

bool weatherFetchGetId(int *idOut) {
  if (!hasData)
    return false;
  if (idOut)
    *idOut = cachedId;
  return true;
}

bool weatherFetchIsNaver(void) { return hasData && preferredIsNaver; }

bool weatherConfigPreferNaver(void) {
  ensureSourcePrefs();
  return preferNaver;
}

const char *weatherConfigSourceId(void) {
  ensureSourcePrefs();
  return preferNaver ? "naver" : "openweather";
}

bool weatherConfigSetSource(const char *src) {
  if (!src || !src[0])
    return false;
  String s(src);
  s.trim();
  s.toLowerCase();
  bool nextNaver = true;
  if (s == "openweather" || s == "ow")
    nextNaver = false;
  else if (s == "naver")
    nextNaver = true;
  else
    return false;

  ensureSourcePrefs();
  preferNaver = nextNaver;
  Preferences prefs;
  if (prefs.begin(kPrefsUiNs, false)) {
    prefs.putString(kKeyWxSrc, preferNaver ? "naver" : "openweather");
    prefs.end();
  }
  publishPreferred();
  if (hasData)
    updatedFlag = true;
  Serial.printf("weather source prefer=%s (active=%s)\n",
                preferNaver ? "naver" : "openweather",
                hasData ? (preferredIsNaver ? "naver" : "openweather")
                        : "none");
  return true;
}

bool weatherFetchGetSunTimes(time_t *sunrise, time_t *sunset) {
  if (!hasSunTimes)
    return false;
  if (sunrise)
    *sunrise = cachedSunrise;
  if (sunset)
    *sunset = cachedSunset;
  return true;
}

bool weatherFetchGetHiLo(float *hiC, float *loC) {
  if (!hasData || !hasHiLo)
    return false;
  if (hiC)
    *hiC = cachedTempHiC;
  if (loC)
    *loC = cachedTempLoC;
  return true;
}

bool weatherFetchOnce(void) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("weather fetch: not connected");
    return false;
  }

  const bool naverOk = fetchNaver();
  const bool owOk = fetchOpenWeather();
  if (!naverOk && !owOk)
    return false;

  publishPreferred();
  updatedFlag = true;
  Serial.printf(
      "weather active=%s (prefer=%s)  T=%.1f RH=%.0f main=%s id=%d\n",
      preferredIsNaver ? "Naver" : "OpenWeather",
      preferNaver ? "naver" : "openweather", cachedTempC, cachedHumidityPct,
      cachedMain, cachedId);
  return true;
}

#else

void weatherFetchBegin(void) {}
bool weatherFetchOnce(void) { return false; }
bool weatherFetchHasData(void) { return false; }
bool weatherFetchTakeUpdated(void) { return false; }
bool weatherFetchGet(float *, float *, char *, size_t) { return false; }
bool weatherFetchGetId(int *) { return false; }
bool weatherFetchIsNaver(void) { return false; }
bool weatherConfigPreferNaver(void) { return true; }
const char *weatherConfigSourceId(void) { return "naver"; }
bool weatherConfigSetSource(const char *) { return false; }
bool weatherFetchGetSunTimes(time_t *, time_t *) { return false; }
bool weatherFetchGetHiLo(float *, float *) { return false; }

#endif
