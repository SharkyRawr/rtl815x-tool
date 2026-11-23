#include "rtl_power.h"
#include <stdio.h>
#include <unistd.h>

static const char *state(bool enabled) { return enabled ? "Enabled" : "Disabled"; }

int rtl_power_print_status(rtl_dev_t *dev) {
    if (rtl_open_device(dev) != 0) return -1;
    rtl_chip_family_t family = dev->version_info->family;
    printf("%s power controls (%s)\n", dev->version_info->name, dev->bsd_name);
    if (family == CHIP_RTL8152B) {
        uint16_t config;
        if (rtl_ocp_reg_read(dev, OCP_ALDPS_CONFIG, &config) != 0) return -1;
        printf("  PHY link-down power saving (0x2010): %s\n", state(config & ENPWRSAVE));
        return 0;
    }
    if (family == CHIP_RTL8153A) {
        uint32_t low;
        uint16_t high;
        if (rtl_ocp_read_dword(dev, MCU_TYPE_USB, USB_TOLERANCE, &low) != 0 ||
            rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_TOLERANCE + 4, &high) != 0) return -1;
        printf("  USB U1/U2 tolerance: 0x%04x%08x\n", high, low);
    } else {
        uint16_t lpm;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CONFIG, &lpm) != 0) return -1;
        printf("  USB U1/U2 (USB 0x%04x): %s\n", USB_LPM_CONFIG, state(lpm & LPM_U1U2_EN));
    }
    if (family == CHIP_RTL8157) {
        uint16_t power_cut;
        uint32_t u2p3;
        if (rtl_adv_read(dev, ADV_CMD_IP, IP_U2P3_CTRL, &u2p3) != 0 ||
            rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_POWER_CUT, &power_cut) != 0) return -1;
        printf("  U2 to P3 (IP 0x%04x): %s\n", IP_U2P3_CTRL, state(u2p3 & U2P3_ENABLE));
        printf("  Suspend UPS (USB 0x%04x): %s (driver managed)\n", USB_POWER_CUT, state(power_cut & UPS_EN));
    } else {
        uint16_t u2p3;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_U2P3_CTRL, &u2p3) != 0) return -1;
        printf("  U2 to P3 (USB 0x%04x): %s\n", USB_U2P3_CTRL, state(u2p3 & USB_U2P3_ENABLE));
    }
    return 0;
}

int rtl_power_set_no_sleep(rtl_dev_t *dev, bool disable_sleep) {
    if (rtl_open_device(dev) != 0) return -1;
    rtl_chip_family_t family = dev->version_info->family;
    if (family == CHIP_RTL8152B) {
        /* r8152_aldps_en keeps ENPDNPS, LINKENA and DIS_SDSAVE set. */
        uint16_t config = 0x0310 | (disable_sleep ? 0 : ENPWRSAVE);
        if (rtl_ocp_reg_write(dev, OCP_ALDPS_CONFIG, config) != 0) return -1;
        if (disable_sleep) usleep(20000);
        printf("[+] PHY link-down power saving %s.\n", state(!disable_sleep));
        return 0;
    }

    bool needs_speed = family != CHIP_RTL8153A && family != CHIP_RTL8153C;
    if (!disable_sleep && needs_speed && !dev->usb_bps) {
        fprintf(stderr, "Cannot select the default U1/U2 setting: USB speed unknown.\n");
        return -1;
    }
    bool u1u2 = !disable_sleep && (!needs_speed || dev->usb_bps >= 5000000000LL);
    /* Defaults follow the per-revision rtl815*_up routines in Realtek r8152. */
    bool u2p3 = !disable_sleep && family != CHIP_RTL8153B && family != CHIP_RTL8153C &&
                dev->version_raw != 0x5c00 && dev->version_raw != 0x5c10;
    if (family == CHIP_RTL8157) {
        uint32_t value;
        if (rtl_adv_read(dev, ADV_CMD_IP, IP_U2P3_CTRL, &value) != 0 ||
            rtl_adv_write(dev, ADV_CMD_IP, IP_U2P3_CTRL,
                          u2p3 ? value | U2P3_ENABLE : value & ~U2P3_ENABLE) != 0) return -1;
    } else {
        uint16_t value;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_U2P3_CTRL, &value) != 0 ||
            rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_U2P3_CTRL,
                               u2p3 ? value | USB_U2P3_ENABLE : value & ~USB_U2P3_ENABLE) != 0) return -1;
    }
    if (family == CHIP_RTL8153A) {
        if (rtl_ocp_write_dword(dev, MCU_TYPE_USB, USB_TOLERANCE, u1u2 ? UINT32_MAX : 0) != 0 ||
            rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_TOLERANCE + 4, u1u2 ? UINT16_MAX : 0) != 0) return -1;
    } else {
        uint16_t value;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CONFIG, &value) != 0 ||
            rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_LPM_CONFIG,
                               u1u2 ? value | LPM_U1U2_EN : value & ~LPM_U1U2_EN) != 0) return -1;
    }
    printf("[+] U1/U2 %s, U2 to P3 %s.\n", state(u1u2), state(u2p3));
    return 0;
}
