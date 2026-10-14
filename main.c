#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "fat32_sd_spi.h"

static volatile bool usb_configured = false;
static usbd_device *usbd_dev = NULL;

static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01, /* USAGE_PAGE (Generic Desktop)     */
    0x09, 0x06, /* USAGE (Keyboard)                */
    0xa1, 0x01, /* COLLECTION (Application)         */
    0x05, 0x07, /*   USAGE_PAGE (Keyboard/Keypad)   */
    0x19, 0xe0, /*   USAGE_MINIMUM (Left Control)  */
    0x29, 0xe7, /*   USAGE_MAXIMUM (Right GUI)     */
    0x15, 0x00, /*   LOGICAL_MINIMUM (0)           */
    0x25, 0x01, /*   LOGICAL_MAXIMUM (1)           */
    0x75, 0x01, /*   REPORT_SIZE (1)               */
    0x95, 0x08, /*   REPORT_COUNT (8)              */
    0x81, 0x02, /*   INPUT (Data,Var,Abs) ; Modifiers */
    0x95, 0x01, /*   REPORT_COUNT (1)              */
    0x75, 0x08, /*   REPORT_SIZE (8)               */
    0x81, 0x03, /*   INPUT (Cnst,Var,Abs) ; Reserved  */
    0x95, 0x05, /*   REPORT_COUNT (5)              */
    0x75, 0x01, /*   REPORT_SIZE (1)               */
    0x05, 0x08, /*   USAGE_PAGE (LEDs)             */
    0x19, 0x01, /*   USAGE_MINIMUM (Num Lock)      */
    0x29, 0x05, /*   USAGE_MAXIMUM (Kana)          */
    0x91, 0x02, /*   OUTPUT (Data,Var,Abs); LED report */
    0x95, 0x01, /*   REPORT_COUNT (1)              */
    0x75, 0x03, /*   REPORT_SIZE (3)               */
    0x91, 0x03, /*   OUTPUT (Cnst,Var,Abs); LED padding */
    0x95, 0x06, /*   REPORT_COUNT (6)              */
    0x75, 0x08, /*   REPORT_SIZE (8)               */
    0x15, 0x00, /*   LOGICAL_MINIMUM (0)           */
    0x25, 0x65, /*   LOGICAL_MAXIMUM (101)         */
    0x05, 0x07, /*   USAGE_PAGE (Keyboard/Keypad)   */
    0x19, 0x00, /*   USAGE_MINIMUM (Reserved)      */
    0x29, 0x65, /*   USAGE_MAXIMUM (Keyboard App)  */
    0x81, 0x00, /*   INPUT (Data,Ary,Abs) ; Keycodes  */
    0xc0        /* END_COLLECTION                  */
};

static const struct {
    struct usb_hid_descriptor hid_descriptor;
    struct {
        uint8_t bReportDescriptorType;
        uint16_t wDescriptorLength;
    } __attribute__((packed)) hid_report;
} __attribute__((packed)) hid_function = {
    .hid_descriptor = {
        .bLength = 9,
        .bDescriptorType = USB_DT_HID,
        .bcdHID = 0x0111,
        .bCountryCode = 0,
        .bNumDescriptors = 1,
    },
    .hid_report = {
        .bReportDescriptorType = USB_DT_REPORT,
        .wDescriptorLength = sizeof(hid_report_descriptor)
    }
};

static const struct usb_endpoint_descriptor hid_endpoint = {
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,
    .bEndpointAddress = 0x81,
    .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize = 8,
    .bInterval = 0x0A
};

static const struct usb_interface_descriptor hid_iface = {
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 1,
    .bInterfaceClass = USB_CLASS_HID,
    .bInterfaceSubClass = 1,
    .bInterfaceProtocol = 1,
    .iInterface = 0,
    .endpoint = &hid_endpoint,
    .extra = &hid_function,
    .extralen = sizeof(hid_function)
};

static const struct usb_interface interfaces[] = {{
    .num_altsetting = 1,
    .altsetting = &hid_iface,
}};

static const struct usb_config_descriptor config = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 37,
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0xA0,
    .bMaxPower = 0x32,
    .interface = interfaces
};

static const struct usb_device_descriptor dev_descr = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x0483,
    .idProduct = 0x5711,
    .bcdDevice = 0x0200,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1
};

static const char *usb_strings[] = {
    "STM67",
    "67",
    "67",
};

static uint8_t usbd_control_buffer[128];

static enum usbd_request_return_codes hid_control_request(usbd_device *dev, struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
                        void (**complete)(usbd_device *, struct usb_setup_data *))
{
    (void)complete;
    (void)dev;
    if ((req->bmRequestType == 0x21) && ((req->bRequest == 0x0A) || (req->bRequest == 0x0B))) {
        return USBD_REQ_HANDLED;
    }

    if ((req->bmRequestType != 0x81) ||
        (req->bRequest != USB_REQ_GET_DESCRIPTOR) ||
        ((req->wValue >> 8) != 0x22))
        return USBD_REQ_NOTSUPP;

    *buf = (uint8_t*)hid_report_descriptor;
    *len = sizeof(hid_report_descriptor);

    return USBD_REQ_HANDLED;
}

static void hid_set_config(usbd_device *dev, uint16_t wValue) {
    (void)wValue;

    usbd_ep_setup(dev, 0x81, USB_ENDPOINT_ATTR_INTERRUPT, 8, NULL);

    usbd_register_control_callback(
        dev,
        USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
        hid_control_request
    );

    usbd_register_control_callback(
        dev,
        USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
        hid_control_request
    );

    usb_configured = true;
}


int dummy_signal_count = 100;

bool send_hid_report(uint8_t report[8]){
        if (!usb_configured) {
                return false;
        }
        if (usbd_ep_write_packet(usbd_dev, 0x81, report, 8) == 8) {
                return true;
        }
        return false;
}

char* text_buffer = "r                            chrome https://obsidianblock.github.io/cool_project/\n"; //forces ctrl+r at the beginning and runs a rickroll on windows

int main(void) {
    uint32_t spi = SPI1;
    uint32_t cs_port = GPIOA;
    uint32_t cs_pin = GPIO4;

    int hwinit = sd_hw_init(SPI1,GPIOA,GPIO4);
    uint8_t data_buffer[512];
    memset(data_buffer, 0, 512);

    fat32_data f32;
    fat32_init(spi,cs_port,cs_pin,&f32);
    
    directory_entry *entries = f32_get_directory_entries(spi,cs_port,cs_pin,&f32);

    int index = -1;
    while(true){
        index++;
        directory_entry centry = entries[index];
        if(centry.name[0] == 0xE5)continue;//ignore free entries
        if(centry.name[0] == 0x00)break;//last entry
        if(centry.name[0] == 0x05)centry.name[0] = 0xE5;//weird mapping
        char *ext = centry.name+8;//extension
        bool is_dir = centry.attr & 0x10;//check if its a directory and not a file
        char name[9];
        memcpy(name,centry.name,8);
        name[8] = 0x00;
        if(strcmp(name,"HI      ") == 0){
                uint32_t sector_addr = f32.root_sector + (*((uint16_t*)centry.fscluslo)-2) * f32.sectors_per_cluster;
                sd_card_read_block(spi, cs_port, cs_pin, sector_addr,data_buffer);
                text_buffer = (char*)malloc(strlen(data_buffer)+15);
                memcpy(text_buffer+15,data_buffer,strlen(data_buffer));
                text_buffer[0] = 'r';
                memset(text_buffer+1,' ',14);
                break;
        }
    }

    //sd card setup over not on the USB

    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_USB);
    rcc_periph_clock_enable(RCC_GPIOC);

    gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
    gpio_clear(GPIOA, GPIO12);
    for (volatile int i = 0; i < 800000; i++) __asm__("nop");
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO12);

    usbd_dev = usbd_init(&st_usbfs_v1_usb_driver, &dev_descr, &config, usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
    usbd_register_set_config_callback(usbd_dev, hid_set_config);

    int8_t dx = 5;
    int32_t step_count = 0;
    uint32_t delay_counter = 0;

    bool toggle_key = true;
    int cursor = 0;
    while(1){
        usbd_poll(usbd_dev);
        if (usb_configured) {
            break;
        }
        else{
                __asm__("nop");
        }
    }
    gpio_clear(GPIOC,GPIO13);
    for (volatile uint32_t i = 0; i < 7200000; i++);

    gpio_set(GPIOC,GPIO13);
    
    int dummy_counter = 0;

    int key_delay = 0;
    while (1) {
        usbd_poll(usbd_dev);

        if(!usb_configured){
                continue;
        }
        
        if(++key_delay <= 7500){
                continue;
        }

        key_delay = 0;

        if(text_buffer[cursor]=='\0')break;
        

        uint8_t report_down[8] = {0};
        if(dummy_counter <  dummy_signal_count){
                uint8_t report_up[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                if(dummy_counter % 2 == 0)report_up[2] = 0x53;
                usbd_ep_write_packet(usbd_dev, 0x81, report_up, 8);
                dummy_counter++;
                continue;
        }




        if (toggle_key) {
                if(text_buffer[cursor] >= 'a' && text_buffer[cursor] <= 'z'){
                        report_down[2] = 0x04 + text_buffer[cursor]-'a';
                }
                else if(text_buffer[cursor] >= 'A' && text_buffer[cursor] <= 'Z'){
                        report_down[2] = 0x04 + text_buffer[cursor]-'A';
                        report_down[0] = 0x02;
                }
                else if(text_buffer[cursor] >= '1' && text_buffer[cursor] <= '9'){
                        report_down[2] = 0x1E + text_buffer[cursor]-'1';
                }
                else{
                        switch(text_buffer[cursor]){
                                case '\n': case '\r': report_down[2] = 0x28; break;
                                case '/': report_down[2] = 0x38; break;
                                case '_': report_down[2] = 0x2D; report_down[0] = 0x02; break;
                                case '?': report_down[2] = 0x38; report_down[0] = 0x02; break;
                                case ':': report_down[2] = 0x33; report_down[0] = 0x02; break;
                                case '&': report_down[2] = 0x24; report_down[0] = 0x02; break;
                                case '=': report_down[2] = 0x2E; break;
                                case '0': report_down[2] = 0x27; break;
                                case '.': report_down[2] = 0x37; break;
                                case ' ': report_down[2] = 0x2C; break;
                                default: report_down[2] = 0x00; break;

                        }
                }
                if(cursor == 0)report_down[0] = 0x08;
                if(usbd_ep_write_packet(usbd_dev, 0x81, report_down, 8) == 8){
                        if(strlen(text_buffer)-1 > cursor && text_buffer[cursor] == text_buffer[cursor+1]){
                                toggle_key = false;
                        }
                        else{
                                cursor++;
                        }
                }
            } else{
                uint8_t report_up[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                if(usbd_ep_write_packet(usbd_dev, 0x81, report_up, 8) == 8){
                        toggle_key = true;
                        cursor++;
                }
            }
        }
    }
