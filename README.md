# Realtek RTL8156 / RTL8157 / RTL815x macOS Diagnostics & Control Toolkit (`rtl815x-tool`)

A standalone macOS CLI utility for inspecting, diagnosing, and tuning **Realtek USB Ethernet adapters** (`RTL8156`, `RTL8156B`, `RTL8153`, `RTL8152`, and `RTL8157`).

The tool communicates directly with the controller's **On-Chip Peripheral (OCP)** bus via **USB Endpoint 0 control transfers** using native macOS `IOKit`. The `status` command reports the macOS driver bound to the selected interface.

---

## Supported Chipsets

| Chipset reported by the CLI | Maximum Ethernet link | Masked hardware revision |
| :--- | :--- | :--- |
| RTL8152B | 100 Mb/s | `0x4c00`, `0x4c10` |
| RTL8153A | 1 Gb/s | `0x5c00`, `0x5c10`, `0x5c20`, `0x5c30` |
| RTL8153B | 1 Gb/s | `0x6000`, `0x6010` |
| RTL8153C | 1 Gb/s | `0x6400` |
| RTL8156A | 2.5 Gb/s | `0x7020`, `0x7030` |
| RTL8156 (Windows family match) | 2.5 Gb/s | `0x7050`, `0x7060`, `0x7070` |
| RTL8156B | 2.5 Gb/s | `0x7400`, `0x7410` |
| RTL8157 | 5 Gb/s | `0x1030` (Realtek `RTL_VER_16`) |

Discovery matches USB vendor/product IDs for Realtek adapters and OEM dongles
and docks. The CLI reads `PLA_TCR1 & 0x7cf0` before selecting the register
layout. An unreadable or unsupported revision prevents feature commands.
The exact revision mappings come from Linux and Realtek's driver.
[Linux revision table](https://github.com/torvalds/linux/blob/b6a9fa34ce4187cb69d3c588a4bbffc31ce5db8f/drivers/net/usb/r8152.c#L10010-L10064) · [Realtek revision table](https://github.com/awesometic/realtek-r8152-dkms/blob/c1034ab62ddc48383118598f43c3227fc53c309e/src/r8152.c#L33445-L33507)

For `0x7050`, `0x7060` and `0x7070`, Realtek's Windows driver
`rtu56cx22x64.sys` matches the family with `(revision & 0x7c00) == 0x7000`
at RVA `0x47f97`. It assigns chip ID `0x4000` and selects the same operations
as `0x7030` at RVA `0x48e17`. This identifies the first-generation register
layout. The exact silicon stepping and presence of shipping boards with each
of these codes remain unconfirmed. The CLI accepts those three codes explicitly.
[Realtek Windows 11 driver package](https://plugable.s3.us-east-1.amazonaws.com/bin/Realtek/Install_USB_Win11_11022_20_02062026.zip)

---

## Features

* **Hardware & Link Overview (`status`):**
  * Auto-detects chipset revision, current controller MAC address, USB link speed reported by macOS, and assigned macOS BSD interface name (`en12`).
  * Real-time PHY link status, negotiated speed (5 Gbps / 2.5 Gbps / 1 Gbps / 100M / 10M), duplex mode, and local/partner pause advertisements.
* **Hardware Tally & Error Counters (`stats` / `--json`):**
  * Reads on-chip hardware counters directly from `PLA_TALLYCNT` (0xE890).
  * Tracks accurate TX/RX packet volume, RX Unicast, Multicast, and Broadcast distribution.
  * Measures hardware CRC errors, alignment errors, collision counts, TX aborts, and **RX FIFO buffer drops (`rx_missed`)** during 2.5G multi-gig saturation.
  * Structured JSON output support (`--json`) for Prometheus, Telegraf, and automated monitoring.
* **Live Terminal Monitor (`watch`):**
  * Interactive fullscreen dashboard reporting real-time packets/sec (pps), delta traffic, and error rates.
* **PHY & Auto-Negotiation Analysis (`phy`):**
  * Deep inspection of MII/MDIO registers (BMCR, BMSR, PHY ID, 1000BASE-T, 2.5GBASE-T, 5GBASE-T).
  * Side-by-side comparison of local and link partner advertisements. Unavailable values display as `dont know`.
  * On-demand PHY auto-negotiation restart (`--restart-an`) to re-sync link without unplugging.
* **Energy Efficient Ethernet Tuning (`eee`):**
  * Read MAC Low Power Idle controls and compare local and partner EEE advertisements for the chipset's speeds. RTL8152B uses indirect MMD access for 100BASE-TX EEE.
  * `eee enable` reads the PHY's supported EEE modes before advertising them. Both `enable` and `disable` restart auto-negotiation.
* **Power management (`power`)**
  * On RTL8152B, `no-sleep` disables PHY link-down power saving and `default` enables it.
  * On the gigabit and multi-gigabit families, `no-sleep` disables USB U1/U2 and U2-to-P3. `default` follows Realtek's interface-up settings. It enables U2-to-P3 on RTL8153A revisions `0x5c20` and `0x5c30`, RTL8156 and RTL8157. For U1/U2 it enables tolerance on RTL8153A, enables U1/U2 on RTL8153C, and uses the negotiated USB speed on RTL8153B, RTL8156 and RTL8157 (SuperSpeed or faster). RTL8157 also reports the driver-managed suspend UPS bit. [Realtek power routines](https://github.com/awesometic/realtek-r8152-dkms/blob/c1034ab62ddc48383118598f43c3227fc53c309e/src/r8152.c#L12035-L12474)
* **LED configuration (`led`)**
  * Read or write one packed selector on RTL8152B/RTL8153, or four per-pin selectors on RTL8156/RTL8157. See the LED command reference below for available presets.
* **Raw Register Access (`reg`):**
  * Direct read/write/dump of PLA, USB, and PHY registers, plus the RTL8157 IP and BMU register spaces for development and reverse engineering.

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

The mock USB endpoint exercises every revision in the table, including speed
reporting, register selection, EEE, power, LEDs and counters. The RTL8157
fixture also emulates the busy flags on its two indirect command interfaces.
Read-only commands have been checked on a physical RTL8156B (`0x7410`) at
2.5 Gb/s using Apple's NCM driver. Other revisions and physical tuning writes
still require hardware verification.

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
 Realtek USB Ethernet Diagnostics: RTL8156B (RTL_VER_13)
=================================================================
 Hardware Identification:
   Chip Family      : RTL8156B
   Revision Code    : 0x7410 (RTL_VER_13)
   USB VID:PID      : 0bda:8156 (Realtek USB 10_100_1G_2_5G LAN)
   USB Bus Speed    : 5000 Mbps
   Hardware MAC     : 00:e0:4c:68:00:82
   macOS Interface  : en12 (Driver: com.apple.driver.usb.cdc.ncm)

 Link & Physical Layer (PHY):
   Link State       : UP (Active)
   Negotiated Speed : 2.5 Gbps (2500BASE-T)
   Duplex Mode      : Full Duplex
   Auto-Negotiation : Completed
   Pause advertised : local symmetric and asymmetric, partner symmetric and asymmetric
   Energy Eff. Eth. : Disabled

 Controller Registers:
   PLA_RMS (Max RX) : 1518 bytes
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

All listed revisions support `led status`, `led set default`, and a raw 16-bit
selector. The default is a preset and can differ from the adapter maker's
configuration. Record the current values to restore the board's original pattern.

| Family | Selectors | Additional named presets |
| :--- | :--- | :--- |
| RTL8152B, RTL8153A/B | Packed word at `0xdd90` | `link-only` (LED0), `activity-only` (LED1), `off` |
| RTL8153C | Packed word at `0xdd90` | Individual function bits unverified |
| RTL8156 / RTL8156A | Four words at `0xdd8a`, `0xdd8c`, `0xdd8e`, `0xdd90` | Individual function bits unverified |
| RTL8156B | Same four words | `link-only`, `activity-only`, `off` on all four pins |
| RTL8157 | Same four words | Individual function bits unverified |

On four-selector chips, `led set 0x<word>` writes that word to every pin.
Use a 16-bit PLA register write to change one pin. The RTL8156 preset in pin
order is `0220 0208 0202 0201`. The RTL8157 preset is `0220 0208 0203 0280`.
The register addresses, widths and multi-gigabit defaults come from
Realtek PATCH64 2.0.29 (`LED_SEL_CFG`). [Realtek programming utility](https://www.station-drivers.com/index.php/en-us/component/remository/Drivers/Realtek/Lan/RTL8152-8153-8155-8156-8157-8159/Utilities--and--Firmwares/Realtek-RTL8153DD-RX-parametre-modification-Tool-Version-2.0.29/lang,en-us/)

The documented link and activity presets preserve the board's polarity bits.
`stealth` is an alias for `off`. [RTL8152B datasheet, §6.2](https://boardcon.com/download/RTL8152B-VB-CG.pdf) · [RTL8153 datasheet, §6.2](https://www.olimex.com/Products/USB-Modules/Ethernet/USB-GIGABIT/resources/rtl8153.pdf) · [RTL8153B datasheet, §6.2](https://www.manuallib.com/download/DA71CC8BAF23E63B442AEA97F042F3FC.pdf) · [RTL8156BG(S) datasheet, §6.2](https://datasheet.lcsc.com/datasheet/pdf/eca7d8d1da81204f4a10b196c7546181.pdf)

```bash
./rtl815x-tool led status
./rtl815x-tool led set default
# Set only LED0 to the RTL8157 preset value
./rtl815x-tool reg write pla 0xdd8a 0x0220 16
```

### 7. Direct Register Access (Developer / Debugger Mode)
```bash
# Read PLA MAC ID register (0xC000)
./rtl815x-tool reg read pla 0xc000

# Read PHY register (0xA400 is MII BMCR)
./rtl815x-tool reg read phy 0xa400

# Hexdump PLA registers from 0xC000 to 0xC040
./rtl815x-tool reg dump pla 0xc000 0x40

# Read the RTL8157 U2-to-P3 control over the indirect IP interface
./rtl815x-tool reg read ip 0xc2c0

# Dump one 32-bit BMU register on RTL8157
./rtl815x-tool reg dump bmu 0x2300 4
```

---

## Technical Notes: How Endpoint 0 Userspace Control Works

### RTL8157 register access

The installed Realtek DriverKit extension 1.1.0 retains symbols for
`RTL8157::CheckHwId_Impl`, `phy_read_Impl`, `phy_write_Impl`,
`EEE_en_Impl`, and `rtl_u2p3_en_Impl`. Its PHY methods send 16-bit commands
through USB registers `0xd630–0xd634`. IP and BMU use a separate command
register at `0xd5dc` with a 32-bit data register. Each command polls the
busy bit and reports a timeout if the request does not finish. Register layout
and EEE modes follow the vendor driver. [Realtek’s driver source](https://github.com/awesometic/realtek-r8152-dkms/blob/c1034ab62ddc48383118598f43c3227fc53c309e/src/r8152.c).

### The macOS NCM Driver vs. Endpoint 0
When an RTL8156 adapter uses Apple's NCM driver, `AppleUSBNCMData` claims **USB Configuration 2 (CDC-NCM)** for standard driverless network traffic.

While the bulk data endpoints (EP1 IN / EP2 OUT) are owned by the kernel network stack, **USB Endpoint 0 (the Default Control Pipe)** remains accessible from userspace via `IOKitLib` and `IOUSBLib`.

Realtek controllers use vendor-specific control transfers on Endpoint 0 (`bRequest = 0x05`) to address their internal **On-Chip Peripheral (OCP)** bus:
* **OCP Register Read (`0xC0`, `0x05`)**: `bmRequestType=0xC0`, `wValue=register_address`, `wIndex=MCU_TYPE`.
* **OCP Register Write (`0x40`, `0x05`)**: `bmRequestType=0x40`, `wValue=register_address`, `wIndex=MCU_TYPE | BYTE_EN_MASK`.
* **GPHY Access**: Accessing MDIO/PHY registers is achieved by switching the base address register (`PLA_OCP_GPHY_BASE` at `0xE86C`) and reading through the GPHY aperture (`0xB000` + offset).

These transfers share the controller with the active network driver. Indirect
access uses multiple USB transfers, so concurrent tools or a driver power
transition can interfere with a register transaction.

---

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License (CC BY-NC-SA 4.0)](LICENSE).
