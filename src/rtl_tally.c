#include "rtl_tally.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <errno.h>

static volatile sig_atomic_t g_keep_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    g_keep_running = 0;
}

int rtl_tally_read(rtl_dev_t *dev, rtl_tally_counters_t *out_counters) {
    if (!dev || !out_counters) return -1;
    if (rtl_open_device(dev) != 0) return -1;

    rtl_tally_counters_t raw;
    memset(&raw, 0, sizeof(raw));
    if (rtl_ocp_read_block(dev, PLA_TALLYCNT, sizeof(raw), &raw, MCU_TYPE_PLA) != 0) {
        return -1;
    }

    out_counters->tx_packets = CFSwapInt64LittleToHost(raw.tx_packets);
    out_counters->rx_packets = CFSwapInt64LittleToHost(raw.rx_packets);
    out_counters->tx_errors = CFSwapInt64LittleToHost(raw.tx_errors);
    out_counters->rx_errors = CFSwapInt32LittleToHost(raw.rx_errors);
    out_counters->rx_missed = CFSwapInt16LittleToHost(raw.rx_missed);
    out_counters->align_errors = CFSwapInt16LittleToHost(raw.align_errors);
    out_counters->tx_one_collision = CFSwapInt32LittleToHost(raw.tx_one_collision);
    out_counters->tx_multi_collision = CFSwapInt32LittleToHost(raw.tx_multi_collision);
    out_counters->rx_unicast = CFSwapInt64LittleToHost(raw.rx_unicast);
    out_counters->rx_broadcast = CFSwapInt64LittleToHost(raw.rx_broadcast);
    out_counters->rx_multicast = CFSwapInt32LittleToHost(raw.rx_multicast);
    out_counters->tx_aborted = CFSwapInt16LittleToHost(raw.tx_aborted);
    out_counters->tx_underrun = CFSwapInt16LittleToHost(raw.tx_underrun);
    return 0;
}

int rtl_tally_reset(rtl_dev_t *dev) {
    if (!dev) return -1;
    if (rtl_open_device(dev) != 0) return -1;

    printf("[*] Resetting hardware tally counters on %s (%s)...\n", dev->version_info->name, dev->bsd_name);
    /* Writing 0x0001 to PLA_RSTTALLY clears on-chip statistics registers */
    if (rtl_ocp_write_dword(dev, MCU_TYPE_PLA, PLA_RSTTALLY, 0x00000001) != 0) return -1;
    printf("[+] Hardware counters successfully reset to 0.\n");
    return 0;
}

int rtl_tally_print_stats(rtl_dev_t *dev, bool json_format) {
    if (!dev) return -1;

    rtl_tally_counters_t c;
    if (rtl_tally_read(dev, &c) != 0) {
        fprintf(stderr, "Error: Failed to read hardware tally counters\n");
        return -1;
    }

    if (json_format) {
        printf("{\n");
        printf("  \"adapter\": {\n");
        printf("    \"chip\": \"%s\",\n", dev->version_info->name);
        printf("    \"revision\": \"%s\",\n", dev->version_info->revision_str);
        printf("    \"bsd_interface\": \"%s\",\n", dev->bsd_name);
        printf("    \"mac_address\": \"%s\",\n",
               dev->mac_address[0] ? dev->mac_address : "dont know");
        printf("    \"vid\": \"0x%04x\",\n", dev->vid);
        printf("    \"pid\": \"0x%04x\"\n", dev->pid);
        printf("  },\n");
        printf("  \"statistics\": {\n");
        printf("    \"tx_packets\": %llu,\n", c.tx_packets);
        printf("    \"rx_packets\": %llu,\n", c.rx_packets);
        printf("    \"rx_unicast\": %llu,\n", c.rx_unicast);
        printf("    \"rx_multicast\": %u,\n", c.rx_multicast);
        printf("    \"rx_broadcast\": %llu,\n", c.rx_broadcast);
        printf("    \"tx_errors\": %llu,\n", c.tx_errors);
        printf("    \"rx_errors\": %u,\n", c.rx_errors);
        printf("    \"rx_missed_fifo_drops\": %u,\n", c.rx_missed);
        printf("    \"align_errors\": %u,\n", c.align_errors);
        printf("    \"tx_one_collision\": %u,\n", c.tx_one_collision);
        printf("    \"tx_multi_collision\": %u,\n", c.tx_multi_collision);
        printf("    \"tx_aborted\": %u,\n", c.tx_aborted);
        printf("    \"tx_underrun\": %u\n", c.tx_underrun);
        printf("  }\n");
        printf("}\n");
        return 0;
    }

    printf("\n=================================================================\n");
    printf(" Realtek Hardware Statistics & Error Counters\n");
    printf(" Interface: %s (%s, %s)\n",
           dev->bsd_name, dev->version_info->name,
           dev->mac_address[0] ? dev->mac_address : "dont know");
    printf("=================================================================\n");
    printf(" Traffic Volume:\n");
    printf("   TX Packets Total     : %'llu\n", c.tx_packets);
    printf("   RX Packets Total     : %'llu\n", c.rx_packets);
    printf("   RX Unicast           : %'llu\n", c.rx_unicast);
    printf("   RX Multicast         : %'u\n", c.rx_multicast);
    printf("   RX Broadcast         : %'llu\n", c.rx_broadcast);

    printf("\n Error & Drop Counters:\n");
    printf("   TX Errors            : %llu\n", c.tx_errors);
    printf("   RX Errors (CRC/Align): %u\n", c.rx_errors);
    printf("   RX Missed (FIFO Drop): %s%u\033[0m\n",
           c.rx_missed > 0 ? "\033[1;31m" : "", c.rx_missed);
    printf("   Alignment Errors     : %u\n", c.align_errors);
    printf("   Single Collisions    : %u\n", c.tx_one_collision);
    printf("   Multiple Collisions  : %u\n", c.tx_multi_collision);
    printf("   TX Aborted           : %u\n", c.tx_aborted);
    printf("   TX Underrun          : %u\n", c.tx_underrun);
    printf("=================================================================\n\n");
    return 0;
}

int rtl_tally_watch(rtl_dev_t *dev, double interval_sec) {
    if (!dev) return -1;
    if (!isfinite(interval_sec) || interval_sec < 0.2 || interval_sec > 3600.0) return -1;
    g_keep_running = 1;

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    rtl_tally_counters_t prev, curr;
    if (rtl_tally_read(dev, &prev) != 0) {
        fprintf(stderr, "Error: Failed initial read of tally counters\n");
        return -1;
    }

    struct timespec delay = {
        .tv_sec = (time_t)interval_sec,
        .tv_nsec = (long)((interval_sec - (time_t)interval_sec) * 1000000000.0)
    };
    int result = 0;

    /* Hide cursor and clear screen */
    printf("\033[?25l\033[2J\033[H");

    while (g_keep_running) {
        struct timespec remaining = delay;
        while (nanosleep(&remaining, &remaining) != 0) {
            if (errno != EINTR) { result = -1; goto done; }
            if (!g_keep_running) goto done;
        }
        if (!g_keep_running) break;
        if (rtl_tally_read(dev, &curr) != 0) { result = -1; break; }

        uint64_t d_tx_pkt = curr.tx_packets - prev.tx_packets;
        uint64_t d_rx_pkt = curr.rx_packets - prev.rx_packets;
        uint64_t d_rx_uni = curr.rx_unicast - prev.rx_unicast;
        uint32_t d_rx_multi = curr.rx_multicast - prev.rx_multicast;
        uint64_t d_rx_bcast = curr.rx_broadcast - prev.rx_broadcast;

        uint64_t d_tx_err = curr.tx_errors - prev.tx_errors;
        uint32_t d_rx_err = curr.rx_errors - prev.rx_errors;
        uint16_t d_rx_miss = curr.rx_missed - prev.rx_missed;

        uint32_t single_delta = curr.tx_one_collision - prev.tx_one_collision;
        uint32_t multi_delta = curr.tx_multi_collision - prev.tx_multi_collision;

        double tx_pps = (double)d_tx_pkt / interval_sec;
        double rx_pps = (double)d_rx_pkt / interval_sec;

        uint16_t latched_bmsr;
        if (rtl_mdio_read(dev, MII_BMSR, &latched_bmsr) != 0) { result = -1; break; }
        uint16_t bmsr;
        if (rtl_mdio_read(dev, MII_BMSR, &bmsr) != 0) { result = -1; break; }
        bool link_up = (bmsr & BMSR_LSTATUS) != 0;

        time_t now = time(NULL);
        char timestr[32];
        strftime(timestr, sizeof(timestr), "%H:%M:%S", localtime(&now));

        /* Move cursor to home position */
        printf("\033[H");
        printf("=================================================================\n");
        printf(" Realtek Live Hardware Monitor [Update: %.1fs | %s]\n", interval_sec, timestr);
        printf(" Device: %s (%s, %s) | Link: %s\n",
               dev->version_info->name, dev->bsd_name, dev->product_name,
               link_up ? "\033[1;32mUP\033[0m" : "\033[1;31mDOWN\033[0m");
        printf("=================================================================\n");
        printf("  Metric                  | Cumulative Total   | Current Rate / Delta\n");
        printf(" -------------------------+--------------------+---------------------\n");
        printf("  TX Packets              | %18llu | \033[1;36m%15.1f pkt/s\033[0m\n", curr.tx_packets, tx_pps);
        printf("  RX Packets              | %18llu | \033[1;36m%15.1f pkt/s\033[0m\n", curr.rx_packets, rx_pps);
        printf("  RX Unicast              | %18llu | +%16llu\n", curr.rx_unicast, d_rx_uni);
        printf("  RX Multicast            | %18u | +%16u\n", curr.rx_multicast, d_rx_multi);
        printf("  RX Broadcast            | %18llu | +%16llu\n", curr.rx_broadcast, d_rx_bcast);
        printf(" -------------------------+--------------------+---------------------\n");
        printf("  TX Errors               | %18llu | %s+%16llu\033[0m\n",
               curr.tx_errors, d_tx_err > 0 ? "\033[1;31m" : "", d_tx_err);
        printf("  RX Errors (CRC/Align)   | %18u | %s+%16u\033[0m\n",
               curr.rx_errors, d_rx_err > 0 ? "\033[1;31m" : "", d_rx_err);
        printf("  RX Missed (FIFO Drops)  | %18u | %s+%16u\033[0m\n",
               curr.rx_missed, d_rx_miss > 0 ? "\033[1;31m" : "", (uint32_t)d_rx_miss);
        printf("  Collisions              | %18llu | +%16llu\n",
               (uint64_t)curr.tx_one_collision + curr.tx_multi_collision, (uint64_t)single_delta + multi_delta);
        printf("=================================================================\n");
        printf(" Press [Ctrl+C] to stop monitoring.\n");
        fflush(stdout);

        prev = curr;
    }

done:
    /* Restore cursor */
    printf("\033[?25h\n");
    if (result == 0) printf("[+] Live monitor stopped.\n");
    return result;
}
