/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

static const struct usb_device_descriptor dev = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = USB_CLASS_CDC,
	.bDeviceSubClass = USB_CDC_SUBCLASS_ACM,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x0483,
	.idProduct = 0x5740,
	.bcdDevice = 0x0200,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

/*
 * This notification endpoint isn't implemented. According to CDC spec its
 * optional, but its absence causes a NULL pointer dereference in Linux
 * cdc_acm driver.
 */
static const struct usb_endpoint_descriptor comm_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x83,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,
	.bInterval = 255,
}};

static const struct usb_endpoint_descriptor data_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x01,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x82,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 1,
}};

static const struct {
	struct usb_cdc_header_descriptor header;
	struct usb_cdc_call_management_descriptor call_mgmt;
	struct usb_cdc_acm_descriptor acm;
	struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcacm_functional_descriptors = {
	.header = {
		.bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
		.bcdCDC = 0x0110,
	},
	.call_mgmt = {
		.bFunctionLength =
			sizeof(struct usb_cdc_call_management_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
		.bmCapabilities = 0,
		.bDataInterface = 1,
	},
	.acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_ACM,
		.bmCapabilities = 2,
	},
	.cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_UNION,
		.bControlInterface = 0,
		.bSubordinateInterface0 = 1,
	 },
};

static const struct usb_interface_descriptor comm_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
	.iInterface = 0,

	.endpoint = comm_endp,

	.extra = &cdcacm_functional_descriptors,
	.extralen = sizeof(cdcacm_functional_descriptors),
}};

static const struct usb_interface_descriptor data_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_DATA,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,

	.endpoint = data_endp,
}};

static const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = comm_iface,
}, {
	.num_altsetting = 1,
	.altsetting = data_iface,
}};

static const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 2,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0x80,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

static char serial_number[25];
static const char *usb_strings[] = {
	"MacroPill",
	"MacroPill Control - 8 Buttons",
	serial_number,
};


/* MacroPill modifications: PA0..PA7 active-low buttons, USB CDC, debounce. */
#include <libopencm3/cm3/systick.h>
#include "buttons.h"
static uint8_t control_buffer[256];
static uint8_t line_coding[7] = {0x00, 0xc2, 0x01, 0x00, 0, 0, 8};
static bool configured, port_open;
static uint8_t pending[32];
static unsigned head, tail;
static volatile uint32_t millis;
void sys_tick_handler(void) { ++millis; }
static void clear_queue(void) { head = tail = 0; }
static enum usbd_request_return_codes control(usbd_device *d,
    struct usb_setup_data *r, uint8_t **b, uint16_t *l,
    void (**complete)(usbd_device *, struct usb_setup_data *)) {
    (void)d; (void)complete;
    if (r->wIndex != 0) return USBD_REQ_NOTSUPP;
    switch (r->bRequest) {
    case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
        port_open = (r->wValue & 1) != 0;
        clear_queue(); return USBD_REQ_HANDLED;
    case USB_CDC_REQ_SET_LINE_CODING:
        if (*l != 7) return USBD_REQ_NOTSUPP;
        for (unsigned i=0;i<7;++i) line_coding[i]=(*b)[i];
        return USBD_REQ_HANDLED;
    case USB_CDC_REQ_GET_LINE_CODING:
        *b=line_coding; *l=7; return USBD_REQ_HANDLED;
    default: return USBD_REQ_NOTSUPP;
    }
}
static void receive(usbd_device *d, uint8_t ep) {
    uint8_t discard[64]; usbd_ep_read_packet(d,ep,discard,sizeof discard);
}
static void reset(void) { configured=false; port_open=false; clear_queue(); }
static void set_config(usbd_device *d, uint16_t value) {
    reset(); if (value != 1) return;
    usbd_ep_setup(d,0x01,USB_ENDPOINT_ATTR_BULK,64,receive);
    usbd_ep_setup(d,0x82,USB_ENDPOINT_ATTR_BULK,64,NULL);
    usbd_ep_setup(d,0x83,USB_ENDPOINT_ATTR_INTERRUPT,16,NULL);
    usbd_register_control_callback(d,USB_REQ_TYPE_CLASS|USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE|USB_REQ_TYPE_RECIPIENT,control);
    configured=true;
}
static void enqueue(unsigned button) {
    unsigned next=(head+1)%32;
    if (next != tail) { pending[head]=(uint8_t)button; head=next; }
}
int main(void) {
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOC);
    gpio_set(GPIOA,0xff);
    gpio_set_mode(GPIOA,GPIO_MODE_INPUT,GPIO_CNF_INPUT_PULL_UPDOWN,0xff);
    gpio_set(GPIOC,GPIO13);
    gpio_set_mode(GPIOC,GPIO_MODE_OUTPUT_2_MHZ,GPIO_CNF_OUTPUT_PUSHPULL,GPIO13);
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
    systick_set_reload(72000-1); systick_interrupt_enable(); systick_counter_enable();
    /* Force USB detach at reset so a previously connected host enumerates again. */
    gpio_clear(GPIOA,GPIO12);
    gpio_set_mode(GPIOA,GPIO_MODE_OUTPUT_2_MHZ,GPIO_CNF_OUTPUT_PUSHPULL,GPIO12);
    uint32_t start=millis; while ((uint32_t)(millis-start)<100) {}
    gpio_set_mode(GPIOA,GPIO_MODE_INPUT,GPIO_CNF_INPUT_FLOAT,GPIO11|GPIO12);
    const uint8_t *uid=(const uint8_t *)0x1ffff7e8;
    const char hex[]="0123456789ABCDEF";
    for(unsigned i=0;i<12;++i) { serial_number[2*i]=hex[uid[i]>>4]; serial_number[2*i+1]=hex[uid[i]&15]; }
    serial_number[24]=0;
    usbd_device *d=usbd_init(&st_usbfs_v1_usb_driver,&dev,&config,usb_strings,3,
        control_buffer,sizeof control_buffer);
    usbd_register_reset_callback(d,reset);
    usbd_register_set_config_callback(d,set_config);
    struct button_state buttons[8];
    uint32_t last_sample=millis;
    for(unsigned i=0;i<8;++i) button_init(&buttons[i],(gpio_get(GPIOA,1u<<i)==0),millis);
    while (1) {
        usbd_poll(d);
        uint32_t now=millis;
        if (now != last_sample) {
            last_sample=now;
            for(unsigned i=0;i<8;++i) {
                bool pressed=gpio_get(GPIOA,1u<<i)==0;
                if(button_update(&buttons[i],pressed,now) && configured && port_open) enqueue(i+1);
            }
            /* Onboard LED: slow blink idle, solid while serial port is open. */
            if(configured && port_open) gpio_clear(GPIOC,GPIO13);
            else if((now%1000)<100) gpio_clear(GPIOC,GPIO13);
            else gpio_set(GPIOC,GPIO13);
        }
        if(configured && port_open && tail != head) {
            char message[6]={'B','T','N',(char)('0'+pending[tail]),'\n',0};
            if(usbd_ep_write_packet(d,0x82,message,5)==5) tail=(tail+1)%32;
        }
    }
}
