#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// LEDC PWM on PIN_TFT_BL + lux→duty (shared by app and checkup).
void blCtrlBegin(void);

// Load auto-brightness flag from NVS (call once at boot after Serial).
void blCtrlLoadPrefs(void);

// on=false → duty 0; on=true → lux map if auto on, else full.
void blCtrlSetOn(bool on);

// Re-read VEML7700 and update duty when backlight is on (no-op if auto off).
void blCtrlUpdateFromLux(void);

// Last applied duty 0–255 (Serial diagnostics).
int blCtrlDuty(void);

// Last raw / EMA lux used for duty (NAN if none yet).
float blCtrlLuxRaw(void);
float blCtrlLuxEma(void);

// Auto brightness from ambient lux (default on). Persisted in NVS clockui.
bool blCtrlAutoEnabled(void);
void blCtrlSetAutoEnabled(bool enabled);

#ifdef __cplusplus
}
#endif
