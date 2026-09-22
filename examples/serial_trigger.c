/* serial_trigger.c - minimal psy_serial demo: list ports, send a trigger
 * byte, then echo whatever the device sends back for a short while.
 *
 * Build (from the repository root):
 *     cc -O2 -I. -o serial_trigger examples/serial_trigger.c   # Linux / macOS
 *     cl /O2 /I. examples\serial_trigger.c                     # Windows (MSVC)
 * or:  cmake -B build && cmake --build build
 *
 * Usage: serial_trigger <device> [trigger_byte] [baud]
 *     serial_trigger COM3 0x2A
 *     serial_trigger /dev/ttyUSB0 42 115200
 * With no arguments it lists the ports it can see and exits.
 *
 * Exit codes: 0 success, 1 the port could not be opened or used, 2 no
 * arguments (it then lists the ports it can see).
 */
#define PSY_SERIAL_IMPLEMENTATION
#include "psy_serial.h"

#include <stdio.h>
#include <stdlib.h>

static void list_ports(void) {
    psys_port_info ports[32];
    int n = psys_list_ports(ports, 32);
    if (n < 0) { fprintf(stderr, "list_ports: %s\n", psys_strerror(n)); return; }
    printf("%d serial port(s) detected\n", n);
    for (int i = 0; i < n && i < 32; i++) {
        printf("  %-24s %s", ports[i].name, ports[i].description);
        if (ports[i].vid) printf("  [%04X:%04X %s @ %s]", ports[i].vid, ports[i].pid,
                                 ports[i].serial_number, ports[i].location);
        printf("\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        list_ports();
        fprintf(stderr, "usage: %s <device> [trigger_byte] [baud]\n", argv[0]);
        return 2;
    }
    unsigned long code = (argc > 2) ? strtoul(argv[2], NULL, 0) : 0x01;

    psys_port sp;
    psys_desc desc = {
        .device      = argv[1],
        .baud        = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0,  /* 0 = 115200 */
        .low_latency = true,   /* FTDI: 16 ms -> 1 ms RX latency where the OS allows */
    };
    /* To find a box by USB id instead of naming the port:
     *     psys_port_filter f = { .vid = 0x0403, .pid = 0x6001 };
     *     psys_port_info info;
     *     if (psys_find_ports(&f, &info, 1) == 1) desc.device = info.name;
     */

    if (!psys_open(&sp, &desc)) {
        fprintf(stderr, "psys_open failed: %s\n", psys_error(&sp));
        return 1;
    }
    printf("port open: %s @ %u baud, %u%c%d, low_latency=%d\n",
           sp.device, sp.baud, sp.data_bits,
           sp.parity == PSYS_PARITY_NONE ? 'N' : sp.parity == PSYS_PARITY_EVEN ? 'E' : 'O',
           sp.stop_bits == PSYS_STOP_BITS_2 ? 2 : 1, sp.low_latency ? 1 : 0);

    /* Devices that reset on DTR (Arduino-style) need a moment before they
     * listen; drop anything they printed while booting. Everything but
     * psys_open() reports through its return value, never psys_error(). */
    int rc = psys_purge(&sp, PSYS_PURGE_RX);
    if (rc < 0) fprintf(stderr, "purge: %s (oserr %d)\n", psys_strerror(rc), sp.rd_oserr);

    /* 2 ms trigger pulse: `code`, then 0x00. Bracket it with the library's
     * clock so the log has both bounds of the onset. */
    uint64_t t0 = psys_now_us();
    int r = psys_pulse(&sp, (uint8_t)code, 0x00, 2000);
    uint64_t t1 = psys_now_us();
    if (r < 0) {
        fprintf(stderr, "pulse failed: %s (oserr %d)\n", psys_strerror(r), sp.wr_oserr);
        psys_close(&sp);
        return 1;
    }
    printf("sent trigger 0x%02lX (2 ms blocking pulse), accepted between +0 and +%llu us\n",
           code & 0xFF, (unsigned long long)(t1 - t0));

    /* Echo incoming bytes for ~2 s. PSYS_READ_ANY returns as soon as anything
     * arrives, so the 100 ms timeout only bounds the idle wait. */
    printf("listening for 2 s...\n");
    uint8_t buf[64];
    for (int i = 0; i < 20; i++) {
        int n = psys_read(&sp, buf, (int)sizeof(buf), 100, PSYS_READ_ANY);
        if (n < 0) { fprintf(stderr, "read failed: %s (oserr %d)\n", psys_strerror(n), sp.rd_oserr); break; }
        for (int k = 0; k < n; k++) printf("%02X ", buf[k]);
        if (n) { printf("\n"); fflush(stdout); }
    }

    psys_close(&sp);
    return 0;
}
