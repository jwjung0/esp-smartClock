#include "time_sync.h"

#if defined(FIRMWARE_MODE_APP)

#include <esp_sntp.h>
#include <sys/time.h>

static const long kGmtOffsetSec = 9 * 3600; // KST
static const int kDaylightOffsetSec = 0;
static int lastSec = -1;

void timeSyncBegin() {
  configTime(kGmtOffsetSec, kDaylightOffsetSec, "pool.ntp.org",
             "time.google.com", "time.cloudflare.com");
  Serial.println("NTP: KST (UTC+9), waiting for sync…");
}

void timeSyncRequestResync() {
  sntp_restart();
  Serial.println("NTP: resync requested");
}

bool timeSyncIsReady() {
  struct tm t;
  return getLocalTime(&t, 0);
}

bool timeSyncTick(struct tm *out) {
  if (!out)
    return false;
  if (!getLocalTime(out, 0))
    return false;
  if (out->tm_sec == lastSec)
    return false;
  lastSec = out->tm_sec;
  return true;
}

void timeSyncFormatTime(const struct tm &t, char *buf, size_t len) {
  snprintf(buf, len, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
}

void timeSyncFormatDate(const struct tm &t, char *buf, size_t len) {
  static const char *kDow[] = {"SUN", "MON", "TUE", "WED",
                               "THU", "FRI", "SAT"};
  const int wday = (t.tm_wday >= 0 && t.tm_wday < 7) ? t.tm_wday : 0;
  snprintf(buf, len, "%04d %02d/%02d %s", t.tm_year + 1900, t.tm_mon + 1,
           t.tm_mday, kDow[wday]);
}

#else

void timeSyncBegin() {}
void timeSyncRequestResync() {}
bool timeSyncIsReady() { return false; }
bool timeSyncTick(struct tm * /*out*/) { return false; }
void timeSyncFormatTime(const struct tm & /*t*/, char *buf, size_t len) {
  if (len)
    buf[0] = '\0';
}
void timeSyncFormatDate(const struct tm & /*t*/, char *buf, size_t len) {
  if (len)
    buf[0] = '\0';
}

#endif
