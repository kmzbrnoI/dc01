// Implements USB CDC using libusb_stm

#include <stdint.h>
#include <string.h>
#include "usb_cdc_link.h"
#include "usb_cdc.h"
#include "gpio.h"
#include "leds.h"

static void main_cdc_rx(usbd_device *dev, uint8_t event, uint8_t ep);
static void main_cdc_tx(usbd_device *dev, uint8_t event, uint8_t ep);

struct {
	uint32_t pos;
	uint8_t fifo[CDC_DC_BUF_SIZE];
} rx;

struct {
	uint32_t pos;
	bool sending;
	size_t size;
} tx;

CdcTxData cdc_tx;

bool _cdc_main_send(uint8_t command_code, uint8_t *data, size_t datasize, bool copy);


#define USB_LP_IRQ_HANDLER USB_LP_CAN1_RX0_IRQHandler
const IRQn_Type usbLpIRQn = USB_LP_CAN1_RX0_IRQn;
const unsigned usbLpIRQnPrio = 8;
volatile bool cdc_dtr_ready = false;

enum {
	STRDESC_LANG,
	STRDESC_MANUFACTURER,
	STRDESC_PRODUCT,
	STRDESC_CDC_IFACE,
	STRDESC_DEBUG_IFACE,
	STRDESC_SERIAL,

	STRDESC_MAX,
};

enum {
	INTERFACE_MAIN_COMM,
	INTERFACE_MAIN_DATA,
	INTERFACE_DEBUG_COMM,
	INTERFACE_DEBUG_DATA,

	INTERFACE_COUNT_ALL,
	INTERFACE_COUNT_NODEBUG = INTERFACE_DEBUG_COMM,
};

const struct usb_string_descriptor lang_desc = USB_ARRAY_DESC(USB_LANGID_ENG_US);
const struct usb_string_descriptor manuf_desc_en = USB_STRING_DESC("KMZ Brno I");
const struct usb_string_descriptor prod_desc_en = USB_STRING_DESC("DC-01");
const struct usb_string_descriptor cdc_iface_desc_en = USB_STRING_DESC("DC-01 Serial Interface");
const struct usb_string_descriptor debug_iface_desc_en = USB_STRING_DESC("DC-01 Debug UART");
struct usb_string_descriptor serial_number_desc_en = USB_STRING_DESC("ffffffffffffffffffffffff"); // will be filled later

static const struct usb_string_descriptor* const dtable[STRDESC_MAX] = {
	&lang_desc,
	&manuf_desc_en,
	&prod_desc_en,
	&cdc_iface_desc_en,
	&debug_iface_desc_en,
	&serial_number_desc_en,
};

#define DEBUG_DESCRIPTORS\
	struct usb_iad_descriptor debug_comm_iad; \
	struct usb_interface_descriptor debug_comm; \
	struct usb_cdc_header_desc debug_cdc_hdr; \
	struct usb_cdc_call_mgmt_desc debug_cdc_mgmt; \
	struct usb_cdc_acm_desc debug_cdc_acm; \
	struct usb_cdc_union_desc debug_cdc_union; \
	struct usb_endpoint_descriptor debug_comm_ep; \
	struct usb_interface_descriptor debug_data; \
	struct usb_endpoint_descriptor debug_data_eprx; \
	struct usb_endpoint_descriptor debug_data_eptx;

struct {
	DEBUG_DESCRIPTORS
} __attribute__((packed)) __debug_descriptors;

static const size_t DebugDescriptorsSize = sizeof(__debug_descriptors);

struct cdc_config {
	struct usb_config_descriptor config;

	struct usb_iad_descriptor main_comm_iad;
	struct usb_interface_descriptor main_comm;
	struct usb_cdc_header_desc main_cdc_hdr;
	struct usb_cdc_call_mgmt_desc main_cdc_mgmt;
	struct usb_cdc_acm_desc main_cdc_acm;
	struct usb_cdc_union_desc main_cdc_union;
	struct usb_endpoint_descriptor main_comm_ep;
	struct usb_interface_descriptor main_data;
	struct usb_endpoint_descriptor main_data_eprx;
	struct usb_endpoint_descriptor main_data_eptx;

	DEBUG_DESCRIPTORS
} __attribute__((packed));

static const struct usb_device_descriptor device_desc = {
	.bLength = sizeof(struct usb_device_descriptor),
	.bDescriptorType = USB_DTYPE_DEVICE,
	.bcdUSB = VERSION_BCD(2, 0, 0),
	.bDeviceClass = USB_CLASS_IAD,
	.bDeviceSubClass = USB_SUBCLASS_IAD,
	.bDeviceProtocol = USB_PROTO_IAD,
	.bMaxPacketSize0 = CDC_EP0_SIZE,
	.idVendor = 0x0483,
	.idProduct = 0x5740,
	.bcdDevice = VERSION_BCD(1, 0, 0),
	.iManufacturer = STRDESC_MANUFACTURER,
	.iProduct = STRDESC_PRODUCT,
	.iSerialNumber = STRDESC_SERIAL,
	.bNumConfigurations = 1,
};

static struct cdc_config config_desc = {
	.config = {
		.bLength = sizeof(struct usb_config_descriptor),
		.bDescriptorType = USB_DTYPE_CONFIGURATION,
		.wTotalLength = sizeof(struct cdc_config),
		.bNumInterfaces = INTERFACE_COUNT_ALL,
		.bConfigurationValue = 1,
		.iConfiguration = NO_DESCRIPTOR,
		.bmAttributes = USB_CFG_ATTR_RESERVED | USB_CFG_ATTR_SELFPOWERED,
		.bMaxPower = USB_CFG_POWER_MA(100),
	},

	.main_comm_iad = {
		.bLength = sizeof(struct usb_iad_descriptor),
		.bDescriptorType = USB_DTYPE_INTERFASEASSOC,
		.bFirstInterface = INTERFACE_MAIN_COMM,
		.bInterfaceCount = 2,
		.bFunctionClass = USB_CLASS_CDC,
		.bFunctionSubClass = USB_CDC_SUBCLASS_ACM,
		.bFunctionProtocol = USB_PROTO_NONE,
		.iFunction = NO_DESCRIPTOR,
	},
	.main_comm = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = USB_DTYPE_INTERFACE,
		.bInterfaceNumber = INTERFACE_MAIN_COMM,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_CLASS_CDC,
		.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
		.bInterfaceProtocol = USB_PROTO_NONE,
		.iInterface = NO_DESCRIPTOR,
	},
	.main_cdc_hdr = {
		.bFunctionLength = sizeof(struct usb_cdc_header_desc),
		.bDescriptorType = USB_DTYPE_CS_INTERFACE,
		.bDescriptorSubType = USB_DTYPE_CDC_HEADER,
		.bcdCDC = VERSION_BCD(1, 1, 0),
	},
	.main_cdc_mgmt = {
		.bFunctionLength = sizeof(struct usb_cdc_call_mgmt_desc),
		.bDescriptorType = USB_DTYPE_CS_INTERFACE,
		.bDescriptorSubType = USB_DTYPE_CDC_CALL_MANAGEMENT,
		.bmCapabilities = 0,
		.bDataInterface = INTERFACE_MAIN_DATA,
	},
	.main_cdc_acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_desc),
		.bDescriptorType = USB_DTYPE_CS_INTERFACE,
		.bDescriptorSubType = USB_DTYPE_CDC_ACM,
		.bmCapabilities = 0,
	},
	.main_cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_desc),
		.bDescriptorType = USB_DTYPE_CS_INTERFACE,
		.bDescriptorSubType = USB_DTYPE_CDC_UNION,
		.bMasterInterface0 = INTERFACE_MAIN_COMM,
		.bSlaveInterface0 = INTERFACE_MAIN_DATA,
	},
	.main_comm_ep = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = USB_DTYPE_ENDPOINT,
		.bEndpointAddress = CDC_MAIN_NTF_EP,
		.bmAttributes = USB_EPTYPE_INTERRUPT,
		.wMaxPacketSize = CDC_NTF_SZ,
		.bInterval = 0xFF,
	},
	.main_data = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = USB_DTYPE_INTERFACE,
		.bInterfaceNumber = INTERFACE_MAIN_DATA,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CLASS_CDC_DATA,
		.bInterfaceSubClass = USB_SUBCLASS_NONE,
		.bInterfaceProtocol = USB_PROTO_NONE,
		.iInterface = STRDESC_CDC_IFACE,
	},
	.main_data_eprx = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = USB_DTYPE_ENDPOINT,
		.bEndpointAddress = CDC_MAIN_RXD_EP,
		.bmAttributes = USB_EPTYPE_BULK,
		.wMaxPacketSize = CDC_DATA_SZ,
		.bInterval = 0x01,
	},
	.main_data_eptx = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = USB_DTYPE_ENDPOINT,
		.bEndpointAddress = CDC_MAIN_TXD_EP,
		.bmAttributes = USB_EPTYPE_BULK,
		.wMaxPacketSize = CDC_DATA_SZ,
		.bInterval = 0x01,
	},


	.debug_comm_iad = {
		.bLength = sizeof(struct usb_iad_descriptor),
		.bDescriptorType = USB_DTYPE_INTERFASEASSOC,
		.bFirstInterface = INTERFACE_DEBUG_COMM,
		.bInterfaceCount = 2,
		.bFunctionClass = USB_CLASS_CDC,
		.bFunctionSubClass = USB_CDC_SUBCLASS_ACM,
		.bFunctionProtocol = USB_PROTO_NONE,
		.iFunction = NO_DESCRIPTOR,
	},
	.debug_comm = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = USB_DTYPE_INTERFACE,
		.bInterfaceNumber = INTERFACE_DEBUG_COMM,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_CLASS_CDC,
		.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
		.bInterfaceProtocol = USB_PROTO_NONE,
		.iInterface = NO_DESCRIPTOR,
	},
	.debug_cdc_hdr = {
		.bFunctionLength = sizeof(struct usb_cdc_header_desc),
		.bDescriptorType = USB_DTYPE_CS_INTERFACE,
		.bDescriptorSubType = USB_DTYPE_CDC_HEADER,
		.bcdCDC = VERSION_BCD(1, 1, 0),
	},
	.debug_cdc_mgmt = {
		.bFunctionLength = sizeof(struct usb_cdc_call_mgmt_desc),
		.bDescriptorType = USB_DTYPE_CS_INTERFACE,
		.bDescriptorSubType = USB_DTYPE_CDC_CALL_MANAGEMENT,
		.bmCapabilities = 0,
		.bDataInterface = INTERFACE_DEBUG_DATA,
	},
	.debug_cdc_acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_desc),
		.bDescriptorType = USB_DTYPE_CS_INTERFACE,
		.bDescriptorSubType = USB_DTYPE_CDC_ACM,
		.bmCapabilities = 0,
	},
	.debug_cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_desc),
		.bDescriptorType = USB_DTYPE_CS_INTERFACE,
		.bDescriptorSubType = USB_DTYPE_CDC_UNION,
		.bMasterInterface0 = INTERFACE_DEBUG_COMM,
		.bSlaveInterface0 = INTERFACE_DEBUG_DATA,
	},
	.debug_comm_ep = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = USB_DTYPE_ENDPOINT,
		.bEndpointAddress = CDC_DEBUG_NTF_EP,
		.bmAttributes = USB_EPTYPE_INTERRUPT,
		.wMaxPacketSize = CDC_NTF_SZ,
		.bInterval = 0xFF,
	},
	.debug_data = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = USB_DTYPE_INTERFACE,
		.bInterfaceNumber = INTERFACE_DEBUG_DATA,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CLASS_CDC_DATA,
		.bInterfaceSubClass = USB_SUBCLASS_NONE,
		.bInterfaceProtocol = USB_PROTO_NONE,
		.iInterface = STRDESC_DEBUG_IFACE,
	},
	.debug_data_eprx = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = USB_DTYPE_ENDPOINT,
		.bEndpointAddress = CDC_DEBUG_RXD_EP,
		.bmAttributes = USB_EPTYPE_BULK,
		.wMaxPacketSize = CDC_DATA_SZ,
		.bInterval = 0x01,
	},
	.debug_data_eptx = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = USB_DTYPE_ENDPOINT,
		.bEndpointAddress = CDC_DEBUG_TXD_EP,
		.bmAttributes = USB_EPTYPE_BULK,
		.wMaxPacketSize = CDC_DATA_SZ,
		.bInterval = 0x01,
	},
};

usbd_device udev;
static uint32_t ubuf[0x20];
static bool enableDebugEp = false;

static struct usb_cdc_line_coding cdc_line_main = {
	.dwDTERate = 115200,
	.bCharFormat = USB_CDC_1_STOP_BITS,
	.bParityType = USB_CDC_NO_PARITY,
	.bDataBits = 8,
};

static struct usb_cdc_line_coding cdc_line_debug = {
	.dwDTERate = 115200,
	.bCharFormat = USB_CDC_1_STOP_BITS,
	.bParityType = USB_CDC_NO_PARITY,
	.bDataBits = 8,
};

static usbd_respond cdc_getdesc(
	usbd_ctlreq* req, void** address, uint16_t* length) {
	const uint8_t dtype = req->wValue >> 8;
	const uint8_t dnumber = req->wValue & 0xFF;
	const void* desc;
	uint16_t len = 0;
	switch (dtype) {
	case USB_DTYPE_DEVICE:
		desc = &device_desc;
		break;
	case USB_DTYPE_CONFIGURATION:
		desc = &config_desc;
		len = config_desc.config.wTotalLength;
		break;
	case USB_DTYPE_STRING:
		if (dnumber < STRDESC_MAX) {
			desc = dtable[dnumber];
		} else {
			return usbd_fail;
		}
		break;
	default:
		return usbd_fail;
	}
	if (len == 0) {
		len = ((struct usb_header_descriptor*)desc)->bLength;
	}
	*address = (void*)desc;
	*length = len;
	return usbd_ack;
};

static usbd_respond cdc_control_main(usbd_device* dev, usbd_ctlreq* req) {
	switch (req->bRequest) {
	case USB_CDC_SET_CONTROL_LINE_STATE: {
		const bool dtr = req->wValue & 0x01;
		// const bool rts = req->wValue & 0x02;
		if (cdc_dtr_ready && !dtr)
			cdc_main_died();
		cdc_dtr_ready = dtr;
		gpio_pin_write(pin_led_blue, !cdc_dtr_ready);
		return usbd_ack;
	}
	case USB_CDC_SET_LINE_CODING: {
		memcpy(&cdc_line_main, req->data, sizeof(cdc_line_main));
		return usbd_ack;
	}
	case USB_CDC_GET_LINE_CODING:
		dev->status.data_ptr = &cdc_line_main;
		dev->status.data_count = sizeof(cdc_line_main);
		return usbd_ack;
	default:
		return usbd_fail;
	}
}

static usbd_respond cdc_control_debug(usbd_device* dev, usbd_ctlreq* req) {
	switch (req->bRequest) {
	case USB_CDC_SET_CONTROL_LINE_STATE: {
		return usbd_ack;
	}
	case USB_CDC_SET_LINE_CODING: {
		if (req->wLength < sizeof(cdc_line_debug))
			return usbd_fail;
		memcpy(&cdc_line_debug, req->data, sizeof(cdc_line_debug));
		//DEBUG("USB_CDC_SET_LINE_CODING %d %d %d %d\n", cdc_line.dwDTERate,
		//	cdc_line.bCharFormat, cdc_line.bDataBits, cdc_line.bParityType);
		return usbd_ack;
	}
	case USB_CDC_GET_LINE_CODING:
		dev->status.data_ptr = &cdc_line_debug;
		dev->status.data_count = sizeof(cdc_line_debug);
		return usbd_ack;
	default:
		return usbd_fail;
	}
}

static usbd_respond cdc_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
	if (((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType)
		== (USB_REQ_INTERFACE | USB_REQ_CLASS)) {
		switch (req->wIndex) {
		case INTERFACE_MAIN_COMM:
			return cdc_control_main(dev, req);
		case INTERFACE_DEBUG_COMM:
			return cdc_control_debug(dev, req);
		}
	}
	return usbd_fail;
}

static usbd_respond cdc_setconf(usbd_device* dev, uint8_t cfg) {
	switch (cfg) {
	case 0:
		/* deconfiguring device */
		usbd_ep_deconfig(dev, CDC_MAIN_NTF_EP);
		usbd_ep_deconfig(dev, CDC_MAIN_TXD_EP);
		usbd_ep_deconfig(dev, CDC_MAIN_RXD_EP);
		if (enableDebugEp) {
			usbd_ep_deconfig(dev, CDC_DEBUG_NTF_EP);
			usbd_ep_deconfig(dev, CDC_DEBUG_TXD_EP);
			usbd_ep_deconfig(dev, CDC_DEBUG_RXD_EP);
		}
		return usbd_ack;
	case 1:
		/* configuring device */
		usbd_ep_config(dev, CDC_MAIN_RXD_EP, USB_EPTYPE_BULK /*| USB_EPTYPE_DBLBUF*/, CDC_DATA_SZ);
		usbd_ep_config(dev, CDC_MAIN_TXD_EP, USB_EPTYPE_BULK /*| USB_EPTYPE_DBLBUF*/, CDC_DATA_SZ);
		usbd_ep_config(dev, CDC_MAIN_NTF_EP, USB_EPTYPE_INTERRUPT, CDC_NTF_SZ);

		if (enableDebugEp) {
			usbd_ep_config(dev, CDC_DEBUG_RXD_EP, USB_EPTYPE_BULK /*| USB_EPTYPE_DBLBUF*/, CDC_DATA_SZ);
			usbd_ep_config(dev, CDC_DEBUG_TXD_EP, USB_EPTYPE_BULK /*| USB_EPTYPE_DBLBUF*/, CDC_DATA_SZ);
			usbd_ep_config(dev, CDC_DEBUG_NTF_EP, USB_EPTYPE_INTERRUPT, CDC_NTF_SZ);
		}

		usbd_reg_endpoint(dev, CDC_MAIN_RXD_EP, main_cdc_rx);
		usbd_reg_endpoint(dev, CDC_MAIN_TXD_EP, main_cdc_tx);

		return usbd_ack;
	default:
		return usbd_fail;
	}
}

void cdc_init() {
	__HAL_RCC_USB_CLK_ENABLE();

	rx.pos = 0;
	tx.sending = false;

	uint32_t uid[3];
	uid[0] = HAL_GetUIDw0();
	uid[1] = HAL_GetUIDw1();
	uid[2] = HAL_GetUIDw2();

	size_t sn_off = 0;
	for (size_t i = 0; i < 3; i++) {
		for (int j = 7; j >= 0; --j) {
			char c;
			size_t num = ((uid[i] >> (4*j)) & 0xF);
			if (num < 0xA)
				c = '0'+num;
			else
				c = 'A'+num-10;
			serial_number_desc_en.wString[sn_off++] = c;
		}
	}

	if (enableDebugEp) {
		config_desc.config.bNumInterfaces = INTERFACE_COUNT_ALL;
		config_desc.config.wTotalLength = sizeof(config_desc);
	} else {
		config_desc.config.bNumInterfaces = INTERFACE_COUNT_NODEBUG;
		config_desc.config.wTotalLength = sizeof(config_desc) - DebugDescriptorsSize;
	}

	usbd_init(&udev, &usbd_hw, CDC_EP0_SIZE, ubuf, sizeof(ubuf));
	usbd_reg_config(&udev, cdc_setconf);
	usbd_reg_control(&udev, cdc_control);
	usbd_reg_descr(&udev, cdc_getdesc);

	HAL_NVIC_SetPriority(usbLpIRQn, usbLpIRQnPrio, 0);
	HAL_NVIC_EnableIRQ(usbLpIRQn);

	usbd_enable(&udev, true);
	usbd_connect(&udev, true);
}

void cdc_deinit() {
	usbd_connect(&udev, false); // disconnect
}

bool cdc_is_debug_ep_enabled() {
	return enableDebugEp;
}

void USB_LP_IRQ_HANDLER(void) {
	usbd_poll(&udev);
}

/* Main CDC ------------------------------------------------------------------*/

static void main_cdc_rx(usbd_device *dev, uint8_t event, uint8_t ep) {
	if (event != usbd_evt_eprx)
		return;

	if (cdc_dtr_ready)
		led_activate(pin_led_blue, 50, 50);

	const size_t RX_MAX_DELAY_MS = 20;
	static size_t last_time = 0;
	if ((rx.pos > 0) && (last_time+RX_MAX_DELAY_MS < HAL_GetTick()))
		rx.pos = 0;
	last_time = HAL_GetTick();

	rx.pos += usbd_ep_read(dev, ep, &rx.fifo[rx.pos], CDC_DATA_SZ);

	if (rx.pos >= 3) {
		size_t msg_begin_pos = 0;
		size_t msg_length;
		do {
			if ((rx.fifo[msg_begin_pos] != 0x37) || (rx.fifo[msg_begin_pos+1] != 0xE2)) {
				rx.pos = 0;
				return;
			}
			msg_length = rx.fifo[msg_begin_pos+2];
			if (msg_length > 123) { // invalid data
				rx.pos = 0;
				return;
			}
			if (rx.pos-msg_begin_pos >= msg_length+3) {
				cdc_main_received(rx.fifo[msg_begin_pos+3], &rx.fifo[msg_begin_pos+4], msg_length-1);
				msg_begin_pos += msg_length+3;
			}
		} while (rx.pos-msg_begin_pos >= msg_length+3);

		// move last unfinished message to begin of buffer
		for (size_t i = 0; i < rx.pos-msg_begin_pos; i++)
			rx.fifo[i] = rx.fifo[i+msg_begin_pos];
		rx.pos = rx.pos-msg_begin_pos;
	}
}


static void main_cdc_tx(usbd_device *dev, uint8_t event, uint8_t ep) {
	if (event != usbd_evt_eptx)
		return;

	if (tx.pos >= tx.size) {
		tx.sending = false;
		return;
	}

	tx.pos += usbd_ep_write(dev, CDC_MAIN_TXD_EP, &cdc_tx.all[tx.pos],
	                        (tx.size-tx.pos < CDC_DATA_SZ) ? tx.size-tx.pos : CDC_DATA_SZ);
}

bool cdc_main_can_send(void) {
	return (!tx.sending) && (cdc_dtr_ready);
}

bool _cdc_main_send(uint8_t command_code, uint8_t *data, size_t datasize, bool copy) {
	if ((!cdc_main_can_send()) || (datasize > CDC_DC_BUF_SIZE-4))
		return false;

	led_activate(pin_led_blue, 50, 50);

	cdc_tx.separate.magic1 = 0x37;
	cdc_tx.separate.magic2 = 0xE2;
	cdc_tx.separate.size = datasize+1;
	cdc_tx.separate.command_code = command_code;

	if (copy)
		memcpy(cdc_tx.separate.data, data, datasize);
	tx.size = datasize+4;
	tx.sending = true;

	tx.pos = usbd_ep_write(&udev, CDC_MAIN_TXD_EP, cdc_tx.all,
	                       (tx.size < CDC_DATA_SZ) ? tx.size : CDC_DATA_SZ);

	if (tx.pos == 0) {
		tx.sending = false;
		return false;
	}

	return true;
}

bool cdc_main_send_copy(uint8_t command_code, uint8_t *data, size_t datasize) {
	return _cdc_main_send(command_code, data, datasize, true);
}

bool cdc_main_send_nocopy(uint8_t command_code, size_t datasize) {
	return _cdc_main_send(command_code, NULL, datasize, false);
}

/* Debug CDC -----------------------------------------------------------------*/

int cdc_debug_send(uint8_t *data, size_t datasize) {
	if (!enableDebugEp)
		return 1;

	usbd_ep_write(&udev, CDC_DEBUG_TXD_EP, data,
	              (datasize < CDC_DATA_SZ) ? datasize : CDC_DATA_SZ);
	return 0;
}

int _write(int fd, char* data, int len) {
	// Warning: can send max 64 bytes of data
	return cdc_debug_send((uint8_t*)data, len);
}
