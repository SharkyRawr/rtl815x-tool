/* Exercise the real CLI and register access against a fake USB endpoint. */
#define main rtl_cli_main
#include "../src/main.c"
#undef main
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

enum { PLA, USB, PHY, IP, BMU };
static uint8_t registers[5][65536];
static bool hold_command;
static int busy_reads;

static uint16_t get_word(int bank, uint16_t addr) {
    return registers[bank][addr] | ((uint16_t)registers[bank][addr + 1] << 8);
}
static uint16_t page;
static uint16_t mmd_reg, mmd_ctrl, mmd_an[64];
static int transfers, fail_at, short_at, enumerations;
static int discovery_count, discovery_error;
static double slept_seconds;
static int sleep_calls, stop_after;
static rtl_dev_t fixture;

static IOReturn request(void *self, IOUSBDevRequest *req) {
    (void)self;
    transfers++;
    assert(req->bRequest == 0x05);
    assert(req->bmRequestType == 0xc0 || req->bmRequestType == 0x40);
    if (transfers == fail_at) return kIOReturnError;
    req->wLenDone = req->wLength;
    if (transfers == short_at) {
        req->wLenDone--;
        return kIOReturnSuccess;
    }
    int bank = (req->wIndex & 0x100) ? 0 : 1;
    uint16_t address = req->wValue;
    if (fixture.version_info && fixture.version_info->family == CHIP_RTL8157)
        assert(bank != PLA || (address != 0xe86c && (address & 0xf000) != 0xb000));
    if (bank == 0 && (address & 0xf000) == 0xb000) {
        bank = 2;
        address = page | (address & 0xfff);
    }
    if (bank == PHY && fixture.version_info) {
        uint32_t speed = fixture.version_info->max_speed_mbps;
        assert(speed >= 1000 || (address != 0xa410 && address != 0xa414 &&
                                 address != 0xa5d0 && address != 0xa430));
        assert(speed >= 2500 || (address != 0xa6d4 && address != 0xa6d0 && address != 0xa5d4));
    }
    if (bank == PHY && address == 0xa41c && req->bmRequestType == 0xc0) {
        assert((mmd_ctrl == 0x4007 || (mmd_ctrl == 0x4003 && mmd_reg == 20)) && mmd_reg < 64);
        registers[PHY][address] = mmd_an[mmd_reg] & 0xff;
        registers[PHY][address + 1] = mmd_an[mmd_reg] >> 8;
    }
    if (req->bmRequestType == 0xc0) {
        if (bank == USB && (address == 0xd634 || address == 0xd5dc) &&
            (registers[USB][address] & 1) && !hold_command && busy_reads-- == 0)
            registers[USB][address] &= ~1;
        memcpy(req->pData, &registers[bank][address], req->wLength);
    } else {
        uint8_t *data = req->pData;
        for (int i = 0; i < req->wLength; i++) {
            if (req->wIndex & (1 << (i % 4))) registers[bank][address + i] = data[i];
        }
        if (bank == PHY && address == 0xa418 && req->wIndex == 0x1cc)
            mmd_ctrl = get_word(PHY, 0xa41a);
        if (bank == PHY && address == 0xa41c) {
            assert(mmd_ctrl == 3 || mmd_ctrl == 7 || mmd_ctrl == 0x4007);
            if (mmd_ctrl == 7 || mmd_ctrl == 3) mmd_reg = get_word(PHY, address);
            else {
                assert(mmd_reg < 64);
                mmd_an[mmd_reg] = get_word(PHY, address);
            }
        }
        if (bank == USB && (address == 0xd634 || address == 0xd5dc)) {
            uint16_t cmd = get_word(USB, address);
            bool phy = address == 0xd634;
            assert(req->wIndex == 0x33 && (cmd & 1) && cmd <= (phy ? 3 : 7));
            int target = phy ? PHY : (cmd & 4) ? IP : BMU;
            uint16_t reg = get_word(USB, phy ? 0xd630 : 0xd5d6);
            uint8_t *data_reg = &registers[USB][phy ? 0xd632 : 0xd5d8];
            size_t width = phy ? 2 : 4;
            assert((unsigned)reg + width <= 65536);
            if (cmd & 2) memcpy(&registers[target][reg], data_reg, width);
            else memcpy(data_reg, &registers[target][reg], width);
            busy_reads = 1;
        }
        if (bank == 0 && address == PLA_OCP_GPHY_BASE) {
            page = registers[0][address] | (registers[0][address + 1] << 8);
        }
    }
    return kIOReturnSuccess;
}

static IOUSBDeviceInterface interface = { .DeviceRequest = request };
static IOUSBDeviceInterface *interface_ptr = &interface;

static void put_word(int bank, uint16_t address, uint16_t value) {
    registers[bank][address] = value & 0xff;
    registers[bank][address + 1] = value >> 8;
}

static void probe_revision(uint16_t tcr1) {
    put_word(PLA, 0xe612, tcr1);
    rtl_read_version(&fixture);
    assert(fixture.revision_known);
    transfers = 0;
}

static void reset_fixture(void) {
    memset(registers, 0, sizeof(registers));
    transfers = fail_at = short_at = enumerations = 0;
    discovery_count = 1;
    discovery_error = 0;
    slept_seconds = 0;
    sleep_calls = 0;
    stop_after = 1;
    page = mmd_ctrl = mmd_reg = 0;
    memset(mmd_an, 0, sizeof(mmd_an));
    hold_command = false;
    busy_reads = 0;
    fixture = (rtl_dev_t){ .dev_interface = &interface_ptr, .service = 1,
        .ocp_base = 0xffff, .usb_bps = 5000000000LL };
    probe_revision(0x7410);
    mmd_an[20] = 2;
    put_word(PHY, 0xa5c4, 6);
    put_word(PHY, 0xa6ec, 3);
    strcpy(fixture.bsd_name, "en-test");
    put_word(2, 0xa402, BMSR_LSTATUS | BMSR_ANEGCOMPLETE);
    put_word(0, 0xe908, 0x0403); /* Resolved 2.5G full duplex. */
}

static void reset_8152(void) {
    reset_fixture();
    probe_revision(0x4c10);
}

static void reset_8153(void) {
    reset_fixture();
    probe_revision(0x5c20);
}

static void reset_8157(void) {
    reset_fixture();
    fixture.vid = 0x0bda;
    fixture.pid = 0x8157;
    probe_revision(0x9037);
    assert(fixture.version_raw == 0x1030);
    fixture.usb_bps = 10000000000LL;
    put_word(PLA, 0xe908, 0x1003);
}

/* The test build renames only core discovery/free; no real adapter is touched. */
int rtl_find_devices(rtl_dev_t **out, int *count) {
    enumerations++;
    *count = discovery_count;
    *out = NULL;
    if (discovery_error || !*count) return discovery_error;
    *out = malloc(sizeof(fixture));
    assert(*out);
    **out = fixture;
    return 0;
}

void rtl_free_devices(rtl_dev_t *devices, int count) {
    (void)count;
    free(devices);
}

int rtl_test_nanosleep(const struct timespec *delay, struct timespec *remaining) {
    (void)remaining;
    slept_seconds = delay->tv_sec + delay->tv_nsec / 1e9;
    if (++sleep_calls < stop_after) {
        rtl_tally_counters_t sample = {.tx_one_collision = 1, .tx_multi_collision = 8};
        memcpy(&registers[0][PLA_TALLYCNT], &sample, sizeof(sample));
        return 0;
    }
    raise(SIGTERM);
    errno = EINTR;
    return -1;
}

static FILE *capture;
static int saved_stdout, saved_stderr;
static char output[32768];

static void capture_start(void) {
    fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    saved_stderr = dup(STDERR_FILENO);
    capture = tmpfile();
    assert(capture && saved_stdout >= 0 && saved_stderr >= 0);
    assert(dup2(fileno(capture), STDOUT_FILENO) >= 0);
    assert(dup2(fileno(capture), STDERR_FILENO) >= 0);
}

static void capture_end(void) {
    fflush(stdout);
    rewind(capture);
    size_t size = fread(output, 1, sizeof(output) - 1, capture);
    output[size] = 0;
    assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);
    close(saved_stdout);
    fclose(capture);
}

static int cli(int argc, char **argv) {
    optind = 1;
    optreset = 1; /* Darwin getopt must be reset between invocations. */
    capture_start();
    int result = rtl_cli_main(argc, argv);
    capture_end();
    return result;
}

static void test_selection(void) {
    rtl_dev_t devices[2] = {0};
    assert(rtl_select_device(devices, 2, "", -1) == devices);
    assert(rtl_select_device(devices, 2, "", 1) == devices + 1);
    assert(rtl_select_device(devices, 2, "", 2) == NULL);
    assert(rtl_select_device(devices, 2, "", -2) == NULL);
    const char *bad[] = {"-1", "abc", "1x", "2147483648", "99999999999999999999999"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) {
        reset_fixture();
        char *args[] = {"test", "-d", (char *)bad[i], "led", "off", NULL};
        assert(cli(5, args) == 1);
        assert(enumerations == 0 && transfers == 0);
    }
    reset_fixture();
    char *args[] = {"test", "-d", "99", "led", "off", NULL};
    assert(cli(5, args) == 1 && transfers == 0);
}

static void test_transfers(void) {
    reset_fixture();
    put_word(0, 0xc000, 0xffff);
    uint16_t word = 0;
    assert(rtl_ocp_read_word(&fixture, MCU_TYPE_PLA, 0xc000, &word) == 0 && word == 0xffff);
    fail_at = transfers + 1;
    word = 0x1234;
    assert(rtl_ocp_read_word(&fixture, MCU_TYPE_PLA, 0xc000, &word) != 0 && word == 0x1234);
    fail_at = 0;
    short_at = transfers + 1;
    assert(rtl_ocp_read_word(&fixture, MCU_TYPE_PLA, 0xc000, &word) != 0 && word == 0x1234);
    short_at = 0;
    assert(rtl_ocp_write_dword(&fixture, MCU_TYPE_PLA, 0xc000, 0x12345678) == 0);
    assert(rtl_ocp_write_word(&fixture, MCU_TYPE_PLA, 0xc002, 0xabcd) == 0);
    assert(rtl_ocp_write_byte(&fixture, MCU_TYPE_PLA, 0xc001, 0xef) == 0);
    uint32_t dword;
    assert(rtl_ocp_read_dword(&fixture, MCU_TYPE_PLA, 0xc000, &dword) == 0 && dword == 0xabcdef78);
    reset_fixture();
    fail_at = 1;
    assert(rtl_ocp_reg_write(&fixture, 0xa400, 0x1200) != 0);
    assert(transfers == 1 && fixture.ocp_base == 0xffff); /* No aperture write after page failure. */
    fail_at = 0;
    assert(rtl_ocp_reg_write(&fixture, 0xa400, 0x1200) == 0 && transfers == 3);
    assert(registers[2][0xa401] == 0x12);
    reset_fixture();
    short_at = 1;
    assert(rtl_ocp_reg_read(&fixture, 0xa400, &word) != 0 && transfers == 1);
    assert(fixture.ocp_base == 0xffff);
}

static int eee_off(rtl_dev_t *dev) { return rtl_eee_set(dev, false); }
static int eee_on(rtl_dev_t *dev) { return rtl_eee_set(dev, true); }
static int power_off(rtl_dev_t *dev) { return rtl_power_set_no_sleep(dev, true); }
static int power_on(rtl_dev_t *dev) { return rtl_power_set_no_sleep(dev, false); }
static int led_default(rtl_dev_t *dev) { return rtl_led_set_mode(dev, "default"); }
static int raw_write(rtl_dev_t *dev) { return cmd_reg_write(dev, "pla", "c000", "1234", "16"); }

static void test_mutation_failures(void (*reset)(void)) {
    int (*operations[])(rtl_dev_t *) = {eee_off, eee_on, power_off, power_on,
        led_default, rtl_tally_reset, rtl_diag_restart_an, raw_write};
    for (size_t i = 0; i < sizeof(operations)/sizeof(*operations); i++) {
        reset();
        capture_start();
        assert(operations[i](&fixture) == 0);
        capture_end();
        int total = transfers;
        assert(total > 0);
        /* Fail each USB operation in turn, both with I/O error and short completion. */
        for (int short_transfer = 0; short_transfer < 2; short_transfer++) {
            for (int n = 1; n <= total; n++) {
                reset();
                if (short_transfer) short_at = n; else fail_at = n;
                capture_start();
                assert(operations[i](&fixture) != 0);
                capture_end();
                assert(transfers == n); /* No later writes, reads, or false success. */
                assert(strstr(output, "[+]") == NULL);
            }
        }
    }
    /* A failed read-modify-write must leave the target unchanged. */
    reset_fixture();
    put_word(0, PLA_EEE_CR, 0x1234);
    fail_at = 1;
    capture_start();
    assert(eee_off(&fixture) != 0);
    capture_end();
    assert(registers[0][PLA_EEE_CR] == 0x34 && registers[0][PLA_EEE_CR + 1] == 0x12);
}

static void test_status_identity(void) {
    reset_fixture();
    fixture.revision_known = false;
    char *args[] = {"test", "status", NULL};
    assert(cli(2, args) == 1 && transfers == 0);
    assert(strstr(output, "en-test (Driver: dont know)"));
    assert(strstr(output, "USB Bus Speed    : dont know"));
    assert(strstr(output, "Hardware MAC     : dont know"));
    assert(strstr(output, "Revision Code    : dont know"));
    probe_revision(0x7410);
    strcpy(fixture.driver, "example.dext.Ethernet");
    fail_at = 1;
    transfers = 0;
    assert(cli(2, args) == 1);
    assert(strstr(output, "en-test (Driver: example.dext.Ethernet)"));
}

static void test_speed(void) {
    const uint16_t statuses[] = {0x0000, 0x0006, 0x000b, 0x0013, 0x0403, 0x1003};
    const char *speeds[] = {"Link Down", "10 Mbps", "100 Mbps", "1.0 Gbps", "2.5 Gbps", "5.0 Gbps"};
    for (size_t i = 0; i < sizeof(statuses)/sizeof(*statuses); i++) {
        reset_fixture();
        if (i == 5) reset_8157();
        put_word(0, 0xe908, statuses[i]);
        /* Advertise gigabit AND 2.5G regardless of the resolved speed. */
        put_word(2, 0xa412, 0x0200);
        put_word(2, 0xa414, 0x0800);
        put_word(2, 0xa5d4, 0x0080);
        put_word(2, 0xa5d6, 0x0020);
        capture_start();
        assert(rtl_diag_print_status(&fixture) == 0);
        capture_end();
        assert(strstr(output, speeds[i]));
        if (i == 1) assert(strstr(output, "Half Duplex"));
        if (i == 3) assert(!strstr(output, "2.5 Gbps"));
    }
    reset_fixture();
    put_word(2, 0xa412, 0x0200);
    put_word(2, 0xa414, 0x0800);
    capture_start();
    assert(rtl_diag_print_phy(&fixture) == 0);
    capture_end();
    char *row = strstr(output, "2.5GBASE-T Full");
    assert(row && strstr(row, "No") && strstr(row, "No") < strchr(row, '\n'));
    put_word(2, 0xa5d4, 0x0080);
    put_word(2, 0xa5d6, 0x0020);
    capture_start();
    assert(rtl_diag_print_phy(&fixture) == 0);
    capture_end();
    row = strstr(output, "2.5GBASE-T Full");
    assert(row && strstr(row, "Yes") && strstr(row, "Yes") < strchr(row, '\n'));
}

static void test_diagnostic_reports(void) {
    reset_fixture();
    probe_revision(0xffff);
    char *phy[] = {"test", "phy", NULL};
    assert(cli(2, phy) != 0 && transfers == 0);
    reset_fixture();

    char *status[] = {"test", "status", NULL};
    put_word(2, OCP_BASE_MII + 2 * MII_ADVERTISE, 0x0800);
    put_word(2, OCP_BASE_MII + 2 * MII_LPA, 0x0800);
    assert(cli(2, status) == 0 && strstr(output, "local asymmetric, partner asymmetric"));
    put_word(2, OCP_BASE_MII + 2 * MII_BMSR, 0);
    assert(cli(2, status) == 0 && strstr(output, "local asymmetric, partner dont know"));

    char *eee[] = {"test", "eee", "status", NULL};
    put_word(2, OCP_EEE_ADV, 0x0004);
    assert(cli(3, eee) == 0);
    assert(strstr(output, "1000BASE-T EEE         | Yes              | dont know"));
    assert(!strstr(output, "EEE is active") && !strstr(output, "AppleUSBNCMData"));

    char *led[] = {"test", "led", "status", NULL};
    char *set[] = {"test", "led", "set", NULL, NULL};
    const char *modes[] = {"default", "link-only", "activity-only", "off", "0xbeef"};
    for (size_t i = 0; i < sizeof(modes)/sizeof(*modes); i++) {
        set[3] = (char *)modes[i];
        assert(cli(4, set) == 0 && cli(3, led) == 0);
        char expected[64];
        snprintf(expected, sizeof(expected), "Matching preset: %s", i == 4 ? "dont know" : modes[i]);
        assert(strstr(output, expected));
    }
    assert(strstr(output, "0xbeef"));

    char *power[] = {"test", "power", "default", NULL};
    assert(cli(3, power) == 0 && strstr(output, "U1/U2 Enabled"));
    power[2] = "status";
    assert(cli(3, power) == 0 && strstr(output, "U1/U2 (USB 0xcfd8): Enabled") && strstr(output, "U2 to P3 (USB 0xb460): Enabled"));

    char *restart[] = {"test", "phy", "--restart-an", NULL};
    assert(cli(3, restart) == 0 && strstr(output, "Timed out waiting for link-up"));
    eee[2] = "disable";
    assert(cli(3, eee) == 0 && strstr(output, "Timed out waiting for link-up"));

    discovery_error = -1;
    assert(cli(2, status) == 1 && strstr(output, "Failed to enumerate"));
    discovery_error = discovery_count = 0;
    assert(cli(2, status) == 1 && strstr(output, "No supported Realtek"));
}

static void test_cli_failures(void) {
    char *commands[][7] = {
        {"test", "status"}, {"test", "stats", "--json"}, {"test", "stats", "--reset"},
        {"test", "tally-reset"}, {"test", "watch"}, {"test", "phy"},
        {"test", "phy", "--restart-an"}, {"test", "eee", "disable"},
        {"test", "power", "no-sleep"}, {"test", "led", "off"},
        {"test", "reg", "write", "pla", "c000", "1234"},
        {"test", "reg", "read", "pla", "c000"}, {"test", "reg", "dump", "pla", "c000"}
    };
    for (size_t i = 0; i < sizeof(commands)/sizeof(*commands); i++) {
        reset_fixture();
        fail_at = 1;
        int argc = 0;
        while (commands[i][argc]) argc++;
        assert(cli(argc, commands[i]) == 1);
        assert(!strstr(output, "[+]"));
    }
}

static void test_watch(void) {
    char *commands[][7] = {
        {"test", "watch", "-n", "5"}, {"test", "monitor", "--interval", "5"},
        {"test", "watch", "--interval=5"}, {"test", "watch", "5"},
        {"test", "-n", "5", "watch"}, {"test", "watch", "-n5"}
    };
    for (size_t i = 0; i < sizeof(commands)/sizeof(*commands); i++) {
        reset_fixture();
        int argc = 0;
        while (commands[i][argc]) argc++;
        assert(cli(argc, commands[i]) == 0);
        assert(slept_seconds == 5.0);
        assert(strstr(output, "\033[?25h"));
    }
    reset_fixture();
    stop_after = 2;
    fail_at = 2; /* First refresh after the initial tally sample. */
    char *watch[] = {"test", "watch", NULL};
    assert(cli(2, watch) == 1);
    assert(strstr(output, "\033[?25h") && !strstr(output, "[+]"));
    reset_fixture();
    stop_after = 2;
    rtl_tally_counters_t before = {.tx_one_collision = UINT32_MAX, .tx_multi_collision = 5};
    memcpy(&registers[0][PLA_TALLYCNT], &before, sizeof(before));
    assert(cli(2, watch) == 0);
    char *row = strstr(output, "Collisions ");
    unsigned long long total, delta;
    assert(row && sscanf(row, "Collisions | %llu | +%llu", &total, &delta) == 2);
    assert(total == 9 && delta == 5);
    const char *invalid[] = {"nan", "inf", "-1", "0", "abc", "5x", "1e100", "3601"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); i++) {
        reset_fixture();
        char *args[] = {"test", "watch", "-n", (char *)invalid[i], NULL};
        assert(cli(4, args) == 1 && enumerations == 0);
    }
    reset_fixture();
    char *args[] = {"test", "watch", "-n", NULL};
    assert(cli(3, args) == 1 && enumerations == 0);
}

static void test_chipset_matrix(void) {
    static const struct {
        uint16_t code;
        rtl_chip_family_t family;
        unsigned speed;
    } cases[] = {
        {0x4c00, CHIP_RTL8152B, 100}, {0x4c10, CHIP_RTL8152B, 100},
        {0x5c00, CHIP_RTL8153A, 1000}, {0x5c10, CHIP_RTL8153A, 1000},
        {0x5c20, CHIP_RTL8153A, 1000}, {0x5c30, CHIP_RTL8153A, 1000},
        {0x6000, CHIP_RTL8153B, 1000}, {0x6010, CHIP_RTL8153B, 1000},
        {0x6400, CHIP_RTL8153C, 1000},
        {0x7020, CHIP_RTL8156A, 2500}, {0x7030, CHIP_RTL8156A, 2500},
        {0x7050, CHIP_RTL8156A, 2500}, {0x7060, CHIP_RTL8156A, 2500},
        {0x7070, CHIP_RTL8156A, 2500},
        {0x7400, CHIP_RTL8156B, 2500}, {0x7410, CHIP_RTL8156B, 2500},
        {0x1030, CHIP_RTL8157, 5000}
    };
    char *status[] = {"test", "status", NULL};
    char *phy[] = {"test", "phy", NULL};
    char *eee[] = {"test", "eee", "status", NULL};
    char *power[] = {"test", "power", "default", NULL};
    char *led[] = {"test", "led", "set", "default", NULL};
    char *stats[] = {"test", "stats", "--json", NULL};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        reset_fixture();
        probe_revision(cases[i].code | 0x8007);
        unsigned speed = cases[i].speed;
        fixture.vid = REALTEK_VID;
        fixture.pid = speed == 100 ? PID_RTL8152 : speed == 1000 ? PID_RTL8153 :
                      speed == 2500 ? PID_RTL8156 : PID_RTL8157;
        assert(fixture.version_raw == cases[i].code && fixture.version_info->family == cases[i].family);
        assert(fixture.version_info->max_speed_mbps == speed);
        put_word(PLA, 0xe908, speed == 100 ? 0xb : speed == 1000 ? 0x13 : speed == 2500 ? 0x403 : 0x1003);
        assert(cli(2, status) == 0);
        const char *negotiated = strstr(output, "Negotiated Speed");
        assert(negotiated && strstr(negotiated, speed == 100 ? "100 Mbps" : speed == 1000 ? "1.0 Gbps" :
                                    speed == 2500 ? "2.5 Gbps" : "5.0 Gbps"));
        assert(cli(2, phy) == 0);
        assert((strstr(output, "1000BASE-T Full") != NULL) == (speed >= 1000));
        assert((strstr(output, "2.5GBASE-T Full") != NULL) == (speed >= 2500));
        assert((strstr(output, "   5GBASE-T Full") != NULL) == (speed >= 5000));
        eee[2] = "enable";
        assert(cli(3, eee) == 0 && (get_word(PLA, 0xe040) & 3) == 3);
        eee[2] = "status";
        assert(cli(3, eee) == 0);
        assert((strstr(output, "1000BASE-T EEE") != NULL) == (speed >= 1000));
        assert((strstr(output, "2.5GBASE-T EEE") != NULL) == (speed >= 2500));
        assert((strstr(output, "   5GBASE-T EEE") != NULL) == (speed >= 5000));
        assert(cli(3, stats) == 0 && strstr(output, fixture.version_info->name));
        char revision_field[32];
        snprintf(revision_field, sizeof(revision_field), "\"revision_code\": \"0x%04x\"", cases[i].code);
        assert(strstr(output, revision_field));
        led[3] = "default";
        assert(cli(4, led) == 0);
        if (speed >= 2500) {
            assert(get_word(PLA, 0xdd8a) == 0x0220 && get_word(PLA, 0xdd8c) == 0x0208);
            assert(get_word(PLA, 0xdd8e) == (speed == 5000 ? 0x0203 : 0x0202));
            assert(get_word(PLA, 0xdd90) == (speed == 5000 ? 0x0280 : 0x0201));
        } else {
            uint16_t packed = cases[i].family == CHIP_RTL8153C ? 0x0ca9 : speed == 100 ? 0x83 : 0x87;
            assert(get_word(PLA, 0xdd90) == packed);
        }
        led[3] = "link-only";
        transfers = 0;
        if (cases[i].family == CHIP_RTL8156A || cases[i].family == CHIP_RTL8157 ||
            cases[i].family == CHIP_RTL8153C) {
            assert(cli(4, led) != 0 && transfers == 0);
        } else {
            put_word(PLA, 0xdd90, 0x1000);
            assert(cli(4, led) == 0);
            uint16_t link = speed == 2500 ? 0x002b : speed == 1000 ? 7 : 3;
            assert(get_word(PLA, 0xdd90) == (0x1000 | link));
        }
        put_word(USB, 0xd496, 0xbeef);
        put_word(USB, 0xb460, 0xf000);
        assert(cli(3, power) == 0);
        if (speed == 100) assert(get_word(PHY, 0x2010) == 0x8310);
        if (speed == 1000 || speed == 2500) {
            bool u2p3 = speed == 2500 || cases[i].code == 0x5c20 || cases[i].code == 0x5c30;
            assert(get_word(USB, 0xb460) == (0xf000 | u2p3));
            if (cases[i].family == CHIP_RTL8153A) {
                assert(get_word(USB, 0xd490) == 0xffff && get_word(USB, 0xd494) == 0xffff);
                assert(get_word(USB, 0xd496) == 0xbeef);
            } else assert(get_word(USB, 0xcfd8) == 1);
        }
    }
    const uint16_t unverified[] = {0x7040, 0x7440, 0x7770};
    char *raw[] = {"test", "reg", "write", "pla", "c000", "abcd", NULL};
    for (size_t i = 0; i < sizeof(unverified) / sizeof(*unverified); i++) {
        reset_fixture();
        probe_revision(unverified[i]);
        assert(cli(2, status) != 0 && strstr(output, "Unsupported hardware revision"));
        assert(cli(4, led) != 0 && cli(3, power) != 0 && cli(6, raw) != 0 && transfers == 0);
    }
    reset_8157();
    probe_revision(0x7410);
    assert(cli(6, raw) != 0 && transfers == 0);
    assert(rtl_is_adapter_usb_id(0x17ef, 0x3082) && rtl_is_adapter_usb_id(0x413c, 0xb097));
    assert(rtl_is_adapter_usb_id(0x0bda, 0x8052) && rtl_is_adapter_usb_id(0x0bda, 0x8056));
    assert(!rtl_is_adapter_usb_id(0x1234, 0x8156) && !rtl_is_adapter_usb_id(0x0bda, 0x815a));
}

static void test_eee_registers(void) {
    reset_8152();
    mmd_an[60] = 0x80;
    mmd_an[61] = 2;
    put_word(PHY, 0x2080, 0x8000);
    put_word(PHY, 0x2092, 1);
    put_word(PHY, 0x2094, 0x55);
    char *eee[] = {"test", "eee", "enable", NULL};
    assert(cli(3, eee) == 0);
    assert(mmd_an[60] == 0x82 && mmd_ctrl == 0 && get_word(PHY, 0x2080) == 0xb310);
    assert(get_word(PHY, 0x2092) == 0x601 && get_word(PHY, 0x2094) == ((42 << 7) | 0x55));
    eee[2] = "status";
    assert(cli(3, eee) == 0 && strstr(output, "100BASE-TX EEE         | Yes              | Yes"));
    eee[2] = "disable";
    assert(cli(3, eee) == 0 && mmd_ctrl == 0 && mmd_an[60] == 0x80);
    assert(get_word(PHY, 0x2080) == 0x8070 && get_word(PHY, 0x2092) == 1 && get_word(PHY, 0x2094) == 0xffd5);

    reset_fixture();
    probe_revision(0x7020);
    put_word(PHY, 0xa5c4, 2);
    put_word(PHY, 0xa6ec, 0);
    put_word(PHY, 0xa6d4, 0x8101);
    eee[2] = "enable";
    assert(cli(3, eee) == 0 && get_word(PHY, 0xa5d0) == 2 && get_word(PHY, 0xa6d4) == 0x8100);
    put_word(PHY, 0xa5c4, 0);
    put_word(PLA, 0xe040, 0x4000);
    assert(cli(3, eee) != 0 && get_word(PLA, 0xe040) == 0x4000);
}

static void test_8157(void) {
    reset_8157();
    assert(fixture.version_info->family == CHIP_RTL8157 && fixture.version_info->max_speed_mbps == 5000);
    const uint16_t ids[] = {0x8157, 0x8057, 0xa157, 0x8d57};
    for (size_t i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
        assert(rtl_is_8157_usb_id(0x0bda, ids[i]));
        assert(!rtl_is_8157_usb_id(0xabcd, ids[i]));
    }
    assert(rtl_is_8157_usb_id(0x1f9b, 0x25c3) && !rtl_is_8157_usb_id(0x1f9b, 0x8157));
    for (int rev = 0x7440; rev <= 0x7460; rev += 0x10) {
        probe_revision(rev);
        assert(fixture.version_info->family == CHIP_UNKNOWN);
    }
    reset_8157();

    char *list[] = {"test", "list", NULL};
    char *status[] = {"test", "status", NULL};
    char *phy[] = {"test", "phy", "--restart-an", NULL};
    char *eee[] = {"test", "eee", "enable", NULL};
    char *power[] = {"test", "power", "no-sleep", NULL};
    char *stats[] = {"test", "stats", "--json", NULL};
    char *watch[] = {"test", "watch", NULL};
    assert(cli(2, list) == 0 && strstr(output, "RTL8157 | Revision: 0x1030"));
    put_word(PLA, 0xc016, 9018);
    assert(cli(2, status) == 0 && strstr(output, "5.0 Gbps") && strstr(output, "9018 bytes"));
    put_word(PHY, 0xa5d4, 0x0100);
    put_word(PHY, 0xa5d6, 0x0060);
    assert(cli(2, phy) == 0 && strstr(output, "5GBASE-T Full          | Yes              | Yes"));
    assert(strstr(output, "2.5GBASE-T Full        | No               | Yes"));
    assert(cli(3, phy) == 0 && get_word(PHY, 0xa400) == 0x1200);

    put_word(PHY, 0xa5d0, 0x0080);
    put_word(PHY, 0xa6d4, 0x8000);
    put_word(PHY, 0xa6d0, 0x0002);
    put_word(PHY, 0xa6d6, 0x0001);
    put_word(PLA, 0xe040, 0x1001);
    eee[2] = "status";
    assert(cli(3, eee) == 0 && strstr(output, "LPI) RX : Enabled") && strstr(output, "LPI) TX : Disabled"));
    eee[2] = "enable";
    assert(cli(3, eee) == 0 && get_word(PHY, 0xa5d0) == 0x86 && get_word(PHY, 0xa6d4) == 0x8003);
    eee[2] = "status";
    assert(cli(3, eee) == 0 && strstr(output, "5GBASE-T EEE           | Yes              | Yes"));
    assert(strstr(output, "2.5GBASE-T EEE         | Yes              | No"));
    eee[2] = "disable";
    assert(cli(3, eee) == 0 && get_word(PHY, 0xa6d4) == 0x8000 &&
           get_word(PHY, 0xa5d0) == 0x80 && get_word(PLA, 0xe040) == 0x1000);

    put_word(USB, 0xcfd8, 0x8001);
    put_word(USB, 0xd80a, 0x18);
    put_word(IP, 0xc2c2, 0xa000);
    assert(cli(3, power) == 0 && get_word(USB, 0xcfd8) == 0x8000 && get_word(IP, 0xc2c2) == 0x8000);
    power[2] = "default";
    assert(cli(3, power) == 0 && get_word(USB, 0xcfd8) == 0x8001 && get_word(IP, 0xc2c2) == 0xa000);
    fixture.usb_bps = 480000000;
    assert(cli(3, power) == 0 && get_word(USB, 0xcfd8) == 0x8000);
    power[2] = "status";
    assert(cli(3, power) == 0 && strstr(output, "U2 to P3 (IP 0xc2c0): Enabled") && strstr(output, "UPS (USB 0xd80a): Enabled"));

    put_word(PLA, 0xe890, 1234);
    put_word(PLA, 0xe89c, 0xcafe);
    assert(cli(3, stats) == 0 && strstr(output, "\"tx_packets\": 1234") && strstr(output, "\"chip\": \"RTL8157\""));
    put_word(PLA, 0xe800, 0x8000);
    stats[2] = "--reset";
    assert(cli(3, stats) == 0 && get_word(PLA, 0xe800) == 0x8001 && get_word(PLA, 0xe89c) == 0xcafe);
    stop_after = 2;
    assert(cli(2, watch) == 0 && strstr(output, "RTL8157") && strstr(output, "UP"));

    char *raw[] = {"test", "reg", "write", "bmu", "0x2300", "0x11223344", NULL};
    assert(cli(6, raw) == 0 && get_word(BMU, 0x2300) == 0x3344);
    raw[2] = "read";
    assert(cli(5, raw) == 0 && strstr(output, "0x11223344"));
    raw[2] = "dump";
    raw[5] = "4";
    assert(cli(6, raw) == 0 && strstr(output, "44 33 22 11"));
    raw[3] = "phy";
    raw[4] = "a400";
    raw[5] = "2";
    assert(cli(6, raw) == 0 && strstr(output, "00 12"));
    char *led[] = {"test", "led", "status", NULL};
    transfers = 0;
    assert(cli(3, led) == 0 && strstr(output, "Selector 0 (PLA 0xdd8a)"));
    led[2] = "0x1234";
    assert(cli(3, led) == 0 && get_word(PLA, 0xdd90) == 0x1234);
    transfers = 0;
    raw[2] = "write";
    raw[3] = "ip";
    raw[4] = "10000";
    assert(cli(6, raw) != 0 && transfers == 0);
    probe_revision(0x7440);
    transfers = 0;
    assert(cli(3, eee) != 0 && transfers == 0);
}

static void test_8157_busy(void) {
    reset_8157();
    uint16_t word = 0xbeef;
    hold_command = true;
    assert(rtl_ocp_reg_read(&fixture, 0xa400, &word) != 0 && word == 0xbeef);
    int previous = transfers;
    assert(rtl_ocp_reg_write(&fixture, 0xa400, 0xbeef) != 0 && transfers == previous + 10);
    assert(get_word(PHY, 0xa400) == 0);
    reset_8157();
    hold_command = true;
    assert(rtl_adv_write(&fixture, ADV_CMD_IP, 0xc2c0, 0x12345678) != 0);
}

int main(void) {
    char *help[] = {"test", "--help", NULL};
    assert(cli(2, help) == 0 && strstr(output, "v1.0.5"));
    test_chipset_matrix();
    test_eee_registers();
    test_8157();
    test_8157_busy();
    test_selection();
    test_transfers();
    test_mutation_failures(reset_fixture);
    test_mutation_failures(reset_8152);
    test_mutation_failures(reset_8153);
    test_mutation_failures(reset_8157);
    test_status_identity();
    test_speed();
    test_diagnostic_reports();
    test_cli_failures();
    test_watch();
    puts("All regression tests passed (mock USB; no adapter settings changed).");
    return 0;
}
