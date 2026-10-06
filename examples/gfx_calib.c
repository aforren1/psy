/* gfx_calib.c - photometer readings in, a canonical psy_gfx.h calibration out.
 *
 * Usage:
 *   gfx_calib                                  check itself on a synthetic
 *                                              display with sRGB's transfer
 *                                              function and primaries
 *   gfx_calib READINGS.csv OUT.psycal [SPECTRA.csv]
 * READINGS.csv: one reading per line, "gun,level,Y,x,y": gun 0, 1, 2, -1
 * (black, all guns 0) or 3 (white, all guns 1); level 0..1; Y in cd/m2; x, y
 * the CIE 1931 chromaticity, or 0,0. Lines starting with # are skipped.
 * SPECTRA.csv: "nm,r,g,b,black" at an even step, W sr-1 m-2 nm-1, each gun at
 * full output.
 * Prints what derive() found: the additivity of white, RGB to XYZ, RGB to LMS
 * and the CLUT at a few points; writes the file. No number here is a
 * measurement of a display unless the readings are.
 * Exit code: 0, 1 when the readings are refused or a file fails, 2 for a bad
 * argument.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define PSY_GFX_IMPLEMENTATION
#include "psy_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static psygfx_cal cal, back;
static unsigned char bytes[sizeof(psygfx_cal)];

static void print_cal(const psygfx_cal* c) {
    int k, n = c->lut_n;
    printf("readings %d, flags%s%s%s, white minus the sum of the guns %+.3f%%\n", c->n_readings,
           c->flags & PSYGFX_CAL_NOMINAL ? " NOMINAL" : "", c->flags & PSYGFX_CAL_HAS_XY ? " XY" : "",
           c->flags & PSYGFX_CAL_HAS_SPECTRA ? " SPECTRA" : "", c->white_err);
    printf("RGB to XYZ (1931):\n");
    for (k = 0; k < 3; k++) printf("  %10.6f %10.6f %10.6f\n", c->rgb_to_xyz[3 * k], c->rgb_to_xyz[3 * k + 1], c->rgb_to_xyz[3 * k + 2]);
    if (c->flags & PSYGFX_CAL_HAS_SPECTRA) {
        printf("RGB to LMS (Stockman-Sharpe 2 deg):\n");
        for (k = 0; k < 3; k++) printf("  %12.6g %12.6g %12.6g\n", c->rgb_to_lms[3 * k], c->rgb_to_lms[3 * k + 1], c->rgb_to_lms[3 * k + 2]);
    }
    printf("CLUT (%d entries), linear -> device:\n", n);
    for (k = 0; k <= 8; k++) {
        int i = k * (n - 1) / 8;
        printf("  %.4f -> %.5f %.5f %.5f\n", (double)i / (n - 1), c->lut[0][i], c->lut[1][i], c->lut[2][i]);
    }
}

static int self_check(void) {
    static const float xy[4][2] = { { 0.64f, 0.33f }, { 0.30f, 0.60f }, { 0.15f, 0.06f }, { 0.3127f, 0.3290f } };
    char err[200];
    float bg[3] = { 0.5f, 0.5f, 0.5f };
    if (psygfx_cal_nominal(&cal, xy, 100.0f, 2.2) < 0) { fprintf(stderr, "gfx_calib: nominal failed\n"); return 1; }
    printf("A nominal display: sRGB's primaries, D65 at 100 cd/m2, gamma 2.2. NOT a measurement.\n");
    print_cal(&cal);
    psygfx_cal_save(&cal, bytes, sizeof bytes);
    if (psygfx_cal_load(&back, bytes, sizeof bytes, err, sizeof err) < 0) { fprintf(stderr, "gfx_calib: %s\n", err); return 1; }
    printf("saved and loaded back: %u bytes, CRC %08x, identical %s\n", (unsigned)sizeof bytes, (unsigned)back.crc,
           memcmp(&cal, &back, sizeof cal) == 0 ? "yes" : "NO");
    printf("max luminance contrast at mid-gray: %.3f\n", (double)psygfx_max_contrast(bg, bg));
    return memcmp(&cal, &back, sizeof cal) == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    FILE* fp;
    char buf[256], err[200];
    int rc;
    if (argc == 1) return self_check();
    if (argc != 3 && argc != 4) { fprintf(stderr, "usage: gfx_calib [READINGS.csv OUT.psycal [SPECTRA.csv]]\n"); return 2; }
    psygfx_cal_init(&cal);
    fp = fopen(argv[1], "r");
    if (!fp) { fprintf(stderr, "gfx_calib: cannot open %s\n", argv[1]); return 1; }
    while (fgets(buf, sizeof buf, fp)) {
        int gun;
        float level, Y, x, y;
        if (buf[0] == '#' || buf[0] == '\n' || buf[0] == '\r') continue;
        if (sscanf(buf, "%d,%f,%f,%f,%f", &gun, &level, &Y, &x, &y) != 5 || psygfx_cal_add(&cal, gun, level, Y, x, y) < 0) {
            fprintf(stderr, "gfx_calib: bad reading: %s", buf);
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    if (argc == 4) {
        static float wl[PSYGFX_CAL_MAX_WL], r[PSYGFX_CAL_MAX_WL], g[PSYGFX_CAL_MAX_WL], b[PSYGFX_CAL_MAX_WL], k[PSYGFX_CAL_MAX_WL];
        int n = 0;
        fp = fopen(argv[3], "r");
        if (!fp) { fprintf(stderr, "gfx_calib: cannot open %s\n", argv[3]); return 1; }
        while (fgets(buf, sizeof buf, fp) && n < PSYGFX_CAL_MAX_WL) {
            if (buf[0] == '#') continue;
            if (sscanf(buf, "%f,%f,%f,%f,%f", &wl[n], &r[n], &g[n], &b[n], &k[n]) == 5) n++;
        }
        fclose(fp);
        if (n < 2 || psygfx_cal_set_spectra(&cal, wl[0], wl[1] - wl[0], n, r, g, b, k) < 0) {
            fprintf(stderr, "gfx_calib: spectra refused\n");
            return 1;
        }
    }
    rc = psygfx_cal_derive(&cal, err, sizeof err);
    if (rc < 0) { fprintf(stderr, "gfx_calib: %s\n", err); return 1; }
    print_cal(&cal);
    psygfx_cal_save(&cal, bytes, sizeof bytes);
    fp = fopen(argv[2], "wb");
    if (!fp || fwrite(bytes, 1, sizeof bytes, fp) != sizeof bytes) { fprintf(stderr, "gfx_calib: cannot write %s\n", argv[2]); if (fp) fclose(fp); return 1; }
    fclose(fp);
    printf("wrote %s (%u bytes)\n", argv[2], (unsigned)sizeof bytes);
    return 0;
}
