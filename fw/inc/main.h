#pragma once

#include <stdbool.h>
#include "stm32f1xx_hal.h"

typedef enum {
	mInitializing = 0,
	mOk = 1,
	mBigRelayTest = 2,
	mOverride = 3,
	mFailure = 4,
} DCmode;

extern DCmode dcmode;
extern bool dccConnected;
extern uint8_t failureCode;

#define DCFAIL_NOFAILURE 0
#define DCFAIL_BRT 1
#define DCFAIL_CONT 2

void setMode(DCmode);
bool dccOnInput();
