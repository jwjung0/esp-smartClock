#include "market_fetch.h"

#if defined(FIRMWARE_MODE_APP)

#include "weather_fetch.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *kUserAgent =
    "Mozilla/5.0 (compatible; SmartClock/1.0; ESP32)";
static const char *kPrefsNs = "market";
static const char *kPrefsKey = "cfg";

enum MarketKind : uint8_t {
  MARKET_KIND_DOMESTIC_INDEX = 0,
  MARKET_KIND_DOMESTIC_STOCK,
  MARKET_KIND_WORLD_INDEX,
  MARKET_KIND_FX,
  MARKET_KIND_UNKNOWN
};

struct MarketSlot {
  char tag[MARKET_TAG_LEN];
  char code[MARKET_CODE_LEN];
  MarketKind kind;
  bool enabled;
  bool isDefault;
};

static bool hasData = false;
static bool updatedFlag = false;
static bool marketVisible = true;
static bool visibilityChanged = false;
static MarketQuote quotes[MARKET_MAX_QUOTES];
static size_t quoteCount = 0;

static MarketSlot slots[MARKET_MAX_QUOTES];
static size_t slotCount = 0;

// Background refresh publishes here; main loop swaps into quotes[].
static MarketQuote pendingQuotes[MARKET_MAX_QUOTES];
static size_t pendingCount = 0;
static volatile bool pendingReady = false;
static volatile bool fetchBusy = false;
static volatile bool fetchQueued = false;
static const uint32_t kHttpTimeoutMs = 5000;
static TaskHandle_t marketTaskHandle = nullptr;

static MarketQuote *storeDst = quotes;
static size_t *storeCount = &quoteCount;

static void clearStore(void) {
  *storeCount = 0;
  memset(storeDst, 0, sizeof(MarketQuote) * MARKET_MAX_QUOTES);
}

static void setStoreTarget(MarketQuote *dst, size_t *count) {
  storeDst = dst;
  storeCount = count;
  clearStore();
}

static void copyTrunc(char *dst, size_t dstLen, const char *src) {
  if (!dst || dstLen == 0)
    return;
  if (!src)
    src = "";
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

static MarketKind detectKind(const char *code) {
  if (!code || !code[0])
    return MARKET_KIND_UNKNOWN;
  if (strncmp(code, "FX_", 3) == 0)
    return MARKET_KIND_FX;
  if (code[0] == '.')
    return MARKET_KIND_WORLD_INDEX;
  if (strcmp(code, "KOSPI") == 0 || strcmp(code, "KOSDAQ") == 0 ||
      strcmp(code, "KPI200") == 0)
    return MARKET_KIND_DOMESTIC_INDEX;
  const size_t len = strlen(code);
  if (len == 6) {
    for (size_t i = 0; i < len; ++i) {
      if (!isalnum(static_cast<unsigned char>(code[i])))
        return MARKET_KIND_UNKNOWN;
    }
    return MARKET_KIND_DOMESTIC_STOCK;
  }
  return MARKET_KIND_UNKNOWN;
}

static void resetDefaultSlots(void) {
  memset(slots, 0, sizeof(slots));
  slotCount = 0;

  auto addDef = [](const char *tag, const char *code, MarketKind kind) {
    if (slotCount >= MARKET_MAX_QUOTES)
      return;
    MarketSlot &s = slots[slotCount++];
    copyTrunc(s.tag, sizeof(s.tag), tag);
    copyTrunc(s.code, sizeof(s.code), code);
    s.kind = kind;
    s.enabled = true;
    s.isDefault = true;
  };

  addDef("KS", "KOSPI", MARKET_KIND_DOMESTIC_INDEX);
  addDef("KQ", "KOSDAQ", MARKET_KIND_DOMESTIC_INDEX);
  addDef("Nasdaq", ".IXIC", MARKET_KIND_WORLD_INDEX);
  addDef("USD", "FX_USDKRW", MARKET_KIND_FX);
}

static size_t countEnabled(void) {
  size_t n = 0;
  for (size_t i = 0; i < slotCount; ++i) {
    if (slots[i].enabled)
      ++n;
  }
  return n;
}

static MarketSlot *findDefault(const char *tag) {
  for (size_t i = 0; i < slotCount; ++i) {
    if (slots[i].isDefault && strcmp(slots[i].tag, tag) == 0)
      return &slots[i];
  }
  return nullptr;
}

static bool parseFloatField(JsonVariantConst v, float *out) {
  if (!out)
    return false;
  if (v.isNull())
    return false;
  if (v.is<float>() || v.is<double>() || v.is<int>() || v.is<long>()) {
    *out = v.as<float>();
    return !isnan(*out);
  }
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    if (!s || !s[0])
      return false;
    char buf[32];
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 1 < sizeof(buf); ++i) {
      if (s[i] == ',')
        continue;
      buf[j++] = s[i];
    }
    buf[j] = '\0';
    char *end = nullptr;
    const float f = strtof(buf, &end);
    if (end == buf || isnan(f))
      return false;
    *out = f;
    return true;
  }
  return false;
}

static bool storeQuote(const char *tag, float price, float pct) {
  if (!tag || !tag[0] || !storeDst || !storeCount ||
      *storeCount >= MARKET_MAX_QUOTES)
    return false;
  MarketQuote &q = storeDst[(*storeCount)++];
  copyTrunc(q.tag, sizeof(q.tag), tag);
  q.price = price;
  q.pct = pct;
  q.valid = true;
  return true;
}

static void logBodyPrefix(const String &body) {
  if (!body.length()) {
    Serial.println("market fetch: empty body");
    return;
  }
  String prefix = body.substring(0, body.length() > 100 ? 100 : body.length());
  prefix.replace('\n', ' ');
  prefix.replace('\r', ' ');
  Serial.printf("market fetch: body[%u]=%s\n", (unsigned)body.length(),
                prefix.c_str());
}

static bool httpGetJson(const char *url, JsonDocument &doc) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("market fetch: not connected");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.setReuse(false);
  http.setUserAgent(kUserAgent);
  if (!http.begin(client, url)) {
    Serial.println("market fetch: http.begin failed");
    return false;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Referer", "https://stock.naver.com/");

  const int code = http.GET();
  String body = http.getString();
  http.end();

  if (code != HTTP_CODE_OK) {
    Serial.printf("market fetch: HTTP %d %s\n", code, url);
    logBodyPrefix(body);
    return false;
  }

  if (!body.length()) {
    Serial.printf("market fetch: empty body %s\n", url);
    return false;
  }

  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("market fetch: JSON %s\n", err.c_str());
    logBodyPrefix(body);
    return false;
  }
  return true;
}

static bool parseCloseAndPct(JsonObjectConst row, float *price, float *pct) {
  if (!price || !pct)
    return false;
  if (!parseFloatField(row["closePriceRaw"], price) &&
      !parseFloatField(row["closePrice"], price) &&
      !parseFloatField(row["calcPrice"], price))
    return false;
  if (!parseFloatField(row["fluctuationsRatioRaw"], pct) &&
      !parseFloatField(row["fluctuationsRatio"], pct))
    *pct = 0.0f;
  return true;
}

static size_t fetchDomesticIndexBatch(const char *codesCsv,
                                      const char *tagForKospi,
                                      const char *tagForKosdaq) {
  char url[160];
  snprintf(url, sizeof(url),
           "https://polling.finance.naver.com/api/realtime/domestic/index/%s",
           codesCsv);

  JsonDocument doc;
  if (!httpGetJson(url, doc))
    return 0;

  JsonArray datas = doc["datas"].as<JsonArray>();
  if (datas.isNull()) {
    Serial.println("market fetch: domestic index missing datas");
    return 0;
  }

  size_t n = 0;
  for (JsonObject row : datas) {
    const char *code = row["itemCode"] | row["symbolCode"] | "";
    const char *tag = nullptr;
    if (tagForKospi && strcmp(code, "KOSPI") == 0)
      tag = tagForKospi;
    else if (tagForKosdaq && strcmp(code, "KOSDAQ") == 0)
      tag = tagForKosdaq;
    else if (strcmp(code, "KPI200") == 0) {
      // Match custom slot by code.
      for (size_t i = 0; i < slotCount; ++i) {
        if (slots[i].enabled && slots[i].kind == MARKET_KIND_DOMESTIC_INDEX &&
            strcmp(slots[i].code, code) == 0) {
          tag = slots[i].tag;
          break;
        }
      }
    } else {
      for (size_t i = 0; i < slotCount; ++i) {
        if (slots[i].enabled && slots[i].kind == MARKET_KIND_DOMESTIC_INDEX &&
            strcmp(slots[i].code, code) == 0) {
          tag = slots[i].tag;
          break;
        }
      }
    }
    if (!tag)
      continue;

    float price = NAN;
    float pct = NAN;
    if (!parseCloseAndPct(row, &price, &pct))
      continue;
    if (storeQuote(tag, price, pct)) {
      ++n;
      Serial.printf("market fetch ok  %s=%.2f pct=%.2f\n", tag, price, pct);
    }
  }
  return n;
}

static size_t fetchDomesticIndexOne(const MarketSlot &slot) {
  return fetchDomesticIndexBatch(slot.code, strcmp(slot.code, "KOSPI") == 0
                                                ? slot.tag
                                                : nullptr,
                                 strcmp(slot.code, "KOSDAQ") == 0 ? slot.tag
                                                                 : nullptr);
}

static size_t fetchDomesticStock(const MarketSlot &slot) {
  char url[160];
  snprintf(url, sizeof(url),
           "https://polling.finance.naver.com/api/realtime/domestic/stock/%s",
           slot.code);

  JsonDocument doc;
  if (!httpGetJson(url, doc))
    return 0;

  JsonArray datas = doc["datas"].as<JsonArray>();
  JsonObject row;
  if (!datas.isNull() && datas.size() > 0)
    row = datas[0].as<JsonObject>();
  else
    row = doc.as<JsonObject>();

  if (row.isNull()) {
    Serial.printf("market fetch: stock %s missing data\n", slot.code);
    return 0;
  }

  float price = NAN;
  float pct = NAN;
  if (!parseCloseAndPct(row, &price, &pct)) {
    Serial.printf("market fetch: stock %s missing price\n", slot.code);
    return 0;
  }
  if (!storeQuote(slot.tag, price, pct))
    return 0;
  Serial.printf("market fetch ok  %s=%.2f pct=%.2f\n", slot.tag, price, pct);
  return 1;
}

static bool isNasdaqSlot(const MarketSlot &slot) {
  return strcmp(slot.tag, "Nasdaq") == 0 || strcmp(slot.code, ".IXIC") == 0;
}

// Nasdaq only: Yahoo chart meta (~1.3KB). Naver's .IXIC basic JSON is too
// heavy for ESP32 ArduinoJson; keep other world indexes on Naver polling.
static size_t fetchNasdaqYahoo(const MarketSlot &slot) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("market fetch: not connected");
    return 0;
  }

  const char *url =
      "https://query1.finance.yahoo.com/v8/finance/chart/%5EIXIC"
      "?range=1d&interval=1d";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.setReuse(false);
  http.setUserAgent(kUserAgent);
  if (!http.begin(client, url)) {
    Serial.println("market fetch: Yahoo http.begin failed");
    return 0;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");

  const int code = http.GET();
  String body = http.getString();
  http.end();
  if (code != HTTP_CODE_OK) {
    Serial.printf("market fetch: Yahoo HTTP %d\n", code);
    logBodyPrefix(body);
    return 0;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("market fetch: Yahoo JSON %s\n", err.c_str());
    logBodyPrefix(body);
    return 0;
  }

  JsonObject meta = doc["chart"]["result"][0]["meta"].as<JsonObject>();
  if (meta.isNull()) {
    Serial.println("market fetch: Yahoo missing meta");
    return 0;
  }

  float price = NAN;
  float prev = NAN;
  if (!parseFloatField(meta["regularMarketPrice"], &price)) {
    Serial.printf("market fetch: %s missing price\n", slot.tag);
    return 0;
  }
  if (!parseFloatField(meta["chartPreviousClose"], &prev) &&
      !parseFloatField(meta["previousClose"], &prev)) {
    Serial.printf("market fetch: %s missing previousClose\n", slot.tag);
    return 0;
  }
  const float pct =
      (prev != 0.0f && !isnan(prev)) ? ((price - prev) / prev) * 100.0f : 0.0f;

  if (!storeQuote(slot.tag, price, pct))
    return 0;
  Serial.printf("market fetch ok  %s=%.2f pct=%.2f (Yahoo)\n", slot.tag, price,
                pct);
  return 1;
}

static size_t fetchWorldIndex(const MarketSlot &slot) {
  if (isNasdaqSlot(slot))
    return fetchNasdaqYahoo(slot);

  char url[192];
  snprintf(url, sizeof(url),
           "https://polling.finance.naver.com/api/realtime/worldstock/index/%s",
           slot.code);

  JsonDocument doc;
  if (!httpGetJson(url, doc))
    return 0;

  JsonArray datas = doc["datas"].as<JsonArray>();
  if (datas.isNull() || datas.size() == 0) {
    Serial.printf("market fetch: %s missing datas\n", slot.tag);
    return 0;
  }

  JsonObject row = datas[0].as<JsonObject>();
  float price = NAN;
  float pct = NAN;
  if (!parseCloseAndPct(row, &price, &pct)) {
    Serial.printf("market fetch: %s missing price\n", slot.tag);
    return 0;
  }

  const char *dir = row["compareToPreviousPrice"]["name"] | "";
  if (pct > 0.0f && strcmp(dir, "FALLING") == 0)
    pct = -pct;
  else if (pct < 0.0f && strcmp(dir, "RISING") == 0)
    pct = -pct;

  if (!storeQuote(slot.tag, price, pct))
    return 0;
  Serial.printf("market fetch ok  %s=%.2f pct=%.2f\n", slot.tag, price, pct);
  return 1;
}

static size_t fetchFx(const MarketSlot &slot) {
  char url[160];
  snprintf(url, sizeof(url),
           "https://api.stock.naver.com/marketindex/exchange/%s", slot.code);

  JsonDocument doc;
  if (!httpGetJson(url, doc))
    return 0;

  JsonObject info = doc["exchangeInfo"].as<JsonObject>();
  if (info.isNull())
    info = doc.as<JsonObject>();
  if (info.isNull()) {
    Serial.printf("market fetch: %s missing exchangeInfo\n", slot.tag);
    return 0;
  }

  float price = NAN;
  float pct = NAN;
  if (!parseCloseAndPct(info, &price, &pct)) {
    Serial.printf("market fetch: %s missing price\n", slot.tag);
    return 0;
  }

  const char *dir = info["fluctuationsType"]["name"] | "";
  if (pct > 0.0f && strcmp(dir, "FALLING") == 0)
    pct = -pct;
  else if (pct < 0.0f && strcmp(dir, "RISING") == 0)
    pct = -pct;

  if (!storeQuote(slot.tag, price, pct))
    return 0;
  Serial.printf("market fetch ok  %s=%.2f pct=%.2f\n", slot.tag, price, pct);
  return 1;
}

static bool saveConfigToNvs(void) {
  String json;
  if (!marketConfigGetJson(json))
    return false;
  Preferences prefs;
  prefs.begin(kPrefsNs, false);
  const bool ok = prefs.putString(kPrefsKey, json) > 0;
  prefs.end();
  return ok;
}

static bool jsonFlagOn(JsonVariantConst v, bool defaultOn) {
  if (v.isNull())
    return defaultOn;
  if (v.is<bool>())
    return v.as<bool>();
  if (v.is<int>() || v.is<long>())
    return v.as<int>() != 0;
  return defaultOn;
}

static void applyDefaultsFromObj(JsonObjectConst d) {
  static const char *kTags[] = {"KS", "KQ", "Nasdaq", "USD"};
  for (const char *tag : kTags) {
    MarketSlot *s = findDefault(tag);
    if (!s)
      continue;
    if (d.isNull() || !d[tag].is<JsonVariantConst>()) {
      s->enabled = true;
      continue;
    }
    s->enabled = jsonFlagOn(d[tag], true);
  }
}

static bool loadConfigFromNvs(void) {
  resetDefaultSlots();
  marketVisible = true;

  Preferences prefs;
  prefs.begin(kPrefsNs, true);
  const String raw = prefs.getString(kPrefsKey, "");
  prefs.end();

  if (!raw.length()) {
    Serial.println("market cfg: defaults (no NVS)");
    return true;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, raw);
  if (err) {
    Serial.printf("market cfg: NVS JSON %s — using defaults\n", err.c_str());
    resetDefaultSlots();
    marketVisible = true;
    return false;
  }

  marketVisible = jsonFlagOn(doc["v"], true);
  applyDefaultsFromObj(doc["d"].as<JsonObjectConst>());

  JsonArrayConst customs = doc["c"].as<JsonArrayConst>();
  if (!customs.isNull()) {
    size_t customN = 0;
    for (JsonObjectConst row : customs) {
      if (customN >= MARKET_MAX_CUSTOM)
        break;
      if (slotCount >= MARKET_MAX_QUOTES)
        break;
      const char *tag = row["t"] | "";
      const char *code = row["c"] | "";
      if (!tag[0] || !code[0])
        continue;
      const MarketKind kind = detectKind(code);
      if (kind == MARKET_KIND_UNKNOWN)
        continue;
      if (countEnabled() >= MARKET_MAX_QUOTES)
        break;

      MarketSlot &s = slots[slotCount++];
      copyTrunc(s.tag, sizeof(s.tag), tag);
      copyTrunc(s.code, sizeof(s.code), code);
      s.kind = kind;
      s.enabled = true;
      s.isDefault = false;
      ++customN;
    }
  }

  Serial.printf("market cfg: loaded slots=%u enabled=%u visible=%d\n",
                (unsigned)slotCount, (unsigned)countEnabled(),
                (int)marketVisible);
  return true;
}

bool marketConfigGetJson(String &out) {
  if (slotCount == 0)
    loadConfigFromNvs();

  out = "{\"v\":";
  out += marketVisible ? '1' : '0';
  out += ",\"d\":{";
  bool firstDef = true;
  for (size_t i = 0; i < slotCount; ++i) {
    if (!slots[i].isDefault)
      continue;
    if (!firstDef)
      out += ',';
    firstDef = false;
    out += '"';
    out += slots[i].tag;
    out += "\":";
    out += slots[i].enabled ? '1' : '0';
  }
  out += "},\"c\":[";
  bool firstC = true;
  for (size_t i = 0; i < slotCount; ++i) {
    if (slots[i].isDefault)
      continue;
    if (!firstC)
      out += ',';
    firstC = false;
    out += "{\"t\":\"";
    out += slots[i].tag;
    out += "\",\"c\":\"";
    out += slots[i].code;
    out += "\"}";
  }
  out += "]}";
  return true;
}

static bool asciiLabelOk(const char *s, size_t maxLen) {
  if (!s || !s[0] || strlen(s) >= maxLen)
    return false;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p;
       ++p) {
    if (*p < 0x20 || *p > 0x7e)
      return false;
  }
  return true;
}

bool marketConfigApplyJson(const char *json, String *errMsg) {
  auto fail = [&](const char *msg) -> bool {
    if (errMsg)
      *errMsg = msg;
    return false;
  };

  if (!json || !json[0])
    return fail("empty body");

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json);
  if (err)
    return fail("bad JSON");

  MarketSlot next[MARKET_MAX_QUOTES];
  size_t nextCount = 0;
  memset(next, 0, sizeof(next));

  auto addDef = [&](const char *tag, const char *code, MarketKind kind,
                    bool enabled) {
    if (nextCount >= MARKET_MAX_QUOTES)
      return;
    MarketSlot &s = next[nextCount++];
    copyTrunc(s.tag, sizeof(s.tag), tag);
    copyTrunc(s.code, sizeof(s.code), code);
    s.kind = kind;
    s.enabled = enabled;
    s.isDefault = true;
  };

  const bool nextVisible = jsonFlagOn(doc["v"], true);

  JsonObjectConst d = doc["d"].as<JsonObjectConst>();
  auto defOn = [&](const char *tag) -> bool {
    if (d.isNull() || !d[tag].is<JsonVariantConst>())
      return true;
    return jsonFlagOn(d[tag], true);
  };

  addDef("KS", "KOSPI", MARKET_KIND_DOMESTIC_INDEX, defOn("KS"));
  addDef("KQ", "KOSDAQ", MARKET_KIND_DOMESTIC_INDEX, defOn("KQ"));
  addDef("Nasdaq", ".IXIC", MARKET_KIND_WORLD_INDEX, defOn("Nasdaq"));
  addDef("USD", "FX_USDKRW", MARKET_KIND_FX, defOn("USD"));

  JsonArrayConst customs = doc["c"].as<JsonArrayConst>();
  size_t customN = 0;
  if (!customs.isNull()) {
    for (JsonObjectConst row : customs) {
      if (customN >= MARKET_MAX_CUSTOM)
        return fail("too many custom tickers");
      const char *tag = row["t"] | "";
      const char *code = row["c"] | "";
      if (!asciiLabelOk(tag, MARKET_TAG_LEN))
        return fail("invalid name");
      if (!asciiLabelOk(code, MARKET_CODE_LEN))
        return fail("invalid code");
      const MarketKind kind = detectKind(code);
      if (kind == MARKET_KIND_UNKNOWN)
        return fail("unsupported code");

      for (size_t i = 0; i < nextCount; ++i) {
        if (strcasecmp(next[i].tag, tag) == 0)
          return fail("duplicate name");
        if (strcasecmp(next[i].code, code) == 0)
          return fail("duplicate code");
      }

      MarketSlot &s = next[nextCount++];
      copyTrunc(s.tag, sizeof(s.tag), tag);
      copyTrunc(s.code, sizeof(s.code), code);
      s.kind = kind;
      s.enabled = true;
      s.isDefault = false;
      ++customN;
    }
  }

  size_t enabled = 0;
  for (size_t i = 0; i < nextCount; ++i) {
    if (next[i].enabled)
      ++enabled;
  }
  if (enabled > MARKET_MAX_QUOTES)
    return fail("too many enabled quotes");

  memcpy(slots, next, sizeof(slots));
  slotCount = nextCount;
  if (nextVisible != marketVisible)
    visibilityChanged = true;
  marketVisible = nextVisible;

  if (!saveConfigToNvs())
    return fail("NVS write failed");

  Serial.printf("market cfg: saved slots=%u enabled=%u visible=%d\n",
                (unsigned)slotCount, (unsigned)enabled, (int)marketVisible);
  if (errMsg)
    errMsg->clear();
  return true;
}

void marketFetchReloadFromNvs(void) { loadConfigFromNvs(); }

static void waitOutWifiScan(void) {
  // A portal /scan holds the Wi‑Fi driver. HTTPS during that stretch times out
  // and stalls the chip; wait it out on this task only.
  while (WiFi.scanComplete() == WIFI_SCAN_RUNNING)
    vTaskDelay(pdMS_TO_TICKS(50));
}

static size_t runFetchPass(void) {
  waitOutWifiScan();
  size_t got = 0;

  bool ksOn = false, kqOn = false;
  const MarketSlot *ksSlot = nullptr;
  const MarketSlot *kqSlot = nullptr;
  for (size_t i = 0; i < slotCount; ++i) {
    if (!slots[i].enabled || slots[i].kind != MARKET_KIND_DOMESTIC_INDEX)
      continue;
    if (strcmp(slots[i].code, "KOSPI") == 0) {
      ksOn = true;
      ksSlot = &slots[i];
    } else if (strcmp(slots[i].code, "KOSDAQ") == 0) {
      kqOn = true;
      kqSlot = &slots[i];
    }
  }

  bool batchDone = false;
  if (ksOn && kqOn) {
    got += fetchDomesticIndexBatch("KOSPI,KOSDAQ", ksSlot->tag, kqSlot->tag);
    batchDone = true;
  }

  for (size_t i = 0; i < slotCount; ++i) {
    const MarketSlot &s = slots[i];
    if (!s.enabled)
      continue;

    if (s.kind == MARKET_KIND_DOMESTIC_INDEX) {
      if (batchDone && (strcmp(s.code, "KOSPI") == 0 ||
                        strcmp(s.code, "KOSDAQ") == 0))
        continue;
      got += fetchDomesticIndexOne(s);
    } else if (s.kind == MARKET_KIND_DOMESTIC_STOCK) {
      got += fetchDomesticStock(s);
    } else if (s.kind == MARKET_KIND_WORLD_INDEX) {
      got += fetchWorldIndex(s);
    } else if (s.kind == MARKET_KIND_FX) {
      got += fetchFx(s);
    }
  }
  return got;
}

static void marketWorker(void * /*arg*/) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    fetchBusy = true;
    fetchQueued = false;
    if (!marketVisible) {
      fetchBusy = false;
      Serial.println("market fetch: bg skip (hidden)");
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("market fetch: bg skip (no STA)");
      fetchBusy = false;
      continue;
    }
    // Weather used to run on the UI task (8s HTTP timeout) and froze the clock.
    weatherFetchOnce();
    if (!marketVisible) {
      fetchBusy = false;
      Serial.println("market fetch: bg skip quotes (hidden)");
      continue;
    }
    while (pendingReady)
      vTaskDelay(pdMS_TO_TICKS(20));

    MarketQuote built[MARKET_MAX_QUOTES];
    size_t builtCount = 0;
    setStoreTarget(built, &builtCount);
    const size_t got = runFetchPass();
    setStoreTarget(quotes, &quoteCount);
    fetchBusy = false;

    if (got == 0) {
      Serial.println("market fetch: bg all failed");
      continue;
    }
    memcpy(pendingQuotes, built, sizeof(pendingQuotes));
    pendingCount = builtCount;
    pendingReady = true;
    Serial.printf("market fetch: bg ready (%u quotes)\n", (unsigned)got);
  }
}

void marketFetchBegin(void) {
  hasData = false;
  updatedFlag = false;
  visibilityChanged = false;
  pendingReady = false;
  pendingCount = 0;
  setStoreTarget(quotes, &quoteCount);
  clearStore();
  loadConfigFromNvs();
  if (!marketTaskHandle) {
    // HTTPS + ArduinoJson need headroom; keep off the LVGL core.
    xTaskCreatePinnedToCore(marketWorker, "market", 12288, nullptr, 1,
                            &marketTaskHandle, 0);
  }
  Serial.println("market fetch: Naver (config slots, with ingest wake)");
}

void marketFetchRequestRefresh(void) {
  if (!marketTaskHandle)
    return;
  if (WiFi.status() != WL_CONNECTED)
    return;
  fetchQueued = true;
  xTaskNotifyGive(marketTaskHandle);
}

bool marketFetchBusy(void) { return fetchQueued || fetchBusy; }

bool marketConfigIsVisible(void) { return marketVisible; }

bool marketConfigTakeVisibilityChanged(void) {
  if (!visibilityChanged)
    return false;
  visibilityChanged = false;
  return true;
}

void marketFetchService(void) {
  if (!pendingReady)
    return;
  memcpy(quotes, pendingQuotes, sizeof(quotes));
  quoteCount = pendingCount;
  pendingReady = false;
  hasData = quoteCount > 0;
  updatedFlag = hasData;
}

bool marketFetchHasData(void) { return hasData; }

bool marketFetchTakeUpdated(void) {
  if (!updatedFlag)
    return false;
  updatedFlag = false;
  return true;
}

size_t marketFetchCount(void) { return quoteCount; }

bool marketFetchGetAt(size_t index, MarketQuote *out) {
  if (!out || index >= quoteCount || !quotes[index].valid)
    return false;
  *out = quotes[index];
  return true;
}

#else

void marketFetchBegin(void) {}
void marketFetchRequestRefresh(void) {}
bool marketFetchBusy(void) { return false; }
void marketFetchService(void) {}
bool marketFetchHasData(void) { return false; }
bool marketFetchTakeUpdated(void) { return false; }
size_t marketFetchCount(void) { return 0; }
bool marketFetchGetAt(size_t, MarketQuote *) { return false; }
void marketFetchReloadFromNvs(void) {}
bool marketConfigGetJson(String &out) {
  out = "{\"v\":1,\"d\":{\"KS\":1,\"KQ\":1,\"Nasdaq\":1,\"USD\":1},\"c\":[]}";
  return true;
}
bool marketConfigApplyJson(const char *, String *errMsg) {
  if (errMsg)
    *errMsg = "unavailable";
  return false;
}
bool marketConfigIsVisible(void) { return true; }
bool marketConfigTakeVisibilityChanged(void) { return false; }

#endif
