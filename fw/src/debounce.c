#include <stddef.h>
#include "debounce.h"

#define BTN_DEBOUNCE_THRESHOLD 100 // 10 ms
#define DCC_DEBOUNCE_THRESHOLD 10 // 1 ms
#define DCC_DEBOUNCE_LIMIT 20 // 2 ms

volatile DebouncePin debounced[DEBOUNCED_COUNT] = {
	{.pin=&pin_btn_go, .threshold_raise=BTN_DEBOUNCE_THRESHOLD, .threshold_fall=0, .limit=BTN_DEBOUNCE_THRESHOLD},
	{.pin=&pin_btn_stop, .threshold_raise=BTN_DEBOUNCE_THRESHOLD, .threshold_fall=0, .limit=BTN_DEBOUNCE_THRESHOLD},
	{.pin=&pin_btn_override, .threshold_raise=BTN_DEBOUNCE_THRESHOLD, .threshold_fall=0, .limit=BTN_DEBOUNCE_THRESHOLD},
	{.pin=&pin_dcc1, .threshold_raise=DCC_DEBOUNCE_THRESHOLD, .threshold_fall=0, .limit=DCC_DEBOUNCE_LIMIT},
	{.pin=&pin_dcc2, .threshold_raise=DCC_DEBOUNCE_THRESHOLD, .threshold_fall=0, .limit=DCC_DEBOUNCE_LIMIT},
};

void debounce_init() {
	for (size_t i = 0; i < DEBOUNCED_COUNT; i++) {
		// All inputs pulled up
		debounced[i].counter = debounced[i].limit;
		debounced[i].state = true;
	}
}

void debounce_update() {
	for (size_t i = 0; i < DEBOUNCED_COUNT; i++) {
		if (gpio_pin_read(*debounced[i].pin)) {
			if (debounced[i].counter < debounced[i].limit) {
				debounced[i].counter++;
				if ((debounced[i].counter == debounced[i].threshold_raise) && (!debounced[i].state)) {
					debounced[i].state = true;
					debounce_on_raise(*debounced[i].pin);
				}
			}
		} else {
			if (debounced[i].counter > 0) {
				debounced[i].counter--;
				if ((debounced[i].counter == debounced[i].threshold_fall) && (debounced[i].state)) {
					debounced[i].state = false;
					debounce_on_fall(*debounced[i].pin);
				}
			}
		}
	}
}
