#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include "rtl_core.h"
#include "rtl_diag.h"
#include "rtl_tally.h"
#include "rtl_eee.h"
#include "rtl_power.h"
#include "rtl_led.h"

static void print_usage(const char *progname) {
    printf("Realtek RTL8156 / RTL815x macOS Diagnostics & Control CLI (v1.0.3)\n\n");
    printf("Usage:\n");
    printf("  %s [options] <command> [arguments]\n\n", progname);
    printf("Commands:\n");
    printf("  list, devices               List all connected Realtek USB Ethernet adapters\n");
    printf("  status, info                Show hardware overview, link speed, and PHY status (Default)\n");
    printf("  stats, tally [--json]       Display on-chip traffic and hardware error counters\n");
    printf("  tally-reset                 Reset hardware traffic and error counters to zero\n");
    printf("  watch, monitor [-n <sec>]   Live terminal dashboard showing real-time pps & error deltas\n");
    printf("  phy [--restart-an]          Detailed MII/PHY register dump and auto-negotiation matrix\n");
    printf("  eee [status|disable|enable] Inspect or toggle Energy Efficient Ethernet\n");
    printf("  power [status|no-sleep|default] Inspect or clear/set USB LPM and UPS controls\n");
    printf("  led [status|set <mode>]     Inspect or customize LED blink patterns (default, off, link-only)\n");
    printf("  reg read <mcu> <addr>       Directly read register (mcu: pla | usb | phy)\n");
    printf("  reg write <mcu> <addr> <val> [size] Directly write register (size: 8, 16, 32)\n");
    printf("  reg dump <mcu> <addr> <cnt> Hexdump a range of registers\n\n");
    printf("Device Selection Options:\n");
    printf("  -i <interface>              Select adapter by macOS BSD interface name (e.g., -i en12)\n");
    printf("  -d <index>                  Select adapter by device index (0, 1, ...)\n");
    printf("  -n <seconds>                Watch interval in seconds (0.2 to 3600)\n");
    printf("  -h, --help                  Display this help message\n\n");
    printf("Examples:\n");
    printf("  %s status                   # Diagnostic overview of first connected adapter\n", progname);
    printf("  %s stats --json             # Output hardware error counters in JSON\n", progname);
    printf("  %s watch -n 1.0             # Live packet and drop monitor\n", progname);
    printf("  %s eee disable              # Disable EEE\n", progname);
    printf("  %s led set off              # Turn off adapter LEDs (stealth mode)\n", progname);
}

static void cmd_list(rtl_dev_t *devices, int count) {
    printf("\nConnected Realtek USB Network Adapters (%d found):\n", count);
    printf("-----------------------------------------------------------------\n");
    for (int i = 0; i < count; i++) {
        printf("[%d] %-10s | %-12s | MAC: %s | %s | %s\n",
               i,
               devices[i].bsd_name,
               devices[i].version_info->name,
               devices[i].mac_address[0] ? devices[i].mac_address : "dont know",
               devices[i].usb_speed[0] ? devices[i].usb_speed : "dont know",
               devices[i].product_name);
    }
    printf("-----------------------------------------------------------------\n\n");
}

static int cmd_reg_read(rtl_dev_t *dev, const char *mcu_str, const char *addr_str) {
    if (!dev || !mcu_str || !addr_str) {
        fprintf(stderr, "Usage: reg read <pla|usb|phy> <addr_hex>\n");
        return -1;
    }
    if (rtl_open_device(dev) != 0) return -1;

    uint32_t addr = (uint32_t)strtoul(addr_str, NULL, 16);
    if (strcasecmp(mcu_str, "phy") == 0) {
        uint16_t val;
        if (rtl_ocp_reg_read(dev, (uint16_t)addr, &val) != 0) return -1;
        printf("[PHY 0x%04x] = 0x%04x (%u)\n", addr, val, val);
    } else if (strcasecmp(mcu_str, "usb") == 0) {
        uint32_t val;
        if (rtl_ocp_read_dword(dev, MCU_TYPE_USB, (uint16_t)addr, &val) != 0) return -1;
        uint16_t wval;
        if (rtl_ocp_read_word(dev, MCU_TYPE_USB, (uint16_t)addr, &wval) != 0) return -1;
        uint8_t bval;
        if (rtl_ocp_read_byte(dev, MCU_TYPE_USB, (uint16_t)addr, &bval) != 0) return -1;
        printf("[USB 0x%04x] 32-bit: 0x%08x | 16-bit: 0x%04x | 8-bit: 0x%02x\n", addr, val, wval, bval);
    } else if (strcasecmp(mcu_str, "pla") == 0) {
        uint32_t val;
        if (rtl_ocp_read_dword(dev, MCU_TYPE_PLA, (uint16_t)addr, &val) != 0) return -1;
        uint16_t wval;
        if (rtl_ocp_read_word(dev, MCU_TYPE_PLA, (uint16_t)addr, &wval) != 0) return -1;
        uint8_t bval;
        if (rtl_ocp_read_byte(dev, MCU_TYPE_PLA, (uint16_t)addr, &bval) != 0) return -1;
        printf("[PLA 0x%04x] 32-bit: 0x%08x | 16-bit: 0x%04x | 8-bit: 0x%02x\n", addr, val, wval, bval);
    } else {
        fprintf(stderr, "Unknown MCU type '%s'. Use 'pla', 'usb', or 'phy'.\n", mcu_str);
        return -1;
    }
    return 0;
}

static int cmd_reg_write(rtl_dev_t *dev, const char *mcu_str, const char *addr_str, const char *val_str, const char *size_str) {
    if (!dev || !mcu_str || !addr_str || !val_str) {
        fprintf(stderr, "Usage: reg write <pla|usb|phy> <addr_hex> <val_hex> [8|16|32]\n");
        return -1;
    }
    if (rtl_open_device(dev) != 0) return -1;

    uint32_t addr = (uint32_t)strtoul(addr_str, NULL, 16);
    uint32_t val = (uint32_t)strtoul(val_str, NULL, 16);
    int size = size_str ? atoi(size_str) : 16;

    if (strcasecmp(mcu_str, "phy") == 0) {
        if (rtl_ocp_reg_write(dev, (uint16_t)addr, (uint16_t)val) != 0) return -1;
        printf("[+] Written [PHY 0x%04x] <= 0x%04x\n", addr, (uint16_t)val);
    } else if (strcasecmp(mcu_str, "usb") == 0 || strcasecmp(mcu_str, "pla") == 0) {
        uint16_t mcu = (strcasecmp(mcu_str, "usb") == 0) ? MCU_TYPE_USB : MCU_TYPE_PLA;
        if (size == 8) {
            if (rtl_ocp_write_byte(dev, mcu, (uint16_t)addr, (uint8_t)val) != 0) return -1;
        } else if (size == 32) {
            if (rtl_ocp_write_dword(dev, mcu, (uint16_t)addr, val) != 0) return -1;
        } else {
            if (rtl_ocp_write_word(dev, mcu, (uint16_t)addr, (uint16_t)val) != 0) return -1;
        }
        printf("[+] Written [%s 0x%04x] <= 0x%x (%d-bit)\n",
               (mcu == MCU_TYPE_USB) ? "USB" : "PLA", addr, val, size);
    } else {
        fprintf(stderr, "Unknown MCU type '%s'. Use 'pla', 'usb', or 'phy'.\n", mcu_str);
        return -1;
    }
    return 0;
}

static int cmd_reg_dump(rtl_dev_t *dev, const char *mcu_str, const char *addr_str, const char *cnt_str) {
    if (!dev || !mcu_str || !addr_str) {
        fprintf(stderr, "Usage: reg dump <pla|usb> <start_addr_hex> [count_hex]\n");
        return -1;
    }
    if (rtl_open_device(dev) != 0) return -1;

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
        return -1;
    }
    printf("\nHexdump of %s registers [0x%04x - 0x%04x]:\n",
           (mcu == MCU_TYPE_USB) ? "USB" : "PLA", start, start + count - 1);
    printf("Offset  | 00 01 02 03  04 05 06 07  08 09 0A 0B  0C 0D 0E 0F\n");
    printf("--------+-------------------------------------------------\n");

    for (uint32_t off = start; off < start + count; off += 16) {
        printf("0x%04x  | ", off);
        for (int i = 0; i < 16; i += 4) {
            uint32_t val;
            if (rtl_ocp_read_dword(dev, mcu, (uint16_t)(off + i), &val) != 0) return -1;
            uint8_t *b = (uint8_t *)&val;
            printf("%02x %02x %02x %02x  ", b[0], b[1], b[2], b[3]);
        }
        printf("\n");
    }
    printf("\n");
    return 0;
}

static int parse_device_index(const char *arg, int *out) {
    char *end;
    errno = 0;
    long value = strtol(arg, &end, 10);
    if (errno || end == arg || *end || value < 0 || value > INT_MAX) {
        fprintf(stderr, "Invalid device index '%s': expected a non-negative integer.\n", arg);
        return -1;
    }
    *out = (int)value;
    return 0;
}

static int parse_interval(const char *arg, double *out) {
    char *end;
    errno = 0;
    double value = strtod(arg, &end);
    if (errno || end == arg || *end || !isfinite(value) || value < 0.2 || value > 3600.0) {
        fprintf(stderr, "Invalid watch interval '%s': expected 0.2 to 3600 seconds.\n", arg);
        return -1;
    }
    *out = value;
    return 0;
}

static int parse_watch_args(int argc, char **argv, double *interval) {
    for (int i = 0; i < argc; i++) {
        const char *value = argv[i];
        if (!strcmp(value, "-n") || !strcmp(value, "--interval")) {
            if (++i == argc) {
                fprintf(stderr, "Watch interval option requires a value.\n");
                return -1;
            }
            value = argv[i];
        } else if (!strncmp(value, "--interval=", 11)) {
            value += 11;
        } else if (!strncmp(value, "-n", 2)) {
            value += 2;
        }
        if (parse_interval(value, interval) != 0) return -1;
    }
    return 0;
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
                if (parse_device_index(optarg, &dev_index) != 0) return 1;
                break;
            case 'n':
                if (parse_interval(optarg, &interval_sec) != 0) return 1;
                break;
            case 'j':
                json_mode = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                return 1;
        }
    }

    const char *command = "status";
    if (optind < argc) {
        command = argv[optind];
    }

    if ((strcasecmp(command, "watch") == 0 || strcasecmp(command, "monitor") == 0) &&
        parse_watch_args(argc - optind - 1, argv + optind + 1, &interval_sec) != 0) return 1;

    rtl_dev_t *devices = NULL;
    int dev_count = 0;
    if (rtl_find_devices(&devices, &dev_count) != 0) {
        fprintf(stderr, "Failed to enumerate USB adapters.\n");
        return 1;
    }
    if (dev_count == 0) {
        fprintf(stderr, "No supported Realtek USB Ethernet adapters detected.\n");
        rtl_free_devices(devices, dev_count);
        return 1;
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

    int result = 0;
    if (strcasecmp(command, "status") == 0 || strcasecmp(command, "info") == 0) {
        result = rtl_diag_print_status(target);
    } else if (strcasecmp(command, "stats") == 0 || strcasecmp(command, "tally") == 0) {
        /* Check if --json or --reset was passed as next arg */
        for (int i = optind + 1; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) {
                json_mode = true;
            } else if (strcmp(argv[i], "--reset") == 0) {
                result = rtl_tally_reset(target);
                rtl_free_devices(devices, dev_count);
                if (result != 0) fprintf(stderr, "Error: Counter reset failed.\n");
                return result == 0 ? 0 : 1;
            }
        }
        result = rtl_tally_print_stats(target, json_mode);
    } else if (strcasecmp(command, "tally-reset") == 0) {
        result = rtl_tally_reset(target);
    } else if (strcasecmp(command, "watch") == 0 || strcasecmp(command, "monitor") == 0) {
        result = rtl_tally_watch(target, interval_sec);
    } else if (strcasecmp(command, "phy") == 0) {
        bool restart_an = false;
        for (int i = optind + 1; i < argc; i++) {
            if (strcmp(argv[i], "--restart-an") == 0 || strcmp(argv[i], "restart") == 0) {
                restart_an = true;
            }
        }
        if (restart_an) {
            result = rtl_diag_restart_an(target);
        } else {
            result = rtl_diag_print_phy(target);
        }
    } else if (strcasecmp(command, "eee") == 0) {
        const char *sub = (optind + 1 < argc) ? argv[optind + 1] : "status";
        if (strcasecmp(sub, "disable") == 0 || strcasecmp(sub, "--disable") == 0 || strcasecmp(sub, "off") == 0) {
            result = rtl_eee_set(target, false);
        } else if (strcasecmp(sub, "enable") == 0 || strcasecmp(sub, "--enable") == 0 || strcasecmp(sub, "on") == 0) {
            result = rtl_eee_set(target, true);
        } else {
            result = rtl_eee_print_status(target);
        }
    } else if (strcasecmp(command, "power") == 0) {
        const char *sub = (optind + 1 < argc) ? argv[optind + 1] : "status";
        if (strcasecmp(sub, "no-sleep") == 0 || strcasecmp(sub, "--no-sleep") == 0) {
            result = rtl_power_set_no_sleep(target, true);
        } else if (strcasecmp(sub, "default") == 0 || strcasecmp(sub, "--default") == 0) {
            result = rtl_power_set_no_sleep(target, false);
        } else {
            result = rtl_power_print_status(target);
        }
    } else if (strcasecmp(command, "led") == 0) {
        const char *sub = (optind + 1 < argc) ? argv[optind + 1] : "status";
        if (strcasecmp(sub, "set") == 0) {
            const char *mode = (optind + 2 < argc) ? argv[optind + 2] : "default";
            result = rtl_led_set_mode(target, mode);
        } else if (optind + 1 < argc && strcasecmp(sub, "status") != 0) {
            result = rtl_led_set_mode(target, sub);
        } else {
            result = rtl_led_print_status(target);
        }
    } else if (strcasecmp(command, "reg") == 0) {
        const char *action = (optind + 1 < argc) ? argv[optind + 1] : "help";
        if (strcasecmp(action, "read") == 0) {
            const char *mcu = (optind + 2 < argc) ? argv[optind + 2] : NULL;
            const char *addr = (optind + 3 < argc) ? argv[optind + 3] : NULL;
            result = cmd_reg_read(target, mcu, addr);
        } else if (strcasecmp(action, "write") == 0) {
            const char *mcu = (optind + 2 < argc) ? argv[optind + 2] : NULL;
            const char *addr = (optind + 3 < argc) ? argv[optind + 3] : NULL;
            const char *val = (optind + 4 < argc) ? argv[optind + 4] : NULL;
            const char *sz = (optind + 5 < argc) ? argv[optind + 5] : NULL;
            result = cmd_reg_write(target, mcu, addr, val, sz);
        } else if (strcasecmp(action, "dump") == 0) {
            const char *mcu = (optind + 2 < argc) ? argv[optind + 2] : NULL;
            const char *addr = (optind + 3 < argc) ? argv[optind + 3] : NULL;
            const char *cnt = (optind + 4 < argc) ? argv[optind + 4] : NULL;
            result = cmd_reg_dump(target, mcu, addr, cnt);
        } else {
            fprintf(stderr, "Usage: reg <read|write|dump> ...\n");
            result = -1;
        }
    } else {
        fprintf(stderr, "Unknown command '%s'. Run '%s --help' for usage.\n", command, argv[0]);
        rtl_free_devices(devices, dev_count);
        return 1;
    }

    rtl_free_devices(devices, dev_count);
    if (result != 0) fprintf(stderr, "Error: Command '%s' failed.\n", command);
    return result == 0 ? 0 : 1;
}
