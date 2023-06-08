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
	} sep;
} DeviceUsbTxReq;

volatile DeviceUsbTxReq device_usb_tx_req;

volatile DCmode dcmode;
volatile bool dccConnected;
uint8_t failureCode;

volatile bool req_debounce_update;

/* Private function prototypes -----------------------------------------------*/

static void error_handler();
static void init(void);
static bool clock_init(void);
static bool debug_uart_init(void);
static inline void poll_usb_tx_flags(void);

static bool iwdg_init(void);

/* Code ----------------------------------------------------------------------*/

int main(void) {
	init();

	while (true) {
		if (req_debounce_update) {
			debounce_update();
			req_debounce_update = false;
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

	req_debounce_update = false;
	device_usb_tx_req.all = 0;

	//dcmode = mInitializing;
	dcmode = mNormalOp;
	setDccConnected(false);
	failureCode = DCFAIL_NOFAILURE;

	gpio_pin_write(pin_led_red, true);
	gpio_pin_write(pin_led_yellow, true);
	gpio_pin_write(pin_led_green, true);
	gpio_pin_write(pin_led_blue, true);

	if (!iwdg_init())
		error_handler();

	cdc_init();
	debug_uart_init();
	__HAL_AFIO_REMAP_SWJ_NOJTAG();

	HAL_Delay(200);

	gpio_pin_write(pin_led_red, false);
	if (cdc_dtr_ready)
		gpio_pin_write(pin_led_yellow, false);
	gpio_pin_write(pin_led_green, false);
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
	if (dccConnected) {
		gpio_pin_toggle(pin_relay1);
		gpio_pin_toggle(pin_relay2);
	}

	req_debounce_update = true;

	HAL_TIM_IRQHandler(&h_tim2);
}

void TIM3_IRQHandler(void) {
	// Timer 3 @ 1 ms (1 kHz)

	#define STATE_SEND_COUNT 500 // ms
	static volatile size_t state_send_counter = 0;
	state_send_counter++;
	if (state_send_counter >= STATE_SEND_COUNT) {
		device_usb_tx_req.sep.state = true;
		state_send_counter = 0;
	}

	leds_update_1ms();
	if (h_iwdg.Instance != NULL)
		HAL_IWDG_Refresh(&h_iwdg);
	HAL_TIM_IRQHandler(&h_tim3);
}

/* USB -----------------------------------------------------------------------*/

void cdc_main_received(uint8_t command_code, uint8_t *data, size_t data_size) {
	//} if (command_code == MTBUSB_CMD_PM_INFO_REQ) {
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
		cdc_tx.separate.data[0] = ((dcmode & 0x07) << 4) | (dccConnected & 1) | ((dccOnInput() & 1) << 1);
		cdc_tx.separate.data[1] = failureCode;

		if (cdc_main_send_nocopy(DC_CMD_MP_STATE, 2))
			device_usb_tx_req.sep.state = false;
	}
}

void cdc_main_died() {
}

/* IO ------------------------------------------------------------------------*/

void debounce_on_fall(PinDef pin) {
	if (pindef_eq(pin, pin_btn_go)) {
		if ((dcmode == mOverride) && (!debounced[DEB_BTN_OVERRIDE].state))
			setDccConnected(true);
	} else if (pindef_eq(pin, pin_btn_stop)) {
		setMode(mOverride);
		setDccConnected(false);
	} else if (pindef_eq(pin, pin_btn_override)) {
		setMode(mOverride);
	}
}

void debounce_on_raise(PinDef pin) {
	if (pindef_eq(pin, pin_btn_override)) {
		// TODO: probably go to test
		setDccConnected(false);
		setMode(mNormalOp);
	}
}

void setMode(DCmode mode) {
	if (dcmode == mode)
		return;
	dcmode = mode;
	device_usb_tx_req.sep.state = true;
}

bool dccOnInput() {
	return (!debounced[DEB_DCC1].state) || (!debounced[DEB_DCC2].state);
}

void setDccConnected(bool state) {
	if (dccConnected != state)
		device_usb_tx_req.sep.state = true;
	dccConnected = state;
	gpio_pin_write(pin_led_go, state);
	gpio_pin_write(pin_led_stop, !state);
}
