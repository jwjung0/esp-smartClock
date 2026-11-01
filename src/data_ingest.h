#pragma once

void dataIngestBegin();

// allowRadioOff: after boot config window, wake → POST → radio off.
// Radio off is deferred until the background market fetch finishes.
void dataIngestLoop(bool allowRadioOff);
// True once, when a deferred radio-off is ready (market fetch idle or timed out).
bool dataIngestTakeRadioOff(void);
