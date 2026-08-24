#include "rtl_power.h"
#include <stdio.h>

void rtl_power_print_status(rtl_dev_t *dev) {
    if (!dev) return;

    if (rtl_open_device(dev) != 0) {
        fprintf(stderr, "Error: Failed to open USB device %04x:%04x\n", dev->vid, dev->pid);
        return;
    }

    uint16_t dev_stat = rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_DEV_STAT);
    uint16_t ups_ctrl = rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_UPS_CTRL);
    uint16_t lpm_ctrl = rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CTRL);
    uint16_t mac_pwr = rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL);

    printf("\n=================================================================\n");
    printf(" USB & Ethernet Power Management State\n");
    printf(" Adapter: %s (%s, %s)\n", dev->version_info->name, dev->bsd_name, dev->product_name);
    printf("=================================================================\n");
    printf(" USB Power & Link Controls:\n");
    printf("   USB Device Status (0xD400) : 0x%04x\n", dev_stat);
    printf("   Ultra Power Saving (0xD404): 0x%04x (UPS %s)\n",
           ups_ctrl, (ups_ctrl & 0x0001) ? "Enabled" : "Disabled");
    printf("   USB LPM Control    (0xC40C): 0x%04x (LPM %s)\n",
           lpm_ctrl, (lpm_ctrl & 0x0001) ? "Active (L1 allowed)" : "Disabled (Full Power)");
    printf("   PLA MAC Power      (0xE0C0): 0x%04x\n", mac_pwr);

    printf("\n Tuning Recommendation:\n");
    if ((ups_ctrl & 0x0001) || (lpm_ctrl & 0x0001)) {
        printf("   \033[1;33m[!] Sleep / Low Power states active.\033[0m\n");
        printf("       If the adapter disconnects or experiences first-packet latency,\n");
        printf("       run `rtl815x-tool power --no-sleep` to keep the USB interface awake.\n");
    } else {
        printf("   \033[1;32m[✓] Adapter configured for continuous active power.\033[0m\n");
    }
    printf("=================================================================\n\n");
}

int rtl_power_set_no_sleep(rtl_dev_t *dev, bool disable_sleep) {
    if (!dev) return -1;
    if (rtl_open_device(dev) != 0) return -1;

    printf("[*] Setting continuous high-performance power mode on %s (%s)...\n",
           dev->version_info->name, dev->bsd_name);

    if (disable_sleep) {
        /* Disable UPS (Ultra Power Saving) */
        uint16_t ups = rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_UPS_CTRL);
        ups &= (uint16_t)~0x0001;
        rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_UPS_CTRL, ups);

        /* Disable USB LPM L1 transitions */
        uint16_t lpm = rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CTRL);
        lpm &= (uint16_t)~0x0001;
        rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_LPM_CTRL, lpm);

        printf("[+] High-performance power mode configured. Aggressive USB sleep disabled.\n");
    } else {
        /* Re-enable defaults */
        uint16_t ups = rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_UPS_CTRL);
        ups |= 0x0001;
        rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_UPS_CTRL, ups);

        uint16_t lpm = rtl_ocp_read_word(dev, MCU_TYPE_USB, USB_LPM_CTRL);
        lpm |= 0x0001;
        rtl_ocp_write_word(dev, MCU_TYPE_USB, USB_LPM_CTRL, lpm);

        printf("[+] Default power saving mode re-enabled.\n");
    }

    return 0;
}
