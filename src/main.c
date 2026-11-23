#include <stdio.h>
#include <ctype.h>
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
    printf("Realtek RTL8156 / RTL8157 / RTL815x macOS Diagnostics & Control CLI (v1.0.5)\n\n");
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
    printf("  power [status|no-sleep|default] Inspect power controls and configure link power saving\n");
    printf("  led [status|set <mode>]     Read LED selectors or apply a preset or raw selector\n");
    printf("  reg read <mcu> <addr>       Directly read register (pla | usb | phy | ip | bmu)\n");
    printf("  reg write <mcu> <addr> <val> [size] Directly write register (size: 8, 16, 32)\n");
    printf("  reg dump <mcu> <addr> <cnt> Hexdump a range of registers\n\n");
    printf("  RTL8157 IP and BMU registers are 32-bit. PHY registers are 16-bit.\n\n");
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
        printf("[%d] %-10s | %s | Revision: ", i, devices[i].bsd_name, devices[i].version_info->name);
        if (devices[i].revision_known) printf("0x%04x", devices[i].version_raw);
        else printf("dont know");
        printf(" | MAC: %s | %s | %s\n",
               devices[i].mac_address[0] ? devices[i].mac_address : "dont know",
               devices[i].usb_speed[0] ? devices[i].usb_speed : "dont know",
               devices[i].product_name);
    }
    printf("-----------------------------------------------------------------\n\n");
}

enum reg_space { REG_PLA, REG_USB, REG_PHY, REG_IP, REG_BMU };

static const char *const reg_names[] = { "PLA", "USB", "PHY", "IP", "BMU" };

static int parse_space(rtl_dev_t *dev, const char *name, enum reg_space *space) {
    for (size_t i = 0; i < sizeof(reg_names) / sizeof(*reg_names); i++) {
        if (name && strcasecmp(name, reg_names[i]) == 0) {
            *space = (enum reg_space)i;
            if ((i == REG_IP || i == REG_BMU) && dev->version_info->family != CHIP_RTL8157) {
                fprintf(stderr, "The %s register space requires an RTL8157.\n", name);
                return -1;
            }
            return 0;
        }
    }
    fprintf(stderr, "Expected register space pla, usb, phy, ip, or bmu.\n");
    return -1;
}

static int parse_hex(const char *arg, uint32_t max, uint32_t *out) {
    char *end;
    errno = 0;
    unsigned long value = arg ? strtoul(arg, &end, 16) : 0;
    if (!arg || !isxdigit((unsigned char)*arg) || *end || errno || value > max) {
        fprintf(stderr, "Invalid hexadecimal register argument '%s'.\n", arg ? arg : "");
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static int read_register(rtl_dev_t *dev, enum reg_space space, uint16_t addr, uint32_t *out) {
    if (space == REG_PHY) {
        uint16_t value;
        if (rtl_ocp_reg_read(dev, addr, &value) != 0) return -1;
        *out = value;
        return 0;
    }
    if (space == REG_IP || space == REG_BMU)
        return rtl_adv_read(dev, space == REG_IP ? ADV_CMD_IP : ADV_CMD_BMU, addr, out);
    return rtl_ocp_read_dword(dev, space == REG_USB ? MCU_TYPE_USB : MCU_TYPE_PLA, addr, out);
}

static int cmd_reg_read(rtl_dev_t *dev, const char *name, const char *address) {
    enum reg_space space;
    uint32_t addr, val;
    if (parse_space(dev, name, &space) != 0 || parse_hex(address, UINT16_MAX, &addr) != 0) return -1;
    if ((space == REG_PHY && (addr & 1)) ||
        ((space == REG_IP || space == REG_BMU) && (addr & 3))) {
        fprintf(stderr, "Unaligned %s register address.\n", name);
        return -1;
    }
    if (rtl_open_device(dev) != 0 || read_register(dev, space, addr, &val) != 0) return -1;
    if (space == REG_PLA || space == REG_USB) {
        printf("[%s 0x%04x] 32-bit: 0x%08x | 16-bit: 0x%04x | 8-bit: 0x%02x\n",
               space == REG_PLA ? "PLA" : "USB", addr, val,
               (val >> ((addr & 2) * 8)) & 0xffff, (val >> ((addr & 3) * 8)) & 0xff);
    } else {
        printf("[%s 0x%04x] = 0x%0*x (%u)\n", reg_names[space], addr,
               space == REG_PHY ? 4 : 8, val, val);
    }
    return 0;
}

static int cmd_reg_write(rtl_dev_t *dev, const char *name, const char *address,
                         const char *value, const char *width) {
    enum reg_space space;
    uint32_t addr, val;
    if (parse_space(dev, name, &space) != 0 || parse_hex(address, UINT16_MAX, &addr) != 0) return -1;
    bool advanced = space == REG_IP || space == REG_BMU;
    if (!width) width = advanced ? "32" : "16";
    int size;
    if (strcmp(width, "8") == 0) size = 8;
    else if (strcmp(width, "16") == 0) size = 16;
    else if (strcmp(width, "32") == 0) size = 32;
    else {
        fprintf(stderr, "Expected register width 8, 16, or 32.\n");
        return -1;
    }
    if ((advanced && size != 32) || (space == REG_PHY && size != 16) ||
        addr % (size / 8) != 0) {
        fprintf(stderr, "Invalid width or alignment for a %s register write.\n", name);
        return -1;
    }
    if (parse_hex(value, UINT32_MAX >> (32 - size), &val) != 0 || rtl_open_device(dev) != 0) return -1;
    int rc;
    uint16_t type = space == REG_USB ? MCU_TYPE_USB : MCU_TYPE_PLA;
    if (advanced) rc = rtl_adv_write(dev, space == REG_IP ? ADV_CMD_IP : ADV_CMD_BMU, addr, val);
    else if (space == REG_PHY) rc = rtl_ocp_reg_write(dev, addr, val);
    else if (size == 8) rc = rtl_ocp_write_byte(dev, type, addr, val);
    else if (size == 16) rc = rtl_ocp_write_word(dev, type, addr, val);
    else rc = rtl_ocp_write_dword(dev, type, addr, val);
    if (rc != 0) return -1;
    if (space == REG_PHY) {
        printf("[+] Written [PHY 0x%04x] <= 0x%04x\n", addr, val);
        return 0;
    }
    printf("[+] Written [%s 0x%04x] <= 0x%x (%d-bit)\n", reg_names[space], addr, val, size);
    return 0;
}

static int cmd_reg_dump(rtl_dev_t *dev, const char *name, const char *address, const char *length) {
    enum reg_space space;
    uint32_t start, count = 64;
    if (parse_space(dev, name, &space) != 0 || parse_hex(address, UINT16_MAX, &start) != 0 ||
        (length && parse_hex(length, UINT32_MAX, &count) != 0)) return -1;
    unsigned step = space == REG_PHY ? 2 : 4;
    start &= ~(step - 1);
    if (count > 512) count = 512;
    count = (count + step - 1) & ~(step - 1);
    if (!count || start + count > 0x10000) {
        fprintf(stderr, "Invalid register dump range.\n");
        return -1;
    }
    if (rtl_open_device(dev) != 0) return -1;
    printf("\nHexdump of %s registers [0x%04x - 0x%04x]:\n", reg_names[space], start, start + count - 1);
    printf("Offset  | 00 01 02 03  04 05 06 07  08 09 0A 0B  0C 0D 0E 0F\n");
    printf("--------+-------------------------------------------------\n");
    for (uint32_t off = 0; off < count; off += step) {
        uint32_t val;
        if (read_register(dev, space, start + off, &val) != 0) return -1;
        if (off % 16 == 0) printf("0x%04x  | ", start + off);
        for (unsigned byte = 0; byte < step; byte++)
            printf("%02x ", (val >> (byte * 8)) & 0xff);
        if ((off + step) % 4 == 0) putchar(' ');
        if ((off + step) % 16 == 0 || off + step == count) putchar('\n');
    }
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
