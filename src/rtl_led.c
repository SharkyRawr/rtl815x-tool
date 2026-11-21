#include "rtl_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *name;
    uint16_t selector;
} presets[] = {
    {"default", 0x002b}, {"link-only", 0x0003}, {"activity-only", 0x0020}, {"off", 0x0000}
};

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
    const char *preset = "dont know";
    for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
        if (ledsel == presets[i].selector) preset = presets[i].name;
    }
    printf(" Matching preset: %s\n", preset);
    printf(" Available presets:\n");
    for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
        printf("   %-13s : 0x%04x\n", presets[i].name, presets[i].selector);
    }
    printf("   0x<hex>       : Direct custom 16-bit register value\n");
    printf("=================================================================\n\n");
    return 0;
}

int rtl_led_set_mode(rtl_dev_t *dev, const char *mode) {
    if (!dev || !mode) return -1;
    if (rtl_open_device(dev) != 0) return -1;

    uint16_t val;
    if (strncasecmp(mode, "0x", 2) == 0) {
        char *end;
        unsigned long selector = strtoul(mode, &end, 16);
        if (*end || selector > UINT16_MAX) {
            fprintf(stderr, "Invalid LED selector '%s'\n", mode);
            return -1;
        }
        val = (uint16_t)selector;
    } else {
        if (strcasecmp(mode, "stealth") == 0) mode = "off";
        size_t i = 0;
        while (i < sizeof(presets) / sizeof(presets[0]) && strcasecmp(mode, presets[i].name) != 0) i++;
        if (i == sizeof(presets) / sizeof(presets[0])) {
            fprintf(stderr, "Unknown LED mode '%s'. Use default, off, link-only, activity-only, or 0x<hex>\n", mode);
            return -1;
        }
        val = presets[i].selector;
    }

    printf("[*] Setting LED configuration to 0x%04x on %s (%s)...\n", val, dev->version_info->name, dev->bsd_name);
    if (rtl_ocp_write_word(dev, MCU_TYPE_PLA, PLA_LEDSEL, val) != 0) return -1;
    printf("[+] LED mode updated.\n");
    return 0;
}
