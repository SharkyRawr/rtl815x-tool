/* Exercise the real CLI and register access against a fake USB endpoint. */
#define main rtl_cli_main
#include "../src/main.c"
#undef main
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

static uint8_t registers[3][65536];
static uint16_t page;
static int transfers, fail_at, short_at, enumerations;
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
    if (bank == 0 && (address & 0xf000) == 0xb000) {
        bank = 2;
        address = page | (address & 0xfff);
    }
    if (req->bmRequestType == 0xc0) {
        memcpy(req->pData, &registers[bank][address], req->wLength);
    } else {
        uint8_t *data = req->pData;
        for (int i = 0; i < req->wLength; i++) {
            if (req->wIndex & (1 << (i % 4))) registers[bank][address + i] = data[i];
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

static void reset_fixture(void) {
    memset(registers, 0, sizeof(registers));
    transfers = fail_at = short_at = enumerations = 0;
    slept_seconds = 0;
    sleep_calls = 0;
    stop_after = 1;
    page = 0;
    fixture = (rtl_dev_t){ .dev_interface = &interface_ptr, .service = 1,
        .ocp_base = 0xffff, .version_info = &RTL_CHIP_VERSIONS[12] };
    strcpy(fixture.bsd_name, "en-test");
    put_word(2, 0xa402, BMSR_LSTATUS | BMSR_ANEGCOMPLETE);
    put_word(0, 0xe908, 0x0403); /* Resolved 2.5G full duplex. */
}

/* The test build renames only core discovery/free; no real adapter is touched. */
int rtl_find_devices(rtl_dev_t **out, int *count) {
    enumerations++;
    *out = malloc(sizeof(fixture));
    assert(*out);
    **out = fixture;
    *count = 1;
    return 0;
}

void rtl_free_devices(rtl_dev_t *devices, int count) {
    (void)count;
    free(devices);
}

int rtl_test_nanosleep(const struct timespec *delay, struct timespec *remaining) {
    (void)remaining;
    slept_seconds = delay->tv_sec + delay->tv_nsec / 1e9;
    if (++sleep_calls < stop_after) return 0;
    raise(SIGTERM);
    errno = EINTR;
    return -1;
}

static FILE *capture;
static int saved_stdout;
static char output[32768];

static void capture_start(void) {
    fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    capture = tmpfile();
    assert(capture && saved_stdout >= 0);
    assert(dup2(fileno(capture), STDOUT_FILENO) >= 0);
}

static void capture_end(void) {
    fflush(stdout);
    rewind(capture);
    size_t size = fread(output, 1, sizeof(output) - 1, capture);
    output[size] = 0;
    assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
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
static int led_off(rtl_dev_t *dev) { return rtl_led_set_mode(dev, "off"); }
static int raw_write(rtl_dev_t *dev) { return cmd_reg_write(dev, "pla", "c000", "1234", "16"); }

static void test_mutation_failures(void) {
    int (*operations[])(rtl_dev_t *) = {eee_off, eee_on, power_off, power_on,
        led_off, rtl_tally_reset, rtl_diag_restart_an, raw_write};
    for (size_t i = 0; i < sizeof(operations)/sizeof(*operations); i++) {
        reset_fixture();
        capture_start();
        assert(operations[i](&fixture) == 0);
        capture_end();
        int total = transfers;
        assert(total > 0);
        /* Fail each USB operation in turn, both with I/O error and short completion. */
        for (int short_transfer = 0; short_transfer < 2; short_transfer++) {
            for (int n = 1; n <= total; n++) {
                reset_fixture();
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

static void test_speed(void) {
    const uint16_t statuses[] = {0x0000, 0x0006, 0x000b, 0x0013, 0x0403, 0x1003};
    const char *speeds[] = {"Link Down", "10 Mbps", "100 Mbps", "1.0 Gbps", "2.5 Gbps", "5.0 Gbps"};
    for (size_t i = 0; i < sizeof(statuses)/sizeof(*statuses); i++) {
        reset_fixture();
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
    assert(row && strstr(row, "Supported") && strstr(row, "Supported") < strchr(row, '\n'));
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

int main(void) {
    char *help[] = {"test", "--help", NULL};
    assert(cli(2, help) == 0 && strstr(output, "v1.0.1"));
    test_selection();
    test_transfers();
    test_mutation_failures();
    test_speed();
    test_cli_failures();
    test_watch();
    puts("All regression tests passed (mock USB; no adapter settings changed).");
    return 0;
}
