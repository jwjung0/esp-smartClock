#pragma once

#include <stddef.h>
#include <stdbool.h>

#if defined(__cplusplus)
#include <Arduino.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define PUTTING_IR_GATES 5
#define PUTTING_CAL_MAX 8

typedef struct {
  float speed_mps;
  float mat_m;
  float slow_m;
  float normal_m;
  float fast_m;
  bool valid;
} PuttingShot;

void puttingIrBegin(void);
void puttingIrLoop(void);

bool puttingIsEnabled(void);
void puttingSetEnabled(bool on);
bool puttingTakeModeChanged(void);

bool puttingIsBusy(void);
bool puttingTakeBusyChanged(void);
bool puttingTakeResult(PuttingShot *out);
bool puttingHasLastShot(void);
void puttingGetLastShot(PuttingShot *out);

float puttingMatMu(void);
int puttingCalCount(void);
float puttingGreenSlowM(void);
float puttingGreenNormalM(void);
float puttingGreenFastM(void);

void puttingReadGateLevels(bool levels[PUTTING_IR_GATES]);

#if defined(__cplusplus)
} // extern "C"

bool puttingCalibrate(float measured_m, String *errMsg);
void puttingCalClear(void);
bool puttingSetGreens(float slow_m, float normal_m, float fast_m,
                      String *errMsg);
void puttingLiveJson(String &out);
#endif
