/* Low-level GPIO functions, pin definitions. */

#pragma once

#include <stdbool.h>
#include "stm32f1xx_hal.h"

typedef struct {
	GPIO_TypeDef* port;
	uint32_t pin;
} PinDef;

extern const PinDef pin_led_red;
extern const PinDef pin_led_green;
extern const PinDef pin_led_blue;
extern const PinDef pin_led_yellow;

extern const PinDef pin_usb_dn;
extern const PinDef pin_usb_dp;
extern const PinDef pin_usb_dp_pullup;

extern const PinDef pin_btn_override;
extern const PinDef pin_btn_go;
extern const PinDef pin_led_go;
extern const PinDef pin_btn_stop;
extern const PinDef pin_led_stop;

extern const PinDef pin_out_alert;
extern const PinDef pin_out_on;

extern const PinDef pin_dcc1;
extern const PinDef pin_dcc2;

extern const PinDef pin_relay1;
extern const PinDef pin_relay2;

extern const PinDef pin_debug_a;
extern const PinDef pin_debug_b;

extern const PinDef pin_debug_cts;
extern const PinDef pin_debug_tx;
extern const PinDef pin_debug_rx;


void gpio_init(void);

void gpio_pin_init(PinDef pin, uint32_t mode, uint32_t pull, uint32_t speed, bool de_init_first);
bool gpio_pin_read(PinDef pin);
void gpio_pin_write(PinDef pin, bool value);
void gpio_pin_toggle(PinDef pin);
