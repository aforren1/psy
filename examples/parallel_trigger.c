/* parallel_trigger.c - minimal psy_parallel demo / trigger sender
 *
 * Build (from the repository root):
 *     cc -O2 -pthread -I. -o parallel_trigger examples/parallel_trigger.c   # Linux
 *     cl /O2 /I. examples\parallel_trigger.c                                # Windows (MSVC)
 * or:  cmake -B build && cmake --build build
 *
 * Run (Linux ppdev): load the ppdev driver and give yourself device access:
 *     sudo modprobe ppdev                       # creates /dev/parport0
 *     sudo usermod -aG lp $USER && newgrp lp    # one-time: grant access
 *     ./parallel_trigger
 *
 * Usage: parallel_trigger [trigger_byte]
 */
#define PSY_PARALLEL_IMPLEMENTATION
#include "psy_parallel.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    unsigned long code = (argc > 1) ? strtoul(argv[1], NULL, 0) : 0x01;

    psyp_port pp;
    /* All defaults: ppdev "/dev/parport0" on Linux, inpout @ LPT1 on Windows.
     * For raw x86 port I/O instead (root):
     *     psyp_desc desc = { .backend = PSYP_BACKEND_DIRECT, .base_addr = PSYP_LPT1 };
     */
    psyp_desc desc = {0};
    if (!psyp_open(&pp, &desc)) {
        fprintf(stderr, "psyp_open failed: %s\n", psyp_error(&pp));
        return 1;
    }

    /* async_policy says what the trailing-edge worker actually got; record it
     * next to any jitter numbers you measure (0 none, 1 deadline, 2 fifo,
     * 3 time_critical, 4 normal). */
    printf("port open (backend %d, base 0x%X, async policy %d)\n",
           pp.backend, pp.base_addr, (int)pp.async_policy);

    /* Send a 2 ms trigger pulse carrying `code`. */
    if (!psyp_pulse(&pp, (uint8_t)code, 2000)) {
        fprintf(stderr, "pulse failed: %s\n", psyp_error(&pp));
        psyp_close(&pp);
        return 1;
    }
    printf("sent trigger 0x%02lX (2 ms blocking pulse)\n", code & 0xFF);

    /* Non-blocking pulse: returns immediately, a real-time worker thread
     * drops the line after the requested width. The onset is written here on
     * this thread, so it is not delayed by the worker waking up. */
    if (!psyp_pulse_async(&pp, (uint8_t)code, 2000)) {
        fprintf(stderr, "async pulse failed: %s\n", psyp_error(&pp));
    } else {
        printf("queued async trigger 0x%02lX (returned without blocking)\n",
               code & 0xFF);
    }
    /* psyp_close() below cleanly stops and joins the worker thread, after the
     * pending trailing edge has been written. */

    /* Read back the input lines. */
    uint8_t status = psyp_read_status(&pp);
    printf("status = 0x%02X  busy=%d ack=%d paper=%d select=%d error=%d\n",
           status,
           (status & PSYP_STATUS_BUSY)   ? 1 : 0,
           (status & PSYP_STATUS_ACK)    ? 1 : 0,
           (status & PSYP_STATUS_PAPER)  ? 1 : 0,
           (status & PSYP_STATUS_SELECT) ? 1 : 0,
           (status & PSYP_STATUS_ERROR)  ? 1 : 0);

    psyp_close(&pp);
    return 0;
}
