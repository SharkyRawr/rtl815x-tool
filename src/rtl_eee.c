#include "rtl_eee.h"
#include <stdio.h>
#include <unistd.h>

static bool has_eee10_config(const rtl_dev_t *dev) {
    rtl_chip_family_t family = dev->version_info->family;
    return family == CHIP_RTL8153A || family == CHIP_RTL8153B || family == CHIP_RTL8153C ||
           family == CHIP_RTL8156A || family == CHIP_RTL8156B;
}

/* RTL8152 exposes Clause 45 registers through the Clause 22 MMD window. */
static int eee_mmd(rtl_dev_t *dev, uint16_t mmd, uint16_t reg, uint16_t *value, bool write) {
    if (rtl_ocp_reg_write(dev, OCP_EEE_AR, mmd) != 0 ||
        rtl_ocp_reg_write(dev, OCP_EEE_DATA, reg) != 0 ||
        rtl_ocp_reg_write(dev, OCP_EEE_AR, 0x4000 | mmd) != 0) return -1;
    int rc = write ? rtl_ocp_reg_write(dev, OCP_EEE_DATA, *value)
                   : rtl_ocp_reg_read(dev, OCP_EEE_DATA, value);
    if (rc != 0) return -1;
    return rtl_ocp_reg_write(dev, OCP_EEE_AR, 0);
}

int rtl_eee_print_status(rtl_dev_t *dev) {
    if (!dev) return -1;

    if (rtl_open_device(dev) != 0) {
        fprintf(stderr, "Error: Failed to open USB device %04x:%04x\n", dev->vid, dev->pid);
        return -1;
    }

    uint16_t pla_eee_cr;
    if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_EEE_CR, &pla_eee_cr) != 0) return -1;
    bool has_eee_cfg = has_eee10_config(dev);
    uint16_t ocp_eee_cfg = 0;
    if (has_eee_cfg && rtl_ocp_reg_read(dev, OCP_EEE_CFG, &ocp_eee_cfg) != 0) return -1;
    uint32_t max_speed = dev->version_info->max_speed_mbps;
    uint16_t ocp_eee_adv, ocp_eee_lp, ocp_eee_adv2 = 0, ocp_eee_lp2 = 0;
    if (dev->version_info->family == CHIP_RTL8152B) {
        if (eee_mmd(dev, 7, 60, &ocp_eee_adv, false) != 0 ||
            eee_mmd(dev, 7, 61, &ocp_eee_lp, false) != 0) return -1;
    } else if (rtl_ocp_reg_read(dev, OCP_EEE_ADV, &ocp_eee_adv) != 0 ||
               rtl_ocp_reg_read(dev, OCP_EEE_LPABLE, &ocp_eee_lp) != 0) return -1;
    if (max_speed >= 2500 &&
        (rtl_ocp_reg_read(dev, OCP_EEE_ADV2, &ocp_eee_adv2) != 0 ||
         rtl_ocp_reg_read(dev, OCP_EEE_LPABLE2, &ocp_eee_lp2) != 0)) return -1;

    uint16_t bmsr;
    /* Link status latches low until read. */
    if (rtl_mdio_read(dev, MII_BMSR, &bmsr) != 0 ||
        rtl_mdio_read(dev, MII_BMSR, &bmsr) != 0) return -1;
    bool peer_known = (bmsr & BMSR_LSTATUS) && (bmsr & BMSR_ANEGCOMPLETE);

    bool tx_eee = (pla_eee_cr & EEE_TX_EN) != 0;
    bool rx_eee = (pla_eee_cr & EEE_RX_EN) != 0;

    printf("\n=================================================================\n");
    printf(" Energy Efficient Ethernet (EEE / IEEE 802.3az) Report\n");
    printf(" Adapter: %s (%s, %s)\n", dev->version_info->name, dev->bsd_name, dev->product_name);
    printf("=================================================================\n");
    printf(" Controller Configuration:\n");
    printf("   Low Power Idle (LPI) TX : %s\n", tx_eee ? "Enabled" : "Disabled");
    printf("   Low Power Idle (LPI) RX : %s\n", rx_eee ? "Enabled" : "Disabled");
    printf("   PLA_EEE_CR (0xE040)     : 0x%04x\n", pla_eee_cr);
    if (has_eee_cfg) printf("   OCP_EEE_CFG (0xA432)    : 0x%04x\n", ocp_eee_cfg);

    printf("\n EEE Advertisement Matrix:\n");
    printf("   %-22s | %-16s | %-16s\n", "Speed / Link Standard", "Local Advertised", "Partner Advertised");
    printf("  ------------------------+------------------+-------------------\n");
    if (max_speed >= 2500)
        printf("   %-22s | %-16s | %-16s\n", "2.5GBASE-T EEE",
               (ocp_eee_adv2 & 0x0001) ? "Yes" : "No",
               !peer_known ? "dont know" : (ocp_eee_lp2 & 0x0001) ? "Yes" : "No");
    if (max_speed >= 5000)
        printf("   %-22s | %-16s | %-16s\n", "5GBASE-T EEE",
               (ocp_eee_adv2 & 0x0002) ? "Yes" : "No",
               !peer_known ? "dont know" : (ocp_eee_lp2 & 0x0002) ? "Yes" : "No");
    if (max_speed >= 1000)
        printf("   %-22s | %-16s | %-16s\n", "1000BASE-T EEE",
               (ocp_eee_adv & 0x0004) ? "Yes" : "No",
               !peer_known ? "dont know" : (ocp_eee_lp & 0x0004) ? "Yes" : "No");
    printf("   %-22s | %-16s | %-16s\n", "100BASE-TX EEE",
           (ocp_eee_adv & 0x0002) ? "Yes" : "No",
           !peer_known ? "dont know" : (ocp_eee_lp & 0x0002) ? "Yes" : "No");

    printf("=================================================================\n\n");
    return 0;
}

static int update_phy_bits(rtl_dev_t *dev, uint16_t addr, uint16_t mask, uint16_t bits) {
    uint16_t value;
    if (rtl_ocp_reg_read(dev, addr, &value) != 0) return -1;
    return rtl_ocp_reg_write(dev, addr, (value & ~mask) | bits);
}

int rtl_eee_set(rtl_dev_t *dev, bool enable) {
    if (!dev) return -1;
    if (rtl_open_device(dev) != 0) return -1;

    printf("[*] %s Energy Efficient Ethernet (EEE) on %s (%s)...\n",
           enable ? "Enabling" : "Disabling",
           dev->version_info->name, dev->bsd_name);

    bool fast_ethernet = dev->version_info->family == CHIP_RTL8152B;
    uint16_t modes = fast_ethernet ? 0x0002 : 0x0006;
    uint16_t modes2 = dev->version_info->max_speed_mbps >= 2500 ? 0x0001 : 0;
    if (dev->version_info->family == CHIP_RTL8157) modes2 |= 0x0002;
    uint16_t supported = 0, supported2 = 0;
    if (enable) {
        int rc = fast_ethernet ? eee_mmd(dev, 3, 20, &supported, false)
                               : rtl_ocp_reg_read(dev, OCP_EEE_ABLE, &supported);
        if (rc != 0 || (modes2 && rtl_ocp_reg_read(dev, OCP_EEE_ABLE2, &supported2) != 0)) return -1;
        if (!(supported & modes) && !(supported2 & modes2)) {
            fprintf(stderr, "PHY reports no supported EEE advertisement modes.\n");
            return -1;
        }
    }

    uint16_t pla_cr;
    if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_EEE_CR, &pla_cr) != 0) return -1;
    uint16_t mask = EEE_TX_EN | EEE_RX_EN;
    if (rtl_ocp_write_word(dev, MCU_TYPE_PLA, PLA_EEE_CR,
                           enable ? pla_cr | mask : pla_cr & ~mask) != 0) return -1;
    if (fast_ethernet) {
        /* r8152_eee_en: quiet signalling, SD rise time and fast SNR threshold. */
        if (update_phy_bits(dev, OCP_EEE_CONFIG1, 0x3370, enable ? 0x3310 : 0x0070) != 0 ||
            update_phy_bits(dev, OCP_EEE_CONFIG2, 0x0600, enable ? 0x0600 : 0) != 0 ||
            update_phy_bits(dev, OCP_EEE_CONFIG3, 0xff80, (enable ? 42 : 511) << 7) != 0) return -1;
        uint16_t adv;
        if (eee_mmd(dev, 7, 60, &adv, false) != 0) return -1;
        adv = enable ? adv | 0x0002 : adv & ~0x0002;
        if (eee_mmd(dev, 7, 60, &adv, true) != 0) return -1;
    } else {
        if (has_eee10_config(dev) &&
            update_phy_bits(dev, OCP_EEE_CFG, EEE10_EN, enable ? EEE10_EN : 0) != 0) return -1;
        if (update_phy_bits(dev, OCP_EEE_ADV, modes, supported & modes) != 0) return -1;
    }
    if (modes2 && update_phy_bits(dev, OCP_EEE_ADV2, modes2, supported2 & modes2) != 0) return -1;

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
            printf("[+] EEE settings written. Link is up and auto-negotiation completed.\n");
            return 0;
        }
    }

    printf("[!] EEE settings written. Timed out waiting for link-up and completed auto-negotiation.\n");
    return 0;
}
