#include "gpio.h"
#include "stm32f1xx_hal.h"

const PinDef pin_led_red = {GPIOB, GPIO_PIN_13};
const PinDef pin_led_green = {GPIOB, GPIO_PIN_15};
const PinDef pin_led_blue = {GPIOB, GPIO_PIN_12};
const PinDef pin_led_yellow = {GPIOB, GPIO_PIN_14};

const PinDef pin_usb_dn = {GPIOA, GPIO_PIN_11};
const PinDef pin_usb_dp = {GPIOA, GPIO_PIN_12};
const PinDef pin_usb_dp_pullup = {USBD_DP_PORT, 1 << USBD_DP_PIN};

const PinDef pin_btn_override = {GPIOB, GPIO_PIN_9};
const PinDef pin_btn_go = {GPIOB, GPIO_PIN_7};
const PinDef pin_led_go = {GPIOB, GPIO_PIN_11};
const PinDef pin_btn_stop = {GPIOB, GPIO_PIN_8};
const PinDef pin_led_stop = {GPIOB, GPIO_PIN_10};

const PinDef pin_out_alert = {GPIOB, GPIO_PIN_6};
const PinDef pin_out_on = {GPIOB, GPIO_PIN_5};

const PinDef pin_dcc1 = {GPIOB, GPIO_PIN_0};
const PinDef pin_dcc2 = {GPIOB, GPIO_PIN_1};

const PinDef pin_relay1 = {GPIOB, GPIO_PIN_2};
const PinDef pin_relay2 = {GPIOB, GPIO_PIN_3};

const PinDef pin_debug_a = {GPIOA, GPIO_PIN_8};
const PinDef pin_debug_b = {GPIOA, GPIO_PIN_9};

const PinDef pin_debug_cts = {GPIOA, GPIO_PIN_0};
const PinDef pin_debug_tx = {GPIOA, GPIO_PIN_2};
const PinDef pin_debug_rx = {GPIOA, GPIO_PIN_3};


void gpio_pins_init(GPIO_TypeDef* port, uint32_t pinMask, uint32_t mode,
                    uint32_t pull, uint32_t speed, bool de_init_first);


void gpio_init(void) {
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();

	gpio_pins_init(
		GPIOB,
		pin_led_blue.pin | pin_led_red.pin | pin_led_yellow.pin | pin_led_green.pin |
			pin_led_go.pin | pin_led_stop.pin | pin_out_alert.pin | pin_out_on.pin |
			pin_relay1.pin | pin_relay2.pin,
		GPIO_MODE_OUTPUT_PP,
		GPIO_NOPULL,
		GPIO_SPEED_FREQ_LOW,
		false
	);

	gpio_pin_init(pin_usb_dn, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH, false);
	gpio_pin_init(pin_usb_dp, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH, false);
	gpio_pin_init(pin_usb_dp_pullup, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, false);

	gpio_pin_init(pin_btn_go, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_SPEED_FREQ_LOW, false);
	gpio_pin_init(pin_btn_stop, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_SPEED_FREQ_LOW, false);
	gpio_pin_init(pin_btn_override, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_SPEED_FREQ_LOW, false);
	gpio_pin_init(pin_dcc1, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_SPEED_FREQ_LOW, false);
	gpio_pin_init(pin_dcc2, GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_SPEED_FREQ_LOW, false);

	gpio_pin_init(pin_debug_a, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH, false);
	gpio_pin_init(pin_debug_b, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_HIGH, false);
}

void gpio_pins_init(GPIO_TypeDef* port, uint32_t pinMask, uint32_t mode,
                    uint32_t pull, uint32_t speed, bool de_init_first) {

	// HAL_GPIO_Init leaves some flags set if called multiple times
	// on the same pin
	if (de_init_first)
		HAL_GPIO_DeInit(port, pinMask);

	GPIO_InitTypeDef init;
	init.Pin = pinMask;
	init.Mode = mode;
	init.Pull = pull;
	init.Speed = speed;
	HAL_GPIO_Init(port, &init);
}

inline void gpio_pin_init(PinDef pin, uint32_t mode, uint32_t pull, uint32_t speed, bool de_init_first) {
	gpio_pins_init(pin.port, pin.pin, mode, pull, speed, de_init_first);
}

bool gpio_pin_read(PinDef pin) {
	return HAL_GPIO_ReadPin(pin.port, pin.pin) == GPIO_PIN_SET;
}

void gpio_pin_write(PinDef pin, bool value) {
	HAL_GPIO_WritePin(pin.port, pin.pin, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void gpio_pin_toggle(PinDef pin) {
	HAL_GPIO_TogglePin(pin.port, pin.pin);
}
