#include "rtl_diag.h"
#include <stdio.h>
#include <unistd.h>

int rtl_diag_print_status(rtl_dev_t *dev) {
    if (!dev) return -1;

    if (rtl_open_device(dev) != 0) {
        fprintf(stderr, "Error: Failed to open USB device %04x:%04x\n", dev->vid, dev->pid);
        return -1;
    }

    /* BMSR link status (bit 2) is latching-low in IEEE 802.3; double-read to get live status */
    uint16_t latched_bmsr;
    if (rtl_mdio_read(dev, MII_BMSR, &latched_bmsr) != 0) return -1;
    uint16_t bmsr;
    if (rtl_mdio_read(dev, MII_BMSR, &bmsr) != 0) return -1;
    uint16_t adv, lpa, phy_status;
    if (rtl_mdio_read(dev, MII_ADVERTISE, &adv) != 0 ||
        rtl_mdio_read(dev, MII_LPA, &lpa) != 0 ||
        rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_PHY_STATUS, &phy_status) != 0) return -1;
    uint16_t eee_cr;
    if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_EEE_CR, &eee_cr) != 0) return -1;
    uint16_t rms;
    if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_RMS, &rms) != 0) return -1;
    uint16_t rcr;
    if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, PLA_RCR, &rcr) != 0) return -1;

    bool link_up = (phy_status & PHY_STAT_LINK) != 0;
    bool an_complete = (bmsr & BMSR_ANEGCOMPLETE) != 0;

    const char *speed_str = "Link Down";
    const char *duplex_str = "N/A";

    /* Report the resolved link, not the highest common advertised ability. */
    if (link_up) {
        if (phy_status & PHY_STAT_5000) speed_str = "5.0 Gbps (5000BASE-T)";
        else if (phy_status & PHY_STAT_2500) speed_str = "2.5 Gbps (2500BASE-T)";
        else if (phy_status & PHY_STAT_1000) speed_str = "1.0 Gbps (1000BASE-T)";
        else if (phy_status & PHY_STAT_100) speed_str = "100 Mbps (100BASE-TX)";
        else if (phy_status & PHY_STAT_10) speed_str = "10 Mbps (10BASE-T)";
        else speed_str = "Connected (Unknown Speed)";
        duplex_str = (phy_status & PHY_STAT_FULL_DUPLEX) ? "Full Duplex" : "Half Duplex";
    }

    const char *usb_speed_str = "USB 2.0 (480 Mbps)";
    if (dev->usb_link_speed_bps >= 10000000000ULL) {
        usb_speed_str = "USB 3.2 Gen 2 (10 Gbps SuperSpeed+)";
    } else if (dev->usb_link_speed_bps >= 5000000000ULL) {
        usb_speed_str = "USB 3.2 Gen 1 (5 Gbps SuperSpeed)";
    }

    printf("\n=================================================================\n");
    printf(" Realtek USB Ethernet Diagnostics: %s (%s)\n", dev->version_info->name, dev->version_info->revision_str);
    printf("=================================================================\n");
    printf(" Hardware Identification:\n");
    printf("   Chip Family      : %s\n", dev->version_info->name);
    printf("   Revision Code    : 0x%04x (%s)\n", dev->version_raw, dev->version_info->revision_str);
    printf("   USB VID:PID      : %04x:%04x (%s %s)\n", dev->vid, dev->pid, dev->vendor_name, dev->product_name);
    printf("   USB Bus Speed    : %s\n", usb_speed_str);
    printf("   Hardware MAC     : %02x:%02x:%02x:%02x:%02x:%02x\n",
           dev->mac_addr[0], dev->mac_addr[1], dev->mac_addr[2],
           dev->mac_addr[3], dev->mac_addr[4], dev->mac_addr[5]);
    printf("   macOS Interface  : %s (Driver: AppleUSBNCMData / NCM Mode)\n", dev->bsd_name);

    printf("\n Link & Physical Layer (PHY):\n");
    printf("   Link State       : %s\n", link_up ? "\033[1;32mUP (Active)\033[0m" : "\033[1;31mDOWN (No Carrier)\033[0m");
    printf("   Negotiated Speed : \033[1;36m%s\033[0m\n", speed_str);
    printf("   Duplex Mode      : %s\n", duplex_str);
    printf("   Auto-Negotiation : %s\n", an_complete ? "Completed" : (link_up ? "In Progress" : "Disabled/Idle"));

    bool sym_pause = (adv & 0x0400) && (lpa & 0x0400);
    bool asym_pause = (adv & 0x0800) && (lpa & 0x0800);
    printf("   Flow Control     : %s\n", sym_pause ? "Symmetric Pause (RX & TX)" : (asym_pause ? "Asymmetric Pause" : "Disabled / None"));

    bool eee_active = (eee_cr & (EEE_TX_EN | EEE_RX_EN)) != 0;
    printf("   Energy Eff. Eth. : %s\n", eee_active ? "Enabled (LPI Active)" : "Disabled (Optimal Low Latency)");

    printf("\n Controller Registers:\n");
    printf("   PLA_RMS (Max RX) : %u bytes\n", rms);
    printf("   PLA_RCR (Filter) : 0x%04x (Accept: %s%s%s)\n",
           rcr,
           (rcr & 0x0002) ? "Physical " : "",
           (rcr & 0x0004) ? "Multicast " : "",
           (rcr & 0x0008) ? "Broadcast " : "");
    printf("=================================================================\n\n");
    return 0;
}

int rtl_diag_print_phy(rtl_dev_t *dev) {
    if (!dev) return -1;

    if (rtl_open_device(dev) != 0) {
        fprintf(stderr, "Error: Failed to open USB device %04x:%04x\n", dev->vid, dev->pid);
        return -1;
    }

    uint16_t bmcr;
    if (rtl_mdio_read(dev, MII_BMCR, &bmcr) != 0) return -1;
    uint16_t latched_bmsr;
    if (rtl_mdio_read(dev, MII_BMSR, &latched_bmsr) != 0) return -1;
    uint16_t bmsr;
    if (rtl_mdio_read(dev, MII_BMSR, &bmsr) != 0) return -1;
    uint16_t phyid1;
    if (rtl_mdio_read(dev, MII_PHYSID1, &phyid1) != 0) return -1;
    uint16_t phyid2;
    if (rtl_mdio_read(dev, MII_PHYSID2, &phyid2) != 0) return -1;
    uint16_t adv;
    if (rtl_mdio_read(dev, MII_ADVERTISE, &adv) != 0) return -1;
    uint16_t lpa;
    if (rtl_mdio_read(dev, MII_LPA, &lpa) != 0) return -1;
    uint16_t giga_ctrl;
    if (rtl_mdio_read(dev, MII_CTRL1000, &giga_ctrl) != 0) return -1;
    uint16_t giga_stat;
    if (rtl_mdio_read(dev, MII_STAT1000, &giga_stat) != 0) return -1;
    uint16_t mg_ctrl = 0, mg_stat = 0;
    if (dev->version_info->max_speed_mbps >= 2500 &&
        (rtl_ocp_reg_read(dev, OCP_2500_CTRL, &mg_ctrl) != 0 ||
         rtl_ocp_reg_read(dev, OCP_2500_STAT, &mg_stat) != 0)) return -1;

    printf("\n=================================================================\n");
    printf(" Realtek PHY & MDIO Diagnostic Report (%s)\n", dev->version_info->name);
    printf("=================================================================\n");
    printf(" PHY Hardware Identifier:\n");
    printf("   OUI / Model / Rev: 0x%04x:0x%04x (Realtek Integrated GPHY)\n", phyid1, phyid2);
    printf("   BMCR (0x00)      : 0x%04x (AutoNeg: %s, Duplex: %s, Loopback: %s)\n",
           bmcr,
           (bmcr & BMCR_ANENABLE) ? "Enabled" : "Disabled",
           (bmcr & BMCR_FULLDPLX) ? "Full" : "Half",
           (bmcr & BMCR_LOOPBACK) ? "Yes" : "No");
    printf("   BMSR (0x01)      : 0x%04x (Link: %s, AutoNeg Done: %s, Extended Cap: %s)\n",
           bmsr,
           (bmsr & BMSR_LSTATUS) ? "UP" : "DOWN",
           (bmsr & BMSR_ANEGCOMPLETE) ? "Yes" : "No",
           (bmsr & BMSR_ESTATEN) ? "Yes" : "No");

    printf("\n Auto-Negotiation Capability Matrix:\n");
    printf("   %-22s | %-16s | %-16s\n", "Speed / Technology", "Local Adapter", "Link Partner (Switch)");
    printf("  ------------------------+------------------+-------------------\n");
    printf("   %-22s | %-16s | %-16s\n", "5GBASE-T Full",
           (mg_ctrl & ADV_5000_FULL) ? "Supported" : "No",
           (mg_stat & LP_5000_FULL) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "2.5GBASE-T Full",
           (mg_ctrl & ADV_2500_FULL) ? "\033[1;32mSupported\033[0m" : "No",
           (mg_stat & LP_2500_FULL) ? "\033[1;32mSupported\033[0m" : "No");
    printf("   %-22s | %-16s | %-16s\n", "1000BASE-T Full",
           (giga_ctrl & 0x0200) ? "Supported" : "No",
           (giga_stat & 0x0800) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "1000BASE-T Half",
           (giga_ctrl & 0x0100) ? "Supported" : "No",
           (giga_stat & 0x0400) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "100BASE-TX Full",
           (adv & 0x0100) ? "Supported" : "No",
           (lpa & 0x0100) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "100BASE-TX Half",
           (adv & 0x0080) ? "Supported" : "No",
           (lpa & 0x0080) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "10BASE-T Full",
           (adv & 0x0040) ? "Supported" : "No",
           (lpa & 0x0040) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "10BASE-T Half",
           (adv & 0x0020) ? "Supported" : "No",
           (lpa & 0x0020) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "Symmetric Pause",
           (adv & 0x0400) ? "Supported" : "No",
           (lpa & 0x0400) ? "Supported" : "No");
    printf("   %-22s | %-16s | %-16s\n", "Asymmetric Pause",
           (adv & 0x0800) ? "Supported" : "No",
           (lpa & 0x0800) ? "Supported" : "No");
    printf("=================================================================\n\n");
    return 0;
}

int rtl_diag_restart_an(rtl_dev_t *dev) {
    if (!dev) return -1;
    if (rtl_open_device(dev) != 0) return -1;

    printf("[*] Restarting PHY Auto-Negotiation on %s (%s)...\n", dev->version_info->name, dev->bsd_name);
    uint16_t bmcr;
    if (rtl_mdio_read(dev, MII_BMCR, &bmcr) != 0) return -1;
    bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
    if (rtl_mdio_write(dev, MII_BMCR, bmcr) != 0) return -1;

    printf("[*] Auto-Negotiation restarted. Waiting for link sync...\n");
    for (int i = 0; i < 20; i++) {
        usleep(150000);
        uint16_t bmsr;
        if (rtl_mdio_read(dev, MII_BMSR, &bmsr) != 0) return -1;
        if ((bmsr & BMSR_LSTATUS) && (bmsr & BMSR_ANEGCOMPLETE)) {
            printf("[+] Link re-established successfully!\n");
            return 0;
        }
    }
    printf("[!] Link re-negotiation still in progress.\n");
    return 0;
}
