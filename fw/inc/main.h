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
extern uint8_t failure_code;

#define DCFAIL_NOFAILURE 0
#define DCFAIL_BRT 1
#define DCFAIL_CONT 2

#define DCCON_WARNING_MS 500
#define DCCON_TIMEOUT_MS 1000

#define BRTEST_NOTEST_MAX_TIME (10) // seconds
#define ALERT_TIME (3000) // milliseconds

void set_mode(DCmode);
void set_relays(bool relay1, bool relay2);
void appl_set_relays(bool state);
bool is_dcc_connected(void);
bool is_dcc_pc_alive(void);

bool dcc_at_least_one(void);
bool dcc_just_single(void);
bool dcc_both(void);
