#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// LEDC PWM on PIN_TFT_BL + lux→duty (shared by app and checkup).
void blCtrlBegin(void);

// Load auto-brightness + night/day % from NVS (call once at boot after Serial).
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

// Night floor / day peak as percent of full PWM (defaults 20 / 80).
// Clamped 1–100; night is forced ≤ day. Persisted in NVS clockui.
uint8_t blCtrlNightPct(void);
uint8_t blCtrlDayPct(void);
bool blCtrlSetNightDayPct(uint8_t nightPct, uint8_t dayPct);

// True when backlight is requested on (PWM or full).
bool blCtrlWantOn(void);

// After CPU light sleep: re-attach LEDC and restore last duty.
void blCtrlReapplyAfterSleep(void);

#ifdef __cplusplus
}
#endif
