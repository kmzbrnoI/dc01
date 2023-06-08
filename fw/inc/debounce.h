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

extern DebouncePin debounced[DEBOUNCED_COUNT];

void debounce_on_fall(PinDef pin);
void debounce_on_raise(PinDef pin);

void debounce_init();

// This function should be called each 100 us
void debounce_update();
