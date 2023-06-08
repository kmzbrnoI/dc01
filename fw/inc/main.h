#pragma once

#include <stdbool.h>
#include "stm32f1xx_hal.h"

typedef enum {
	mInitializing = 0,
	mNormalOp = 1,
	mBigRelayTest = 2,
	mOverride = 3,
	mFailure = 4,
} DCmode;

extern volatile DCmode dcmode;
extern volatile bool dccConnected;
extern uint8_t failureCode;

#define DCFAIL_NOFAILURE 0
#define DCFAIL_BRT 1
#define DCFAIL_CONT 2

#define DCCON_WARNING_MS 500
#define DCCON_TIMEOUT_MS 1000

void setMode(DCmode);
bool dccOnInput();
void setDccConnected(bool state);
bool IsDCCPCAlive();
