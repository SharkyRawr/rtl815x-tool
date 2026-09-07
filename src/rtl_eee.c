#include "rtl_eee.h"
#include <stdio.h>
#include <unistd.h>

int rtl_eee_print_status(rtl_dev_t *dev) {
    if (!dev) return -1;

    if (rtl_open_device(dev) != 0) {
        fprintf(stderr, "Error: Failed to open USB device %04x:%04x\n", dev->vid, dev->pid);
        return -1;
    }

    uint16_t pla_eee_cr;
    if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_EEE_CR, &pla_eee_cr) != 0) return -1;
    uint16_t ocp_eee_cfg;
    if (rtl_ocp_reg_read(dev, OCP_EEE_CFG, &ocp_eee_cfg) != 0) return -1;
    uint16_t ocp_eee_adv;
    if (rtl_ocp_reg_read(dev, OCP_EEE_ADV, &ocp_eee_adv) != 0) return -1;
    uint16_t ocp_eee_adv2;
    if (rtl_ocp_reg_read(dev, OCP_EEE_ADV2, &ocp_eee_adv2) != 0) return -1;
    uint16_t ocp_eee_lp;
    if (rtl_ocp_reg_read(dev, OCP_EEE_LPABLE, &ocp_eee_lp) != 0) return -1;
    uint16_t ocp_eee_lp2;
    if (rtl_ocp_reg_read(dev, OCP_EEE_LPABLE2, &ocp_eee_lp2) != 0) return -1;

    bool tx_eee = (pla_eee_cr & EEE_TX_EN) != 0;
    bool rx_eee = (pla_eee_cr & EEE_RX_EN) != 0;

    printf("\n=================================================================\n");
    printf(" Energy Efficient Ethernet (EEE / IEEE 802.3az) Report\n");
    printf(" Adapter: %s (%s, %s)\n", dev->version_info->name, dev->bsd_name, dev->product_name);
    printf("=================================================================\n");
    printf(" Controller State:\n");
    printf("   Low Power Idle (LPI) TX : %s\n", tx_eee ? "\033[1;33mEnabled (Power saving active)\033[0m" : "\033[1;32mDisabled (Low latency)\033[0m");
    printf("   Low Power Idle (LPI) RX : %s\n", rx_eee ? "\033[1;33mEnabled (Power saving active)\033[0m" : "\033[1;32mDisabled (Low latency)\033[0m");
    printf("   PLA_EEE_CR (0xE040)     : 0x%04x\n", pla_eee_cr);
    printf("   OCP_EEE_CFG (0xA430)    : 0x%04x\n", ocp_eee_cfg);

    printf("\n EEE Advertisement Matrix:\n");
    printf("   %-22s | %-16s | %-16s\n", "Speed / Link Standard", "Local Advertised", "Link Partner Ability");
    printf("  ------------------------+------------------+-------------------\n");
    printf("   %-22s | %-16s | %-16s\n", "2.5GBASE-T EEE",
           (ocp_eee_adv2 & 0x0001) ? "Enabled" : "Disabled",
           (ocp_eee_lp2 & 0x0001) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "5GBASE-T EEE",
           (ocp_eee_adv2 & 0x0002) ? "Enabled" : "Disabled",
           (ocp_eee_lp2 & 0x0002) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "1000BASE-T EEE",
           (ocp_eee_adv & 0x0004) ? "Enabled" : "Disabled",
           (ocp_eee_lp & 0x0004) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "100BASE-TX EEE",
           (ocp_eee_adv & 0x0002) ? "Enabled" : "Disabled",
           (ocp_eee_lp & 0x0002) ? "Supported" : "No");

    printf("\n Impact on macOS Performance:\n");
    if (tx_eee || rx_eee || (ocp_eee_adv & 0x0006) || (ocp_eee_adv2 & 0x0003)) {
        printf("   \033[1;33m[!] EEE is active.\033[0m On macOS with AppleUSBNCMData, EEE often\n");
        printf("       triggers micro-stalls, packet jitter, or drops to 1Gbps.\n");
        printf("       Run `rtl815x-tool eee --disable` to turn off EEE and fix latency.\n");
    } else {
        printf("   \033[1;32m[✓] EEE is fully disabled.\033[0m Maximum throughput and minimum jitter.\n");
    }
    printf("=================================================================\n\n");
    return 0;
}

int rtl_eee_set(rtl_dev_t *dev, bool enable) {
    if (!dev) return -1;
    if (rtl_open_device(dev) != 0) return -1;

    printf("[*] %s Energy Efficient Ethernet (EEE) on %s (%s)...\n",
           enable ? "Enabling" : "Disabling",
           dev->version_info->name, dev->bsd_name);

    if (!enable) {
        /* Disable PLA EEE Control */
        uint16_t pla_cr;
        if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_EEE_CR, &pla_cr) != 0) return -1;
        pla_cr &= (uint16_t)~(EEE_TX_EN | EEE_RX_EN);
        if (rtl_ocp_write_word(dev, MCU_TYPE_PLA, PLA_EEE_CR, pla_cr) != 0) return -1;

        /* Clear 100M/1G EEE Advertisements */
        if (rtl_ocp_reg_write(dev, OCP_EEE_ADV, 0x0000) != 0) return -1;

        /* Clear 2.5G/5G EEE Advertisements */
        if (rtl_ocp_reg_write(dev, OCP_EEE_ADV2, 0x0000) != 0) return -1;

        /* Clear EEE config flags */
        uint16_t cfg;
        if (rtl_ocp_reg_read(dev, OCP_EEE_CFG, &cfg) != 0) return -1;
        cfg &= (uint16_t)~0x2000; /* Disable EEE_10_CAP */
        if (rtl_ocp_reg_write(dev, OCP_EEE_CFG, cfg) != 0) return -1;
    } else {
        /* Enable PLA EEE Control */
        uint16_t pla_cr;
        if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_EEE_CR, &pla_cr) != 0) return -1;
        pla_cr |= (EEE_TX_EN | EEE_RX_EN);
        if (rtl_ocp_write_word(dev, MCU_TYPE_PLA, PLA_EEE_CR, pla_cr) != 0) return -1;

        /* Enable 100M/1G EEE */
        if (rtl_ocp_reg_write(dev, OCP_EEE_ADV, 0x0006) != 0) return -1;

        /* Enable 2.5G EEE if 2.5G or 5G capable */
        if (dev->version_info->max_speed_mbps >= 2500) {
            if (rtl_ocp_reg_write(dev, OCP_EEE_ADV2, 0x0001) != 0) return -1;
        }
    }

    /* Restart PHY Auto-Negotiation so link partner receives updated abilities */
    printf("[*] Restarting PHY Auto-Negotiation to apply changes...\n");
    uint16_t bmcr;
    if (rtl_mdio_read(dev, MII_BMCR, &bmcr) != 0) return -1;
    bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
    if (rtl_mdio_write(dev, MII_BMCR, bmcr) != 0) return -1;

    for (int i = 0; i < 20; i++) {
        usleep(100000);
        uint16_t bmsr;
        if (rtl_mdio_read(dev, MII_BMSR, &bmsr) != 0) return -1;
        if ((bmsr & BMSR_LSTATUS) && (bmsr & BMSR_ANEGCOMPLETE)) {
            printf("[+] Link re-established! EEE is now %s.\n", enable ? "ENABLED" : "DISABLED");
            return 0;
        }
    }

    printf("[+] Changes written. Auto-negotiation in progress.\n");
    return 0;
}
