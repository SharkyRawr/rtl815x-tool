#include "rtl_power.h"
#include <stdio.h>

int rtl_power_print_status(rtl_dev_t *dev) {
    if (!dev) return -1;

    if (rtl_open_device(dev) != 0) {
        fprintf(stderr, "Error: Failed to open USB device %04x:%04x\n", dev->vid, dev->pid);
        return -1;
    }

    if (dev->version_info->family == CHIP_RTL8157) {
        uint16_t lpm, power_cut;
        uint32_t u2p3;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CONFIG, &lpm) != 0 ||
            rtl_adv_read(dev, ADV_CMD_IP, IP_U2P3_CTRL, &u2p3) != 0 ||
            rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_POWER_CUT, &power_cut) != 0) return -1;
        printf("RTL8157 power controls (%s)\n", dev->bsd_name);
        printf("  USB U1/U2 (USB 0x%04x): %s\n", USB_LPM_CONFIG, lpm & LPM_U1U2_EN ? "Enabled" : "Disabled");
        printf("  U2 to P3 (IP 0x%04x): %s\n", IP_U2P3_CTRL, u2p3 & U2P3_ENABLE ? "Enabled" : "Disabled");
        printf("  Suspend UPS (USB 0x%04x): %s (driver managed)\n", USB_POWER_CUT, power_cut & UPS_EN ? "Enabled" : "Disabled");
        return 0;
    }

    uint16_t dev_stat;
    if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_DEV_STAT, &dev_stat) != 0) return -1;
    uint16_t ups_ctrl;
    if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_UPS_CTRL, &ups_ctrl) != 0) return -1;
    uint16_t lpm_ctrl;
    if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CTRL, &lpm_ctrl) != 0) return -1;
    uint16_t mac_pwr;
    if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL, &mac_pwr) != 0) return -1;

    printf("\n=================================================================\n");
    printf(" USB & Ethernet Power Management State\n");
    printf(" Adapter: %s (%s, %s)\n", dev->version_info->name, dev->bsd_name, dev->product_name);
    printf("=================================================================\n");
    printf(" USB Power & Link Controls:\n");
    printf("   USB Device Status (0xD400) : 0x%04x\n", dev_stat);
    printf("   Ultra Power Saving (0xD404): 0x%04x (UPS %s)\n",
           ups_ctrl, (ups_ctrl & 0x0001) ? "Enabled" : "Disabled");
    printf("   USB LPM Control    (0xC40C): 0x%04x (LPM %s)\n",
           lpm_ctrl, (lpm_ctrl & 0x0001) ? "Enabled" : "Disabled");
    printf("   PLA MAC Power      (0xE0C0): 0x%04x\n", mac_pwr);

    printf("=================================================================\n\n");
    return 0;
}

int rtl_power_set_no_sleep(rtl_dev_t *dev, bool disable_sleep) {
    if (!dev) return -1;
    if (rtl_open_device(dev) != 0) return -1;

    if (dev->version_info->family == CHIP_RTL8157) {
        /* UPS belongs to the driver's suspend/resume sequence. */
        uint32_t u2p3;
        uint16_t lpm;
        if (!disable_sleep && !dev->usb_bps) {
            fprintf(stderr, "Cannot select the RTL8157 default U1/U2 setting: USB speed unknown.\n");
            return -1;
        }
        if (rtl_adv_read(dev, ADV_CMD_IP, IP_U2P3_CTRL, &u2p3) != 0 ||
            rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CONFIG, &lpm) != 0) return -1;
        bool u1u2 = !disable_sleep && dev->usb_bps >= 5000000000LL;
        if (rtl_adv_write(dev, ADV_CMD_IP, IP_U2P3_CTRL,
                         disable_sleep ? u2p3 & ~U2P3_ENABLE : u2p3 | U2P3_ENABLE) != 0 ||
            rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_LPM_CONFIG,
                               u1u2 ? lpm | LPM_U1U2_EN : lpm & ~LPM_U1U2_EN) != 0) return -1;
        printf("[+] RTL8157 U1/U2 %s, U2 to P3 %s.\n",
               u1u2 ? "enabled" : "disabled", disable_sleep ? "disabled" : "enabled");
        return 0;
    }

    printf("[*] %s UPS and LPM on %s (%s)...\n", disable_sleep ? "Disabling" : "Enabling",
           dev->version_info->name, dev->bsd_name);

    if (disable_sleep) {
        /* Disable UPS (Ultra Power Saving) */
        uint16_t ups;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_UPS_CTRL, &ups) != 0) return -1;
        ups &= (uint16_t)~0x0001;
        if (rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_UPS_CTRL, ups) != 0) return -1;

        /* Disable USB LPM L1 transitions */
        uint16_t lpm;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CTRL, &lpm) != 0) return -1;
        lpm &= (uint16_t)~0x0001;
        if (rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_LPM_CTRL, lpm) != 0) return -1;

        printf("[+] UPS and LPM control bits cleared.\n");
    } else {
        uint16_t ups;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_UPS_CTRL, &ups) != 0) return -1;
        ups |= 0x0001;
        if (rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_UPS_CTRL, ups) != 0) return -1;

        uint16_t lpm;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CTRL, &lpm) != 0) return -1;
        lpm |= 0x0001;
        if (rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_LPM_CTRL, lpm) != 0) return -1;

        printf("[+] UPS and LPM control bits set.\n");
    }

    return 0;
}
