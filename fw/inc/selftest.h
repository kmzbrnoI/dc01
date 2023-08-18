#pragma once

#include <stddef.h>
#include <stdbool.h>

#define TEST_WARNING_TIMEOUT 2 // 100 ms
#define TEST_STEP_TIMEOUT 10 // 1000 ms single step timeout

typedef enum {
	brttStopped = 0,
	brttInitTurnoff = 1,
	brttWaitForSingleSide = 2,
	brttBothOnWait = 3,
	brttR1OffWait = 4,
	brttR1OnWait = 5,
	brttR2OffWait = 6,
	brttR2Onwait = 7,
	brttFinished = 8,
} BRTestStep;

typedef enum {
	brtsNotYetRun = 0,
	brtsInProgress = 1,
	brtsFinished = 2,
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
size_t brtest_start(void);
void brtest_update(void); // call each 100ms
void brtest_interrupt(void);
bool brtest_ready(void);
bool brtest_running(void);

void brtest_finished(void);
void brtest_failed(void);
void brtest_changed(void);
