#pragma once

void dataIngestBegin();

// allowRadioOff: after boot config window, wake → POST → radio off.
void dataIngestLoop(bool allowRadioOff);
