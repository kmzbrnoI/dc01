/* Blinking with LEDs: call to ‹led_activate› activates LED for millis_enable
 * milliseconds and then keep it disabled for millis_disable millisenconds.
 * This function could be called faster (e.g. when processing data etc.), but
 * LED will never blink as fast as data arrive. Aim of this code is for LEDs
 * blinking to be human-readable.
 */

#pragma once

#include <stdint.h>
#include "gpio.h"

void leds_init(void);
void leds_update_1ms(void);
void led_activate(PinDef pin, size_t millis_enable, size_t millis_disable);
