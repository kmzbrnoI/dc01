#include "selftest.h"
#include "main.h"

/* Private variables ---------------------------------------------------------*/

BRTestStep brTestStep;
BRTestState brTestState;
BRTestError brTestError;

/* Private function prototypes -----------------------------------------------*/

static void brtest_set_state(BRTestState new);

/* Code ----------------------------------------------------------------------*/

void brtest_init() {
	brTestStep = brttStopped;
	brTestState = brtsNotYetRun;
	brTestError = brteNoError;
}

void brtest_run(void) {
	if (!brtest_ready()) {
		
	}
}

void brtest_interrupt(void) {
}

void brtest_update(void) {
}

bool brtest_ready(void) {
	return dccOnInput();
}

void brtest_set_state(BRTestState new) {
	if (new == brTestState)
		return;

	brTestState = new;
	brtest_changed();

	if (new == brtsFail)
		brtest_failed();
	else if (new == brtsDone)
		brtest_finished();
}
