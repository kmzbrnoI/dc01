#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef enum {
	brttStopped = 0,
	brttInitTurnoff = 1,
} BRTestStep;

typedef enum {
	brtsNotYetRun = 0,
	brtsInProgress = 1,
	brtsDone = 2,
	brtsFail = 3,
	brtsInterrupted = 4,
} BRTestState;

typedef enum {
	brteNoError = 0,
	brteDccNotAppeared = 1,
	brteDccNotDisappeared = 2,
} BRTestError;

extern BRTestStep brTestStep;
extern BRTestState brTestState;
extern BRTestError brTestError;

void brtest_init(void);
void brtest_run(void);
void brtest_update(void); // call each 100ms
void brtest_interrupt(void);
bool brtest_ready(void);

void brtest_finished(void);
void brtest_failed(void);
void brtest_changed(void);
