#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "rtl_core.h"
#include "rtl_diag.h"
#include "rtl_tally.h"
#include "rtl_eee.h"
#include "rtl_power.h"
#include "rtl_led.h"

static void print_usage(const char *progname) {
    printf("Realtek RTL8156 / RTL815x macOS Diagnostics & Control CLI (v1.0.0)\n\n");
    printf("Usage:\n");
    printf("  %s [options] <command> [arguments]\n\n", progname);
    printf("Commands:\n");
    printf("  list, devices               List all connected Realtek USB Ethernet adapters\n");
    printf("  status, info                Show hardware overview, link speed, and PHY status (Default)\n");
    printf("  stats, tally [--json]       Display on-chip traffic and hardware error counters\n");
    printf("  tally-reset                 Reset hardware traffic and error counters to zero\n");
    printf("  watch, monitor [-n <sec>]   Live terminal dashboard showing real-time pps & error deltas\n");
    printf("  phy [--restart-an]          Detailed MII/PHY register dump and auto-negotiation matrix\n");
    printf("  eee [status|disable|enable] Inspect or toggle Energy Efficient Ethernet (fixes latency/drops)\n");
    printf("  power [status|no-sleep]     Inspect or configure USB LPM / UPS continuous power mode\n");
    printf("  led [status|set <mode>]     Inspect or customize LED blink patterns (default, off, link-only)\n");
    printf("  reg read <mcu> <addr>       Directly read register (mcu: pla | usb | phy)\n");
    printf("  reg write <mcu> <addr> <val> [size] Directly write register (size: 8, 16, 32)\n");
    printf("  reg dump <mcu> <addr> <cnt> Hexdump a range of registers\n\n");
    printf("Device Selection Options:\n");
    printf("  -i <interface>              Select adapter by macOS BSD interface name (e.g., -i en12)\n");
    printf("  -d <index>                  Select adapter by device index (0, 1, ...)\n");
    printf("  -n <seconds>                Update interval for the watch command\n");
    printf("  -h, --help                  Display this help message\n\n");
    printf("Examples:\n");
    printf("  %s status                   # Diagnostic overview of first connected adapter\n", progname);
    printf("  %s stats --json             # Output hardware error counters in JSON\n", progname);
    printf("  %s watch -n 1.0             # Live packet and drop monitor\n", progname);
    printf("  %s eee disable              # Disable EEE to prevent sleep drops & latency spikes\n", progname);
    printf("  %s led set off              # Turn off adapter LEDs (stealth mode)\n", progname);
}

static void cmd_list(rtl_dev_t *devices, int count) {
    printf("\nConnected Realtek USB Network Adapters (%d found):\n", count);
    printf("-----------------------------------------------------------------\n");
    for (int i = 0; i < count; i++) {
        const char *usb_speed = "USB 2.0 (480M)";
        if (devices[i].usb_link_speed_bps >= 10000000000ULL) {
            usb_speed = "USB 3.2 (10G)";
        } else if (devices[i].usb_link_speed_bps >= 5000000000ULL) {
            usb_speed = "USB 3.2 (5G)";
        }
        printf("[%d] %-10s | %-12s | MAC: %02x:%02x:%02x:%02x:%02x:%02x | %s | %s\n",
               i,
               devices[i].bsd_name,
               devices[i].version_info->name,
               devices[i].mac_addr[0], devices[i].mac_addr[1], devices[i].mac_addr[2],
               devices[i].mac_addr[3], devices[i].mac_addr[4], devices[i].mac_addr[5],
               usb_speed,
               devices[i].product_name);
    }
    printf("-----------------------------------------------------------------\n\n");
}

static void cmd_reg_read(rtl_dev_t *dev, const char *mcu_str, const char *addr_str) {
    if (!dev || !mcu_str || !addr_str) {
        fprintf(stderr, "Usage: reg read <pla|usb|phy> <addr_hex>\n");
        return;
    }
    if (rtl_open_device(dev) != 0) return;

    uint32_t addr = (uint32_t)strtoul(addr_str, NULL, 16);
    if (strcasecmp(mcu_str, "phy") == 0) {
        uint16_t val = rtl_ocp_reg_read(dev, (uint16_t)addr);
        printf("[PHY 0x%04x] = 0x%04x (%u)\n", addr, val, val);
    } else if (strcasecmp(mcu_str, "usb") == 0) {
        uint32_t val = rtl_ocp_read_dword(dev, MCU_TYPE_USB, (uint16_t)addr);
        uint16_t wval = rtl_ocp_read_word(dev, MCU_TYPE_USB, (uint16_t)addr);
        uint8_t bval = rtl_ocp_read_byte(dev, MCU_TYPE_USB, (uint16_t)addr);
        printf("[USB 0x%04x] 32-bit: 0x%08x | 16-bit: 0x%04x | 8-bit: 0x%02x\n", addr, val, wval, bval);
    } else if (strcasecmp(mcu_str, "pla") == 0) {
        uint32_t val = rtl_ocp_read_dword(dev, MCU_TYPE_PLA, (uint16_t)addr);
        uint16_t wval = rtl_ocp_read_word(dev, MCU_TYPE_PLA, (uint16_t)addr);
        uint8_t bval = rtl_ocp_read_byte(dev, MCU_TYPE_PLA, (uint16_t)addr);
        printf("[PLA 0x%04x] 32-bit: 0x%08x | 16-bit: 0x%04x | 8-bit: 0x%02x\n", addr, val, wval, bval);
    } else {
        fprintf(stderr, "Unknown MCU type '%s'. Use 'pla', 'usb', or 'phy'.\n", mcu_str);
    }
}

static void cmd_reg_write(rtl_dev_t *dev, const char *mcu_str, const char *addr_str, const char *val_str, const char *size_str) {
    if (!dev || !mcu_str || !addr_str || !val_str) {
        fprintf(stderr, "Usage: reg write <pla|usb|phy> <addr_hex> <val_hex> [8|16|32]\n");
        return;
    }
    if (rtl_open_device(dev) != 0) return;

    uint32_t addr = (uint32_t)strtoul(addr_str, NULL, 16);
    uint32_t val = (uint32_t)strtoul(val_str, NULL, 16);
    int size = size_str ? atoi(size_str) : 16;

    if (strcasecmp(mcu_str, "phy") == 0) {
        rtl_ocp_reg_write(dev, (uint16_t)addr, (uint16_t)val);
        printf("[+] Written [PHY 0x%04x] <= 0x%04x\n", addr, (uint16_t)val);
    } else if (strcasecmp(mcu_str, "usb") == 0 || strcasecmp(mcu_str, "pla") == 0) {
        uint16_t mcu = (strcasecmp(mcu_str, "usb") == 0) ? MCU_TYPE_USB : MCU_TYPE_PLA;
        if (size == 8) {
            rtl_ocp_write_byte(dev, mcu, (uint16_t)addr, (uint8_t)val);
        } else if (size == 32) {
            rtl_ocp_write_dword(dev, mcu, (uint16_t)addr, val);
        } else {
            rtl_ocp_write_word(dev, mcu, (uint16_t)addr, (uint16_t)val);
        }
        printf("[+] Written [%s 0x%04x] <= 0x%x (%d-bit)\n",
               (mcu == MCU_TYPE_USB) ? "USB" : "PLA", addr, val, size);
    } else {
        fprintf(stderr, "Unknown MCU type '%s'. Use 'pla', 'usb', or 'phy'.\n", mcu_str);
    }
}

static void cmd_reg_dump(rtl_dev_t *dev, const char *mcu_str, const char *addr_str, const char *cnt_str) {
    if (!dev || !mcu_str || !addr_str) {
        fprintf(stderr, "Usage: reg dump <pla|usb> <start_addr_hex> [count_hex]\n");
        return;
    }
    if (rtl_open_device(dev) != 0) return;

    uint32_t start = (uint32_t)strtoul(addr_str, NULL, 16) & ~3;
    uint32_t count = cnt_str ? (uint32_t)strtoul(cnt_str, NULL, 16) : 64;
    if (count > 512) count = 512;
    count = (count + 3) & ~3;

    uint16_t mcu;
    if (strcasecmp(mcu_str, "usb") == 0) {
        mcu = MCU_TYPE_USB;
    } else if (strcasecmp(mcu_str, "pla") == 0) {
        mcu = MCU_TYPE_PLA;
    } else {
        fprintf(stderr, "Unknown MCU type '%s'. Use 'pla' or 'usb'.\n", mcu_str);
        return;
    }
    printf("\nHexdump of %s registers [0x%04x - 0x%04x]:\n",
           (mcu == MCU_TYPE_USB) ? "USB" : "PLA", start, start + count - 1);
    printf("Offset  | 00 01 02 03  04 05 06 07  08 09 0A 0B  0C 0D 0E 0F\n");
    printf("--------+-------------------------------------------------\n");

    for (uint32_t off = start; off < start + count; off += 16) {
        printf("0x%04x  | ", off);
        for (int i = 0; i < 16; i += 4) {
            uint32_t val = rtl_ocp_read_dword(dev, mcu, (uint16_t)(off + i));
            uint8_t *b = (uint8_t *)&val;
            printf("%02x %02x %02x %02x  ", b[0], b[1], b[2], b[3]);
        }
        printf("\n");
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    char bsd_name[32] = "";
    int dev_index = -1;
    double interval_sec = 1.0;
    bool json_mode = false;

    static struct option long_options[] = {
        {"interface", required_argument, 0, 'i'},
        {"device",    required_argument, 0, 'd'},
        {"interval",  required_argument, 0, 'n'},
        {"json",      no_argument,       0, 'j'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "+i:d:n:jh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                strncpy(bsd_name, optarg, sizeof(bsd_name) - 1);
                break;
            case 'd':
                dev_index = atoi(optarg);
                break;
            case 'n':
                interval_sec = atof(optarg);
                break;
            case 'j':
                json_mode = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                break;
        }
    }

    rtl_dev_t *devices = NULL;
    int dev_count = 0;
    if (rtl_find_devices(&devices, &dev_count) != 0 || dev_count == 0) {
        fprintf(stderr, "No supported Realtek USB Ethernet adapters detected.\n");
        return 1;
    }

    const char *command = "status";
    if (optind < argc) {
        command = argv[optind];
    }

    if (strcasecmp(command, "list") == 0 || strcasecmp(command, "devices") == 0) {
        cmd_list(devices, dev_count);
        rtl_free_devices(devices, dev_count);
        return 0;
    }

    rtl_dev_t *target = rtl_select_device(devices, dev_count, bsd_name, dev_index);
    if (!target) {
        fprintf(stderr, "Error: Selected device '%s' (index %d) not found.\n", bsd_name, dev_index);
        cmd_list(devices, dev_count);
        rtl_free_devices(devices, dev_count);
        return 1;
    }

    if (strcasecmp(command, "status") == 0 || strcasecmp(command, "info") == 0) {
        rtl_diag_print_status(target);
    } else if (strcasecmp(command, "stats") == 0 || strcasecmp(command, "tally") == 0) {
        /* Check if --json or --reset was passed as next arg */
        for (int i = optind + 1; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) {
                json_mode = true;
            } else if (strcmp(argv[i], "--reset") == 0) {
                rtl_tally_reset(target);
                rtl_free_devices(devices, dev_count);
                return 0;
            }
        }
        rtl_tally_print_stats(target, json_mode);
    } else if (strcasecmp(command, "tally-reset") == 0) {
        rtl_tally_reset(target);
    } else if (strcasecmp(command, "watch") == 0 || strcasecmp(command, "monitor") == 0) {
        if (optind + 1 < argc && argv[optind + 1][0] != '-') {
            interval_sec = atof(argv[optind + 1]);
        }
        rtl_tally_watch(target, interval_sec);
    } else if (strcasecmp(command, "phy") == 0) {
        bool restart_an = false;
        for (int i = optind + 1; i < argc; i++) {
            if (strcmp(argv[i], "--restart-an") == 0 || strcmp(argv[i], "restart") == 0) {
                restart_an = true;
            }
        }
        if (restart_an) {
            rtl_diag_restart_an(target);
        } else {
            rtl_diag_print_phy(target);
        }
    } else if (strcasecmp(command, "eee") == 0) {
        const char *sub = (optind + 1 < argc) ? argv[optind + 1] : "status";
        if (strcasecmp(sub, "disable") == 0 || strcasecmp(sub, "--disable") == 0 || strcasecmp(sub, "off") == 0) {
            rtl_eee_set(target, false);
        } else if (strcasecmp(sub, "enable") == 0 || strcasecmp(sub, "--enable") == 0 || strcasecmp(sub, "on") == 0) {
            rtl_eee_set(target, true);
        } else {
            rtl_eee_print_status(target);
        }
    } else if (strcasecmp(command, "power") == 0) {
        const char *sub = (optind + 1 < argc) ? argv[optind + 1] : "status";
        if (strcasecmp(sub, "no-sleep") == 0 || strcasecmp(sub, "--no-sleep") == 0) {
            rtl_power_set_no_sleep(target, true);
        } else if (strcasecmp(sub, "default") == 0 || strcasecmp(sub, "--default") == 0) {
            rtl_power_set_no_sleep(target, false);
        } else {
            rtl_power_print_status(target);
        }
    } else if (strcasecmp(command, "led") == 0) {
        const char *sub = (optind + 1 < argc) ? argv[optind + 1] : "status";
        if (strcasecmp(sub, "set") == 0) {
            const char *mode = (optind + 2 < argc) ? argv[optind + 2] : "default";
            rtl_led_set_mode(target, mode);
        } else if (optind + 1 < argc && strcasecmp(sub, "status") != 0) {
            rtl_led_set_mode(target, sub);
        } else {
            rtl_led_print_status(target);
        }
    } else if (strcasecmp(command, "reg") == 0) {
        const char *action = (optind + 1 < argc) ? argv[optind + 1] : "help";
        if (strcasecmp(action, "read") == 0) {
            const char *mcu = (optind + 2 < argc) ? argv[optind + 2] : NULL;
            const char *addr = (optind + 3 < argc) ? argv[optind + 3] : NULL;
            cmd_reg_read(target, mcu, addr);
        } else if (strcasecmp(action, "write") == 0) {
            const char *mcu = (optind + 2 < argc) ? argv[optind + 2] : NULL;
            const char *addr = (optind + 3 < argc) ? argv[optind + 3] : NULL;
            const char *val = (optind + 4 < argc) ? argv[optind + 4] : NULL;
            const char *sz = (optind + 5 < argc) ? argv[optind + 5] : NULL;
            cmd_reg_write(target, mcu, addr, val, sz);
        } else if (strcasecmp(action, "dump") == 0) {
            const char *mcu = (optind + 2 < argc) ? argv[optind + 2] : NULL;
            const char *addr = (optind + 3 < argc) ? argv[optind + 3] : NULL;
            const char *cnt = (optind + 4 < argc) ? argv[optind + 4] : NULL;
            cmd_reg_dump(target, mcu, addr, cnt);
        } else {
            fprintf(stderr, "Usage: reg <read|write|dump> ...\n");
        }
    } else {
        fprintf(stderr, "Unknown command '%s'. Run '%s --help' for usage.\n", command, argv[0]);
        rtl_free_devices(devices, dev_count);
        return 1;
    }

    rtl_free_devices(devices, dev_count);
    return 0;
}
