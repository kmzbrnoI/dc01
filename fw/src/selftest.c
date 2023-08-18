#include "selftest.h"
#include "main.h"

/* Private variables ---------------------------------------------------------*/

BRTestStep brTestStep;
BRTestState brTestState;
BRTestError brTestError;
size_t timeout_counter;

/* Private function prototypes -----------------------------------------------*/

static void _brtest_set_state(BRTestState new);
static void _brtest_set_step(BRTestStep new);
static void _brtest_inc_and_check_timeout(void);

/* Code ----------------------------------------------------------------------*/

void brtest_init() {
	brTestStep = brttStopped;
	brTestState = brtsNotYetRun;
	brTestError = brteNoError;
}

size_t brtest_start(void) {
	if (!brtest_ready())
		return 1;
	if (brTestState == brtsInProgress)
		return 2;
	// Do not check step, step remains after finish/interrupt/fail for diagnostics.
	_brtest_set_state(brtsInProgress);
	_brtest_set_step(brttInitTurnoff);
	return 0;
}

void brtest_interrupt(void) {
	if (brTestState != brtsInProgress)
		return;
	_brtest_set_state(brtsInterrupted);
	// Do not reset step – last reached step remains for diagnostics.
}

bool brtest_ready(void) {
	return (dcc_at_least_one()) && (brTestState != brtsInProgress);
}

bool brtest_running(void) {
	return brTestState == brtsInProgress;
}

void _brtest_set_state(BRTestState new) {
	if (new == brTestState)
		return;

	brTestState = new;
	timeout_counter = 0;
	brtest_changed();

	if (new == brtsFail)
		brtest_failed();
	else if (new == brtsFinished)
		brtest_finished();
}

void _brtest_set_step(BRTestStep new) {
	timeout_counter = 0;

	if (new == brTestStep)
		return;

	brTestStep = new;
	brtest_changed();
}

void _brtest_inc_and_check_timeout(void) {
	timeout_counter++;
	if (timeout_counter >= TEST_WARNING_TIMEOUT)
		warnings.sep.brtest_time = true;
	if (timeout_counter >= TEST_STEP_TIMEOUT)
		_brtest_set_state(brtsFail);
}

void brtest_update(void) {
	// called each 100 ms
	if (brTestState != brtsInProgress)
		return;

	_brtest_inc_and_check_timeout();
	if (!dcc_at_least_one()) {
		brtest_interrupt();
		return;
	}

	switch (brTestStep) {
	case brttInitTurnoff:
		set_relays(false, false);
		_brtest_set_step(brttWaitForSingleSide);
		break;

	case brttWaitForSingleSide:
		if (dcc_just_single()) {
			set_relays(true, true);
			_brtest_set_step(brttBothOnWait);
		}
		break;

	case brttBothOnWait:
		if (dcc_both()) {
			set_relays(false, true);
			_brtest_set_step(brttR1OffWait);
		}
		break;

	case brttR1OffWait:
		if (dcc_just_single()) {
			set_relays(true, true);
			_brtest_set_step(brttR1OnWait);
		}
		break;

	case brttR1OnWait:
		if (dcc_both()) {
			set_relays(true, false);
			_brtest_set_step(brttR2OffWait);
		}
		break;

	case brttR2OffWait:
		if (dcc_just_single()) {
			set_relays(true, true);
			_brtest_set_step(brttR2Onwait);
		}
		break;

	case brttR2Onwait:
		if (dcc_both())
			_brtest_set_step(brttFinished);
		break;

	case brttFinished:
		_brtest_set_state(brtsFinished);
		break;

	default:
		break;
	}
}
