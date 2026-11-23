#include "rtl_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Realtek PATCH64 LED_SEL_CFG uses one packed word or four little-endian words. */
static unsigned selector_count(const rtl_dev_t *dev) {
    return dev->version_info->max_speed_mbps >= 2500 ? 4 : 1;
}

static uint16_t selector_address(unsigned count, unsigned pin) {
    return count == 1 ? PLA_LEDSEL : PLA_LEDSEL_MULTI + 2 * pin;
}

static const char *const modes[] = {"default", "link-only", "activity-only", "off"};
enum { DEFAULT, LINK, ACTIVITY, OFF };

static bool preset_value(const rtl_dev_t *dev, unsigned pin, unsigned mode,
                         uint16_t *out, uint16_t *mask) {
    rtl_chip_family_t family = dev->version_info->family;
    *mask = UINT16_MAX;
    if (selector_count(dev) == 4) {
        /* PATCH64 defaults, in LED0..LED3 order. */
        static const uint16_t rtl8156[] = {0x0220, 0x0208, 0x0202, 0x0201};
        static const uint16_t rtl8157[] = {0x0220, 0x0208, 0x0203, 0x0280};
        if (mode == DEFAULT) {
            *out = family == CHIP_RTL8157 ? rtl8157[pin] : rtl8156[pin];
            return true;
        }
        if (family != CHIP_RTL8156B) return false;
        *mask = 0x022b; /* Link and activity, preserving each pin's polarity. */
        *out = mode == LINK ? 0x002b : mode == ACTIVITY ? 0x0200 : 0;
    } else if (family == CHIP_RTL8153C) {
        if (mode != DEFAULT) return false;
        *out = 0x0ca9; /* PATCH64 RTL8153C LED_SEL_CFG bytes a9 0c. */
    } else {
        uint16_t link = family == CHIP_RTL8152B ? 0x3 : 0x7;
        if (mode != DEFAULT) *mask = family == CHIP_RTL8152B ? 0x00bb : 0x0fff;
        *out = mode == DEFAULT ? link | 0x80 : mode == LINK ? link : mode == ACTIVITY ? 0x80 : 0;
    }
    return true;
}

int rtl_led_print_status(rtl_dev_t *dev) {
    if (rtl_open_device(dev) != 0) return -1;
    unsigned count = selector_count(dev);
    uint16_t selectors[4];
    printf("LED configuration on %s (%s)\n", dev->version_info->name, dev->bsd_name);
    for (unsigned i = 0; i < count; i++) {
        uint16_t addr = selector_address(count, i);
        if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, addr, &selectors[i]) != 0) return -1;
        printf("  Selector %u (PLA 0x%04x): 0x%04x\n", i, addr, selectors[i]);
    }
    if (count == 1) {
        uint16_t feature;
        if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_LED_FEATURE, &feature) != 0) return -1;
        printf("  PLA_LED_FEATURE (0xDD92): 0x%04x\n", feature);
    }
    for (unsigned mode = 0; mode < sizeof(modes) / sizeof(*modes); mode++) {
        unsigned i = 0;
        for (; i < count; i++) {
            uint16_t expected, mask;
            if (!preset_value(dev, i, mode, &expected, &mask) || (selectors[i] & mask) != expected) break;
        }
        if (i == count) {
            printf(" Matching preset: %s\n", modes[mode]);
            return 0;
        }
    }
    puts(" Matching preset: dont know");
    return 0;
}

int rtl_led_set_mode(rtl_dev_t *dev, const char *mode) {
    if (!mode || rtl_open_device(dev) != 0) return -1;
    unsigned count = selector_count(dev);
    uint16_t selectors[4];
    if (strncasecmp(mode, "0x", 2) == 0) {
        char *end;
        unsigned long value = strtoul(mode, &end, 16);
        if (*end || value > UINT16_MAX) {
            fprintf(stderr, "Invalid LED selector '%s'\n", mode);
            return -1;
        }
        for (unsigned i = 0; i < count; i++) selectors[i] = (uint16_t)value;
    } else {
        if (strcasecmp(mode, "stealth") == 0) mode = "off";
        unsigned m = 0;
        while (m < sizeof(modes) / sizeof(*modes) && strcasecmp(mode, modes[m])) m++;
        if (m == sizeof(modes) / sizeof(*modes)) {
            fprintf(stderr, "Unknown LED mode '%s'.\n", mode);
            return -1;
        }
        for (unsigned i = 0; i < count; i++) {
            uint16_t mask;
            if (!preset_value(dev, i, m, &selectors[i], &mask)) {
                fprintf(stderr, "LED preset '%s' has no verified encoding on %s. Use default or 0x<word>.\n",
                        mode, dev->version_info->name);
                return -1;
            }
            if (mask != UINT16_MAX) {
                uint16_t current;
                if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, selector_address(count, i), &current) != 0) return -1;
                selectors[i] |= current & ~mask;
            }
        }
    }
    for (unsigned i = 0; i < count; i++) {
        if (rtl_ocp_write_word(dev, MCU_TYPE_PLA, selector_address(count, i), selectors[i]) != 0)
            return -1;
    }
    puts("[+] LED selectors updated.");
    return 0;
}
