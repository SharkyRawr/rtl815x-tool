#include "rtl_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rtl_led_print_status(rtl_dev_t *dev) {
    if (!dev) return -1;

    if (rtl_open_device(dev) != 0) {
        fprintf(stderr, "Error: Failed to open USB device %04x:%04x\n", dev->vid, dev->pid);
        return -1;
    }

    uint16_t ledsel;
    if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_LEDSEL, &ledsel) != 0) return -1;
    uint16_t ledfeat;
    if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_LED_FEATURE, &ledfeat) != 0) return -1;

    printf("\n=================================================================\n");
    printf(" Realtek LED Configuration\n");
    printf(" Adapter: %s (%s, %s)\n", dev->version_info->name, dev->bsd_name, dev->product_name);
    printf("=================================================================\n");
    printf(" Registers:\n");
    printf("   PLA_LEDSEL      (0xDD90): 0x%04x\n", ledsel);
    printf("   PLA_LED_FEATURE (0xDD92): 0x%04x\n", ledfeat);
    printf(" Status: %s\n", (ledsel == 0x0000) ? "Stealth Mode (All LEDs Off)" : "Active / Standard");
    printf(" Available Modes:\n");
    printf("   default       : Standard activity & link indicator (0x002b)\n");
    printf("   link-only     : Solid LED on link, no activity blinking (0x0003)\n");
    printf("   activity-only : Blink on TX/RX packets only (0x0020)\n");
    printf("   off           : Turn off all adapter LEDs (0x0000)\n");
    printf("   0x<hex>       : Direct custom 16-bit register value\n");
    printf("=================================================================\n\n");
    return 0;
}

int rtl_led_set_mode(rtl_dev_t *dev, const char *mode) {
    if (!dev || !mode) return -1;
    if (rtl_open_device(dev) != 0) return -1;

    uint16_t val = 0x002b;
    if (strcasecmp(mode, "default") == 0) {
        val = 0x002b;
    } else if (strcasecmp(mode, "off") == 0 || strcasecmp(mode, "stealth") == 0) {
        val = 0x0000;
    } else if (strcasecmp(mode, "link-only") == 0) {
        val = 0x0003;
    } else if (strcasecmp(mode, "activity-only") == 0) {
        val = 0x0020;
    } else if (strncmp(mode, "0x", 2) == 0 || strncmp(mode, "0X", 2) == 0) {
        val = (uint16_t)strtoul(mode, NULL, 16);
    } else {
        fprintf(stderr, "Unknown LED mode '%s'. Use default, off, link-only, activity-only, or 0x<hex>\n", mode);
        return -1;
    }

    printf("[*] Setting LED configuration to 0x%04x on %s (%s)...\n", val, dev->version_info->name, dev->bsd_name);
    if (rtl_ocp_write_word(dev, MCU_TYPE_PLA, PLA_LEDSEL, val) != 0) return -1;
    printf("[+] LED mode updated.\n");
    return 0;
}
