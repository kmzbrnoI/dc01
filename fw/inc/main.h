/* Main header file with global defines, types and variables. */

#pragma once

#include <stdbool.h>
#include "stm32f1xx_hal.h"

typedef enum {
	mInitializing = 0,
	mNormalOp = 1,
	mOverride = 2,
	mFailure = 3,
} DCmode;

extern volatile DCmode dcmode;
extern uint8_t failure_code;

typedef union {
	size_t all;
	struct {
		bool brtest_time: 1;
		bool timeout :1;
	} sep;
} Warnings;

extern volatile Warnings warnings;

#define DCFAIL_NOFAILURE 0
#define DCFAIL_BRT 1
#define DCFAIL_CONT 2

#define DCCON_WARNING_MS 500
#define DCCON_TIMEOUT_MS 2000

#define BRTEST_NOTEST_MAX_TIME (10) // seconds
#define ALERT_TIME (1000) // milliseconds

void set_mode(DCmode);
void set_relays(bool relay1, bool relay2);
void appl_set_relays(bool state);
bool is_dcc_connected(void);
bool is_dcc_pc_alive(void);

bool dcc_at_least_one(void);
bool dcc_just_single(void);
bool dcc_both(void);
