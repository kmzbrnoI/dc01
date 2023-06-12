/* Main implementation file.
 *
 * Packet transmission: # TODO - edit for DC-01
 * 1) MTBbus → CDC:
 *    There is no buffer in this direction, bacause we can never receive data
 *    asynchronously. We can receive data only when we send data to MTBbus module
 *    (inquiry or command from PC). We send data to MTBbus slvae module only
 *    if we have space for the response (= previous response is sent to USB).
 * 2) CDC → MTBbus:
 *    As data from USB can be received asynchronously, there is a 256-byte
 *    ring buffer in this direction. When message at the beginning of the buffer
 *    is ready, it is transmissed to MTBbus. Message waits nin buffer till
 *    module responds. MTB-USB tries to resend the packet up to 3 times.
 *    If device does not responds 3 times, packet is removed and error message
 *    is sent to PC. **MTB-USB module provides MTBbus retransmission.**
 */

#include "main.h"
#include "usb_cdc_link.h"
#include "gpio.h"
#include "common.h"
#include "leds.h"
#include "debounce.h"
#include "selftest.h"

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef h_uart_debug;
TIM_HandleTypeDef h_tim2;
TIM_HandleTypeDef h_tim3;
IWDG_HandleTypeDef h_iwdg;

typedef union {
	size_t all;
	struct {
		bool info: 1;
		bool state: 1;
		bool brtsState: 1;
	} sep;
} DeviceUsbTxReq;

volatile DeviceUsbTxReq device_usb_tx_req;

volatile DCmode dcmode;
volatile bool _relay1;
volatile bool _relay2;
uint8_t failure_code;
volatile uint32_t dccon_timer_ms;
bool brtest_request; // brtest_ready & brtest_request → start brtest
volatile uint32_t brtest_timer;

typedef union {
	size_t all;
	struct {
		bool debounce_update: 1;
		bool leds_update: 1;
		bool brtest_update: 1;
	} sep;
} InterruptReq;

volatile InterruptReq interrupt_req;

/* Private function prototypes -----------------------------------------------*/

static void error_handler();
static void init(void);
static bool clock_init(void);
static bool debug_uart_init(void);
static inline void poll_usb_tx_flags(void);
static bool iwdg_init(void);
static void state_leds_update(void);
static void dcc_on_timeout(void);
static bool _brtest_is_time(void);

/* Code ----------------------------------------------------------------------*/

int main(void) {
	init();

	while (true) {
		if (interrupt_req.sep.debounce_update) {
			debounce_update();
			interrupt_req.sep.debounce_update = false;
		}
		if (interrupt_req.sep.leds_update) {
			state_leds_update();
			interrupt_req.sep.leds_update = false;
		}
		if (interrupt_req.sep.brtest_update) {
			brtest_update();
			interrupt_req.sep.brtest_update = false;
		}
		if ((brtest_request) && (brtest_ready())) {
			brtest_start();
		}
		poll_usb_tx_flags();
	}
}

void init(void) {
	h_iwdg.Instance = NULL;

	if (!clock_init())
		error_handler();
	HAL_Init();
	gpio_init();
	leds_init();
	debounce_init();

	interrupt_req.all = 0;
	device_usb_tx_req.all = 0;
	brtest_request = false;
	brtest_timer = BRTEST_NOTEST_MAX_TIME;

	dcmode = mInitializing;
	set_relays(false, false);
	failure_code = DCFAIL_NOFAILURE;

	gpio_pin_write(pin_led_red, true);
	gpio_pin_write(pin_led_yellow, true);
	gpio_pin_write(pin_led_green, true);
	gpio_pin_write(pin_led_blue, true);

	if (!iwdg_init())
		error_handler();

	cdc_init();
	debug_uart_init();
	__HAL_AFIO_REMAP_SWJ_NOJTAG();

	for (size_t i = 0; i < 200; i++) { // time must be enough for debounce
		debounce_update(); // read DCC state
		HAL_Delay(1); // this time does not corespond with main loop's debounce_update period!
	}

	if (dcmode == mInitializing) // if debounce_update did not change mode to mOverride
		set_mode(mNormalOp);

	gpio_pin_write(pin_led_red, false);
	gpio_pin_write(pin_led_yellow, false);
	gpio_pin_write(pin_led_green, false);
	if (cdc_dtr_ready)
		gpio_pin_write(pin_led_blue, false);
}

bool clock_init(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

	// HSE 8 MHz
	// SYSCLK 48 MHz
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
		return false;

	/** Initializes the CPU, AHB and APB buses clocks
	*/
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
	                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
		return false;

	// USB 48 MHz
	PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
	PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
	if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
		return false;

	__HAL_RCC_AFIO_CLK_ENABLE();
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_RCC_TIM2_CLK_ENABLE();
	__HAL_RCC_TIM3_CLK_ENABLE();

	// Timer 2 @ 100 us
	TIM_ClockConfigTypeDef sClockSourceConfig = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};

	h_tim2.Instance = TIM2;
	h_tim2.Init.Prescaler = 32;
	h_tim2.Init.CounterMode = TIM_COUNTERMODE_UP;
	h_tim2.Init.Period = 146;
	h_tim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	h_tim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_Base_Init(&h_tim2) != HAL_OK)
		return false;
	HAL_TIM_Base_Start_IT(&h_tim2);

	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	if (HAL_TIM_ConfigClockSource(&h_tim2, &sClockSourceConfig) != HAL_OK)
		return false;
	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&h_tim2, &sMasterConfig) != HAL_OK)
		return false;

	HAL_NVIC_SetPriority(TIM2_IRQn, 8, 0);
	HAL_NVIC_EnableIRQ(TIM2_IRQn);

	// Timer 3 @ 1 ms
	h_tim3.Instance = TIM3;
	h_tim3.Init.Prescaler = 128;
	h_tim3.Init.CounterMode = TIM_COUNTERMODE_UP;
	h_tim3.Init.Period = 186;
	h_tim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	h_tim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_Base_Init(&h_tim3) != HAL_OK)
		return false;
	HAL_TIM_Base_Start_IT(&h_tim3);

	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	if (HAL_TIM_ConfigClockSource(&h_tim3, &sClockSourceConfig) != HAL_OK)
		return false;
	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&h_tim3, &sMasterConfig) != HAL_OK)
		return false;

	HAL_NVIC_SetPriority(TIM3_IRQn, 8, 0);
	HAL_NVIC_EnableIRQ(TIM3_IRQn);

	return true;
}

static bool debug_uart_init(void) {
	h_uart_debug.Instance = USART2;
	h_uart_debug.Init.BaudRate = 115200;
	h_uart_debug.Init.WordLength = UART_WORDLENGTH_8B;
	h_uart_debug.Init.StopBits = UART_STOPBITS_1;
	h_uart_debug.Init.Parity = UART_PARITY_NONE;
	h_uart_debug.Init.Mode = UART_MODE_TX_RX;
	h_uart_debug.Init.HwFlowCtl = UART_HWCONTROL_CTS;
	h_uart_debug.Init.OverSampling = UART_OVERSAMPLING_16;
	if (HAL_UART_Init(&h_uart_debug) != HAL_OK)
		return false;

	__HAL_RCC_USART2_CLK_ENABLE();

	gpio_pin_init(pin_debug_cts, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, false);
	gpio_pin_init(pin_debug_rx, GPIO_MODE_INPUT, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, false);
	gpio_pin_init(pin_debug_tx, GPIO_MODE_AF_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, false);

	return true;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance == TIM1)
		HAL_IncTick();
}

void error_handler(void) {
	__disable_irq();
	while (true);
}

static bool iwdg_init(void) {
	h_iwdg.Instance = IWDG;
	h_iwdg.Init.Prescaler = IWDG_PRESCALER_4; // Watchdog counter decrements each 100 us
	h_iwdg.Init.Reload = 1000; // Watchdog timeout 100 ms
	return (HAL_IWDG_Init(&h_iwdg) == HAL_OK);
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {
}
#endif /* USE_FULL_ASSERT */

/* Interrupt handlers --------------------------------------------------------*/

// This function handles Non maskable interrupt.
void NMI_Handler(void) {
	while (true);
}

void HardFault_Handler(void) {
	while (true);
}

void MemManage_Handler(void) {
	while (true);
}

void BusFault_Handler(void) {
	while (true);
}

void UsageFault_Handler(void) {
	while (true);
}

void SVC_Handler(void) {}

void DebugMon_Handler(void) {}

// This function handles Pendable request for system service.
void PendSV_Handler(void) {}

void SysTick_Handler(void) {
	HAL_IncTick();
}

void TIM2_IRQHandler(void) {
	// Timer 2 @ 100 us (10 kHz)
	if (_relay1)
		gpio_pin_toggle(pin_relay1);
	if (_relay2)
		gpio_pin_toggle(pin_relay2);

	interrupt_req.sep.debounce_update = true;
	HAL_TIM_IRQHandler(&h_tim2);
}

void TIM3_IRQHandler(void) {
	// Timer 3 @ 1 ms (1 kHz)

	static volatile size_t counter_500ms = 0;
	static volatile bool counter_1s = false;
	counter_500ms++;
	if ((counter_500ms%100) == 0) {
		interrupt_req.sep.brtest_update = true;
	}
	if (counter_500ms >= 500) {
		device_usb_tx_req.sep.state = true;
		interrupt_req.sep.leds_update = true;
		counter_500ms = 0;
		counter_1s = !counter_1s;
		if ((!counter_1s) && (brtest_timer < BRTEST_NOTEST_MAX_TIME))
			brtest_timer++;
	}

	if (dccon_timer_ms < DCCON_TIMEOUT_MS) {
		dccon_timer_ms++;
		if ((dcmode == mNormalOp) && (dccon_timer_ms == DCCON_WARNING_MS)) {
			gpio_pin_write(pin_led_yellow, true);
		}
		if ((dcmode == mNormalOp) && (dccon_timer_ms == DCCON_TIMEOUT_MS)) {
			dcc_on_timeout();
			gpio_pin_write(pin_led_yellow, false);
		}
	}

	leds_update_1ms();

	if (h_iwdg.Instance != NULL)
		HAL_IWDG_Refresh(&h_iwdg);
	HAL_TIM_IRQHandler(&h_tim3);
}

/* USB -----------------------------------------------------------------------*/

void cdc_main_received(uint8_t command_code, uint8_t *data, size_t data_size) {
	if ((command_code == DC_CMD_PM_SET_STATE) && (data_size >= 1)) {
		bool state = (data[0] & 1);
		if (state) {
			dccon_timer_ms = 0;
			gpio_pin_write(pin_led_yellow, false);
		} else {
			dccon_timer_ms = DCCON_TIMEOUT_MS;
		}
		if (dcmode == mNormalOp) {
			if ((state) && (!is_dcc_connected()) && (!brtest_running()) && (_brtest_is_time()))
				brtest_request = true;
			if (!brtest_running())
				set_relays(state, state);
			if ((!state) && (brtest_running())) {
				brtest_interrupt();
				brtest_request = false;
				set_relays(false, false);
			}
		}
	}
}

static inline void poll_usb_tx_flags(void) {
	if (!cdc_dtr_ready)
		device_usb_tx_req.all = 0;  // computer does not listen → ignore all flags
	if (!cdc_main_can_send())
		return; // USB busy → wait for next poll

	if (device_usb_tx_req.sep.info) {
		cdc_tx.separate.data[0] = FW_VER_MAJOR;
		cdc_tx.separate.data[1] = FW_VER_MINOR;

		if (cdc_main_send_nocopy(DC_CMD_MP_INFO, 2))
			device_usb_tx_req.sep.info = false;

	} else if (device_usb_tx_req.sep.state) {
		cdc_tx.separate.data[0] = ((dcmode & 0x07) << 4) | (is_dcc_connected()) | ((dcc_at_least_one() & 1) << 1);
		cdc_tx.separate.data[1] = failure_code;

		if (cdc_main_send_nocopy(DC_CMD_MP_STATE, 2))
			device_usb_tx_req.sep.state = false;
	} else if (device_usb_tx_req.sep.brtsState) {
		cdc_tx.separate.data[0] = brTestState;
		cdc_tx.separate.data[1] = brTestStep;
		cdc_tx.separate.data[2] = brTestError;

		if (cdc_main_send_nocopy(DC_CMD_MP_BRSTATE, 3))
			device_usb_tx_req.sep.brtsState = false;
	}
}

void cdc_main_died() {
	if (dcmode == mNormalOp)
		set_relays(false, false);
}

/* IO ------------------------------------------------------------------------*/

void debounce_on_fall(PinDef pin) {
	if (pindef_eq(pin, pin_btn_go)) {
		if ((dcmode == mOverride) && (!debounced[DEB_BTN_OVERRIDE].state)) {
			if (_brtest_is_time())
				brtest_request = true;
			set_relays(true, true);
		}
	} else if (pindef_eq(pin, pin_btn_stop)) {
		if ((is_dcc_connected()) || (brtest_running())) {
			if (brtest_running()) {
				brtest_interrupt();
				brtest_request = false;
			}
			set_mode(mOverride);
			set_relays(false, false);
		}
	} else if (pindef_eq(pin, pin_btn_override)) {
		set_mode(mOverride);
	}
}

void debounce_on_raise(PinDef pin) {
	if (pindef_eq(pin, pin_btn_override)) {
		if ((!is_dcc_connected()) && (!brtest_running()) && (_brtest_is_time()) && (is_dcc_pc_alive()))
			brtest_request = true;
		set_mode(mNormalOp);
	}
}

void set_mode(DCmode mode) {
	if (dcmode == mode)
		return;
	dcmode = mode;
	device_usb_tx_req.sep.state = true;

	if (brtest_running()) {
		brtest_interrupt();
		brtest_request = false;
	}

	switch (mode) {
	case mInitializing:
		gpio_pin_write(pin_led_red, true);
		gpio_pin_write(pin_led_yellow, true);
		break;
	case mNormalOp:
		set_relays(is_dcc_pc_alive(), is_dcc_pc_alive());
		// intentionally no break here
	case mOverride:
		gpio_pin_write(pin_led_red, false);
		gpio_pin_write(pin_led_green, true);
		break;
	case mBigRelayTest:
		gpio_pin_write(pin_led_red, false);
		gpio_pin_write(pin_led_green, false);
		gpio_pin_write(pin_led_yellow, true);

		size_t brtest_started = brtest_start();
		if (brtest_started != 0) // TODO: check all situations in which this could happen
			set_mode(mFailure);

		break;
	case mFailure:
		gpio_pin_write(pin_led_red, true);
		gpio_pin_write(pin_led_green, false);
		set_relays(false, false);
		break;
	}
}

bool dcc_at_least_one() {
	return (!debounced[DEB_DCC1].state) || (!debounced[DEB_DCC2].state);
}

bool dcc_just_single(void) {
	return (debounced[DEB_DCC1].state) ^ (debounced[DEB_DCC2].state);
}

bool dcc_both(void) {
	return (!debounced[DEB_DCC1].state) || (!debounced[DEB_DCC2].state);
}

void set_relays(bool relay1, bool relay2) {
	if ((relay1 && relay2) != (_relay1 && _relay2))
		device_usb_tx_req.sep.state = true;
	_relay1 = relay1;
	_relay2 = relay2;
	gpio_pin_write(pin_led_go, relay1 && relay2);
	gpio_pin_write(pin_led_stop, !(relay1 && relay2));
}

bool is_dcc_connected(void) {
	return _relay1 && _relay2;
}

void state_leds_update(void) {
	switch (dcmode) {
	case mInitializing:
		gpio_pin_toggle(pin_led_yellow);
		break;
	case mNormalOp:
		gpio_pin_toggle(pin_led_green);
		break;
	case mOverride:
		gpio_pin_toggle(pin_led_green);
		if (debounced[DEB_BTN_OVERRIDE].state)
			gpio_pin_toggle(pin_led_yellow);
		else
			gpio_pin_write(pin_led_yellow, false);
		break;
	case mBigRelayTest:
		gpio_pin_toggle(pin_led_yellow);
		break;
	case mFailure:
		gpio_pin_toggle(pin_led_red);
		break;
	}
}

bool is_dcc_pc_alive() {
	return dccon_timer_ms < DCCON_TIMEOUT_MS;
}

void dcc_on_timeout(void) {
	set_relays(false, false);
}

void brtest_finished(void) {
	device_usb_tx_req.sep.brtsState = true;
	brtest_request = false;
	brtest_timer = 0;

	if (dcmode == mInitializing)
		set_mode(mNormalOp);
	else if (dcmode == mNormalOp)
		set_relays(is_dcc_pc_alive(), is_dcc_pc_alive());
	else if (dcmode == mOverride)
		set_relays(true, true); // test run only when changed to true
}

void brtest_failed(void) {
	device_usb_tx_req.sep.brtsState = true;
	set_mode(mFailure);
}

void brtest_changed(void) {
	device_usb_tx_req.sep.brtsState = true;
}

bool _brtest_is_time(void) {
	return brtest_timer >= BRTEST_NOTEST_MAX_TIME;
}
