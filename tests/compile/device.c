/* Compile check: ysp/device.h as a C translation unit, with its
 * implementation (which implements ysp/rt.h, ysp/box.h and ysp/serial.h).
 * CMake and CI build this as C11 with warnings as errors; device.cpp builds
 * the same source as C++17. It starts and stops a manual instance whose
 * port never opens, and returns 0. */
#define YSP_DEVICE_IMPLEMENTATION
#include "ysp/device.h"

#include <string.h>

int main(void) {
    static ydev_device dev;
    ydev_desc d;
    int ok;
    memset(&d, 0, sizeof d);
    d.role = "check";
    d.family = YBOX_LINE;
    d.key = "port:ysp-compile-check-no-such-port";
    d.device = 1;
    d.manual = true;
    if (!ydev_start(&dev, &d)) return 1;
    ok = ydev_poll(&dev, 0) == YDEV_OPENING;
    ydev_stop(&dev);
    return ok && ydev_state(&dev) == YDEV_CLOSED && ydev_key_match("serial:16C0::", "serial:16c0:0483:1:A") ? 0 : 1;
}
