#pragma once

#include <stddef.h>
#include <stdbool.h>

#if defined(__cplusplus)
#include <Arduino.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define MARKET_MAX_QUOTES 8
#define MARKET_MAX_CUSTOM 4
#define MARKET_TAG_LEN 16
#define MARKET_CODE_LEN 16

typedef struct {
  char tag[MARKET_TAG_LEN];
  float price;
  float pct; // signed percent change; 0 = unchanged
  bool valid;
} MarketQuote;

void marketFetchBegin(void);

// HTTPS GETs Naver Finance quotes while STA is connected (blocking).
// Use at boot / ingest wake only — not from the HTTP/UI path.
bool marketFetchOnce(void);

// Queue a background refetch (portal save). Non-blocking.
void marketFetchRequestRefresh(void);
// Apply finished background results on the LVGL/main loop.
void marketFetchService(void);

bool marketFetchHasData(void);
bool marketFetchTakeUpdated(void); // true once per successful fetch

size_t marketFetchCount(void);
bool marketFetchGetAt(size_t index, MarketQuote *out);

#if defined(__cplusplus)
} // extern "C"

// Config portal helpers (C++ only).
void marketFetchReloadFromNvs(void);
bool marketConfigGetJson(String &out);
// Apply JSON from portal; on success persists NVS and reloads slots.
// Optional errMsg receives a short reason on failure.
bool marketConfigApplyJson(const char *json, String *errMsg);
#endif
