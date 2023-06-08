#pragma once

/* Inputs debouncing. */

#include <stdbool.h>
#include <stdint.h>
#include "gpio.h"

typedef struct {
	const PinDef *pin;
	const uint32_t threshold_raise;
	const uint32_t threshold_fall;
	const uint32_t limit;
	uint32_t counter;
	bool state;
} DebouncePin;

#define DEBOUNCED_COUNT 5

extern volatile DebouncePin debounced[DEBOUNCED_COUNT];

#define DEB_BTN_GO 0
#define DEB_BTN_STOP 1
#define DEB_BTN_OVERRIDE 2
#define DEB_DCC1 3
#define DEB_DCC2 4

void debounce_on_fall(PinDef pin);
void debounce_on_raise(PinDef pin);

void debounce_init();

// This function should be called each 100 us
void debounce_update();
