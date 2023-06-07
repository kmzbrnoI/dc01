#include <stdint.h>
#include "leds.h"
#include "gpio.h"

typedef struct {
	PinDef pin;
	size_t global;
	size_t shutdown;
} LedCounters;

#define LEDS_COUNT 4
LedCounters _counters[LEDS_COUNT];

void leds_init(void) {
	_counters[0].pin = pin_led_red;
	_counters[1].pin = pin_led_yellow;
	_counters[2].pin = pin_led_green;
	_counters[3].pin = pin_led_blue;

	for (size_t i = 0; i < LEDS_COUNT; i++)
		_counters[i].global = 0;
}

void leds_update_1ms(void) {
	for (size_t i = 0; i < LEDS_COUNT; i++) {
		if (_counters[i].global > 0) {
			_counters[i].global--;
			if (_counters[i].global == _counters[i].shutdown)
				gpio_pin_write(_counters[i].pin, false);
		}
	}
}

void led_activate(PinDef pin, size_t millis_enable, size_t millis_disable) {
	size_t i = 0;

	if ((pin.port == pin_led_red.port) && (pin.pin == pin_led_red.pin))
		i = 0;
	else if ((pin.port == pin_led_yellow.port) && (pin.pin == pin_led_yellow.pin))
		i = 1;
	else if ((pin.port == pin_led_green.port) && (pin.pin == pin_led_green.pin))
		i = 2;
	else if ((pin.port == pin_led_blue.port) && (pin.pin == pin_led_blue.pin))
		i = 3;
	else
		return;

	if (_counters[i].global == 0) {
		_counters[i].global = millis_enable+millis_disable;
		_counters[i].shutdown = millis_disable;
		gpio_pin_write(pin, true);
	}
}
