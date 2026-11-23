#ifndef RTL_REGS_H
#define RTL_REGS_H

#include <stdint.h>

/* USB Vendor & Product IDs */
#define REALTEK_VID             0x0bda

#define PID_RTL8152             0x8152
#define PID_RTL8153             0x8153
#define PID_RTL8156             0x8156
#define PID_RTL8157             0x8157
#define PID_RTL8155             0x8155

/* USB Request Types */
#define RTL8152_REQ_GET_REGS    0x05
#define RTL8152_REQ_SET_REGS    0x05
#define RTL8152_REQT_READ       0xc0
#define RTL8152_REQT_WRITE      0x40

/* MCU Types */
#define MCU_TYPE_PLA            0x0100
#define MCU_TYPE_USB            0x0000

/* Byte Enable Masks */
#define BYTE_EN_DWORD           0x00ff
#define BYTE_EN_WORD            0x0033
#define BYTE_EN_BYTE            0x0011
#define BYTE_EN_SIX_BYTES       0x003f

/* PLA (Platform) Registers */
#define PLA_IDR                 0xc000   /* MAC Address (6 bytes) */
#define PLA_RCR                 0xc010   /* Receive Configuration Register */
#define PLA_RMS                 0xc016   /* Rx Packet Max Size */
#define PLA_RXFIFO_CTRL0        0xc0a0
#define PLA_RXFIFO_CTRL1        0xc0a4
#define PLA_RXFIFO_CTRL2        0xc0a8
#define PLA_DMY_REG0            0xc0b0
#define PLA_FMC                 0xc0b4
#define PLA_CFG_WOL             0xc0b6
#define PLA_TERA_MCU_CTRL       0xc0b8

#define PLA_TXFIFO_CTRL         0xe618
#define PLA_TXFIFO_FULL         0xe61a
#define PLA_PHY_STATUS          0xe908   /* Link status and speed */
#define PLA_TCR0                0xe610   /* Transmit Configuration Register 0 */
#define PLA_TCR1                0xe612   /* Transmit Configuration Register 1 (Chip Version) */
#define PLA_MTPS                0x001c   /* Max Tx Packet Size */

#define PLA_CR                  0xe000
#define PLA_CRWECR              0xe008
#define PLA_CONFIG12            0xe00a
#define PLA_CONFIG34            0xe00c
#define PLA_CONFIG5             0xe00e
#define PLA_PHYPWR              0xe00c
#define PLA_OOB_CTRL            0xe84c
#define PLA_CPCR                0xe010
#define PLA_MISC_0              0xe012
#define PLA_MISC_1              0xe014
#define PLA_OCP_GPHY_BASE       0xe86c   /* Base address for GPHY register bank */
#define PLA_TALLYCNT            0xe890   /* Hardware Tally Counters */
#define PLA_RSTTALLY            0xe800   /* Reset Tally Counters */
#define PLA_TALLY               0xe080   /* Legacy Tally offset */

#define PLA_EEE_CR              0xe040   /* Energy Efficient Ethernet Control */
#define PLA_EEEP_CR             0xe080
#define PLA_MAC_PWR_CTRL        0xe0c0
#define PLA_MAC_PWR_CTRL2       0xe0c2
#define PLA_MAC_PWR_CTRL3       0xe0c4
#define PLA_MAC_PWR_CTRL4       0xe0c6
#define PLA_WDT64_CTRL          0xe0c8
#define PLA_LEDSEL_MULTI        0xdd8a   /* Four 16-bit selectors */
#define PLA_LEDSEL              0xdd90   /* LED Select */
#define PLA_LED_FEATURE         0xdd92   /* LED Feature */
#define PLA_GPHY_CTRL           0xd300

/* USB Registers */
#define USB_TOLERANCE           0xd490
#define USB_U2P3_CTRL           0xb460
#define USB_U2P3_ENABLE         0x0001
#define OCP_ALDPS_CONFIG        0x2010
#define ENPWRSAVE               0x8000
#define USB_USB_CTRL            0xd406
#define USB_ECM_OPTION          0xd408
#define USB_C21_CTRL            0xd40a
#define USB_CSR                 0xd40c
#define USB_USB_SPEC            0xd40e
#define USB_CONNECT_TIMER       0xc3fc
#define USB_BURST_SIZE          0xc402
#define USB_SSPHYLPM_CNTR       0xc40e
#define USB_PHY_PWR             0xc400
#define USB_PM_CTRL_STATUS      0xd414

/* RTL8157 indirect USB commands. */
#define USB_TGPHY_ADDR          0xd630
#define USB_TGPHY_DATA          0xd632
#define USB_TGPHY_CMD           0xd634
#define USB_ADV_ADDR            0xd5d6
#define USB_ADV_DATA            0xd5d8
#define USB_ADV_CMD             0xd5dc
#define CMD_BUSY                0x0001
#define CMD_WRITE               0x0002
#define ADV_CMD_BMU             0x0000
#define ADV_CMD_IP              0x0004
#define IP_U2P3_CTRL            0xc2c0
#define U2P3_ENABLE             (1u << 29)
#define USB_LPM_CONFIG          0xcfd8
#define LPM_U1U2_EN             0x0001
#define USB_POWER_CUT           0xd80a
#define UPS_EN                  0x0010

/* GPHY / MII Registers (Bank Base: 0xa400) */
#define OCP_BASE_MII            0xa400

#define MII_BMCR                0x00     /* Basic Mode Control Register */
#define MII_BMSR                0x01     /* Basic Mode Status Register */
#define MII_PHYSID1             0x02     /* PHY Identifier 1 */
#define MII_PHYSID2             0x03     /* PHY Identifier 2 */
#define MII_ADVERTISE           0x04     /* Auto-Negotiation Advertisement */
#define MII_LPA                 0x05     /* Auto-Negotiation Link Partner Ability */
#define MII_EXPANSION           0x06     /* Auto-Negotiation Expansion */
#define MII_NEXTPAGE            0x07     /* Auto-Negotiation Next Page Tx */
#define MII_LPNPA               0x08     /* Auto-Negotiation Link Partner Next Page */
#define MII_CTRL1000            0x09     /* 1000BASE-T Control */
#define MII_STAT1000            0x0a     /* 1000BASE-T Status */
#define MII_ESTATUS             0x0f     /* Extended Status */

/* BMCR Bits */
#define BMCR_RESET              0x8000
#define BMCR_LOOPBACK           0x4000
#define BMCR_SPEED100           0x2000
#define BMCR_ANENABLE           0x1000
#define BMCR_POWERDOWN          0x0800
#define BMCR_ISOLATE            0x0400
#define BMCR_ANRESTART          0x0200
#define BMCR_FULLDPLX           0x0100
#define BMCR_SPEED1000          0x0040

/* BMSR Bits */
#define BMSR_100FULL            0x4000
#define BMSR_100HALF            0x2000
#define BMSR_10FULL             0x1000
#define BMSR_10HALF             0x0800
#define BMSR_ESTATEN            0x0100
#define BMSR_ANEGCOMPLETE       0x0020
#define BMSR_RFAULT             0x0010
#define BMSR_ANEGCAPABLE        0x0008
#define BMSR_LSTATUS            0x0004
#define BMSR_ERCAP              0x0001

/* Multi-Gigabit (2.5G/5G) OCP Registers */
#define OCP_2500_CTRL           0xa5d4   /* MDIO AN 10GBT control */
#define OCP_2500_STAT           0xa5d6   /* MDIO AN 10GBT status */

/* Linux r8152.c PLA_PHYSTATUS and IEEE MDIO AN 10GBT capability bits. */
#define PHY_STAT_FULL_DUPLEX   0x0001
#define PHY_STAT_LINK          0x0002
#define PHY_STAT_10            0x0004
#define PHY_STAT_100           0x0008
#define PHY_STAT_1000          0x0010
#define PHY_STAT_2500          0x0400
#define PHY_STAT_5000          0x1000
#define ADV_2500_FULL          0x0080
#define ADV_5000_FULL          0x0100
#define LP_2500_FULL           0x0020
#define LP_5000_FULL           0x0040

/* Energy Efficient Ethernet (EEE) Registers */
#define OCP_EEE_CONFIG1         0x2080
#define OCP_EEE_CONFIG2         0x2092
#define OCP_EEE_CONFIG3         0x2094
#define OCP_EEE_AR              0xa41a
#define OCP_EEE_DATA            0xa41c
#define OCP_EEE_CFG             0xa432
#define EEE10_EN                0x0010
#define OCP_EEE_ABLE            0xa5c4
#define OCP_EEE_ABLE2           0xa6ec
#define OCP_EEE_ADV             0xa5d0   /* 100M/1G EEE Advertisement */
#define OCP_EEE_LPABLE          0xa5d2   /* 100M/1G EEE Link Partner Ability */
#define OCP_EEE_ADV2            0xa6d4   /* 2.5G/5G EEE Advertisement */
#define OCP_EEE_LPABLE2         0xa6d0   /* 2.5G/5G EEE Link Partner Ability */

/* PLA_EEE_CR Bits */
#define EEE_TX_EN               0x0002
#define EEE_RX_EN               0x0001

/* Chip Version Detection (PLA_TCR1 & 0x7cf0) */
#define VERSION_MASK            0x7cf0

typedef enum {
    CHIP_UNKNOWN = 0,
    CHIP_RTL8152B,
    CHIP_RTL8153A,
    CHIP_RTL8153B,
    CHIP_RTL8153C,
    CHIP_RTL8156A,
    CHIP_RTL8156B,
    CHIP_RTL8157
} rtl_chip_family_t;

typedef struct {
    uint16_t version_code;
    rtl_chip_family_t family;
    const char *name;
    const char *revision_str;
    uint32_t max_speed_mbps;
} rtl_version_info_t;

static const rtl_version_info_t RTL_CHIP_VERSIONS[] = {
    { 0x4c00, CHIP_RTL8152B, "RTL8152B", "RTL_VER_01", 100 },
    { 0x4c10, CHIP_RTL8152B, "RTL8152B", "RTL_VER_02", 100 },
    { 0x5c00, CHIP_RTL8153A, "RTL8153A", "RTL_VER_03", 1000 },
    { 0x5c10, CHIP_RTL8153A, "RTL8153A", "RTL_VER_04", 1000 },
    { 0x5c20, CHIP_RTL8153A, "RTL8153A", "RTL_VER_05", 1000 },
    { 0x5c30, CHIP_RTL8153A, "RTL8153A", "RTL_VER_06", 1000 },
    { 0x6000, CHIP_RTL8153B, "RTL8153B", "RTL_VER_08", 1000 },
    { 0x6010, CHIP_RTL8153B, "RTL8153B", "RTL_VER_09", 1000 },
    { 0x6400, CHIP_RTL8153C, "RTL8153C", "RTL_VER_14", 1000 },
    { 0x7020, CHIP_RTL8156A, "RTL8156A", "RTL_VER_10", 2500 },
    { 0x7030, CHIP_RTL8156A, "RTL8156A", "RTL_VER_11", 2500 },
    /* rtu56cx22x64 dispatches these through the same ops as 0x7030. */
    { 0x7050, CHIP_RTL8156A, "RTL8156", "Windows family match", 2500 },
    { 0x7060, CHIP_RTL8156A, "RTL8156", "Windows family match", 2500 },
    { 0x7070, CHIP_RTL8156A, "RTL8156", "Windows family match", 2500 },
    { 0x7400, CHIP_RTL8156B, "RTL8156B", "RTL_VER_12", 2500 },
    { 0x7410, CHIP_RTL8156B, "RTL8156B", "RTL_VER_13", 2500 },
    { 0x1030, CHIP_RTL8157, "RTL8157", "RTL_VER_16", 5000 },
    { 0x0000, CHIP_UNKNOWN,  "Unknown Realtek", "Unknown",        0 }
};

/* Hardware Tally Structure (Packed 64 bytes) */
#pragma pack(push, 1)
typedef struct {
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_errors;
    uint32_t rx_errors;
    uint16_t rx_missed;          /* FIFO drops */
    uint16_t align_errors;
    uint32_t tx_one_collision;
    uint32_t tx_multi_collision;
    uint64_t rx_unicast;
    uint64_t rx_broadcast;
    uint32_t rx_multicast;
    uint16_t tx_aborted;
    uint16_t tx_underrun;
} rtl_tally_counters_t;
#pragma pack(pop)

#endif /* RTL_REGS_H */
