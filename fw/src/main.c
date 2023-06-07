/* Main implementation file.
 * This file connects MTBbus & USB CDC. It retransmits data between these
 * busses.
 *
 * Packet transmission:
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
#include "bus_measure.h"
#include "mtbbus.h"
#include "gpio.h"
#include "modules.h"
#include "ring_buffer.h"
#include "common.h"
#include "ee.h"
#include "leds.h"
#include "adafruit_ina219.h"
#include "dwt_delay.h"

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef h_uart_debug;
TIM_HandleTypeDef h_tim2;
TIM_HandleTypeDef h_tim3;
IWDG_HandleTypeDef h_iwdg;

#define RING_USB_TO_MTBBUS_SIZE 256
uint8_t _usb_to_mtbbus_data[RING_USB_TO_MTBBUS_SIZE];
volatile ring_buffer ring_usb_to_mtbbus;

typedef union {
	size_t all;
	struct {
		bool ack :1;
		bool info: 1;
		bool active_modules :1;
		bool full_buffer :1;
	} sep;
} DeviceUsbTxReq;

volatile uint8_t _error_full_buffer_command_code;
volatile uint8_t _error_full_buffer_module_addr;

volatile DeviceUsbTxReq device_usb_tx_req;
volatile uint32_t _speed_change_req = 0;
size_t _inq_period_counter = 0;
size_t _inq_period_max = 5;

struct {
	uint8_t mtbbus_speed;
} config;
volatile bool _config_save = false;

#define MTBBUS_SPEEDS 4
const uint32_t _speed_to_br[MTBBUS_SPEEDS] = {38400, 38400, 57600, 115200};
const size_t _speed_to_inq_period[MTBBUS_SPEEDS] = {3, 3, 2, 1}; // in milliseconds

// Send 3 resets in 50 ms interval
volatile size_t mtbbus_reset_counter = 0;
#define MTBBUS_RESET_FULL 151
#define MTBBUS_DO_RESET 50

#define MTBBUS_SEND_ATTEMPTS 3
volatile uint8_t mtbbus_resent_times = 0;
volatile bool mtbbus_send_lock = false;

// Forwarding packets is allowed at most in half of outgoing commands:
// At least 1 inquiry between each 2 commands from USB.
volatile bool mtbbus_last_inq = false;

/* Private function prototypes -----------------------------------------------*/

static void error_handler();
static void init(void);
static bool clock_init(void);
static bool debug_uart_init(void);
bool forward_mtbbus_received_to_usb();
static inline void mtbbus_poll_rx_flags(void);
static void ring_usb_to_mtbbus_poll(void);
static inline bool ring_usb_to_mtbbus_message_ready(void);
static inline void poll_usb_tx_flags(void);
static inline void poll_speed_change(void);
static inline void reboot_to_dfu();

static inline void config_load(void);
static inline bool config_save(void);
static inline void config_save_poll(void);

static void mtbbus_message_processed(void);

static bool iwdg_init(void);

/* Code ----------------------------------------------------------------------*/

int main(void) {
	init();

	while (true) {
		mtbbus_poll_rx_flags();
		ring_usb_to_mtbbus_poll();
		poll_usb_tx_flags();
		poll_speed_change();
		config_save_poll();

		DWT_Delay(200); // 200 us
	}
}

void init(void) {
	if (!clock_init())
		error_handler();
	DWT_Init();
	HAL_Init();
	gpio_init();
	leds_init();

	gpio_pin_write(pin_led_red, true);
	gpio_pin_write(pin_led_yellow, true);
	gpio_pin_write(pin_led_green, true);
	gpio_pin_write(pin_led_blue, true);

	ee_init();
	config_load();

	if (!iwdg_init())
		error_handler();

	_inq_period_max = _speed_to_inq_period[config.mtbbus_speed];
	if (!mtbbus_init(_speed_to_br[config.mtbbus_speed]))
		error_handler();
	/*if (!bus_measure_init())
		error_handler();*/

	cdc_init();

	debug_uart_init();
	modules_init();
	ring_init(&ring_usb_to_mtbbus, _usb_to_mtbbus_data, RING_USB_TO_MTBBUS_SIZE);

	__HAL_AFIO_REMAP_SWJ_NOJTAG();

	HAL_Delay(100);

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

	// Timer 2 @ 50 us (MTBbus timeout)
	TIM_ClockConfigTypeDef sClockSourceConfig = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};

	h_tim2.Instance = TIM2;
	h_tim2.Init.Prescaler = 32;
	h_tim2.Init.CounterMode = TIM_COUNTERMODE_UP;
	h_tim2.Init.Period = 73;
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
	h_tim3.Init.Period = 372;
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

static inline void reboot_to_dfu() {
	__disable_irq();
	cdc_deinit();

	volatile uint32_t* st = (uint32_t*)0x020004ffc; // __stack
	*st = 0x157F32D4; // DFU_BOOTKEY

	NVIC_SystemReset();
}

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
	// Timer 2 @ 50 us (20 kHz)
	mtbbus_update_50us();
	HAL_TIM_IRQHandler(&h_tim2);
}

void TIM3_IRQHandler(void) {
	// Timer 3 @ 1 ms (1 kHz)
	/*static size_t busmeasure_counter = 0;
	#define BUSMEASURE_TICKS 50*/

	if (mtbbus_reset_counter > 0) {
		if ((mtbbus_reset_counter % MTBBUS_DO_RESET) == 0) {
			if ((mtbbus_can_send()) && (!mtbbus_send_lock)) {
				mtbbus_send(0, MTBBUS_CMD_MOSI_RESET_OUTPUTS, NULL, 0);
				mtbbus_reset_counter--;
			}
		} else {
			mtbbus_reset_counter--;
		}
	}

	_inq_period_counter++;
	if (_inq_period_counter >= _inq_period_max) {
		_inq_period_counter = 0;
		if ((mtbbus_can_send()) && ((!ring_usb_to_mtbbus_message_ready()) || (!mtbbus_last_inq)) &&
		    (_speed_change_req == 0) && (!mtbbus_send_lock)) {
			mtbbus_modules_inquiry();
			mtbbus_last_inq = true;

			HAL_IWDG_Refresh(&h_iwdg);

			/*busmeasure_counter++;
			if (busmeasure_counter == BUSMEASURE_TICKS) {
				busmeasure_counter = 0;
				ina219_startMeasure();
			}*/
			led_activate(pin_led_green, 50, 50);
		}
	}

	leds_update_1ms();
	HAL_TIM_IRQHandler(&h_tim3);
}

/* USB -----------------------------------------------------------------------*/

void cdc_main_received(uint8_t command_code, uint8_t *data, size_t data_size) {
	if ((command_code == MTBUSB_CMD_PM_FORWARD) && (data_size >= 2)) {
		if (ring_free_space(&ring_usb_to_mtbbus) < data_size+1) {
			device_usb_tx_req.sep.full_buffer = true;
			_error_full_buffer_command_code = data[1];
			_error_full_buffer_module_addr = data[0];
			return;
		}

		ring_add_byte(&ring_usb_to_mtbbus, data_size+1);
		ring_add_bytes(&ring_usb_to_mtbbus, data, data_size);

	} else if (command_code == MTBUSB_CMD_PM_INFO_REQ) {
		device_usb_tx_req.sep.info = true;

	} else if ((command_code == MTBUSB_CMD_PM_CHANGE_SPEED) && (data_size >= 1)) {
		uint8_t speed = data[0];
		if ((speed >= MTBBUS_SPEEDS) || (speed == 0))
			speed = 1;

		_speed_change_req = _speed_to_br[speed];
		_inq_period_max = _speed_to_inq_period[speed];
		config.mtbbus_speed = speed;
		_config_save = true;

		device_usb_tx_req.sep.ack = true;

	} else if (command_code == MTBUSB_CMD_PM_ACTIVE_MODULES_REQ) {
		device_usb_tx_req.sep.active_modules = true;

	} else if (command_code == MTBUSB_CMD_PM_PING) {
		device_usb_tx_req.sep.ack = true;

	} else if (command_code == MTBUSB_CMD_PM_REBOOT_BOOTLOADER) {
		reboot_to_dfu();

	}
}

static inline void poll_usb_tx_flags(void) {
	if (!cdc_dtr_ready)
		device_usb_tx_req.all = 0;  // computer does not listen → ignore all flags
	if (!cdc_main_can_send())
		return; // USB busy → wait for next poll

	if (device_usb_tx_req.sep.ack) {
		cdc_send_ack();
		device_usb_tx_req.sep.ack = false;

	} else if (device_usb_tx_req.sep.info) {
		cdc_tx.separate.data[0] = MTBUSB_TYPE;
		cdc_tx.separate.data[1] = config.mtbbus_speed;
		cdc_tx.separate.data[2] = FW_VER_MAJOR;
		cdc_tx.separate.data[3] = FW_VER_MINOR;
		cdc_tx.separate.data[4] = MTBBUS_PROT_VER_MAJOR;
		cdc_tx.separate.data[5] = MTBBUS_PROT_VER_MINOR;

		if (cdc_main_send_nocopy(MTBUSB_CMD_MP_INFO, 6))
			device_usb_tx_req.sep.info = false;

	} else if (device_usb_tx_req.sep.active_modules) {
		for (size_t i = 0; i < 32; i++)
			cdc_tx.separate.data[i] = modules_active[i/4] >> (8*(i%4));

		if (cdc_main_send_nocopy(MTBUSB_CMD_MP_ACTIVE_MODULES_LIST, 32))
			device_usb_tx_req.sep.active_modules = false;

	} else if (device_usb_tx_req.sep.full_buffer) {
		cdc_send_error(MTBUSB_ERROR_FULL_BUFFER, _error_full_buffer_command_code, _error_full_buffer_module_addr);
		device_usb_tx_req.sep.full_buffer = false;
	}
}

void cdc_main_died() {
	// Send 'RESET OUTPUTS' to MTBbus when PC disconnects.
	mtbbus_reset_counter = MTBBUS_RESET_FULL;
}

/* MTBbus --------------------------------------------------------------------*/

bool forward_mtbbus_received_to_usb() {
	cdc_tx.separate.data[0] = mtbbus_resent_times;
	cdc_tx.separate.data[1] = mtbbus_addr;
	for (size_t i = 1; i < mtbbus_received_data[0]+1; i++)
		cdc_tx.separate.data[i+1] = mtbbus_received_data[i] & 0xFF;
	return cdc_main_send_nocopy(MTBUSB_CMD_MP_FORWARD, mtbbus_received_data[0]+2);
}

static inline void mtbbus_poll_rx_flags(void) {
	if (!cdc_dtr_ready)
		mtbbus_rx_flags.all = 0;  // computer does not listen → ignore all events from MTBbus
	if (!cdc_main_can_send())
		return; // USB busy → wait for next poll

	if (mtbbus_rx_flags.sep.received) {
		if (forward_mtbbus_received_to_usb()) {
			mtbbus_message_processed();
			mtbbus_rx_flags.sep.received = false;
		}

	} else if (mtbbus_rx_flags.sep.timeout_pc) {
		if (mtbbus_resent_times == MTBBUS_SEND_ATTEMPTS) {
			cdc_send_error(MTBUSB_ERROR_NO_RESPONSE, mtbbus_command_code, mtbbus_addr);
			mtbbus_message_processed();
		}
		mtbbus_rx_flags.sep.timeout_pc = false;

	} else if (mtbbus_rx_flags.sep.timeout_inquiry) {
		cdc_tx.separate.data[0] = mtbbus_addr;
		cdc_tx.separate.data[1] = module_get_attempts(mtbbus_addr);
		if (cdc_main_send_nocopy(MTBUSB_CMD_MP_MODULE_FAILED, 2))
			mtbbus_rx_flags.sep.timeout_inquiry = false;

	} else if (mtbbus_rx_flags.sep.discovered) {
		cdc_tx.separate.data[0] = mtbbus_addr;
		if (cdc_main_send_nocopy(MTBUSB_CMD_MP_NEW_MODULE, 1))
			mtbbus_rx_flags.sep.discovered = false;
	}
}

static void ring_usb_to_mtbbus_poll(void) {
	// Warning: this function could be interrupted with module inquiry periodic send
	mtbbus_send_lock = true;
	if ((mtbbus_last_inq) && (mtbbus_can_send()) && (ring_usb_to_mtbbus_message_ready()) &&
	    (mtbbus_resent_times < MTBBUS_SEND_ATTEMPTS)) {
		mtbbus_last_inq = false;
		if (mtbbus_send_from_ring(&ring_usb_to_mtbbus))
			mtbbus_resent_times++;
	}
	mtbbus_send_lock = false;
}

static inline bool ring_usb_to_mtbbus_message_ready(void) {
	return (!ring_empty(&ring_usb_to_mtbbus)) &&
	       (ring_length(&ring_usb_to_mtbbus) >= ring_get_byte_begin(&ring_usb_to_mtbbus, 0));
}

static inline void poll_speed_change(void) {
	if ((_speed_change_req > 0) && (mtbbus_can_send())) {
		mtbbus_change_speed(_speed_change_req);
		_speed_change_req = 0;
	}
}

void mtbbus_bad_checksum(void) {
	led_activate(pin_led_red, 100, 100);
}

static void mtbbus_message_processed(void) {
	mtbbus_resent_times = 0;
	if (ring_usb_to_mtbbus_message_ready())
		ring_move_begin(&ring_usb_to_mtbbus, ring_get_byte_begin(&ring_usb_to_mtbbus, 0));
}

/* Config --------------------------------------------------------------------*/

static inline void config_save_poll(void) {
	if (_config_save) {
		config_save();
		_config_save = false;
	}
}

static inline void config_load(void) {
	ee_read(0, 1, &config.mtbbus_speed);
	if ((config.mtbbus_speed >= MTBBUS_SPEEDS) || (config.mtbbus_speed == 0))
		config.mtbbus_speed = 1;
}

static inline bool config_save(void) {
	if (!ee_format(false))
		return false;
	return ee_write(0, 1, &config.mtbbus_speed);
}
