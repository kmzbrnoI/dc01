/* USB CDC implementation. */

#pragma once

#include "stm32.h"
#include "usb.h"

#define CDC_EP0_SIZE 0x08
#define CDC_MAIN_RXD_EP 0x01
#define CDC_MAIN_TXD_EP 0x82
#define CDC_MAIN_NTF_EP 0x83
#define CDC_DEBUG_RXD_EP 0x02
#define CDC_DEBUG_TXD_EP 0x84
#define CDC_DEBUG_NTF_EP 0x85

#define CDC_DATA_SZ 0x40
#define CDC_NTF_SZ 0x08
#define CDC_DC_BUF_SIZE 0x80

typedef union {
	uint8_t all[CDC_DC_BUF_SIZE];
	struct {
		uint8_t magic1 :8;
		uint8_t magic2 :8;
		uint8_t size :8;
		uint8_t command_code :8;
		uint8_t data[CDC_DC_BUF_SIZE-4]; // user fills only this item
	} separate;
} CdcTxData;

extern CdcTxData cdc_tx;
extern volatile bool cdc_dtr_ready; // if computer reads data

// Events:
void cdc_main_received(uint8_t command_code, uint8_t *data, size_t data_size);

void cdc_init();
void cdc_deinit();
bool cdc_is_debug_ep_enabled();
bool cdc_main_can_send(void);
bool cdc_main_send_copy(uint8_t command_code, uint8_t *data, size_t datasize);
bool cdc_main_send_nocopy(uint8_t command_code, size_t datasize);
void cdc_main_died(void);

bool cdc_send_ack(void);
bool cdc_send_error(uint8_t error_code, uint8_t command_code, uint8_t module);

int cdc_debug_send(uint8_t *data, size_t datasize);

#define DC_CMD_PM_CONTROL 0x01
#define DC_CMD_PM_ASK_STATE 0x01
#define DC_CMD_PM_PING 0x02

#define DC_CMD_MP_ACK 0x01
#define DC_CMD_MP_ERROR 0x02
#define DC_CMD_MP_STATE 0x10

#define DC_ERROR_NO_RESPONSE 0x01
#define DC_ERROR_FULL_BUFFER 0x02
