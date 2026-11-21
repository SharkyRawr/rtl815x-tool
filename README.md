# Realtek RTL8156 / RTL815x macOS Diagnostics & Control Toolkit (`rtl815x-tool`)

A standalone macOS CLI utility for inspecting, diagnosing, and tuning **Realtek USB Ethernet adapters** (`RTL8156`, `RTL8156B`, `RTL8153`, `RTL8152`, and `RTL8157`).

The tool communicates directly with the controller's **On-Chip Peripheral (OCP)** bus via **USB Endpoint 0 control transfers** using native macOS `IOKit`. It works seamlessly in userspace while macOS is running standard plug-and-play **CDC-NCM mode** (`AppleUSBNCMData`).

---

## Supported Chipsets

| Chipset | Max Link Speed | Revision Codes | Typical Hardware |
| :--- | :--- | :--- | :--- |
| **RTL8156B** | **2.5 Gbps** | `0x7050`, `0x7060`, `0x7070`, `0x7400`, `0x7410` | 2.5G USB-C/USB-3 Adapters & Docks |
| **RTL8156 / RTL8156A** | **2.5 Gbps** | `0x7020`, `0x7030` | 2.5G USB-A / USB-C Adapters (1st Gen) |
| **RTL8157** | **5.0 Gbps** | `0x7440`, `0x7450`, `0x7460` | 5G Multi-Gigabit USB Adapters |
| **RTL8153 / RTL8153B/C** | **1.0 Gbps** | `0x5c00`, `0x5c10`, `0x5c20`, `0x5c30`, `0x6000`, `0x6010` | Standard Gigabit USB Dongles & Docks |
| **RTL8152 / RTL8152B** | **100 Mbps** | `0x4c00`, `0x4c10` | Fast Ethernet Adapters |

---

## Features

* **Hardware & Link Overview (`status`):**
  * Auto-detects chipset revision, hardware eFUSE MAC address, USB link speed (SuperSpeed 5Gbps vs USB2 480Mbps), and assigned macOS BSD interface name (`en12`).
  * Real-time PHY link status, negotiated speed (2.5 Gbps / 1 Gbps / 100M / 10M), duplex mode, and local/partner pause advertisements.
* **Hardware Tally & Error Counters (`stats` / `--json`):**
  * Reads on-chip hardware counters directly from `PLA_TALLYCNT` (0xE890).
  * Tracks accurate TX/RX packet volume, RX Unicast, Multicast, and Broadcast distribution.
  * Measures hardware CRC errors, alignment errors, collision counts, TX aborts, and **RX FIFO buffer drops (`rx_missed`)** during 2.5G multi-gig saturation.
  * Structured JSON output support (`--json`) for Prometheus, Telegraf, and automated monitoring.
* **Live Terminal Monitor (`watch`):**
  * Interactive fullscreen dashboard reporting real-time packets/sec (pps), delta traffic, and error rates.
* **PHY & Auto-Negotiation Analysis (`phy`):**
  * Deep inspection of MII/MDIO registers (BMCR, BMSR, PHY ID, 1000Base-T, 2.5GBase-T).
  * Side-by-side comparison of local and link partner advertisements. Unavailable values display as `dont know`.
  * On-demand PHY auto-negotiation restart (`--restart-an`) to re-sync link without unplugging.
* **Energy Efficient Ethernet Tuning (`eee`):**
  * Inspect Low Power Idle (LPI) TX/RX configuration and EEE advertisement across 100M, 1G, and 2.5G.
  * Change EEE configuration with `eee disable` or `eee enable`.
* **Power Management Tuning (`power`):**
  * Inspect and configure USB Link Power Management (LPM L1) and Ultra Power Saving (UPS).
  * Clear the UPS and LPM control bits with `power no-sleep`, or set them with `power default`.
* **LED Pattern Customization (`led`):**
  * Configure on-adapter LEDs: standard, link-only (solid), activity-only (blink on transfer), stealth mode (all LEDs off), or custom bitmasks.
* **Raw Register Access (`reg`):**
  * Direct read/write/dump of PLA, USB, and GPHY registers for development and reverse engineering.

---

## Building & Installation

### Requirements
* macOS 10.15 (Catalina), 11 (Big Sur), 12 (Monterey), 13 (Ventura), 14 (Sonoma), or 15 (Sequoia).
* Xcode Command Line Tools (`clang`, `make`).

### Option 1: Homebrew (Recommended)

```bash
# Tap with explicit URL, then trust and install
brew tap SharkyRawr/rtl815x-tool https://github.com/SharkyRawr/rtl815x-tool
brew trust SharkyRawr/rtl815x-tool
brew install rtl815x-tool
```

(`brew trust` is required because this is a third-party tap.)

### Option 2: Build from Source

```bash
git clone https://github.com/SharkyRawr/rtl815x-tool.git
cd rtl815x-tool
make
sudo make install
```
This builds and installs `rtl815x-tool` into `/usr/local/bin`.

---

## Validation and error handling

Run `make test` on macOS to exercise the CLI and register access with a mock USB
endpoint under AddressSanitizer and UndefinedBehaviorSanitizer. The tests cover
device selection, failed and short USB transfers, write error propagation,
resolved link speeds, and watch intervals without changing a connected adapter.

Commands return a nonzero exit status on USB failures. Register operations stop
at the first failure; a command that already performed some writes can leave
partial changes applied. Invalid explicit device indices are rejected.

`watch -n 5`, `watch --interval 5`, `watch 5`, and `-n 5 watch` all select a
five-second interval. Intervals must be finite and between 0.2 and 3600 seconds.

## Usage & CLI Reference

### 1. View Adapter Status
```bash
./rtl815x-tool status
```
```text
=================================================================
 Realtek USB Ethernet Diagnostics: RTL8156B (v2-alt/BG (2.5GbE))
=================================================================
 Hardware Identification:
   Chip Family      : RTL8156B
   Revision Code    : 0x7410 (v2-alt/BG (2.5GbE))
   USB VID:PID      : 0bda:8156 (Realtek USB 10_100_1G_2_5G LAN)
   USB Bus Speed    : 5000 Mbps
   Hardware MAC     : 00:e0:4c:68:00:82
   macOS Interface  : en12 (Driver: com.realtek.dext.RTL815xEthernet)

 Link & Physical Layer (PHY):
   Link State       : UP (Active)
   Negotiated Speed : 2.5 Gbps (2500BASE-T)
   Duplex Mode      : Full Duplex
   Auto-Negotiation : Completed
   Pause advertised : local symmetric and asymmetric, partner symmetric and asymmetric
   Energy Eff. Eth. : Disabled

 Controller Registers:
   PLA_RMS (Max RX) : 0 bytes
   PLA_RCR (Filter) : 0x480e (Accept: Physical Multicast Broadcast )
=================================================================
```

### 2. Inspect Hardware Error & Drop Counters
```bash
./rtl815x-tool stats
```
```text
=================================================================
 Realtek Hardware Statistics & Error Counters
 Interface: en12 (RTL8156B, 00:e0:4c:68:00:82)
=================================================================
 Traffic Volume:
   TX Packets Total     : 83,029
   RX Packets Total     : 440,793
   RX Unicast           : 417,956
   RX Multicast         : 15,534
   RX Broadcast         : 7,303

 Error & Drop Counters:
   TX Errors            : 0
   RX Errors (CRC/Align): 0
   RX Missed (FIFO Drop): 0
   Alignment Errors     : 0
   Single Collisions    : 0
   Multiple Collisions  : 0
   TX Aborted           : 0
   TX Underrun          : 0
=================================================================
```

To export counters in JSON format:
```bash
./rtl815x-tool stats --json
```

To reset the hardware tally counters to 0:
```bash
./rtl815x-tool tally-reset
```

### 3. Live Dashboard Monitor
```bash
./rtl815x-tool watch -n 1.0
```
Displays an in-place updating terminal dashboard tracking packet rates (pkt/s), unicast/multicast throughput, and real-time delta error counts.

### 4. PHY Diagnostics & Advertisements
```bash
./rtl815x-tool phy
```
```text
=================================================================
 Realtek PHY & MDIO Diagnostic Report (RTL8156B)
=================================================================
 PHY Hardware Identifier:
   PHY ID registers : 0x001c:0xc840
   BMCR (0x00)      : 0x1000 (AutoNeg: Enabled, Duplex: Half, Loopback: No)
   BMSR (0x01)      : 0x79ad (Link: UP, AutoNeg Done: Yes, Extended Cap: Yes)

 Auto-Negotiation Advertisements:
   Speed / Technology     | Local Adapter    | Link Partner
  ------------------------+------------------+-------------------
   5GBASE-T Full          | No               | No
   2.5GBASE-T Full        | Yes              | Yes
   1000BASE-T Full        | Yes              | Yes
   1000BASE-T Half        | No               | No
   100BASE-TX Full        | Yes              | Yes
   100BASE-TX Half        | Yes              | Yes
   10BASE-T Full          | Yes              | Yes
   10BASE-T Half          | Yes              | Yes
   Symmetric Pause        | Yes              | Yes
   Asymmetric Pause       | Yes              | Yes
=================================================================
```

To force PHY auto-negotiation restart:
```bash
./rtl815x-tool phy --restart-an
```

### 5. Energy Efficient Ethernet (EEE)
To check TX/RX configuration and advertised EEE modes:
```bash
./rtl815x-tool eee status
```

To disable EEE:
```bash
./rtl815x-tool eee disable
```

To re-enable EEE:
```bash
./rtl815x-tool eee enable
```

### 6. LED Configuration
```bash
# Check current LED configuration
./rtl815x-tool led status

# Turn off all LEDs (stealth mode)
./rtl815x-tool led set off

# Solid link LED only (no activity blinking)
./rtl815x-tool led set link-only

# Apply the tool’s default LED preset
./rtl815x-tool led set default
```

### 7. Direct Register Access (Developer / Debugger Mode)
```bash
# Read PLA MAC ID register (0xC000)
./rtl815x-tool reg read pla 0xc000

# Read PHY register (0xA400 is MII BMCR)
./rtl815x-tool reg read phy 0xa400

# Hexdump PLA registers from 0xC000 to 0xC040
./rtl815x-tool reg dump pla 0xc000 0x40

# Write 16-bit value to register
./rtl815x-tool reg write pla 0xdd90 0x0000 16
```

---

## Technical Notes: How Endpoint 0 Userspace Control Works

### The macOS NCM Driver vs. Endpoint 0
When an RTL8156 adapter uses Apple's NCM driver, `AppleUSBNCMData` claims **USB Configuration 2 (CDC-NCM)** for standard driverless network traffic.

While the bulk data endpoints (EP1 IN / EP2 OUT) are owned by the kernel network stack, **USB Endpoint 0 (the Default Control Pipe)** remains accessible from userspace via `IOKitLib` and `IOUSBLib`.

Realtek controllers use vendor-specific control transfers on Endpoint 0 (`bRequest = 0x05`) to address their internal **On-Chip Peripheral (OCP)** bus:
* **OCP Register Read (`0xC0`, `0x05`)**: `bmRequestType=0xC0`, `wValue=register_address`, `wIndex=MCU_TYPE`.
* **OCP Register Write (`0x40`, `0x05`)**: `bmRequestType=0x40`, `wValue=register_address`, `wIndex=MCU_TYPE | BYTE_EN_MASK`.
* **GPHY Access**: Accessing MDIO/PHY registers is achieved by switching the base address register (`PLA_OCP_GPHY_BASE` at `0xE86C`) and reading through the GPHY aperture (`0xB000` + offset).

This enables complete visibility into hardware counters, PHY registers, link health, and power configurations without needing custom kernel extensions or DriverKit signing entitlements.

---

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License (CC BY-NC-SA 4.0)](LICENSE).
