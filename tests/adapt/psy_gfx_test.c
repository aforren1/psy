/* psy_gfx_test.c - self-checking test for psy_gfx.h. Returns 0 when every
 * check passed, 1 after printing each failure.
 *
 * Two halves:
 *   CPU   the calibration math against published values (IEC 61966-2-1 for
 *         sRGB, CVRL's cone fundamentals, Psychtoolbox's ComputeDKL_M run in
 *         MATLAB), the canonical file, the noise hash, the coordinate
 *         transform, the parameter table, the shader wrapper, the null
 *         backend and the ring records. Needs nothing.
 *   GL    every stimulus rendered offscreen into an RGBA32F scene and read
 *         back, against a CPU reference in double at pixel centers, on every
 *         GL ES 3.0 renderer it can open through tests/adapt/
 *         psy_gfx_headless.h: ANGLE on D3D11 hardware, D3D11 WARP and
 *         Vulkan SwiftShader on Windows (PSYSCR_ANGLE_DIR), Mesa llvmpipe
 *         on Linux. Pixel positions come from psygfx_local(), the inverse
 *         of the one coordinate transform, never from hard-coded positions.
 * With no GL it says so and passes, unless PSYGFX_TEST_REQUIRE_GL=1.
 * PSYGFX_TEST_DEVICES=warp,swiftshader,hardware,mesa picks renderers.
 *
 *     gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -O2 -I. \
 *         -o gfx_test tests/adapt/psy_gfx_test.c -lm -pthread -ldl && ./gfx_test
 *     cl /nologo /W4 /WX /I. tests\adapt\psy_gfx_test.c
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   /* getenv() under /W4 /WX */
#endif
#ifndef PSYSCR_NO_SDL
#define PSYSCR_NO_SDL
#endif
#define PSY_GFX_IMPLEMENTATION
#include "psy_gfx.h"
#include "psy_gfx_headless.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------- harness */

static int g_failures = 0;
static const char* g_where = "cpu";

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "psy_gfx_test [%s]: FAIL line %d: %s\n", g_where, __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_LE(v, lim) do { double v_ = (double)(v), l_ = (double)(lim); if (!(v_ <= l_)) { \
    fprintf(stderr, "psy_gfx_test [%s]: FAIL line %d: %s = %.4g, limit %.4g\n", g_where, __LINE__, #v, v_, l_); g_failures++; } } while (0)

static double maxd(double a, double b) { return a > b ? a : b; }

/* ---------------------------------------------------------- reference data */

/* Psychtoolbox-3's B_monitor (PsychColorimetricMatFiles), 380 to 780 nm at
 * 5 nm, the three primaries. */
static const float b_monitor[81][3] = {
    {2.54448866e-05f, 1.81754529e-05f, 0.000218614355f}, {1.70652778e-05f, 1.62274359e-05f, 0.00033646906f}, {1.73548826e-05f, 1.97121607e-05f, 0.000524075586f},
    {1.10430579e-05f, 2.10026232e-05f, 0.000785003663f}, {1.68514725e-05f, 2.47077267e-05f, 0.00112989339f}, {2.79205103e-05f, 3.02806071e-05f, 0.00162449058f},
    {3.68489024e-05f, 4.3492429e-05f, 0.00231158115f}, {4.61356456e-05f, 5.90593486e-05f, 0.00321444369f}, {6.40115125e-05f, 7.92486369e-05f, 0.00426287615f},
    {7.87895945e-05f, 0.000104398287f, 0.00536524553f}, {9.38017677e-05f, 0.000125842801f, 0.00629551011f}, {0.000104837476f, 0.000147268436f, 0.00699425461f},
    {0.000112854764f, 0.000169745887f, 0.00746987017f}, {0.000114891959f, 0.000190823113f, 0.0076538175f}, {0.000113321335f, 0.000219694306f, 0.00751941252f},
    {0.000112543437f, 0.000266895291f, 0.00715063755f}, {0.000115063854f, 0.000339613208f, 0.00661929067f}, {0.000164001973f, 0.000461715278f, 0.00595535941f},
    {0.00016166626f, 0.000648717897f, 0.00517660815f}, {0.000120422241f, 0.000935842356f, 0.00432736373f}, {9.11649293e-05f, 0.00134511289f, 0.00350724567f},
    {0.000119135588f, 0.00186204566f, 0.00284930202f}, {0.000173795432f, 0.00248474805f, 0.0022775671f}, {0.000217951362f, 0.00319023907f, 0.0018092097f},
    {0.000130290168f, 0.00396391904f, 0.00140759439f}, {0.000122895089f, 0.00469058849f, 0.00108401518f}, {0.000260265236f, 0.00530546436f, 0.000855244478f},
    {0.000242082122f, 0.00582612983f, 0.000675835781f}, {0.000125001984f, 0.00619461866f, 0.000536908805f}, {0.000119160078f, 0.00638613652f, 0.000421890475f},
    {0.000200565443f, 0.00641397414f, 0.000340849067f}, {0.000596439756f, 0.00634772689f, 0.000284081004f}, {0.000647111018f, 0.00618893013f, 0.00023846896f},
    {0.000250648378f, 0.00593243205f, 0.000196789675f}, {0.000247729627f, 0.00556196501f, 0.00016515944f}, {0.000325163104f, 0.00514279496f, 0.000142684411f},
    {0.000199194325f, 0.00460565852f, 0.000119278831f}, {0.000160859763f, 0.00399303906f, 9.87605758e-05f}, {0.000128333928f, 0.003296585f, 7.91288e-05f},
    {0.000217279486f, 0.00271931692f, 6.52297389e-05f}, {0.000692523735f, 0.00221373642f, 5.69836332e-05f}, {0.00121961917f, 0.00176864745f, 5.11066997e-05f},
    {0.00186081802f, 0.00140740744f, 4.68066546e-05f}, {0.00217286748f, 0.00115532229f, 4.334209e-05f}, {0.000777411976f, 0.000937724927f, 2.86030878e-05f},
    {0.000531028716f, 0.00075858179f, 2.25191296e-05f}, {0.00243411125f, 0.000613520213f, 3.55863737e-05f}, {0.00581217444f, 0.000521860978f, 6.10288797e-05f},
    {0.00935351762f, 0.000454585774f, 8.77379022e-05f}, {0.0160540849f, 0.000436561159f, 0.000141387244f}, {0.00646433981f, 0.000278232127f, 5.95202758e-05f},
    {0.00109971325f, 0.000179542211f, 1.49869678e-05f}, {0.000321805723f, 0.000136373934f, 7.90651162e-06f}, {0.000206917831f, 0.00010714503f, 5.72250985e-06f},
    {0.000194161332f, 8.52289108e-05f, 5.93693812e-06f}, {0.00019565519f, 6.70061216e-05f, 6.69041293e-06f}, {0.000166207001f, 5.45137183e-05f, 5.55323827e-06f},
    {0.000173404739f, 4.39852646e-05f, 4.75026921e-06f}, {0.000219653503f, 3.87610868e-05f, 5.56665309e-06f}, {0.000185579946f, 3.26363917e-05f, 5.4201717e-06f},
    {0.000377182835f, 3.00902558e-05f, 6.73444907e-06f}, {0.000782160371f, 2.79583629e-05f, 9.95153023e-06f}, {0.000641983526f, 2.25897484e-05f, 9.5864052e-06f},
    {0.00121424651f, 2.84244951e-05f, 1.59049557e-05f}, {0.00716850812f, 7.78583087e-05f, 5.98901973e-05f}, {0.0110984043f, 0.000112950694f, 9.41880103e-05f},
    {0.00310588215f, 3.90380642e-05f, 2.99187701e-05f}, {0.000240500983f, 1.10132229e-05f, 6.52342284e-06f}, {0.00017952384f, 9.10364834e-06f, 8.86790287e-06f},
    {0.000148740834f, 8.25892437e-06f, 8.14868661e-06f}, {0.000108493237f, 9.04293725e-06f, 1.11748754e-05f}, {9.72366149e-05f, 1.08798554e-05f, 9.62156905e-06f},
    {9.05450273e-05f, 9.37974836e-06f, 9.52454068e-06f}, {9.33611452e-05f, 1.00271812e-05f, 1.21351602e-05f}, {8.26694132e-05f, 1.13333422e-05f, 1.32683446e-05f},
    {7.27135662e-05f, 1.295684e-05f, 1.20727194e-05f}, {8.11668954e-05f, 1.47604411e-05f, 1.60195486e-05f}, {6.6912716e-05f, 1.75663715e-05f, 1.53622966e-05f},
    {7.04220745e-05f, 2.08366916e-05f, 2.80584074e-05f}, {7.34996324e-05f, 1.47124747e-05f, 4.57011947e-05f}, {6.55843052e-05f, 1.81984221e-05f, 5.75373665e-05f},
};

/* tests/compare/psy_gfx_dkl.m, run in MATLAB R2023a with Psychtoolbox's own
 * ComputeDKL_M.m on CVRL linss2_10e_1 and B_monitor resampled linearly to
 * 1 nm, background RGB (0.4, 0.5, 0.3), luminance 0.68990272 L + 0.34832189 M. */
static const double ref_rgb_to_lms[9] = {
    0.15758320328193681, 0.36737427350739571, 0.050179703670330963,
    0.057646833363934384, 0.38451094060277291, 0.072627135835000883,
    0.0065563050333017972, 0.034056269070785766, 0.33055572889735307 };
static const double ref_dkl[9] = {
    4.5402989511704481, 2.2923311736424345, 0,
    2.8824328742926806, -3.182368078038305, 0,
    -2.6213428216596335, -1.3234780201742304, 8.4162774860657272 };
/* the unit DKL axes as RGB increments, by column: lum, L-M, S */
static const double ref_dkl_rgb[9] = {
    0.23094010767585041, 3.080313307241469, 0.06864359988388552,
    0.28867513459481281, -1.0311128493218227, -0.079473690462270277,
    0.17320508075688773, 0.045137269434734333, 0.36627380390229192 };

static double srgb_eotf(double v) { return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double srgb_oetf(double l) { return l <= 0.0031308 ? 12.92 * l : 1.055 * pow(l, 1.0 / 2.4) - 0.055; }

/* --------------------------------------------------------------- CPU tests */

static void test_noise_cpu(void) {
    float buf[64 * 48];
    uint32_t h = 2166136261u;
    int i, below = 0;
    double sum = 0, sq = 0;
    psygfx_noise_fill(buf, 64, 48, 7, PSYGFX_UNIFORM);
    for (i = 0; i < 64 * 48; i++) {
        uint32_t b;
        memcpy(&b, &buf[i], 4);
        h = (h ^ b) * 16777619u;
        CHECK(buf[i] > -1.0f && buf[i] < 1.0f);
        sum += buf[i];
        below += buf[i] < 0;
    }
    /* Pinned: the same bits on every compiler and platform CI builds. */
    printf("noise uniform seed 7, 64x48: FNV-1a %08x, mean %.4f\n", (unsigned)h, sum / (64 * 48));
    CHECK(h == 0xf70dbc0bu);
    CHECK(fabs(sum / (64 * 48)) < 0.03);
    CHECK(below > 1400 && below < 1672);
    psygfx_noise_fill(buf, 64, 48, 7, PSYGFX_BINARY);
    for (i = 0; i < 64 * 48; i++) CHECK(buf[i] == 1.0f || buf[i] == -1.0f);
    psygfx_noise_gauss(buf, 64, 48, 3);
    sum = sq = 0;
    for (i = 0; i < 64 * 48; i++) { sum += buf[i]; sq += (double)buf[i] * buf[i]; }
    sum /= 64 * 48;
    sq = sq / (64 * 48) - sum * sum;
    CHECK(fabs(sum) < 0.06);
    CHECK(fabs(sq - 1.0) < 0.08);
    CHECK(psygfx_hash(0) == psygfx_hash(0) && psygfx_hash(1) != psygfx_hash(2));
}

static void test_cal_srgb(void) {
    static psygfx_cal c, c2;
    static const float xy[4][2] = { { 0.64f, 0.33f }, { 0.30f, 0.60f }, { 0.15f, 0.06f }, { 0.3127f, 0.3290f } };
    /* IEC 61966-2-1:1999, as published (4 decimals) */
    static const double iec[9] = { 0.4124, 0.3576, 0.1805, 0.2126, 0.7152, 0.0722, 0.0193, 0.1192, 0.9505 };
    static unsigned char bytes[sizeof(psygfx_cal)];
    char err[200];
    double e = 0, e17 = 0, e256 = 0;
    int k, gun, rc;
    CHECK(psygfx_cal_nominal(&c, xy, 1.0f, 2.2) == PSYGFX_OK);
    for (k = 0; k < 9; k++) e = maxd(e, fabs(c.rgb_to_xyz[k] - iec[k]));
    printf("sRGB primaries to XYZ, against IEC 61966-2-1's printed matrix: max |diff| %.2e\n", e);
    /* 0.9505 is printed; the primaries and D65 give 0.95030 */
    CHECK_LE(e, 1.2e-4);      /* measured 3.9e-5 */
    CHECK(c.flags & PSYGFX_CAL_NOMINAL);
    CHECK(fabs(c.white_err) < 1e-4);
    /* The CLUT of a display with sRGB's transfer function is sRGB's
     * encoding, from 17 and from 64 levels per gun (the file holds 256
     * readings in all). */
    for (rc = 0; rc < 2; rc++) {
        int n = rc ? 64 : 17;
        double m = 0;
        psygfx_cal_init(&c2);
        psygfx_cal_add(&c2, PSYGFX_GUN_BLACK, 0, 0, 0, 0);
        for (gun = 0; gun < 3; gun++)
            for (k = 1; k < n; k++) psygfx_cal_add(&c2, gun, (float)k / (float)(n - 1), (float)(80.0 * srgb_eotf((double)k / (n - 1))), 0, 0);
        CHECK(psygfx_cal_derive(&c2, err, sizeof err) == PSYGFX_OK);
        for (k = 0; k < PSYGFX_CAL_MAX_LUT; k++) m = maxd(m, fabs(c2.lut[1][k] - srgb_oetf((double)k / (PSYGFX_CAL_MAX_LUT - 1))));
        if (rc) e256 = m; else e17 = m;
    }
    printf("CLUT against sRGB's encoding: 17 levels per gun max |diff| %.2e, 64 levels %.2e\n", e17, e256);
    CHECK_LE(e17, 4e-3);      /* measured 1.30e-3 */
    CHECK_LE(e256, 1.1e-4);   /* measured 3.64e-5 */
    /* round trip, then every refusal */
    CHECK(psygfx_cal_save(&c2, bytes, sizeof bytes) == (int)sizeof bytes);
    memset(&c, 0, sizeof c);
    CHECK(psygfx_cal_load(&c, bytes, sizeof bytes, err, sizeof err) == PSYGFX_OK);
    CHECK(memcmp(&c, &c2, sizeof c) == 0);
    CHECK(psygfx_cal_check(&c, err, sizeof err) == PSYGFX_OK);
    CHECK(psygfx_cal_load(&c, bytes, sizeof bytes - 1, err, sizeof err) == PSYGFX_ERR_FORMAT);
    bytes[1000] ^= 1;
    CHECK(psygfx_cal_load(&c, bytes, sizeof bytes, err, sizeof err) == PSYGFX_ERR_FORMAT);
    CHECK(strstr(err, "CRC") != NULL);
    bytes[1000] ^= 1;
    c2.lut[0][100] += 1e-4f;   /* a table its readings do not give */
    psygfx_cal_save(&c2, bytes, sizeof bytes);
    CHECK(psygfx_cal_load(&c, bytes, sizeof bytes, err, sizeof err) == PSYGFX_ERR_FORMAT);
    CHECK(strstr(err, "CLUT") != NULL);
    /* readings that do not rise are refused, with the level */
    psygfx_cal_init(&c2);
    psygfx_cal_add(&c2, PSYGFX_GUN_BLACK, 0, 0.5f, 0, 0);
    for (gun = 0; gun < 3; gun++) {
        psygfx_cal_add(&c2, gun, 0.5f, 10.0f, 0, 0);
        psygfx_cal_add(&c2, gun, 0.75f, 9.0f, 0, 0);
        psygfx_cal_add(&c2, gun, 1.0f, 30.0f, 0, 0);
    }
    CHECK(psygfx_cal_derive(&c2, err, sizeof err) == PSYGFX_ERR_RANGE);
    CHECK(strstr(err, "0.75") != NULL);
    /* no cone space without spectra */
    {
        float bg[3] = { 0.5f, 0.5f, 0.5f }, cc[3] = { 0.1f, 0, 0 }, dir[3];
        CHECK(psygfx_cal_dir_cone(&c, bg, cc, dir) == PSYGFX_ERR_REFUSED);
    }
}

static void test_cones_dkl(void) {
    static psygfx_cal c;
    float r[81], g[81], b[81];
    double l[3], m[9], e = 0;
    char err[200];
    const float bg[3] = { 0.4f, 0.5f, 0.3f };
    int i, k;
    /* CVRL linss2_10e_1 rows, as published */
    psygfx_cone_fundamentals(440.0, l);
    CHECK(fabs(l[0] / 4.02563e-2 - 1) < 1e-7 && fabs(l[1] / 6.47782e-2 - 1) < 1e-7 && fabs(l[2] / 9.91020e-1 - 1) < 1e-7);
    psygfx_cone_fundamentals(570.0, l);
    CHECK(fabs(l[0] / 9.99993e-1 - 1) < 1e-7 && fabs(l[1] / 8.13509e-1 - 1) < 1e-7 && fabs(l[2] / 2.81800e-4 - 1) < 1e-7);
    psygfx_cone_fundamentals(700.0, l);
    CHECK(l[2] == 0.0 && l[0] > 0.0);
    psygfx_cone_fundamentals(389.0, l);
    CHECK(l[0] == 0.0);
    for (i = 0; i < 81; i++) { r[i] = b_monitor[i][0]; g[i] = b_monitor[i][1]; b[i] = b_monitor[i][2]; }
    psygfx_cal_init(&c);
    psygfx_cal_add(&c, PSYGFX_GUN_BLACK, 0, 0, 0, 0);
    for (k = 0; k < 3; k++) psygfx_cal_add(&c, k, 1, 1, 0, 0);
    CHECK(psygfx_cal_set_spectra(&c, 380, 5, 81, r, g, b, NULL) == PSYGFX_OK);
    CHECK(psygfx_cal_derive(&c, err, sizeof err) == PSYGFX_OK);
    for (k = 0; k < 9; k++) e = maxd(e, fabs(c.rgb_to_lms[k] - ref_rgb_to_lms[k]) / fabs(ref_rgb_to_lms[k]));
    printf("RGB to LMS against Psychtoolbox (B_monitor, SS2): max relative diff %.2e\n", e);
    CHECK_LE(e, 1e-6);
    CHECK(psygfx_cal_dkl_matrix(&c, bg, m) == PSYGFX_OK);
    e = 0;
    for (k = 0; k < 9; k++) e = maxd(e, fabs(m[k] - ref_dkl[k]) / (fabs(ref_dkl[k]) + 1e-12));
    printf("DKL matrix against ComputeDKL_M: max relative diff %.2e\n", e);
    CHECK_LE(e, 1e-6);
    e = 0;
    for (k = 0; k < 3; k++) {
        float unit[3] = { 0, 0, 0 }, dir[3];
        double lb[3] = { 0, 0, 0 }, lc[3] = { 0, 0, 0 }, inc[3], v;
        int j;
        unit[k] = 1.0f;
        CHECK(psygfx_cal_dir_dkl(&c, bg, unit, dir) == PSYGFX_OK);
        for (j = 0; j < 3; j++) e = maxd(e, fabs(dir[j] - ref_dkl_rgb[j * 3 + k]) / fabs(ref_dkl_rgb[j * 3 + k]));
        /* the axes isolate: L-M keeps S and luminance, S keeps L and M
         * (the cone increment of the float dir, in double) */
        psygfx_cal_lms(&c, bg, lb);
        for (j = 0; j < 3; j++)
            inc[j] = c.rgb_to_lms[j * 3] * dir[0] + c.rgb_to_lms[j * 3 + 1] * dir[1] + c.rgb_to_lms[j * 3 + 2] * dir[2];
        (void)lc;
        v = 0.68990272 * inc[0] + 0.34832189 * inc[1];
        if (k == 1) { CHECK(fabs(inc[2]) < 1e-6 * fabs(inc[0])); CHECK(fabs(v) < 1e-5 * fabs(inc[0])); }
        if (k == 2) { CHECK(fabs(inc[0]) < 1e-5 * fabs(inc[2])); CHECK(fabs(inc[1]) < 1e-5 * fabs(inc[2])); }
        if (k == 0) CHECK(fabs(inc[0] / lb[0] - inc[1] / lb[1]) < 1e-5 * fabs(inc[0] / lb[0]));
    }
    printf("DKL unit axes as RGB against Psychtoolbox: max relative diff %.2e\n", e);
    CHECK_LE(e, 1e-5);
    {   /* cone contrast: 10% L only */
        float cc[3] = { 0.1f, 0, 0 }, dir[3], p[3];
        double lb[3] = { 0, 0, 0 }, lc[3] = { 0, 0, 0 };
        int j;
        CHECK(psygfx_cal_dir_cone(&c, bg, cc, dir) == PSYGFX_OK);
        for (j = 0; j < 3; j++) p[j] = bg[j] + dir[j];
        psygfx_cal_lms(&c, bg, lb);
        psygfx_cal_lms(&c, p, lc);
        CHECK(fabs(lc[0] / lb[0] - 1.1) < 1e-5);
        CHECK(fabs(lc[1] / lb[1] - 1.0) < 1e-5 && fabs(lc[2] / lb[2] - 1.0) < 1e-5);
    }
    {
        float d3[3], dir[3] = { 0.1f, -0.2f, 0.05f }, bgm[3] = { 0.5f, 0.5f, 0.25f };
        psygfx_dkl_from_sph(90, 0, 2, d3);
        CHECK(fabs(d3[0] - 2) < 1e-6 && fabs(d3[1]) < 1e-6);
        psygfx_dkl_from_sph(0, 90, 1, d3);
        CHECK(fabs(d3[2] - 1) < 1e-6 && fabs(d3[0]) < 1e-6);
        CHECK(fabsf(psygfx_max_contrast(bgm, dir) - 2.5f) < 1e-6f);
    }
}

static void test_tables(void) {
    int n = 0, m = 0, i, j;
    const psygfx_param* p = psygfx_params(&n);
    const psyscr_param* d = psygfx_desc_params(&m);
    psygfx_stim s;
    psygfx_bind b[3];
    float v[4] = { 0.25f, 3.0f, 0.0f, 9.0f };
    char buf[64];
    CHECK(n == PSYGFX_P_COUNT && m > 5 && d);
    for (i = 0; i < n; i++) {
        CHECK(p[i].offset + sizeof(float) <= sizeof(psygfx_stim));
        CHECK(p[i].min <= p[i].def && p[i].def <= p[i].max);
        for (j = 0; j < i; j++) CHECK(strcmp(p[i].name, p[j].name) != 0);
    }
    CHECK(p[PSYGFX_P_X].offset == offsetof(psygfx_stim, x));
    CHECK(p[PSYGFX_P_AY].offset == offsetof(psygfx_stim, ay));
    CHECK(p[PSYGFX_P_CONTRAST].offset == offsetof(psygfx_stim, contrast));
    CHECK(p[PSYGFX_P_P0 + 31].offset == offsetof(psygfx_stim, p[31]));
    s = psygfx_gabor(NULL);
    memset(b, 0, sizeof b);
    b[0].stim = &s; b[0].param = PSYGFX_P_CONTRAST; b[0].channel = 0;
    b[1].stim = &s; b[1].param = PSYGFX_P_X; b[1].channel = 1;
    b[2].stim = &s; b[2].param = PSYGFX_P_COUNT; b[2].channel = 3;   /* bad */
    CHECK(psygfx_apply(b, 3, v) == PSYGFX_ERR_ARG && s.contrast == 1.0f && s.x == 0.0f);
    CHECK(psygfx_apply(b, 2, v) == 2 && s.contrast == 0.25f && s.x == 3.0f);
    /* the wrapper */
    {
        int len = psygfx_shader_wrap("float psy_main(vec2 p) { return 0.0; }", PSYGFX_MODULATION, NULL, 0);
        char* t;
        CHECK(len < -1000);
        t = (char*)malloc((size_t)(-len - 1000) + 1);
        CHECK(psygfx_shader_wrap("float psy_main(vec2 p) { return 0.0; }", PSYGFX_MODULATION, t, (size_t)(-len - 1000) + 1) == -len - 1000);
        CHECK(strstr(t, "#line 1\nfloat psy_main") != NULL && strncmp(t, "#version 300 es", 15) == 0);
        free(t);
        CHECK(psygfx_shader_wrap("x", PSYGFX_COLOR, buf, sizeof buf) < 0);
    }
}

/* The transform, without GL: a 800 x 600 simulated screen and the null backend. */
static void test_coordinates(void) {
    static psyscr_screen scr;
    static psygfx_gfx g;
    psyscr_desc d;
    psygfx_desc gd;
    psygfx_stim s;
    psygfx_shape_desc sd;
    float x, y, x0, y0, x1, y1, lx, ly;
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_SIM;
    d.sim_period_ns = 1000000;
    CHECK(psyscr_open(&scr, &d));
    memset(&gd, 0, sizeof gd);
    gd.screen = &scr;
    CHECK(psygfx_open(&g, &gd));
    psygfx_size(&g, &x, &y);
    CHECK(x == 800 && y == 600);
    psygfx_center(&g, &x, &y);
    CHECK(x == 400 && y == 300);
    s = psygfx_shape(NULL);
    s.x = 100;
    psygfx_resolve(&g, &s, &x, &y);
    CHECK(x == 500 && y == 300);                       /* .x = 100: right of the center */
    memset(&sd, 0, sizeof sd);
    sd.place = PSYGFX_TOP_LEFT; sd.x = 10; sd.y = 10; sd.w = 100; sd.h = 50;
    s = psygfx_shape(&sd);
    psygfx_resolve(&g, &s, &x, &y);
    CHECK(x == 10 && y == 10);                         /* in from the corner, y down */
    psygfx_bounds(&g, &s, &x0, &y0, &x1, &y1);
    CHECK(x0 == -40 && y0 == -15 && x1 == 60 && y1 == 35);   /* anchored at its center */
    sd.anchor = PSYGFX_TOP_LEFT; sd.x = 0; sd.y = 0;
    s = psygfx_shape(&sd);
    CHECK(s.ax == 0.0f && s.ay == 0.0f);
    psygfx_bounds(&g, &s, &x0, &y0, &x1, &y1);
    CHECK(x0 == 0 && y0 == 0 && x1 == 100 && y1 == 50);
    psygfx_local(&g, &s, 50, 25, &lx, &ly);
    CHECK(lx == 0 && ly == 0);
    psygfx_local(&g, &s, 60, 35, &lx, &ly);
    CHECK(lx == 10 && ly == 10);                       /* local axes are the screen's at ori 0 */
    CHECK(psygfx_hit(&g, &s, 99.5f, 49.5f) && !psygfx_hit(&g, &s, 100.5f, 10));
    /* ori 90 about the top-left anchor: clockwise, so the box hangs below
     * the anchor and to its left */
    s.ori = 90;
    psygfx_bounds(&g, &s, &x0, &y0, &x1, &y1);
    CHECK(fabsf(x0 + 50) < 1e-4f && fabsf(y0) < 1e-4f && fabsf(x1) < 1e-4f && fabsf(y1 - 100) < 1e-4f);
    psygfx_local(&g, &s, -10, 80, &lx, &ly);           /* local x runs down the screen */
    CHECK(fabsf(lx - 30) < 1e-4f && fabsf(ly + 15) < 1e-4f);  /* local y points left */
    sd.anchor = PSYGFX_BOTTOM_RIGHT; sd.place = PSYGFX_BOTTOM_RIGHT;
    s = psygfx_shape(&sd);
    psygfx_bounds(&g, &s, &x0, &y0, &x1, &y1);
    CHECK(x0 == 700 && y0 == 550 && x1 == 800 && y1 == 600);
    psygfx_close(&g);
    /* degrees: one scale factor */
    gd.units = PSYGFX_DEG;
    gd.view.distance_mm = 573.0f;
    gd.view.width_mm = 400.0f;
    CHECK(psygfx_open(&g, &gd));
    s = psygfx_shape(NULL);
    s.x = 1;
    psygfx_resolve(&g, &s, &x, &y);
    CHECK(fabs(x - (400.0 + 2.0 * 573.0 * tan(3.14159265358979 / 180.0))) < 1e-3);
    psygfx_close(&g);
    psyscr_close(&scr);
}

/* The null backend runs the frame; the ring gets the open and clipped records. */
static void test_null_and_ring(void) {
    static psyscr_screen scr;
    static psygfx_gfx g;
    static unsigned char mem[64 * 64];
    psyrt_ring ring;
    psyrt_ring_desc rd;
    psyrt_event ev[16];
    psyscr_desc d;
    psygfx_desc gd;
    psyscr_frame f;
    psygfx_gabor_desc gab;
    psygfx_stim s;
    float px[4];
    int n, i, opens = 0, clips = 0;
    memset(&ring, 0, sizeof ring);
    memset(&rd, 0, sizeof rd);
    rd.memory = mem;
    rd.bytes = sizeof mem;
    CHECK(psyrt_ring_open(&ring, &rd));
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_SIM;
    d.sim_period_ns = 1000000;
    CHECK(psyscr_open(&scr, &d));
    memset(&gd, 0, sizeof gd);
    gd.screen = &scr;
    gd.ring = &ring;
    gd.background[0] = gd.background[1] = gd.background[2] = 0.5f;
    CHECK(psygfx_open(&g, &gd));
    memset(&gab, 0, sizeof gab);
    gab.sigma = 10; gab.sf = 0.1f; gab.contrast = 1.5f;   /* leaves 0..1 */
    s = psygfx_gabor(&gab);
    CHECK(psygfx_end(&g) == PSYGFX_ERR_ORDER);
    CHECK(psygfx_draw(&g, &s) == PSYGFX_ERR_ORDER);
    for (i = 0; i < 2; i++) {
        CHECK(psyscr_begin(&scr, &f) == PSYSCR_OK);
        CHECK(psygfx_begin(&g, &f) == PSYGFX_OK);
        CHECK(psygfx_begin(&g, &f) == PSYGFX_ERR_ORDER);
        CHECK(psygfx_texture_update(&g, psygfx_texture(&g, NULL), 0, 0, 1, 1, px, 0) == PSYGFX_ERR_ORDER);
        if (i == 0) CHECK(psygfx_draw(&g, &s) == PSYGFX_OK);
        CHECK(psygfx_end(&g) == PSYGFX_OK);
        CHECK(psyscr_flip(&scr) == PSYSCR_OK);
    }
    CHECK(psygfx_clipped(&g) == 1);
    CHECK(psygfx_read_scene(&g, 0, 0, 1, 1, px) == PSYGFX_ERR_NOT_IMPLEMENTED);
    n = psyrt_ring_drain(&ring, ev, 16);
    for (i = 0; i < n; i++) {
        if (ev[i].source != PSYRT_SRC_GFX) continue;
        if (ev[i].kind == PSYGFX_EV_OPEN) opens++;
        if (ev[i].kind == PSYGFX_EV_CLIPPED) { clips++; CHECK(ev[i].aux == 1 && ev[i].u.i64[0] == 0); }
    }
    CHECK(opens == 1 && clips == 1);   /* none for the frame without a clip */
    psygfx_close(&g);
    psyscr_close(&scr);
    /* refusals */
    gd.output = PSYGFX_OUT_10;
    CHECK(!psygfx_open(&g, &gd) && strstr(psygfx_error(&g), "8-bit") != NULL);
    gd.output = PSYGFX_OUT_8;
    gd.stereo = PSYGFX_SIDE_BY_SIDE;
    CHECK(!psygfx_open(&g, &gd));
    gd.stereo = PSYGFX_MONO;
    gd.screen = NULL;
    CHECK(!psygfx_open(&g, &gd));
}

/* ---------------------------------------------------------------- GL tests */

#define W 320
#define H 200

typedef struct rig {
    psygfx_headless hl;
    psyscr_screen   scr;
    psygfx_gfx      g;
    psyscr_frame    f;
    float           scene[W * H * 4];
    uint8_t         out[W * H * 4];
} rig;

static rig* R;

/* What every renderer measured, for docs/psy_gfx.md. */
typedef struct stats {
    double shape_soft, grating, gabor, square_bad, dots, image, user;
    long   hard_bad, noise_bad, out_bad, lut_bad, dither_bad, hit_bad, ties;
    double anchor_diff, overlap, batch_diff, dither_mean;
} stats;

/* What v0.2's checks measured, per renderer. */
typedef struct stats2 {
    double stroke_soft, mask_gpu, sprite_lin, tint, group_diff, target_diff, add_diff;
    long   stroke_hard_bad, stroke_hit_bad, miter_bad, sprite_bad, bleed, group_hit_bad, target_bad;
    long   refused_ok, refused_n, epoch_bad, epoch_control_bad, persist_bad, clut_bad, lost_ok, lost_bad;
    long   edt_bad, alpha_scene_bad, alpha_control, passes_bad;
    double mask_sdf_cpu;   /* the distance texture against the analytic disc, texels */
} stats2;

static stats2 S2;

static int gl_open(int bg_gray, psygfx_format fmt, psygfx_dither dither) {
    psygfx_desc gd;
    memset(&gd, 0, sizeof gd);
    gd.screen = &R->scr;
    gd.background[0] = gd.background[1] = gd.background[2] = bg_gray ? 0.5f : 0.0f;
    psygfx__test_scene32 = fmt == PSYGFX_RGBA32F;
    gd.dither = dither;
    gd.seed = 99;
    if (!psygfx_open(&R->g, &gd)) {
        fprintf(stderr, "psy_gfx_test [%s]: open: %s\n", g_where, psygfx_error(&R->g));
        g_failures++;
        return 0;
    }
    return 1;
}

/* One frame: draw, read the scene and the output back, flip. */
static void gl_frame(const psygfx_stim* s, int n) {
    int rc;
    CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
    CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
    rc = psygfx_draw_n(&R->g, s, n);
    CHECK(rc == PSYGFX_OK);
    CHECK(psygfx_end(&R->g) == PSYGFX_OK);
    CHECK(psygfx_read_scene(&R->g, 0, 0, W, H, R->scene) == PSYGFX_OK);
    CHECK(psygfx_read_output(&R->g, 0, 0, W, H, R->out) == PSYGFX_OK);
    CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
}

static double cov(int edge, double w, double d) {
    if (edge == PSYGFX_EDGE_HARD || w <= 0) return d <= 0 ? 1.0 : 0.0;
    if (edge == PSYGFX_EDGE_COSINE) {
        if (d <= -0.5 * w) return 1.0;
        if (d >= 0.5 * w) return 0.0;
        return 0.5 + 0.5 * cos(3.14159265358979323846 * (d + 0.5 * w) / w);
    }
    return 0.5 * erfc(d / (w * sqrt(2.0)));
}

/* The signed distance of screen pixel (i, j)'s center to the stimulus's
 * aperture, through the transform's inverse; inside the quad or not. */
static double pix_sdf(const psygfx_stim* s, int i, int j, int* in_quad, double* lxo, double* lyo) {
    float lx, ly, hx, hy, sp[3];
    int shape;
    psygfx_local(&R->g, s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
    shape = psygfx__geom(&R->g, s, &hx, &hy, sp);
    if (lxo) *lxo = lx;
    if (lyo) *lyo = ly;
    if (in_quad) *in_quad = fabsf(lx) <= hx && fabsf(ly) <= hy;
    if (shape == PSYGFX_NO_APERTURE || s->kind == PSYGFX_GABOR) return -1e30;
    {
        float ml;
        psygfx__miter(s, shape, &ml);
        return psygfx__sdf(shape, lx, ly, hx, hy, sp, s->p, R->g.ppu * psygfx__scale(s), ml, 0.0);
    }
}

static void gl_shapes(stats* st) {
    static const float tri[6] = { -40, 30, 40, 30, 0, -35 };
    int shape, e, o, a;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    for (shape = PSYGFX_RECT; shape <= PSYGFX_CROSS; shape++)
        for (e = 0; e < 3; e++)
            for (o = 0; o < 2; o++)
                for (a = 0; a < 2; a++) {
                    psygfx_shape_desc sd;
                    psygfx_stim s;
                    int i, j;
                    memset(&sd, 0, sizeof sd);
                    sd.shape = (psygfx_shape_kind)shape;
                    sd.edge = (psygfx_edge)e;
                    sd.edge_width = e == PSYGFX_EDGE_COSINE ? 6.0f : 1.5f;
                    sd.w = shape == PSYGFX_LINE ? 100.0f : 80.0f;
                    sd.h = shape == PSYGFX_RECT ? 50.0f : (shape == PSYGFX_CIRCLE || shape == PSYGFX_ANNULUS ? 80.0f : 70.0f);
                    sd.ori = o ? 30.0f : 0.0f;
                    sd.x = a ? -50.0f : 3.25f;
                    sd.y = a ? -40.0f : -2.5f;
                    sd.anchor = a ? PSYGFX_TOP_LEFT : PSYGFX_CENTER;
                    sd.color[0] = sd.color[1] = sd.color[2] = 1.0f;
                    if (shape == PSYGFX_RECT) sd.shape_p[1] = 8;
                    if (shape == PSYGFX_ANNULUS) sd.shape_p[0] = 20;
                    if (shape == PSYGFX_LINE) sd.shape_p[0] = 6;
                    if (shape == PSYGFX_CROSS) sd.shape_p[0] = 10;
                    if (shape == PSYGFX_POLYGON) { sd.shape_p[0] = 3; sd.vertices = tri; }
                    s = psygfx_shape(&sd);
                    gl_frame(&s, 1);
                    for (j = 0; j < H; j++)
                        for (i = 0; i < W; i++) {
                            double d = pix_sdf(&s, i, j, NULL, NULL, NULL);
                            double want = cov(e, sd.edge_width, d), got = R->scene[(j * W + i) * 4];
                            int hit = psygfx_hit(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f);
                            if (fabs(d) < 1e-3) { st->ties++; continue; }
                            if (e == PSYGFX_EDGE_HARD) st->hard_bad += fabs(got - want) > 1e-6;
                            else st->shape_soft = maxd(st->shape_soft, fabs(got - want));
                            st->hit_bad += hit != (got >= 0.5);
                        }
                }
    psygfx_close(&R->g);
}

static void gl_gratings(stats* st) {
    static const float sfs[3] = { 0.05f, 0.13f, 0.4f }, oris[3] = { 0, 37, 90 }, phases[3] = { 0, 0.25f, 0.7f };
    int k, sq;
    if (!gl_open(1, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    for (sq = 0; sq < 2; sq++)
        for (k = 0; k < 9; k++) {
            psygfx_grating_desc gd;
            psygfx_stim s;
            int i, j;
            memset(&gd, 0, sizeof gd);
            gd.w = gd.h = 150;
            gd.sf = sfs[k % 3]; gd.ori = oris[k / 3]; gd.phase = phases[(k + 1) % 3];
            gd.aperture = PSYGFX_CIRCLE; gd.edge = PSYGFX_EDGE_COSINE; gd.edge_width = 8;
            gd.square = sq;
            gd.x = 5;
            s = psygfx_grating(&gd);
            gl_frame(&s, 1);
            for (j = 0; j < H; j++)
                for (i = 0; i < W; i++) {
                    double lx, ly, d = pix_sdf(&s, i, j, NULL, &lx, &ly), x = s.sf * lx + s.phase;
                    double c = cos(2.0 * 3.14159265358979323846 * x), g;
                    double want, got = R->scene[(j * W + i) * 4];
                    (void)ly;
                    if (sq) {
                        double fr = x - floor(x);
                        if (fabs(c) < 1e-3) continue;   /* a transition at the pixel center */
                        g = (fr < 0.25 || fr >= 0.75) ? 1.0 : -1.0;
                    } else {
                        g = c;
                    }
                    want = 0.5 + 0.5 * g * cov(PSYGFX_EDGE_COSINE, 8, d);
                    if (sq) st->square_bad = maxd(st->square_bad, fabs(got - want));
                    else st->grating = maxd(st->grating, fabs(got - want));
                }
        }
    psygfx_close(&R->g);
}

static double gabor_ref(const psygfx_stim* s, int i, int j, int* in) {
    double lx, ly;
    pix_sdf(s, i, j, in, &lx, &ly);
    return cos(2.0 * 3.14159265358979323846 * (s->sf * lx + s->phase)) *
           exp(-(lx * lx + s->aspect * s->aspect * ly * ly) / (2.0 * s->sigma * s->sigma));
}

static void gl_gabors(stats* st) {
    static float a1[W * H * 4];
    psygfx_gabor_desc gd;
    psygfx_stim s, t, two[2];
    int i, j, k;
    if (!gl_open(1, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    for (k = 0; k < 4; k++) {
        memset(&gd, 0, sizeof gd);
        gd.sigma = 18; gd.sf = 0.08f; gd.phase = 0.3f;
        gd.ori = k & 1 ? 45.0f : 0.0f;
        gd.aspect = k & 2 ? 1.5f : 0.0f;
        gd.x = -20.5f; gd.y = 7;
        s = psygfx_gabor(&gd);
        gl_frame(&s, 1);
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                int in;
                double g = gabor_ref(&s, i, j, &in);
                double want = in ? 0.5 + 0.5 * g : 0.5;
                st->gabor = maxd(st->gabor, fabs(R->scene[(j * W + i) * 4] - want));
            }
    }
    /* The anchor moves the box, not the pattern: the same box placed by its
     * center and by its top-left corner gives the same pixels. */
    memset(&gd, 0, sizeof gd);
    gd.sigma = 15; gd.sf = 0.1f; gd.phase = 0.15f;
    gd.x = 11; gd.y = -6;
    s = psygfx_gabor(&gd);
    gl_frame(&s, 1);
    memcpy(a1, R->scene, sizeof a1);
    gd.anchor = PSYGFX_TOP_LEFT;
    gd.x = 11 - 60; gd.y = -6 - 60;            /* the 8-sigma box is 120 x 120 */
    t = psygfx_gabor(&gd);
    gl_frame(&t, 1);
    for (i = 0; i < W * H * 4; i++) st->anchor_diff = maxd(st->anchor_diff, fabs(a1[i] - R->scene[i]));
    /* overlapping modulations add */
    two[0] = s;
    two[1] = s;
    two[1].x += 25; two[1].ori = 60; two[1].sf = 0.05f;
    gl_frame(&two[1], 1);
    for (i = 0; i < W * H * 4; i += 4) a1[i] += R->scene[i] - 0.5f;   /* a1 still holds s alone */
    gl_frame(two, 2);
    for (i = 0; i < W * H * 4; i += 4) st->overlap = maxd(st->overlap, fabs(a1[i] - R->scene[i]));
    psygfx_close(&R->g);
}

static void gl_dots(stats* st) {
    static const float pos[12] = { -60, -40, 0, 0, 50, 30, 70, -50, -30, 45, 200, 0 };
    psygfx_dots_desc dd;
    psygfx_stim s;
    psygfx_buf b;
    int i, j, k;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    b = psygfx_buffer(&R->g, sizeof pos);
    CHECK(b.id != 0);
    CHECK(psygfx_buffer_update(&R->g, b, 0, pos, sizeof pos) == PSYGFX_OK);
    memset(&dd, 0, sizeof dd);
    dd.buf = b; dd.count = 6; dd.dot_size = 10; dd.edge = PSYGFX_EDGE_COSINE; dd.edge_width = 2;
    dd.color[0] = dd.color[1] = dd.color[2] = 1;
    dd.aperture = PSYGFX_CIRCLE; dd.w = 200;    /* the last dot is outside */
    dd.ori = 20; dd.x = 8; dd.y = 3;
    s = psygfx_dots(&dd);
    gl_frame(&s, 1);
    for (j = 0; j < H; j++)
        for (i = 0; i < W; i++) {
            float lx, ly;
            double want = 0;
            psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
            for (k = 0; k < 5; k++) {
                double dx = lx - pos[2 * k], dy = ly - pos[2 * k + 1];
                want = maxd(want, cov(PSYGFX_EDGE_COSINE, 2, sqrt(dx * dx + dy * dy) - 5));
            }
            st->dots = maxd(st->dots, fabs(R->scene[(j * W + i) * 4] - want));
        }
    {   /* ring dots: a stroke on each dot, in a field with no aperture */
        int e, al;
        for (e = 0; e < 2; e++)
            for (al = 0; al < 3; al++) {
                float a1, a2;
                dd.aperture = PSYGFX_NO_APERTURE;
                dd.edge = e ? PSYGFX_EDGE_COSINE : PSYGFX_EDGE_HARD;
                dd.stroke = 3; dd.stroke_align = (psygfx_stroke_align)al;
                s = psygfx_dots(&dd);
                psygfx__band(&s, &a1, &a2);
                gl_frame(&s, 1);
                for (j = 0; j < H; j++)
                    for (i = 0; i < W; i++) {
                        float lx, ly;
                        double want = 0, got = R->scene[(j * W + i) * 4];
                        int tie = 0;
                        psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                        for (k = 0; k < 6; k++) {
                            double dx = lx - pos[2 * k], dy = ly - pos[2 * k + 1], d = sqrt(dx * dx + dy * dy) - 5;
                            want = maxd(want, cov(dd.edge, 2, d - a2) - cov(dd.edge, 2, d - a1));
                            tie |= fabs(d - a2) < 1e-3 || fabs(d - a1) < 1e-3;
                        }
                        if (e) S2.stroke_soft = maxd(S2.stroke_soft, fabs(got - want));
                        else if (!tie) S2.stroke_hard_bad += fabs(got - want) > 1e-6;
                    }
            }
    }
    psygfx_close(&R->g);
}

static void gl_images(stats* st) {
    static const psygfx_format fmts[] = { PSYGFX_R8, PSYGFX_RG8, PSYGFX_RGBA8, PSYGFX_R16F, PSYGFX_RGBA16F,
                                          PSYGFX_R32F, PSYGFX_RGBA32F, PSYGFX_R16UI };
    unsigned char u8[8 * 4 * 4];
    uint16_t u16[8 * 4];
    float f32[8 * 4 * 4];
    int fi, k;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    for (fi = 0; fi < (int)(sizeof fmts / sizeof fmts[0]); fi++) {
        psygfx_format fm = fmts[fi];
        int ch = fm == PSYGFX_RG8 ? 2 : ((fm == PSYGFX_RGBA8 || fm == PSYGFX_RGBA16F || fm == PSYGFX_RGBA32F) ? 4 : 1);
        psygfx_texture_desc td;
        psygfx_image_desc idd;
        psygfx_stim s;
        psygfx_tex t;
        const void* data;
        int x, y;
        for (k = 0; k < 8 * 4 * 4; k++) {
            u8[k] = (unsigned char)((k * 37 + 11) & 255);
            f32[k] = (float)((k * 13) % 64) / 64.0f;   /* exact in half */
        }
        for (k = 0; k < 8 * 4; k++) u16[k] = (uint16_t)(k * 2039 + 7);
        if (ch == 2) for (k = 0; k < 8 * 4; k++) u8[2 * k + 1] = 255;
        if (ch == 4) for (k = 0; k < 8 * 4; k++) { u8[4 * k + 3] = 255; f32[4 * k + 3] = 1.0f; }
        data = fm == PSYGFX_R16UI ? (const void*)u16 : ((fm == PSYGFX_R8 || fm == PSYGFX_RG8 || fm == PSYGFX_RGBA8) ? (const void*)u8 : (const void*)f32);
        memset(&td, 0, sizeof td);
        td.w = 8; td.h = 4; td.format = fm; td.data = data;
        t = psygfx_texture(&R->g, &td);
        CHECK(t.id != 0);
        memset(&idd, 0, sizeof idd);
        idd.tex = t; idd.place = PSYGFX_TOP_LEFT; idd.anchor = PSYGFX_TOP_LEFT; idd.x = 10; idd.y = 20;
        s = psygfx_image(&R->g, &idd);
        gl_frame(&s, 1);
        for (y = 0; y < 4; y++)
            for (x = 0; x < 8; x++) {
                const float* px = &R->scene[((20 + y) * W + 10 + x) * 4];
                int c;
                for (c = 0; c < 3; c++) {
                    double want;
                    int src = ch == 4 ? c : 0;
                    if (fm == PSYGFX_R16UI) want = (float)u16[y * 8 + x] * (1.0f / 65535.0f);
                    else if (data == u8) want = u8[(y * 8 + x) * ch + src] / 255.0;
                    else want = f32[(y * 8 + x) * ch + src];
                    st->image = maxd(st->image, fabs(px[c] - want));
                }
            }
        psygfx_texture_free(&R->g, t);
    }
    psygfx_close(&R->g);
}

static void gl_noise(stats* st) {
    psygfx_noise_desc nd;
    psygfx_stim s;
    int i, j, dist;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    for (dist = 0; dist < 2; dist++) {
        memset(&nd, 0, sizeof nd);
        nd.w = 64; nd.h = 48; nd.check = 2; nd.seed = 1234 + dist;
        nd.dist = (psygfx_noise_dist)dist;
        nd.aperture = PSYGFX_NO_APERTURE;
        nd.dir[0] = nd.dir[1] = nd.dir[2] = 1.0f;
        nd.ori = dist ? 90.0f : 0.0f;
        nd.x = -30; nd.y = 10;
        s = psygfx_noise(&nd);
        gl_frame(&s, 1);
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                float lx, ly;
                int cx, cy;
                psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                if (fabsf(lx) >= 32 || fabsf(ly) >= 24) continue;
                cx = (int)floor((lx + 32.0) / 2.0);
                cy = (int)floor((ly + 24.0) / 2.0);
                st->noise_bad += R->scene[(j * W + i) * 4] != psygfx_noise_value(cx, cy, nd.seed, nd.dist);
            }
    }
    psygfx_close(&R->g);
}

/* An image of known values into the scene, then the output stage. */
static psygfx_stim ramp_image(psygfx_tex* t, const float* v, int n) {
    psygfx_texture_desc td;
    psygfx_image_desc idd;
    memset(&td, 0, sizeof td);
    td.w = n; td.h = 1; td.format = PSYGFX_R32F; td.data = v;
    *t = psygfx_texture(&R->g, &td);
    memset(&idd, 0, sizeof idd);
    idd.tex = *t; idd.place = PSYGFX_TOP_LEFT; idd.anchor = PSYGFX_TOP_LEFT;
    idd.h = 16;   /* each texel 1 px wide, 16 px tall */
    return psygfx_image(&R->g, &idd);
}

static void gl_output(stats* st) {
    static float v[256];
    static const float lut[3 * 5] = { 0, 0.1f, 0.35f, 0.7f, 1, 0, 0.5f, 0.7f, 0.86f, 1, 0, 0.25f, 0.5f, 0.75f, 1 };
    psygfx_tex t;
    psygfx_stim s;
    int k, c, i, j;
    for (k = 0; k < 256; k++) v[k] = (float)k / 255.0f;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    s = ramp_image(&t, v, 256);
    gl_frame(&s, 1);
    for (k = 0; k < 256; k++) for (c = 0; c < 3; c++) st->out_bad += R->out[(5 * W + k) * 4 + c] != k;
    psygfx_texture_free(&R->g, t);
    /* rounds to nearest: 0.005 of a code above each halfway point goes up */
    for (k = 0; k < 255; k++) v[k] = ((float)k + 0.505f) / 255.0f;
    v[255] = 1.0f;
    s = ramp_image(&t, v, 256);
    gl_frame(&s, 1);
    for (k = 0; k < 255; k++) for (c = 0; c < 3; c++) st->out_bad += R->out[(5 * W + k) * 4 + c] != k + 1;
    psygfx_texture_free(&R->g, t);
    for (k = 0; k < 256; k++) v[k] = (float)k / 255.0f;
    s = ramp_image(&t, v, 256);
    /* a CLUT */
    CHECK(psygfx_set_lut(&R->g, lut, 5) == PSYGFX_OK);
    gl_frame(&s, 1);
    for (k = 0; k < 256; k++)
        for (c = 0; c < 3; c++) {
            double x = v[k] * 4.0, d, code;
            int i0 = (int)x < 3 ? (int)x : 3;
            d = lut[c * 5 + i0] + (lut[c * 5 + i0 + 1] - lut[c * 5 + i0]) * (x - i0);
            code = d * 255.0 + 0.5;
            if (fabs(code - floor(code) - 0.5) > 0.5 - 1e-3) { st->ties++; continue; }
            st->lut_bad += R->out[(5 * W + k) * 4 + c] != (int)floor(code);
        }
    psygfx_close(&R->g);
    /* ordered dither: Bayer by bit interleaving, on the scene's grid */
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_ORDERED)) return;
    /* every threshold of the 8 x 8 matrix: 64 fractional levels across */
    for (k = 0; k < 256; k++) v[k] = (100.0f + ((float)(k % 64) + 0.25f) / 64.0f) / 255.0f;
    s = ramp_image(&t, v, 256);
    s.h = 64;
    gl_frame(&s, 1);
    for (j = 0; j < 64; j++)
        for (i = 0; i < 256; i++) {
            int x = i & 7, y = (H - 1 - j) & 7, z = x ^ y;
            int b = ((z & 1) << 5) | ((x & 1) << 4) | ((z & 2) << 2) | ((x & 2) << 1) | ((z & 4) >> 1) | ((x & 4) >> 2);
            double code = (double)v[i] * 255.0 + (b + 0.5) / 64.0;
            if (fabs(code - floor(code)) < 1e-3) { st->ties++; continue; }
            st->dither_bad += R->out[(j * W + i) * 4] != (int)floor(code);
        }
    psygfx_close(&R->g);
    /* noise dither: unbiased, a new pattern each frame */
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NOISE)) return;
    for (k = 0; k < 256; k++) v[k] = 100.3f / 255.0f;
    s = ramp_image(&t, v, 256);
    s.h = 64;
    {
        static uint8_t first[64 * 256];
        double sum = 0;
        long changed = 0;
        gl_frame(&s, 1);
        for (j = 0; j < 64; j++) for (i = 0; i < 256; i++) { first[j * 256 + i] = R->out[(j * W + i) * 4]; sum += first[j * 256 + i]; }
        gl_frame(&s, 1);
        for (j = 0; j < 64; j++) for (i = 0; i < 256; i++) { changed += R->out[(j * W + i) * 4] != first[j * 256 + i]; sum += R->out[(j * W + i) * 4]; }
        st->dither_mean = sum / (2.0 * 64 * 256) - 100.3;
        CHECK(changed > 1000);
    }
    psygfx_close(&R->g);
}

static void gl_user_batch_rows(stats* st) {
    static float a1[W * H * 4];
    psygfx_pipeline_desc pd;
    psygfx_pipe p;
    psygfx_user_desc ud;
    psygfx_grating_desc gd;
    psygfx_stim s[300], u, gr;
    psygfx_shape_desc sd;
    static const float tri3[6] = { -5, 4, 5, 4, 0, -5 };
    static const unsigned char texa[16] = { 10, 200, 30, 90, 250, 0, 60, 120, 5, 15, 25, 35, 45, 55, 65, 75 };
    static const unsigned char texb[16] = { 255, 0, 255, 0, 128, 128, 128, 128, 1, 2, 3, 4, 5, 6, 7, 8 };
    psygfx_tex ta, tb;
    psygfx_texture_desc tdd;
    uint32_t seed = 5;
    int i, j;
    if (!gl_open(1, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    /* a user shader that is a grating, against the grating */
    memset(&pd, 0, sizeof pd);
    pd.body = "float psy_main(vec2 p) {\n    return cos(2.0 * psy_PI * fract(psy_param(0) * p.x + psy_param(1)));\n}\n";
    pd.mode = PSYGFX_MODULATION;
    p = psygfx_pipeline(&R->g, &pd);
    CHECK(p.id != 0);
    memset(&ud, 0, sizeof ud);
    {
        static const float pp[2] = { 0.07f, 0.4f };
        ud.pipe = p; ud.w = ud.h = 120; ud.ori = 15; ud.p = pp; ud.n_p = 2;
    }
    u = psygfx_user(&ud);
    gl_frame(&u, 1);
    memcpy(a1, R->scene, sizeof a1);
    memset(&gd, 0, sizeof gd);
    gd.w = gd.h = 120; gd.ori = 15; gd.sf = 0.07f; gd.phase = 0.4f;
    gr = psygfx_grating(&gd);
    gl_frame(&gr, 1);
    for (i = 0; i < W * H * 4; i++) st->user = maxd(st->user, fabs(a1[i] - R->scene[i]));
    /* the compiler's log names the body's own line */
    pd.body = "float psy_main(vec2 p) {\n    float a = 1.0;\n    return a + bogus;\n}\n";
    p = psygfx_pipeline(&R->g, &pd);
    CHECK(p.id == 0 && strstr(psygfx_error(&R->g), "0:3") != NULL);   /* ANGLE 0:3:, Mesa 0:3( */
    if (p.id != 0 || !strstr(psygfx_error(&R->g), "0:3")) fprintf(stderr, "  log: %s\n", psygfx_error(&R->g));
    /* batched and one draw each: the same bits */
    memset(&tdd, 0, sizeof tdd);
    tdd.w = 4; tdd.h = 4; tdd.format = PSYGFX_R8;
    tdd.data = texa;
    ta = psygfx_texture(&R->g, &tdd);
    tdd.data = texb;
    tb = psygfx_texture(&R->g, &tdd);
    for (i = 0; i < 300; i++) {
        seed = seed * 1664525u + 1013904223u;
        if ((seed & 0x7000) == 0x7000) {   /* an image, from one of two textures */
            psygfx_image_desc idd;
            memset(&idd, 0, sizeof idd);
            idd.tex = (seed & 0x8000) ? ta : tb;
            idd.w = idd.h = 12;
            idd.x = (float)((int)((seed >> 3) % W) - W / 2); idd.y = (float)((int)((seed >> 11) % H) - H / 2);
            s[i] = psygfx_image(&R->g, &idd);
        } else if (seed & 0x100) {
            memset(&sd, 0, sizeof sd);
            sd.shape = (psygfx_shape_kind)((seed >> 9) % 6);
            sd.shape_p[0] = 3;
            sd.w = (float)(10 + (seed >> 12) % 30); sd.h = (float)(10 + (seed >> 17) % 30);
            sd.x = (float)((int)((seed >> 3) % W) - W / 2); sd.y = (float)((int)((seed >> 11) % H) - H / 2);
            sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 3;
            sd.color[0] = (float)(seed % 7) / 7.0f; sd.color[1] = 0.3f; sd.color[2] = 0.6f; sd.opacity = 0.5f;
            sd.vertices = tri3;
            s[i] = psygfx_shape(&sd);
        } else {
            psygfx_gabor_desc gb;
            memset(&gb, 0, sizeof gb);
            gb.sigma = (float)(3 + seed % 6); gb.sf = 0.1f; gb.contrast = 0.1f;
            gb.x = (float)((int)((seed >> 3) % W) - W / 2); gb.y = (float)((int)((seed >> 11) % H) - H / 2);
            s[i] = psygfx_gabor(&gb);
        }
    }
    gl_frame(s, 300);
    memcpy(a1, R->scene, sizeof a1);
    R->g.max_batch = 1;
    gl_frame(s, 300);
    R->g.max_batch = PSYGFX__BATCH;
    for (i = 0; i < W * H * 4; i++) st->batch_diff = maxd(st->batch_diff, fabs(a1[i] - R->scene[i]));
    psygfx_close(&R->g);
    /* rows: a rect placed 3, 4 px in from the top-left corner is there in
     * the scene and in the output */
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    memset(&sd, 0, sizeof sd);
    sd.place = PSYGFX_TOP_LEFT; sd.anchor = PSYGFX_TOP_LEFT; sd.x = 3; sd.y = 4; sd.w = 10; sd.h = 6;
    sd.color[0] = sd.color[1] = sd.color[2] = 1;
    s[0] = psygfx_shape(&sd);
    gl_frame(s, 1);
    for (j = 0; j < 14; j++)
        for (i = 0; i < 16; i++) {
            int in = i >= 3 && i < 13 && j >= 4 && j < 10;
            CHECK((R->scene[(j * W + i) * 4] == 1.0f) == in);
            CHECK((R->out[(j * W + i) * 4] == 255) == in);
        }
    psygfx_close(&R->g);
}


/* ---------------------------------------------------------------- v0.2 */


/* The band's coverage on the CPU: the fill's coverage at a2 minus at a1. */
static double band_ref(const psygfx_stim* s, int i, int j, int* tie) {
    float lx, ly, hx, hy, sp[3], a1, a2, ml;
    int shape;
    double k = R->g.ppu * psygfx__scale(s), d1, d2, w = s->edge == PSYGFX_EDGE_HARD ? 0.0 : s->edge_width;
    psygfx_local(&R->g, s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
    shape = psygfx__geom(&R->g, s, &hx, &hy, sp);
    psygfx__miter(s, shape, &ml);
    if (s->stroke <= 0.0f) { a1 = -1e30f; a2 = 0.0f; } else psygfx__band(s, &a1, &a2);
    d2 = psygfx__sdf(shape, lx, ly, hx, hy, sp, s->p, k, ml, a2);
    if (s->stroke <= 0.0f) { *tie = fabs(d2) < 1e-3; return cov(s->edge, w, d2); }
    d1 = psygfx__sdf(shape, lx, ly, hx, hy, sp, s->p, k, ml, a1);
    *tie = fabs(d2 - a2) < 1e-3 || fabs(d1 - a1) < 1e-3;
    return cov(s->edge, w, d2 - a2) - cov(s->edge, w, d1 - a1);
}

static void gl_strokes(stats* st) {
    static const float tri[6] = { -40, 30, 40, 30, 0, -35 };
    static const float sharp[6] = { -60, 20, 60, 20, 0, 5 };    /* a 22 degree apex at the bottom */
    static const float concave[8] = { -30, -30, 30, -30, 0, 0, -30, 30 };
    int shape, e, al, o, jn;
    (void)st;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    for (shape = PSYGFX_RECT; shape <= PSYGFX_CROSS; shape++)
        for (e = 0; e < 3; e++)
            for (al = 0; al < 3; al++)
                for (o = 0; o < 2; o++)
                    for (jn = 0; jn < 2; jn++) {
                        psygfx_shape_desc sd;
                        psygfx_stim s;
                        int i, j, tie;
                        int miter_ok = shape == PSYGFX_RECT || shape == PSYGFX_CROSS || shape == PSYGFX_POLYGON;
                        if (jn && !miter_ok) continue;
                        memset(&sd, 0, sizeof sd);
                        sd.shape = (psygfx_shape_kind)shape;
                        sd.edge = (psygfx_edge)e;
                        sd.edge_width = e == PSYGFX_EDGE_COSINE ? 3.0f : 1.0f;
                        sd.w = shape == PSYGFX_LINE ? 100.0f : 80.0f;
                        sd.h = shape == PSYGFX_RECT ? 50.0f : (shape == PSYGFX_CIRCLE || shape == PSYGFX_ANNULUS ? 80.0f : 70.0f);
                        sd.ori = o ? 30.0f : 0.0f;
                        sd.x = 2.25f; sd.y = -1.5f;
                        sd.color[0] = sd.color[1] = sd.color[2] = 1.0f;
                        sd.stroke = 6.0f;
                        sd.stroke_align = (psygfx_stroke_align)al;
                        sd.join = jn ? PSYGFX_JOIN_MITER : PSYGFX_JOIN_ROUND;
                        if (shape == PSYGFX_RECT && !jn) sd.shape_p[1] = 8;
                        if (shape == PSYGFX_ANNULUS) sd.shape_p[0] = 20;
                        if (shape == PSYGFX_LINE) sd.shape_p[0] = 6;
                        if (shape == PSYGFX_CROSS) sd.shape_p[0] = 14;
                        if (shape == PSYGFX_POLYGON) { sd.shape_p[0] = 3; sd.vertices = tri; }
                        s = psygfx_shape(&sd);
                        gl_frame(&s, 1);
                        for (j = 0; j < H; j++)
                            for (i = 0; i < W; i++) {
                                double want = band_ref(&s, i, j, &tie), got = R->scene[(j * W + i) * 4];
                                int hit = psygfx_hit(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f);
                                if (tie) { st->ties++; continue; }
                                if (e == PSYGFX_EDGE_HARD) {
                                    S2.stroke_hard_bad += fabs(got - want) > 1e-6;
                                    S2.stroke_hit_bad += hit != (got >= 0.5);
                                } else {
                                    S2.stroke_soft = maxd(S2.stroke_soft, fabs(got - want));
                                }
                            }
                    }
    {   /* the alignments by hand, not through the header's helpers: a
         * circle of radius 40, a hard 6 px band, along +x from its center */
        static const double r[4] = { 35.5, 38.5, 41.5, 44.5 };
        static const int want[3][4] = { { 0, 1, 1, 0 },     /* CENTER: 37 to 43  */
                                        { 1, 1, 0, 0 },     /* INSIDE: 34 to 40  */
                                        { 0, 0, 1, 1 } };   /* OUTSIDE: 40 to 46 */
        psygfx_shape_desc sd;
        psygfx_stim s;
        int a, q;
        for (a = 0; a < 3; a++) {
            memset(&sd, 0, sizeof sd);
            sd.shape = PSYGFX_CIRCLE; sd.w = 80; sd.stroke = 6; sd.stroke_align = (psygfx_stroke_align)a;
            sd.color[0] = sd.color[1] = sd.color[2] = 1;
            s = psygfx_shape(&sd);
            gl_frame(&s, 1);
            for (q = 0; q < 4; q++) {
                int i = (int)(W / 2 + r[q] - 0.5);
                S2.miter_bad += (R->scene[(H / 2 * W + i) * 4] == 1.0f) != want[a][q];
            }
        }
    }
    {   /* a thin band (narrower than the edge) never reaches 1 */
        psygfx_shape_desc sd;
        psygfx_stim s;
        int i, j, tie;
        double peak = 0;
        memset(&sd, 0, sizeof sd);
        sd.shape = PSYGFX_CIRCLE; sd.w = 120; sd.edge = PSYGFX_EDGE_GAUSSIAN; sd.edge_width = 2;
        sd.stroke = 1; sd.color[0] = sd.color[1] = sd.color[2] = 1;
        s = psygfx_shape(&sd);
        gl_frame(&s, 1);
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                double got = R->scene[(j * W + i) * 4];
                S2.stroke_soft = maxd(S2.stroke_soft, fabs(got - band_ref(&s, i, j, &tie)));
                peak = maxd(peak, got);
            }
        CHECK(peak > 0.15 && peak < 0.25);   /* 2 erf(0.25 / sqrt 2) = 0.197 */
    }
    {   /* MITER: the corner pixel a round join leaves empty is covered; an
         * acute corner is beveled at the limit */
        psygfx_shape_desc sd;
        psygfx_stim s;
        float cx, cy;
        int px, py, tie, k;
        memset(&sd, 0, sizeof sd);
        sd.shape = PSYGFX_RECT; sd.w = 60; sd.h = 40; sd.stroke = 8; sd.stroke_align = PSYGFX_STROKE_OUTSIDE;
        sd.color[0] = sd.color[1] = sd.color[2] = 1;
        sd.join = PSYGFX_JOIN_MITER;
        s = psygfx_shape(&sd);
        gl_frame(&s, 1);
        psygfx_resolve(&R->g, &s, &cx, &cy);
        px = (int)(cx + 30 + 6); py = (int)(cy + 20 + 6);   /* 6, 6 px out of the corner: Chebyshev 6, Euclidean 8.5 */
        S2.miter_bad += R->scene[(py * W + px) * 4] != 1.0f;
        sd.join = PSYGFX_JOIN_ROUND;
        s = psygfx_shape(&sd);
        gl_frame(&s, 1);
        S2.miter_bad += R->scene[(py * W + px) * 4] != 0.0f;
        /* a limit below sqrt 2 bevels a right angle: |dx| + |dy| <= 8 out
         * of the corner, so (6, 6) is empty; RECT and CROSS against the CPU */
        sd.join = PSYGFX_JOIN_MITER; sd.miter_limit = 1.2f;
        for (k = 0; k < 2; k++) {
            int i, j;
            sd.shape = k ? PSYGFX_CROSS : PSYGFX_RECT;
            sd.shape_p[0] = k ? 14.0f : 0.0f;
            sd.ori = k ? 20.0f : 0.0f;
            s = psygfx_shape(&sd);
            gl_frame(&s, 1);
            if (!k) S2.miter_bad += R->scene[(py * W + px) * 4] != 0.0f || R->scene[((py - 3) * W + px - 3) * 4] != 1.0f;
            for (j = 0; j < H; j++)
                for (i = 0; i < W; i++) {
                    double want = band_ref(&s, i, j, &tie);
                    if (!tie) S2.miter_bad += fabs(R->scene[(j * W + i) * 4] - want) > 1e-6;
                }
        }
        sd.shape_p[0] = 0; sd.ori = 0; sd.miter_limit = 0;
        /* the sharp triangle, beveled */
        memset(&sd, 0, sizeof sd);
        sd.shape = PSYGFX_POLYGON; sd.shape_p[0] = 3; sd.vertices = sharp; sd.w = 120; sd.h = 30;
        sd.stroke = 6; sd.stroke_align = PSYGFX_STROKE_OUTSIDE; sd.join = PSYGFX_JOIN_MITER; sd.y = -40;
        sd.color[0] = sd.color[1] = sd.color[2] = 1;
        s = psygfx_shape(&sd);
        gl_frame(&s, 1);
        {
            int i, j;
            for (j = 0; j < H; j++)
                for (i = 0; i < W; i++) {
                    double want = band_ref(&s, i, j, &tie);
                    if (!tie) S2.miter_bad += fabs(R->scene[(j * W + i) * 4] - want) > 1e-6;
                }
        }
        /* refusals: MITER on a curve and on a concave polygon */
        sd.vertices = concave; sd.shape_p[0] = 4;
        s = psygfx_shape(&sd);
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        S2.refused_n++; S2.refused_ok += psygfx_draw(&R->g, &s) == PSYGFX_ERR_ARG;
        sd.shape = PSYGFX_CIRCLE;
        s = psygfx_shape(&sd);
        S2.refused_n++; S2.refused_ok += psygfx_draw(&R->g, &s) == PSYGFX_ERR_ARG;
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
    }
    psygfx_close(&R->g);
}

/* The CPU's bilinear interpolation of a distance texture, as the shader does
 * it: texel centers at + 0.5, indices clamped to the texture. */
static double bilinear_ref(const float* t, int tw, int th, double x, double y) {
    double fx = x - 0.5, fy = y - 0.5, ix = floor(fx), iy = floor(fy), wx = fx - ix, wy = fy - iy;
    int x0 = (int)ix, y0 = (int)iy, x1 = x0 + 1, y1 = y0 + 1;
    double c00, c10, c01, c11;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > tw - 1) x1 = tw - 1;
    if (y1 > th - 1) y1 = th - 1;
    if (x0 > tw - 1) x0 = tw - 1;
    if (y0 > th - 1) y0 = th - 1;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    c00 = t[y0 * tw + x0]; c10 = t[y0 * tw + x1]; c01 = t[y1 * tw + x0]; c11 = t[y1 * tw + x1];
    return (c00 + (c10 - c00) * wx) + ((c01 + (c11 - c01) * wx) - (c00 + (c10 - c00) * wx)) * wy;
}

static void gl_masks(stats* st) {
    enum { MW = 64, PAD = 8, TW = MW + 2 * PAD };
    static uint8_t mask[MW * MW];
    static float sdf[TW * TW];
    int x, y, k, sc;
    psygfx_texture_desc td;
    psygfx_tex t;
    (void)st;
    for (y = 0; y < MW; y++)
        for (x = 0; x < MW; x++) {
            double dx = x + 0.5 - 32, dy = y + 0.5 - 32;
            mask[y * MW + x] = (uint8_t)(dx * dx + dy * dy <= 24.0 * 24.0 ? 255 : 0);
        }
    {   /* the distance transform against brute force on 20 random masks:
         * every texel to the nearest texel center of the other kind, the
         * padding outside */
        static uint8_t rm[23 * 17];
        static float ro[31 * 25];
        uint32_t seed = 12345u;
        int m, rw, rh, rp, RW, RH, i, j, u, v2;
        for (m = 0; m < 20; m++) {
            rw = 5 + m % 19; rh = 3 + (m * 7) % 15; rp = m % 5;
            RW = rw + 2 * rp; RH = rh + 2 * rp;
            for (i = 0; i < rw * rh; i++) {
                seed = seed * 1664525u + 1013904223u;
                rm[i] = (uint8_t)((seed >> 24) < (uint32_t)(40 + 10 * m) ? 255 : (seed >> 16) & 127);
            }
            rm[0] = 255; rm[rw * rh - 1] = 0;   /* both kinds, always */
            CHECK(psygfx_sdf_from_mask(ro, rm, rw, rh, rp) == PSYGFX_OK);
            for (j = 0; j < RH; j++)
                for (i = 0; i < RW; i++) {
                    int mx = i - rp, my = j - rp;
                    int in0 = mx >= 0 && my >= 0 && mx < rw && my < rh && rm[my * rw + mx] >= 128;
                    double best = 1e30;
                    float want;
                    for (v2 = 0; v2 < RH; v2++)
                        for (u = 0; u < RW; u++) {
                            int nx = u - rp, ny = v2 - rp;
                            int in1 = nx >= 0 && ny >= 0 && nx < rw && ny < rh && rm[ny * rw + nx] >= 128;
                            if (in1 != in0) {
                                double d2 = (double)(u - i) * (u - i) + (double)(v2 - j) * (v2 - j);
                                if (d2 < best) best = d2;
                            }
                        }
                    want = in0 ? (float)(0.5 - sqrt(best)) : (float)(sqrt(best) - 0.5);
                    S2.edt_bad += ro[j * RW + i] != want;
                }
        }
    }
    CHECK(psygfx_sdf_from_mask(sdf, mask, MW, MW, PAD) == PSYGFX_OK);
    for (y = 0; y < TW; y++)
        for (x = 0; x < TW; x++) {
            double dx = x + 0.5 - (32 + PAD), dy = y + 0.5 - (32 + PAD);
            S2.mask_sdf_cpu = maxd(S2.mask_sdf_cpu, fabs(sdf[y * TW + x] - (sqrt(dx * dx + dy * dy) - 24.0)));
        }
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    memset(&td, 0, sizeof td);
    td.w = TW; td.h = TW; td.format = PSYGFX_R32F; td.data = sdf;
    t = psygfx_texture(&R->g, &td);
    CHECK(t.id != 0);
    for (sc = 1; sc <= 2; sc++)
        for (k = 0; k < 2; k++) {
            psygfx_shape_desc sd;
            psygfx_stim s;
            int i, j;
            double box = TW * sc;
            memset(&sd, 0, sizeof sd);
            sd.shape = PSYGFX_MASK_TEX; sd.mask = t; sd.w = sd.h = (float)box;
            sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 2;
            sd.ori = 17; sd.x = 3;
            if (k) { sd.stroke = 5; sd.stroke_align = PSYGFX_STROKE_CENTER; }
            sd.color[0] = sd.color[1] = sd.color[2] = 1;
            s = psygfx_shape(&sd);
            gl_frame(&s, 1);
            for (j = 0; j < H; j++)
                for (i = 0; i < W; i++) {
                    float lx, ly;
                    double tx, ty, d, want;
                    psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                    if (fabs(lx) > 0.5 * box || fabs(ly) > 0.5 * box) continue;
                    tx = (lx + 0.5 * box) / sc;
                    ty = (ly + 0.5 * box) / sc;
                    d = bilinear_ref(sdf, TW, TW, tx, ty) * sc;
                    want = k ? cov(1, 2, d - 2.5) - cov(1, 2, d + 2.5) : cov(1, 2, d);
                    S2.mask_gpu = maxd(S2.mask_gpu, fabs(R->scene[(j * W + i) * 4] - want));
                }
        }
    {   /* a non-uniform fit is refused */
        psygfx_shape_desc sd;
        psygfx_stim s;
        memset(&sd, 0, sizeof sd);
        sd.shape = PSYGFX_MASK_TEX; sd.mask = t; sd.w = 80; sd.h = 60;
        s = psygfx_shape(&sd);
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        S2.refused_n++; S2.refused_ok += psygfx_draw(&R->g, &s) == PSYGFX_ERR_ARG;
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
    }
    psygfx_close(&R->g);
}

static void gl_sprites(stats* st) {
    enum { AW = 24 };
    static unsigned char atlas[AW * AW * 4];
    psygfx_texture_desc td;
    psygfx_tex t;
    int sp, mode;
    (void)st;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    memset(&td, 0, sizeof td);
    td.w = AW; td.h = AW; td.format = PSYGFX_RGBA8;
    t = psygfx_texture(&R->g, &td);
    for (sp = 0; sp < 9; sp++) {
        int sx = (sp % 3) * 8, sy = (sp / 3) * 8, x, y;
        /* this sprite's texels: blue 0; every other texel: blue 255 */
        for (y = 0; y < AW; y++)
            for (x = 0; x < AW; x++) {
                unsigned char* q = &atlas[(y * AW + x) * 4];
                int own = x >= sx && x < sx + 8 && y >= sy && y < sy + 8;
                q[0] = (unsigned char)(17 * x + 3 * y + 11 * sp);
                q[1] = (unsigned char)(5 * x + 23 * y);
                q[2] = own ? 0 : 255;
                q[3] = 255;
            }
        CHECK(psygfx_texture_update(&R->g, t, 0, 0, AW, AW, atlas, 0) == PSYGFX_OK);
        for (mode = 0; mode < 3; mode++) {
            psygfx_image_desc idd;
            psygfx_stim s;
            float scale = mode == 0 ? 1.0f : (mode == 1 ? 3.0f : 2.5f);
            int i, j;
            memset(&idd, 0, sizeof idd);
            idd.tex = t; idd.place = PSYGFX_TOP_LEFT; idd.anchor = PSYGFX_TOP_LEFT; idd.x = 7; idd.y = 5;
            idd.src[0] = (float)sx; idd.src[1] = (float)sy; idd.src[2] = 8; idd.src[3] = 8;
            idd.w = idd.h = 8 * scale;
            idd.linear = mode == 2;
            s = psygfx_image(&R->g, &idd);
            gl_frame(&s, 1);
            for (j = 5; j < 5 + (int)(8 * scale); j++)
                for (i = 7; i < 7 + (int)(8 * scale); i++) {
                    const float* px = &R->scene[(j * W + i) * 4];
                    double u = (i + 0.5 - 7) / scale, v = (j + 0.5 - 5) / scale;
                    S2.bleed += px[2] != 0.0f;
                    if (mode < 2) {
                        int tx = sx + (int)floor(u), ty = sy + (int)floor(v);
                        S2.sprite_bad += fabs(px[0] - atlas[(ty * AW + tx) * 4] / 255.0) > 2.2e-7;
                    } else {
                        /* the same clamped bilinear on the CPU, red channel */
                        static float red[AW * AW];
                        double fx = sx + u - 0.5, fy = sy + v - 0.5, ix = floor(fx), iy = floor(fy), wx = fx - ix, wy = fy - iy;
                        int x0 = (int)ix, y0 = (int)iy, x1 = x0 + 1, y1 = y0 + 1, q;
                        double want;
                        for (q = 0; q < AW * AW; q++) red[q] = atlas[q * 4] / 255.0f;
                        if (x0 < sx) x0 = sx;
                        if (y0 < sy) y0 = sy;
                        if (x1 > sx + 7) x1 = sx + 7;
                        if (y1 > sy + 7) y1 = sy + 7;
                        if (x0 > sx + 7) x0 = sx + 7;
                        if (y0 > sy + 7) y0 = sy + 7;
                        want = (red[y0 * AW + x0] + (red[y0 * AW + x1] - red[y0 * AW + x0]) * wx) * (1 - wy) +
                               (red[y1 * AW + x0] + (red[y1 * AW + x1] - red[y1 * AW + x0]) * wx) * wy;
                        S2.sprite_lin = maxd(S2.sprite_lin, fabs(px[0] - want));
                    }
                }
        }
    }
    {   /* a flipbook: binding src_x moves the frame */
        psygfx_image_desc idd;
        psygfx_stim s;
        psygfx_bind b;
        float vals[1] = { 16.0f };
        memset(&idd, 0, sizeof idd);
        idd.tex = t; idd.place = PSYGFX_TOP_LEFT; idd.anchor = PSYGFX_TOP_LEFT;
        idd.src[2] = 8; idd.src[3] = 8;
        s = psygfx_image(&R->g, &idd);
        memset(&b, 0, sizeof b);
        b.stim = &s; b.param = PSYGFX_P_SRC_X; b.channel = 0;
        CHECK(psygfx_apply(&b, 1, vals) == 1 && s.src[0] == 16.0f);
        gl_frame(&s, 1);
        S2.sprite_bad += fabs(R->scene[0] - atlas[16 * 4] / 255.0) > 2.2e-7;
    }
    psygfx_close(&R->g);
}

static void gl_tint(stats* st) {
    static const unsigned char rgba[4 * 4 * 4] = {
        200, 100, 50, 255,  10, 20, 30, 128,  255, 255, 255, 64,  0, 0, 0, 255,
        1, 2, 3, 4,  90, 180, 45, 200,  60, 70, 80, 90,  120, 130, 140, 150,
        33, 66, 99, 255,  250, 5, 125, 32,  77, 88, 99, 16,  160, 170, 180, 190,
        11, 22, 33, 44,  55, 66, 77, 88,  99, 111, 122, 133,  144, 155, 166, 177 };
    static const float g16[4 * 4] = { -1, -0.5f, 0, 0.5f, 1, 0.25f, -0.25f, 0.75f, -0.75f, 0.125f, -0.125f, 0.375f,
                                      -0.375f, 0.625f, -0.625f, 0.875f };
    psygfx_texture_desc td;
    psygfx_image_desc idd;
    psygfx_stim s;
    psygfx_tex tc, tm;
    int x, y, c;
    (void)st;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    memset(&td, 0, sizeof td);
    td.w = 4; td.h = 4; td.format = PSYGFX_RGBA8; td.data = rgba;
    tc = psygfx_texture(&R->g, &td);
    td.format = PSYGFX_R32F; td.data = g16;
    tm = psygfx_texture(&R->g, &td);
    memset(&idd, 0, sizeof idd);
    idd.tex = tc; idd.place = PSYGFX_TOP_LEFT; idd.anchor = PSYGFX_TOP_LEFT;
    idd.tint[0] = 0.5f; idd.tint[1] = 1.0f; idd.tint[2] = 0.25f; idd.tint[3] = 0.6f;
    s = psygfx_image(&R->g, &idd);
    gl_frame(&s, 1);   /* over black */
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            for (c = 0; c < 3; c++) {
                const unsigned char* q = &rgba[(y * 4 + x) * 4];
                double a = q[3] / 255.0 * 0.6, want = q[c] / 255.0 * idd.tint[c] * a;
                S2.tint = maxd(S2.tint, fabs(R->scene[(y * W + x) * 4 + c] - want));
            }
    psygfx_close(&R->g);
    if (!gl_open(1, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    tm = psygfx_texture(&R->g, &td);
    memset(&idd, 0, sizeof idd);
    idd.tex = tm; idd.place = PSYGFX_TOP_LEFT; idd.anchor = PSYGFX_TOP_LEFT; idd.modulation = true;
    idd.contrast = 0.8f; idd.tint[0] = 1.0f; idd.tint[1] = 0.5f; idd.tint[2] = 0.0f; idd.tint[3] = 0.5f;
    s = psygfx_image(&R->g, &idd);
    gl_frame(&s, 1);   /* over mid-gray; dir 0 = the background */
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            for (c = 0; c < 3; c++) {
                double want = 0.5 + 0.8 * g16[y * 4 + x] * 0.5 * idd.tint[c] * 0.5;
                S2.tint = maxd(S2.tint, fabs(R->scene[(y * W + x) * 4 + c] - want));
            }
    {   /* ADD: the increment times contrast, tint.rgb and tint.a */
        static float inc[4 * 4 * 4];
        for (x = 0; x < 64; x++) inc[x] = 0.5f * g16[x / 4] * (x % 4 == 1 ? -1.0f : 1.0f);
        td.format = PSYGFX_RGBA32F; td.data = inc;
        tm = psygfx_texture(&R->g, &td);
        memset(&idd, 0, sizeof idd);
        idd.tex = tm; idd.place = PSYGFX_TOP_LEFT; idd.anchor = PSYGFX_TOP_LEFT; idd.add = true;
        idd.contrast = 0.8f; idd.tint[0] = 1.0f; idd.tint[1] = 0.5f; idd.tint[2] = 0.25f; idd.tint[3] = 0.5f;
        s = psygfx_image(&R->g, &idd);
        gl_frame(&s, 1);
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++)
                for (c = 0; c < 3; c++) {
                    double want = 0.5 + 0.8 * (double)inc[(y * 4 + x) * 4 + c] * idd.tint[c] * 0.5;
                    S2.tint = maxd(S2.tint, fabs(R->scene[(y * W + x) * 4 + c] - want));
                }
    }
    psygfx_close(&R->g);
}

static void gl_groups(stats* st) {
    static float a1[W * H * 4];
    psygfx_group gr;
    psygfx_group_desc gdsc;
    psygfx_stim m[3], eq[3];
    psygfx_shape_desc sd;
    psygfx_gabor_desc gb;
    int k, i, j;
    (void)st;
    if (!gl_open(1, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    memset(&gdsc, 0, sizeof gdsc);
    gdsc.x = 10; gdsc.y = -5; gdsc.ori = 25; gdsc.scale = 1.5f; gdsc.opacity = 0.5f;
    gdsc.w = 100; gdsc.h = 60; gdsc.anchor = PSYGFX_TOP_LEFT;
    gr = psygfx_group_make(&gdsc);
    memset(&sd, 0, sizeof sd);
    sd.place = PSYGFX_TOP_LEFT; sd.x = 10; sd.y = 10; sd.w = 20; sd.h = 10; sd.ori = 10;
    sd.color[0] = 0.9f; sd.color[1] = 0.1f; sd.color[2] = 0.3f; sd.group = &gr;
    m[0] = psygfx_shape(&sd);
    sd.shape = PSYGFX_CIRCLE; sd.place = PSYGFX_CENTER; sd.x = 5; sd.y = 0; sd.w = sd.h = 16;
    sd.edge = PSYGFX_EDGE_COSINE; sd.edge_width = 3; sd.stroke = 2;
    m[1] = psygfx_shape(&sd);
    memset(&gb, 0, sizeof gb);
    gb.sigma = 6; gb.sf = 0.1f; gb.contrast = 0.5f; gb.group = &gr;
    m[2] = psygfx_gabor(&gb);
    m[2].place = PSYGFX_BOTTOM_RIGHT; m[2].x = -20; m[2].y = -15;
    /* the same stimuli placed alone, from the group's transform done by hand */
    for (k = 0; k < 3; k++) {
        float ax, ay;
        psygfx_resolve(&R->g, &m[k], &ax, &ay);
        eq[k] = m[k];
        eq[k].group = NULL;
        eq[k].place = PSYGFX_TOP_LEFT;
        eq[k].x = ax; eq[k].y = ay;
        eq[k].ori = m[k].ori + 25.0f;
        eq[k].w *= 1.5f; eq[k].h *= 1.5f;
        eq[k].sf /= 1.5f; eq[k].sigma *= 1.5f;
        eq[k].gate *= 0.5f;
    }
    for (k = 0; k < 3; k++) {
        gl_frame(&m[k], 1);
        memcpy(a1, R->scene, sizeof a1);
        gl_frame(&eq[k], 1);
        for (i = 0; i < W * H * 4; i++) S2.group_diff = maxd(S2.group_diff, fabs(a1[i] - R->scene[i]));
    }
    {   /* by hand: the rect member's anchor */
        float ax, ay;
        double c = cos(25 * 3.14159265358979323846 / 180), s = sin(25 * 3.14159265358979323846 / 180);
        double qx = (10 + 0 - 0) * 1.5, qy = (10 + 0 - 0) * 1.5;   /* place TOP_LEFT of the box; pivot its top-left */
        psygfx_resolve(&R->g, &m[0], &ax, &ay);
        CHECK(fabs(ax - (W / 2.0 + 10 + c * qx - s * qy)) < 1e-3 && fabs(ay - (H / 2.0 - 5 + s * qx + c * qy)) < 1e-3);
    }
    {   /* hit through the group, against the GPU's hard coverage */
        psygfx_stim h = m[0];
        h.gate = 2.0f;    /* group opacity 0.5: full coverage reads 0.4 x 2 x ... */
        gl_frame(&h, 1);
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                int tie;
                double want = band_ref(&h, i, j, &tie);
                int hit = psygfx_hit(&R->g, &h, (float)i + 0.5f, (float)j + 0.5f);
                if (!tie) S2.group_hit_bad += hit != (want >= 0.5);
            }
    }
    {   /* a binding on the group moves every member */
        psygfx_bind b;
        float v[1] = { 40.0f }, x0[3], y0[3], x1, y1;
        for (k = 0; k < 3; k++) psygfx_resolve(&R->g, &m[k], &x0[k], &y0[k]);
        memset(&b, 0, sizeof b);
        b.group = &gr; b.param = PSYGFX_G_X; b.channel = 0;
        CHECK(psygfx_apply(&b, 1, v) == 1);
        for (k = 0; k < 3; k++) {
            psygfx_resolve(&R->g, &m[k], &x1, &y1);
            CHECK(fabsf(x1 - x0[k] - 30.0f) < 1e-3f && fabsf(y1 - y0[k]) < 1e-3f);
        }
    }
    psygfx_close(&R->g);
}

static int g_mess_n;   /* reset per renderer in gl_targets */

/* A frame with no readback: a read drops the GL state cache by itself. */
static void frame_noread(const psygfx_stim* s, int n) {
    CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
    CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
    CHECK(psygfx_draw_n(&R->g, s, n) == PSYGFX_OK);
    CHECK(psygfx_end(&R->g) == PSYGFX_OK);
    CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
}
static void mess(void* ctx, const psyscr_present_info* info) {
    void (PSYGFX__APIENTRY * use)(unsigned int) = NULL;
    void (PSYGFX__APIENTRY * bindva)(unsigned int) = NULL;
    void (PSYGFX__APIENTRY * dis)(unsigned int) = NULL;
    void (PSYGFX__APIENTRY * bbase)(unsigned int, unsigned int, unsigned int) = NULL;
    void (PSYGFX__APIENTRY * active)(unsigned int) = NULL;
    void (PSYGFX__APIENTRY * bindtex)(unsigned int, unsigned int) = NULL;
    psyscr_proc p;
    (void)info;
    p = psyscr_gl_proc((psyscr_screen*)ctx, "glUseProgram");      memcpy(&use, &p, sizeof p);
    p = psyscr_gl_proc((psyscr_screen*)ctx, "glBindVertexArray"); memcpy(&bindva, &p, sizeof p);
    p = psyscr_gl_proc((psyscr_screen*)ctx, "glDisable");         memcpy(&dis, &p, sizeof p);
    p = psyscr_gl_proc((psyscr_screen*)ctx, "glBindBufferBase");  memcpy(&bbase, &p, sizeof p);
    p = psyscr_gl_proc((psyscr_screen*)ctx, "glActiveTexture");   memcpy(&active, &p, sizeof p);
    p = psyscr_gl_proc((psyscr_screen*)ctx, "glBindTexture");     memcpy(&bindtex, &p, sizeof p);
    use(0); bindva(0); dis(0x0BE2); bbase(0x8A11, 0, 0); bbase(0x8A11, 1, 0);
    active(0x84C0); bindtex(0x0DE1, 0);
    active(0x84C1); bindtex(0x0DE1, 0);
    g_mess_n++;
}

static void gl_targets(stats* st) {
    static float a1[W * H * 4], rb[64 * 48 * 4];
    static uint8_t o1[W * H * 4];
    psygfx_target_desc tdsc;
    psygfx_tex t;
    psygfx_shape_desc sd;
    psygfx_stim shapes[3], img, direct[3];
    int k, i, j;
    (void)st;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    memset(&tdsc, 0, sizeof tdsc);
    tdsc.w = 64; tdsc.h = 48;
    t = psygfx_target(&R->g, &tdsc);
    CHECK(t.id != 0);
    memset(&sd, 0, sizeof sd);
    sd.place = PSYGFX_TOP_LEFT; sd.anchor = PSYGFX_TOP_LEFT; sd.x = 2; sd.y = 3; sd.w = 10; sd.h = 6;
    sd.color[0] = 0.5f; sd.color[1] = 0.25f; sd.color[2] = 0.75f;
    shapes[0] = psygfx_shape(&sd);
    sd.x = 30; sd.y = 20; sd.w = 20; sd.h = 20; sd.shape = PSYGFX_CIRCLE; sd.color[0] = 1; sd.color[1] = 0.125f; sd.color[2] = 0;
    shapes[1] = psygfx_shape(&sd);
    sd.x = 20; sd.y = 30; sd.shape = PSYGFX_RECT; sd.w = 12; sd.h = 4; sd.color[0] = 0; sd.color[1] = 1; sd.color[2] = 0.5f; sd.opacity = 0.5f;
    shapes[2] = psygfx_shape(&sd);
    /* frame 1: draw into the target, then the target 1:1 at (5, 7) */
    {
        psygfx_image_desc idd;
        static const float zero[4] = { 0, 0, 0, 0 };
        memset(&idd, 0, sizeof idd);
        idd.tex = t; idd.place = PSYGFX_TOP_LEFT; idd.anchor = PSYGFX_TOP_LEFT; idd.x = 5; idd.y = 7;
        img = psygfx_image(&R->g, &idd);
        CHECK(img.flags & PSYGFX_STIM_PREMULTIPLIED);
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        CHECK(psygfx_begin_target(&R->g, t, zero) == PSYGFX_OK);
        CHECK(psygfx_begin_target(&R->g, t, zero) == PSYGFX_ERR_ORDER);      /* not nested */
        CHECK(psygfx_draw_n(&R->g, shapes, 3) == PSYGFX_OK);
        S2.refused_n++; S2.refused_ok += psygfx_draw(&R->g, &img) == PSYGFX_ERR_ORDER;   /* its own target */
        CHECK(psygfx_end_target(&R->g) == PSYGFX_OK);
        CHECK(psygfx_draw(&R->g, &img) == PSYGFX_OK);
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psygfx_read_scene(&R->g, 0, 0, W, H, R->scene) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
        CHECK(psygfx_begin_target(&R->g, t, NULL) == PSYGFX_ERR_ORDER);       /* outside a frame */
        CHECK(psygfx_read_target(&R->g, t, 0, 0, 64, 48, rb) == PSYGFX_OK);
        /* rows top first: the first rect's top-left texel is (2, 3) */
        S2.target_bad += rb[(3 * 64 + 2) * 4] != 0.5f || rb[(3 * 64 + 1) * 4] != 0.0f || rb[(2 * 64 + 2) * 4] != 0.0f;
        S2.target_bad += rb[(3 * 64 + 2) * 4 + 3] != 1.0f;   /* premultiplied alpha accumulated */
        memcpy(a1, R->scene, sizeof a1);
    }
    for (k = 0; k < 3; k++) { direct[k] = shapes[k]; direct[k].x += 5; direct[k].y += 7; }
    gl_frame(direct, 3);
    for (i = 0; i < W * H * 4; i++) S2.target_diff = maxd(S2.target_diff, fabs(a1[i] - R->scene[i]));
    /* persistence: sampled on later frames without drawing into it */
    for (k = 0; k < 4; k++) gl_frame(&img, 1);
    for (i = 0; i < W * H * 4; i++) S2.persist_bad += a1[i] != R->scene[i];
    {   /* a clear color fills the target; a pass with no clear keeps it */
        static const float c[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
        int pass;
        for (pass = 0; pass < 2; pass++) {
            CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
            CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
            CHECK(psygfx_begin_target(&R->g, t, pass ? NULL : c) == PSYGFX_OK);
            if (pass) CHECK(psygfx_draw(&R->g, &shapes[0]) == PSYGFX_OK);
            CHECK(psygfx_end_target(&R->g) == PSYGFX_OK);
            CHECK(psygfx_end(&R->g) == PSYGFX_OK);
            CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
            CHECK(psygfx_read_target(&R->g, t, 0, 0, 64, 48, rb) == PSYGFX_OK);
            for (i = 0; i < 64 * 48; i++) {
                int x = i % 64, y = i / 64, in = x >= 2 && x < 12 && y >= 3 && y < 9;
                if (pass && in) S2.target_bad += rb[i * 4] != 0.5f || rb[i * 4 + 2] != 0.75f;
                else for (k = 0; k < 4; k++) S2.target_bad += rb[i * 4 + k] != c[k];
            }
        }
    }
    psygfx_close(&R->g);

    /* ADD: 30 gabors rendered once into a float target, added each frame */
    if (!gl_open(1, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    memset(&tdsc, 0, sizeof tdsc);
    tdsc.w = W; tdsc.h = H;
    t = psygfx_target(&R->g, &tdsc);
    {
        psygfx_stim gabs[30];
        psygfx_gabor_desc gb;
        psygfx_image_desc idd;
        static const float zero[4] = { 0, 0, 0, 0 };
        for (k = 0; k < 30; k++) {
            memset(&gb, 0, sizeof gb);
            gb.sigma = 8; gb.sf = 0.07f; gb.contrast = 0.3f; gb.ori = (float)(k * 13);
            gb.x = (float)((k * 37) % 280 - 140); gb.y = (float)((k * 53) % 160 - 80);
            gabs[k] = psygfx_gabor(&gb);
        }
        memset(&idd, 0, sizeof idd);
        idd.tex = t; idd.add = true;
        img = psygfx_image(&R->g, &idd);
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        CHECK(psygfx_begin_target(&R->g, t, zero) == PSYGFX_OK);
        CHECK(psygfx_draw_n(&R->g, gabs, 30) == PSYGFX_OK);
        CHECK(psygfx_end_target(&R->g) == PSYGFX_OK);
        CHECK(psygfx_draw(&R->g, &img) == PSYGFX_OK);
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psygfx_read_scene(&R->g, 0, 0, W, H, a1) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
        gl_frame(gabs, 30);
        for (i = 0; i < W * H * 4; i += 4) S2.add_diff = maxd(S2.add_diff, fabs(a1[i] - R->scene[i]));
    }
    psygfx_close(&R->g);

    /* the CLUT once: through a target the output codes equal direct drawing */
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    {
        static const float lut[3 * 5] = { 0, 0.1f, 0.35f, 0.7f, 1, 0, 0.5f, 0.7f, 0.86f, 1, 0, 0.25f, 0.5f, 0.75f, 1 };
        psygfx_image_desc idd;
        static const float zero[4] = { 0, 0, 0, 0 };
        CHECK(psygfx_set_lut(&R->g, lut, 5) == PSYGFX_OK);
        memset(&tdsc, 0, sizeof tdsc);
        tdsc.w = 64; tdsc.h = 48;
        t = psygfx_target(&R->g, &tdsc);
        memset(&idd, 0, sizeof idd);
        idd.tex = t; idd.place = PSYGFX_TOP_LEFT; idd.anchor = PSYGFX_TOP_LEFT;
        img = psygfx_image(&R->g, &idd);
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        CHECK(psygfx_begin_target(&R->g, t, zero) == PSYGFX_OK);
        CHECK(psygfx_draw_n(&R->g, shapes, 2) == PSYGFX_OK);
        CHECK(psygfx_end_target(&R->g) == PSYGFX_OK);
        CHECK(psygfx_draw(&R->g, &img) == PSYGFX_OK);
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psygfx_read_output(&R->g, 0, 0, W, H, o1) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
        gl_frame(shapes, 2);
        for (i = 0; i < W * H * 4; i++) S2.clut_bad += o1[i] != R->out[i];
    }
    psygfx_close(&R->g);

    /* RGBA8 targets read back as their codes */
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    {
        static const float zero[4] = { 0, 0, 0, 0 };
        memset(&tdsc, 0, sizeof tdsc);
        tdsc.w = 64; tdsc.h = 48; tdsc.format = PSYGFX_RGBA8;
        t = psygfx_target(&R->g, &tdsc);
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        CHECK(psygfx_begin_target(&R->g, t, zero) == PSYGFX_OK);
        CHECK(psygfx_draw(&R->g, &shapes[0]) == PSYGFX_OK);
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);   /* end() closes an open target pass */
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
        CHECK(psygfx_read_target(&R->g, t, 0, 0, 64, 48, rb) == PSYGFX_OK);
        S2.target_bad += rb[(3 * 64 + 2) * 4] != 128.0f / 255.0f && rb[(3 * 64 + 2) * 4] != 127.0f / 255.0f;
        S2.target_bad += rb[(3 * 64 + 2) * 4 + 1] != 64.0f / 255.0f;
    }
    psygfx_close(&R->g);

    /* the GL epoch: a present hook that changes GL state; the next frame must
     * be the reference all the same. The control: the same changes made
     * behind psy_gfx's back with no epoch, which must show. */
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    gl_frame(shapes, 3);
    memcpy(o1, R->out, sizeof o1);
#if PSYGFX__EPOCH
    g_mess_n = 0;
    psyscr_on_present(&R->scr, mess, &R->scr);
    frame_noread(shapes, 3);    /* fills the cache; the hook then changes GL */
    frame_noread(shapes, 3);
    psyscr_on_present(&R->scr, NULL, NULL);
    CHECK(psygfx_read_output(&R->g, 0, 0, W, H, R->out) == PSYGFX_OK);
    for (i = 0; i < W * H * 4; i++) S2.epoch_bad += o1[i] != R->out[i];
    CHECK(g_mess_n == 2);
    frame_noread(shapes, 3);    /* the control: the same change, no epoch */
    mess(&R->scr, NULL);
    frame_noread(shapes, 3);
    CHECK(psygfx_read_output(&R->g, 0, 0, W, H, R->out) == PSYGFX_OK);
    for (i = 0; i < W * H * 4; i++) S2.epoch_control_bad += o1[i] != R->out[i];
    {   /* a new context, simulated by moving the screen's generation: the
         * next GL call reports it and the handle closes without deleting
         * (the names would belong to the new context); close and open again
         * give the reference frame */
        uint32_t keep = R->scr.gl_generation;
        R->scr.gl_generation = keep + 1;
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        CHECK(psygfx_draw_n(&R->g, shapes, 3) == PSYGFX_OK);
        S2.lost_ok += psygfx_end(&R->g) == PSYGFX_ERR_LOST;
        S2.lost_ok += !psygfx_is_open(&R->g);
        S2.lost_ok += psygfx_read_output(&R->g, 0, 0, W, H, R->out) == PSYGFX_ERR_CLOSED;
        S2.lost_ok += strstr(psygfx_error(&R->g), "context is new") != NULL;
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
        R->scr.gl_generation = keep;
        psygfx_close(&R->g);
        if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
        gl_frame(shapes, 3);
        for (i = 0; i < W * H * 4; i++) S2.lost_bad += o1[i] != R->out[i];
    }
#endif
    psygfx_close(&R->g);
    (void)j;
}

/* v0.1's OVER kept the destination's alpha; v0.2's accumulates it, which a
 * target needs. The scene's rgb and the output codes must not move: the same
 * frame through a backend that puts v0.1's blend back after each apply. */
static psygfx__gl g_v01_ctx;
static psygfx_backend g_v01_be;
static void v01_apply(void* c, const psygfx_bindings* b) {
    psygfx__gl* gl = (psygfx__gl*)c;
    psygfx__gl_backend.apply(c, b);
    if (gl->c_blend == PSYGFX_BLEND_OVER)
        gl->f.BlendFuncSeparate(PSYGFX__GL_ONE, PSYGFX__GL_ONE_MINUS_SRC_ALPHA, PSYGFX__GL_ZERO, PSYGFX__GL_ONE);
}

static void gl_alpha_passes(stats* st) {
    static float s1[W * H * 4], t1[64 * 48 * 4], t2[64 * 48 * 4];
    static uint8_t o1[W * H * 4];
    static const unsigned char rgba[2 * 2 * 4] = { 200, 100, 50, 255, 10, 20, 30, 128, 255, 255, 255, 64, 1, 2, 3, 4 };
    static const float zero[4] = { 0, 0, 0, 0 };
    psygfx_stim s[4];
    int v, i, k;
    (void)st;
    g_v01_be = psygfx__gl_backend;
    g_v01_be.apply = v01_apply;
    for (v = 0; v < 2; v++) {
        psygfx_desc gd;
        psygfx_shape_desc sd;
        psygfx_image_desc idd;
        psygfx_texture_desc td;
        psygfx_target_desc rd;
        psygfx_tex tex, tg;
        memset(&gd, 0, sizeof gd);
        gd.screen = &R->scr;
        gd.background[0] = 0.3f; gd.background[1] = 0.5f; gd.background[2] = 0.7f;
        psygfx__test_scene32 = 1;
        if (v) { gd.backend = &g_v01_be; gd.backend_ctx = &g_v01_ctx; }
        if (!psygfx_open(&R->g, &gd)) { fprintf(stderr, "psy_gfx_test [%s]: open: %s\n", g_where, psygfx_error(&R->g)); g_failures++; return; }
        memset(&td, 0, sizeof td);
        td.w = 2; td.h = 2; td.format = PSYGFX_RGBA8; td.data = rgba;
        tex = psygfx_texture(&R->g, &td);
        memset(&rd, 0, sizeof rd);
        rd.w = 64; rd.h = 48;
        tg = psygfx_target(&R->g, &rd);
        memset(&sd, 0, sizeof sd);
        sd.w = 120; sd.h = 80; sd.color[0] = 0.9f; sd.color[1] = 0.2f; sd.color[2] = 0.1f; sd.opacity = 0.6f;
        sd.edge = PSYGFX_EDGE_GAUSSIAN; sd.edge_width = 2;
        s[0] = psygfx_shape(&sd);
        sd.shape = PSYGFX_CIRCLE; sd.x = 30; sd.w = 90; sd.color[0] = 0.1f; sd.color[1] = 0.8f; sd.opacity = 0.35f;
        s[1] = psygfx_shape(&sd);
        memset(&idd, 0, sizeof idd);
        idd.tex = tex; idd.w = 100; idd.h = 100; idd.x = -40; idd.linear = true;
        s[2] = psygfx_image(&R->g, &idd);
        s[3] = s[1]; s[3].opacity = 0.5f;
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        CHECK(psygfx_draw_n(&R->g, s, 3) == PSYGFX_OK);
        CHECK(psygfx_begin_target(&R->g, tg, zero) == PSYGFX_OK);
        CHECK(psygfx_draw_n(&R->g, s, 2) == PSYGFX_OK);
        CHECK(psygfx_end_target(&R->g) == PSYGFX_OK);
        CHECK(psygfx_draw(&R->g, &s[3]) == PSYGFX_OK);
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psygfx_read_scene(&R->g, 0, 0, W, H, R->scene) == PSYGFX_OK);
        CHECK(psygfx_read_output(&R->g, 0, 0, W, H, R->out) == PSYGFX_OK);
        CHECK(psygfx_read_target(&R->g, tg, 0, 0, 64, 48, v ? t2 : t1) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
        if (!v) { memcpy(s1, R->scene, sizeof s1); memcpy(o1, R->out, sizeof o1); psygfx_close(&R->g); continue; }
        for (i = 0; i < W * H * 4; i++) {
            if (i % 4 != 3) S2.alpha_scene_bad += s1[i] != R->scene[i];
            S2.alpha_scene_bad += o1[i] != R->out[i];
        }
        /* the control: in a target, cleared to alpha 0, the two blends differ */
        for (i = 3; i < 64 * 48 * 4; i += 4) S2.alpha_control += t1[i] != t2[i];
        /* at most PSYGFX_MAX_PASSES target passes a frame */
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        for (k = 0; k < PSYGFX_MAX_PASSES; k++) {
            S2.passes_bad += psygfx_begin_target(&R->g, tg, NULL) != PSYGFX_OK;
            S2.passes_bad += psygfx_end_target(&R->g) != PSYGFX_OK;
        }
        S2.passes_bad += psygfx_begin_target(&R->g, tg, NULL) != PSYGFX_ERR_ORDER;
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
        psygfx_close(&R->g);
    }
}

/* -------------------------------------------- the edge model against truth */

/* The checks above compare the GPU with the CPU form of the same function,
 * so they cannot see where that function is not the Euclidean distance, nor
 * where a profile of the distance is not a blur of the shape. These compare
 * with references that do not share the header's arithmetic: the exact
 * Gaussian blur of the hard shape, and the Euclidean distance by brute force
 * over the boundary's segments. Each deviation must be at most the analytic
 * value plus the renderer's tolerance, and at least a stated fraction of it
 * (the control: the reference must be able to see the deviation). */

typedef struct stats3 {
    double vtx_blur, disc_blur[2], disc_pred[2], miter_bound, miter_pred;
} stats3;

static stats3 S3;

static double Phi(double x) { return 0.5 * erfc(-x / sqrt(2.0)); }

/* The hard disc of radius R blurred by a Gaussian of SD sg, at distance r
 * from its center: a 1-D integral over y = R sin t, composite Simpson. */
static double blurred_disc(double r, double rad, double sg) {
    const int n = 2000;
    const double pi = 3.14159265358979323846, h = pi / n;
    double sum = 0;
    int k;
    for (k = 0; k <= n; k++) {
        double t = -0.5 * pi + k * h, y = rad * sin(t), hx = rad * cos(t);
        double f = (Phi((hx - r) / sg) - Phi((-hx - r) / sg)) * exp(-0.5 * y * y / (sg * sg)) / (sg * sqrt(2 * pi)) * rad * cos(t);
        sum += f * (k == 0 || k == n ? 1 : (k % 2 ? 4 : 2));
    }
    return sum * h / 3.0;
}

/* Signed Euclidean distance to the boundary of an axis-aligned box, by brute
 * force over its four edge segments; inside negative. */
static double seg_dist(double px, double py, double ax, double ay, double bx, double by) {
    double ex = bx - ax, ey = by - ay, t = ((px - ax) * ex + (py - ay) * ey) / (ex * ex + ey * ey);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return sqrt((px - ax - t * ex) * (px - ax - t * ex) + (py - ay - t * ey) * (py - ay - t * ey));
}
static double box_brute(double px, double py, double x0, double y0, double x1, double y1) {
    double d = seg_dist(px, py, x0, y0, x1, y0);
    d = fmin(d, seg_dist(px, py, x1, y0, x1, y1));
    d = fmin(d, seg_dist(px, py, x1, y1, x0, y1));
    d = fmin(d, seg_dist(px, py, x0, y1, x0, y0));
    return (px > x0 && px < x1 && py > y0 && py < y1) ? -d : d;
}

static void gl_edge_truth(stats* st) {
    psygfx_shape_desc sd;
    psygfx_stim s;
    int i, j, k;
    (void)st;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    /* a sharp rect, GAUSSIAN SD 2 px, its top-left vertex on a pixel center */
    memset(&sd, 0, sizeof sd);
    sd.place = PSYGFX_TOP_LEFT; sd.anchor = PSYGFX_TOP_LEFT; sd.x = 40.5f; sd.y = 30.5f; sd.w = 120; sd.h = 80;
    sd.edge = PSYGFX_EDGE_GAUSSIAN; sd.edge_width = 2;
    sd.color[0] = sd.color[1] = sd.color[2] = 1;
    for (k = 0; k < 2; k++) {
        sd.join = k ? PSYGFX_JOIN_MITER : PSYGFX_JOIN_ROUND;
        s = psygfx_shape(&sd);
        gl_frame(&s, 1);
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                double x = i + 0.5, y = j + 0.5, got = R->scene[(j * W + i) * 4];
                if (!k) {   /* the exact blur of the rect: a product of erf */
                    double b = (Phi((x - 40.5) / 2) - Phi((x - 160.5) / 2)) * (Phi((y - 30.5) / 2) - Phi((y - 110.5) / 2));
                    S3.vtx_blur = maxd(S3.vtx_blur, fabs(got - b));
                } else {    /* MITER's field against the Euclidean distance */
                    double d = box_brute(x, y, 40.5, 30.5, 160.5, 110.5);
                    S3.miter_bound = maxd(S3.miter_bound, fabs(got - cov(PSYGFX_EDGE_GAUSSIAN, 2, d)));
                }
            }
    }
    /* the predicted MITER deviation, on a fine grid outside a corner */
    S3.miter_pred = 0;
    for (j = 0; j <= 400; j++)
        for (i = 0; i <= 400; i++) {
            double x = i * 0.02, y = j * 0.02;
            S3.miter_pred = maxd(S3.miter_pred, cov(PSYGFX_EDGE_GAUSSIAN, 2, x > y ? x : y) -
                                                cov(PSYGFX_EDGE_GAUSSIAN, 2, sqrt(x * x + y * y)));
        }
    /* discs of R / sigma 5 and 20, centered off the pixel grid */
    for (k = 0; k < 2; k++) {
        enum { NT = 4801 };
        static double tab[NT];
        const double Rr = k ? 40.0 : 10.0, sg = 2.0, r0 = Rr - 12.0, dr = 24.0 / (NT - 1);
        int q;
        for (q = 0; q < NT; q++) tab[q] = blurred_disc(r0 + q * dr, Rr, sg);
        S3.disc_pred[k] = 0;
        for (q = 0; q < NT; q++) S3.disc_pred[k] = maxd(S3.disc_pred[k], fabs(cov(PSYGFX_EDGE_GAUSSIAN, sg, r0 + q * dr - Rr) - tab[q]));
        memset(&sd, 0, sizeof sd);
        sd.shape = PSYGFX_CIRCLE; sd.w = (float)(2 * Rr); sd.x = 0.3f; sd.y = -0.2f;
        sd.edge = PSYGFX_EDGE_GAUSSIAN; sd.edge_width = (float)sg;
        sd.color[0] = sd.color[1] = sd.color[2] = 1;
        s = psygfx_shape(&sd);
        gl_frame(&s, 1);
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                float lx, ly;
                double r, u, b;
                int q0;
                psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                r = sqrt((double)lx * lx + (double)ly * ly);
                if (r < r0 || r >= r0 + 24.0) continue;
                u = (r - r0) / dr; q0 = (int)u;
                if (q0 >= NT - 1) q0 = NT - 2;
                b = tab[q0] + (tab[q0 + 1] - tab[q0]) * (u - q0);
                S3.disc_blur[k] = maxd(S3.disc_blur[k], fabs(R->scene[(j * W + i) * 4] - b));
            }
    }
    psygfx_close(&R->g);
}

/* ---------------------------------------------------------------- v0.3 */

typedef struct stats4 {
    double soft, cpu_brute, dash_soft;   /* T1; T2 distance error (px), CPU form against brute force */
    long   hard_bad, hit_bad, ties, n_shapes;
    char   worst[64];
} stats4;

static stats4 S4;

/* The shader's psy_along_ (dashes and trim), in double, from the block. */
static double along_ref(const float* b, double cm, double nn, double hh, double s) {
    int fl = (int)b[23], cap = (int)b[26], k;
    int dsh = (fl & 1) != 0, trm = (fl & 2) != 0;
    double P = (double)b[16] + b[17], u = dsh ? s - b[18] - P * floor((s - b[18]) / P) : 0.0;
    int prof = (int)b[21];
    double w = b[22];
    if (cap != 1) {
        double ex = cap == 2 ? hh : 0.0, a = 1.0;
        if (dsh) {
            a = 0.0;
            for (k = -1; k <= 1; k++) { double uk = u + k * P; a += cov(prof, w, uk - b[16] - ex) - cov(prof, w, uk + ex); }
        }
        if (trm) a *= cov(prof, w, s - b[25] - ex) - cov(prof, w, s - b[24] + ex);
        return cm * a;
    } else {
        double ga = 0.0;
        if (dsh) { ga = 1e300; for (k = -1; k <= 1; k++) ga = fmin(ga, fmax(fabs(u + k * P - 0.5 * b[16]) - 0.5 * b[16], 0.0)); }
        if (trm) ga = fmax(ga, fmax(fabs(s - 0.5 * ((double)b[24] + b[25])) - 0.5 * ((double)b[25] - b[24]), 0.0));
        return cov(prof, w, sqrt(ga * ga + nn * nn) - hh);
    }
}

/* The main layer's coverage of a vector stimulus on the CPU, at a pixel. */
static double vec_ref(const float* b, const psygfx_stim* s, int i, int j, int* tie) {
    float lx, ly;
    double f0[4], f1[4], d, hw = b[15], c = b[11], cm, w = b[22];
    int prof = (int)b[21], fl = (int)b[23];
    psygfx_local(&R->g, s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
    psygfx__vfield(b, lx, ly, f0, f1);
    d = f0[0] - b[10];
    *tie = hw > 0 ? (fabs(d - c - hw) < 1e-3 || fabs(d - c + hw) < 1e-3) : fabs(d) < 1e-3;
    cm = hw > 0 ? cov(prof, w, d - c - hw) - cov(prof, w, d - c + hw) : cov(prof, w, d);
    if (fl & 3) {
        double P = (double)b[16] + b[17], u = f0[3] - b[18] - P * floor((f0[3] - b[18]) / P);
        cm = along_ref(b, cm, hw > 0 ? d - c : d + b[35], hw > 0 ? hw : b[35], f0[3]);
        /* a pixel on a dash's end is a tie too */
        if ((fl & 1) && (fabs(u) < 2e-2 || fabs(u - b[16]) < 2e-2 || fabs(u - P) < 2e-2)) *tie = 1;
        if ((fl & 2) && (fabs(f0[3] - b[24]) < 2e-2 || fabs(f0[3] - b[25]) < 2e-2)) *tie = 1;
    }
    return cm;
}

/* One stimulus against the CPU form, every pixel. */
static void vec_check(const psygfx_stim* s, const char* what) {
    static float blk[16 * 64];
    double ext[2], worst = 0;
    const char* err = "";
    int clip, i, j, tie, n;
    n = psygfx__vpack(&R->g, s, blk, ext, &clip, &err);
    if (n < 0) { fprintf(stderr, "psy_gfx_test [%s]: %s refused: %s\n", g_where, what, err); g_failures++; return; }
    gl_frame(s, 1);
    S4.n_shapes++;
    for (j = 0; j < H; j++)
        for (i = 0; i < W; i++) {
            double want = vec_ref(blk, s, i, j, &tie), got = R->scene[(j * W + i) * 4];
            int hit = psygfx_hit(&R->g, s, (float)i + 0.5f, (float)j + 0.5f);
            if (tie) { S4.ties++; continue; }
            if (s->edge == PSYGFX_EDGE_HARD) {
                if (fabs(got - want) > 1e-6) { S4.hard_bad++; if (S4.hard_bad < 40 && getenv("PSYGFX_TEST_VERBOSE")) fprintf(stderr, "  hard %s at %d %d: got %g want %g\n", what, i, j, got, want); }
                S4.hit_bad += hit != (got >= 0.5);
            } else {
                worst = maxd(worst, fabs(got - want));
            }
        }
    /* coverage that depends on the arc length (dashes, trim) apart: its
     * float error grows with the length along the boundary */
    if ((int)blk[23] & 3) S4.dash_soft = maxd(S4.dash_soft, worst);
    else if (worst > S4.soft) { S4.soft = worst; snprintf(S4.worst, sizeof S4.worst, "%s", what); }
    if (worst > 1e-3 && getenv("PSYGFX_TEST_VERBOSE")) {
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                double want = vec_ref(blk, s, i, j, &tie), got = R->scene[(j * W + i) * 4];
                if (!tie && fabs(got - want) > 0.5 * worst) {
                    float lx, ly;
                    double f0[4], f1[4];
                    psygfx_local(&R->g, s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                    psygfx__vfield(blk, lx, ly, f0, f1);
                    fprintf(stderr, "  %s: worst %.3g at %d %d local %.2f %.2f: got %.5f want %.5f (d %.4f s %.3f)\n",
                            what, worst, i, j, lx, ly, got, want, f0[0], f0[3]);
                    j = H; break;
                }
            }
    }
}

static void gl_v03_kinds(stats* st) {
    static const float qb[6] = { -70, 40, 0, -80, 70, 40 };
    static const float zig[8] = { -110, 30, -40, -40, 20, 35, 100, -20 };
    static const float conc[10] = { -60, -50, 60, -50, 60, 50, 0, 0, -60, 50 };
    static const float tri[6] = { -60, 45, 60, 45, 0, -55 };
    psygfx_shape_desc base[24];
    const char* names[24];
    int nb = 0, k, e, o, st2;
    (void)st;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    memset(base, 0, sizeof base);
#define ADD_(nm) (names[nb] = nm, &base[nb++])
    { psygfx_shape_desc* d = ADD_("rrect"); d->shape = PSYGFX_RRECT; d->w = 120; d->h = 80; d->shape_p[0] = 5; d->shape_p[1] = 20; d->shape_p[3] = 35; }
    { psygfx_shape_desc* d = ADD_("circle"); d->shape = PSYGFX_CIRCLE; d->w = 110; d->dash[0] = 14; d->dash[1] = 9; d->stroke = 5; }
    { psygfx_shape_desc* d = ADD_("annulus"); d->shape = PSYGFX_ANNULUS; d->w = 130; d->shape_p[0] = 30; d->dash[0] = 20; d->dash[1] = 7; d->stroke = 4; }
    { psygfx_shape_desc* d = ADD_("rect dashed"); d->shape = PSYGFX_RECT; d->w = 130; d->h = 70; d->shape_p[1] = 10; d->dash[0] = 12; d->dash[1] = 6; d->dash_offset = 3; d->stroke = 4; }
    { psygfx_shape_desc* d = ADD_("line dashed"); d->shape = PSYGFX_LINE; d->w = 200; d->shape_p[0] = 12; d->dash[0] = 18; d->dash[1] = 10; }
    { psygfx_shape_desc* d = ADD_("capsule"); d->shape = PSYGFX_CAPSULE; d->w = 160; d->shape_p[0] = 24; d->shape_p[1] = 9; }
    { psygfx_shape_desc* d = ADD_("arc"); d->shape = PSYGFX_ARC; d->w = 150; d->shape_p[0] = 250; d->shape_p[1] = 16; d->shape_p[2] = 30; }
    { psygfx_shape_desc* d = ADD_("arc trimmed"); d->shape = PSYGFX_ARC; d->w = 150; d->shape_p[0] = 300; d->shape_p[1] = 16; d->trim[0] = 0.2f; d->trim[1] = 0.7f; d->cap = PSYGFX_CAP_ROUND; }
    { psygfx_shape_desc* d = ADD_("pie 120"); d->shape = PSYGFX_PIE; d->w = 150; d->shape_p[0] = 120; d->shape_p[2] = -60; }
    { psygfx_shape_desc* d = ADD_("pie 300"); d->shape = PSYGFX_PIE; d->w = 150; d->shape_p[0] = 300; d->shape_p[2] = 10; }
    { psygfx_shape_desc* d = ADD_("ngon 5"); d->shape = PSYGFX_NGON; d->w = 150; d->shape_p[0] = 5; }
    { psygfx_shape_desc* d = ADD_("ngon 6 round"); d->shape = PSYGFX_NGON; d->w = 150; d->shape_p[0] = 6; d->shape_p[1] = 14; d->dash[0] = 15; d->dash[1] = 8; d->dash_snap = true; d->stroke = 4; }
    { psygfx_shape_desc* d = ADD_("star 5"); d->shape = PSYGFX_STAR; d->w = 160; d->shape_p[0] = 5; d->shape_p[1] = 0.45f; }
    { psygfx_shape_desc* d = ADD_("star 7 round"); d->shape = PSYGFX_STAR; d->w = 160; d->shape_p[0] = 7; d->shape_p[1] = 0.5f; d->shape_p[2] = 5; }
    { psygfx_shape_desc* d = ADD_("ellipse 2:1"); d->shape = PSYGFX_ELLIPSE; d->w = 180; d->h = 90; d->dash[0] = 16; d->dash[1] = 8; d->stroke = 4; }
    { psygfx_shape_desc* d = ADD_("ellipse 10:1"); d->shape = PSYGFX_ELLIPSE; d->w = 200; d->h = 20; }
    { psygfx_shape_desc* d = ADD_("qbezier"); d->shape = PSYGFX_QBEZIER; d->path = qb; d->n_path = 3; d->shape_p[0] = 10; d->cap = PSYGFX_CAP_BUTT; d->dash[0] = 20; d->dash[1] = 6; }
    { psygfx_shape_desc* d = ADD_("polyline miter"); d->shape = PSYGFX_POLYLINE; d->path = zig; d->n_path = 4; d->shape_p[0] = 12; d->join = PSYGFX_JOIN_MITER; d->cap = PSYGFX_CAP_SQUARE; }
    { psygfx_shape_desc* d = ADD_("polyline round"); d->shape = PSYGFX_POLYLINE; d->path = zig; d->n_path = 4; d->shape_p[0] = 12; d->cap = PSYGFX_CAP_ROUND; d->trim[0] = 0.1f; d->trim[1] = 0.8f; }
    { psygfx_shape_desc* d = ADD_("polyline bevel"); d->shape = PSYGFX_POLYLINE; d->path = zig; d->n_path = 4; d->shape_p[0] = 12; d->join = PSYGFX_JOIN_BEVEL; }
    { psygfx_shape_desc* d = ADD_("polygon concave fillet"); d->shape = PSYGFX_POLYGON; d->vertices = conc; d->shape_p[0] = 5; d->shape_p[1] = 8; }
    { psygfx_shape_desc* d = ADD_("polygon triangle fillet"); d->shape = PSYGFX_POLYGON; d->vertices = tri; d->shape_p[0] = 3; d->shape_p[1] = 12; d->dash[0] = 25; d->dash[1] = 10; d->stroke = 5; }
#undef ADD_
    for (k = 0; k < nb; k++)
        for (e = 0; e < 3; e++)
            for (o = 0; o < 2; o++)
                for (st2 = 0; st2 < 2; st2++) {
                    psygfx_shape_desc d = base[k];
                    psygfx_stim s;
                    char what[96];
                    if (st2 && d.stroke > 0) continue;
                    if (st2 && (d.shape == PSYGFX_POLYLINE || d.shape == PSYGFX_QBEZIER) && d.trim[1] > 0) continue;
                    d.edge = (psygfx_edge)e; d.edge_width = e == PSYGFX_EDGE_COSINE ? 4.0f : 1.5f;
                    d.ori = o ? 25.0f : 0.0f; d.x = 3.25f; d.y = -2.5f;
                    if (st2) { d.stroke = 4; d.stroke_align = PSYGFX_STROKE_CENTER; d.dash[0] = d.dash[0]; }
                    d.color[0] = d.color[1] = d.color[2] = 1.0f;
                    s = psygfx_shape(&d);
                    snprintf(what, sizeof what, "%s e%d o%d s%d", names[k], e, o, st2);
                    vec_check(&s, what);
                }
    {   /* compounds: each operation, onion, a smooth blend */
        static psygfx_prim pr[3];
        static const int ops[7] = { PSYGFX_OP_UNION, PSYGFX_OP_INTERSECT, PSYGFX_OP_SUBTRACT, PSYGFX_OP_XOR,
                                    PSYGFX_OP_SMOOTH_UNION, PSYGFX_OP_SMOOTH_INTERSECT, PSYGFX_OP_SMOOTH_SUBTRACT };
        psygfx_compound_desc cd;
        psygfx_stim s;
        char what[64];
        for (k = 0; k < 7; k++)
            for (e = 0; e < 3; e++) {
                memset(pr, 0, sizeof pr);
                pr[0].shape = PSYGFX_CIRCLE; pr[0].x = -30; pr[0].w = 110;
                pr[1].shape = PSYGFX_RRECT; pr[1].x = 35; pr[1].y = 10; pr[1].w = 100; pr[1].h = 60; pr[1].ori = 20;
                pr[1].p[0] = pr[1].p[2] = 10; pr[1].op = (uint8_t)ops[k]; pr[1].k = 25;
                pr[2].shape = PSYGFX_STAR; pr[2].y = -50; pr[2].w = 60; pr[2].p[0] = 5; pr[2].p[1] = 0.5f; pr[2].op = PSYGFX_OP_UNION;
                pr[2].onion = k == 3 ? 4.0f : 0.0f;
                memset(&cd, 0, sizeof cd);
                cd.prims = pr; cd.n = 3; cd.w = 200; cd.h = 160; cd.ori = e == 2 ? 15.0f : 0.0f; cd.x = 1.5f;
                cd.edge = (psygfx_edge)e; cd.edge_width = e == PSYGFX_EDGE_COSINE ? 4.0f : 1.5f;
                cd.color[0] = cd.color[1] = cd.color[2] = 1;
                if (k == 6) cd.stroke = 3;
                s = psygfx_compound(&cd);
                snprintf(what, sizeof what, "compound op%d e%d", ops[k], e);
                vec_check(&s, what);
            }
    }
    psygfx_close(&R->g);
}

/* --- brute force: a boundary as pieces (segments and arcs), in local px */

typedef struct piece { int arc; double ax, ay, bx, by, cx, cy, r, t0, dt; } piece;
static piece PC[8192];
static int NPC;
static double PX_[70000], PY_[70000];   /* the boundary as a polygon, for inside */
static int NPX;
static int PC_OPEN;                     /* an open curve: distance minus PC_HW, no sign test */
static double PC_HW;

static void pc_reset(void) { NPC = 0; NPX = 0; PC_OPEN = 0; PC_HW = 0; }
static void pc_seg(double ax, double ay, double bx, double by) {
    piece* p = &PC[NPC++];
    memset(p, 0, sizeof *p);
    p->ax = ax; p->ay = ay; p->bx = bx; p->by = by;
    PX_[NPX] = ax; PY_[NPX++] = ay;
}
static void pc_arc(double cx, double cy, double r, double t0, double dt) {
    piece* p = &PC[NPC++];
    int k;
    memset(p, 0, sizeof *p);
    p->arc = 1; p->cx = cx; p->cy = cy; p->r = r; p->t0 = t0; p->dt = dt;
    for (k = 0; k < 256; k++) { double t = t0 + dt * k / 256.0; PX_[NPX] = cx + r * cos(t); PY_[NPX++] = cy + r * sin(t); }
}
static double piece_dist(const piece* p, double x, double y) {
    if (!p->arc) return seg_dist(x, y, p->ax, p->ay, p->bx, p->by);
    {
        double a = atan2(y - p->cy, x - p->cx), rel = a - p->t0, tw = 2 * 3.14159265358979323846;
        if (p->dt < 0) rel = -rel;
        rel -= tw * floor(rel / tw);
        if (rel <= fabs(p->dt)) return fabs(sqrt((x - p->cx) * (x - p->cx) + (y - p->cy) * (y - p->cy)) - p->r);
        {
            double e0x = p->cx + p->r * cos(p->t0), e0y = p->cy + p->r * sin(p->t0);
            double e1x = p->cx + p->r * cos(p->t0 + p->dt), e1y = p->cy + p->r * sin(p->t0 + p->dt);
            return fmin(sqrt((x - e0x) * (x - e0x) + (y - e0y) * (y - e0y)), sqrt((x - e1x) * (x - e1x) + (y - e1y) * (y - e1y)));
        }
    }
}
static double pc_dist(double x, double y) {
    double d = 1e300;
    int k, in = 0, j;
    for (k = 0; k < NPC; k++) d = fmin(d, piece_dist(&PC[k], x, y));
    if (PC_OPEN) return d - PC_HW;
    for (k = 0, j = NPX - 1; k < NPX; j = k++)
        if ((PY_[k] > y) != (PY_[j] > y) && x < (PX_[j] - PX_[k]) * (y - PY_[k]) / (PY_[j] - PY_[k]) + PX_[k]) in = !in;
    return in ? -d : d;
}

/* A closed polygon with every corner rounded to radius r (a fillet tangent
 * to both edges, convex or concave corner alike), written apart from the
 * header's. */
static void pc_fillet(const double* v, int n, double r) {
    int i;
    double pin[64][2], pout[64][2], c[64][2], t0[64], dt[64];
    for (i = 0; i < n; i++) {
        int ip = (i + n - 1) % n, in = (i + 1) % n;
        double u1x = v[2 * i] - v[2 * ip], u1y = v[2 * i + 1] - v[2 * ip + 1], u2x = v[2 * in] - v[2 * i], u2y = v[2 * in + 1] - v[2 * i + 1];
        double l1 = sqrt(u1x * u1x + u1y * u1y), l2 = sqrt(u2x * u2x + u2y * u2y), dl, td, bx, by, bl, a0, a1, d;
        u1x /= l1; u1y /= l1; u2x /= l2; u2y /= l2;
        dl = acos(-(u1x * u2x + u1y * u2y));
        td = r / tan(0.5 * dl);
        pin[i][0] = v[2 * i] - u1x * td; pin[i][1] = v[2 * i + 1] - u1y * td;
        pout[i][0] = v[2 * i] + u2x * td; pout[i][1] = v[2 * i + 1] + u2y * td;
        bx = u2x - u1x; by = u2y - u1y; bl = sqrt(bx * bx + by * by);
        c[i][0] = v[2 * i] + bx / bl * r / sin(0.5 * dl); c[i][1] = v[2 * i + 1] + by / bl * r / sin(0.5 * dl);
        a0 = atan2(pin[i][1] - c[i][1], pin[i][0] - c[i][0]); a1 = atan2(pout[i][1] - c[i][1], pout[i][0] - c[i][0]);
        d = a1 - a0;
        while (d > 3.14159265358979323846) d -= 2 * 3.14159265358979323846;
        while (d < -3.14159265358979323846) d += 2 * 3.14159265358979323846;
        t0[i] = a0; dt[i] = d;
    }
    for (i = 0; i < n; i++) {
        int in = (i + 1) % n;
        pc_arc(c[i][0], c[i][1], r, t0[i], dt[i]);
        pc_seg(pout[i][0], pout[i][1], pin[in][0], pin[in][1]);
    }
}

static void pc_poly(const double* v, int n) {
    int i;
    for (i = 0; i < n; i++) pc_seg(v[2 * i], v[2 * i + 1], v[2 * ((i + 1) % n)], v[2 * ((i + 1) % n) + 1]);
}

typedef struct stats5 {
    double t2_cov, t2_dist, t2_qb_cov, t2_qb_dist, t3_union, t3_union_pred, t3_smooth, t3_connect, t3_poly, t4_exact, t4_fd, fx, blend;
    double dash_period, paint_rgb, paint_oklab, paint_dkl, paint_vertex, msdf, glyph, batch;
    long   dash_count_bad, trim_bad, refused_ok, refused_n;
    char   t2_worst[48];
} stats5;
static stats5 S5;

/* GPU coverage against F(brute-force Euclidean d), pixels whose distance is
 * within reach of the edge; *dist gets the CPU form's distance error. */
static double truth_check(const psygfx_stim* s, double sg, double* dist) {
    static float blk[16 * 64];
    double ext[2], worst = 0.0;
    const char* err;
    int clip, i, j;
    if (psygfx__vpack(&R->g, s, blk, ext, &clip, &err) < 0) { fprintf(stderr, "truth: %s\n", err); g_failures++; return 1; }
    gl_frame(s, 1);
    for (j = 0; j < H; j++)
        for (i = 0; i < W; i++) {
            float lx, ly;
            double db, f0[4], f1[4];
            psygfx_local(&R->g, s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
            db = pc_dist(lx, ly);
            if (fabs(db) > 6 * sg) continue;
            worst = maxd(worst, fabs(R->scene[(j * W + i) * 4] - cov(PSYGFX_EDGE_GAUSSIAN, sg, db)));
            if (dist) { psygfx__vfield(blk, lx, ly, f0, f1); *dist = maxd(*dist, fabs(f0[0] - db)); }
        }
    return worst;
}

static void gl_v03_truth(stats* st) {
    const double PI_ = 3.14159265358979323846;
    psygfx_shape_desc d;
    psygfx_stim s;
    int o, k;
    (void)st;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    for (o = 0; o < 2; o++)
        for (k = 0; k < 12; k++) {
            const char* nm = "";
            double w;
            memset(&d, 0, sizeof d);
            d.edge = PSYGFX_EDGE_GAUSSIAN; d.edge_width = 1.5f; d.ori = o ? 25.0f : 0.0f; d.x = 3.25f; d.y = -2.5f;
            d.color[0] = d.color[1] = d.color[2] = 1;
            pc_reset();
            switch (k) {
            case 0:
                nm = "rrect"; d.shape = PSYGFX_RRECT; d.w = 120; d.h = 80; d.shape_p[0] = 5; d.shape_p[1] = 20; d.shape_p[3] = 35;
                pc_seg(-55, -40, 40, -40); pc_arc(40, -20, 20, -PI_ / 2, PI_ / 2); pc_seg(60, -20, 60, 40);
                pc_seg(60, 40, -25, 40); pc_arc(-25, 5, 35, PI_ / 2, PI_ / 2); pc_seg(-60, 5, -60, -35);
                pc_arc(-55, -35, 5, PI_, PI_ / 2);
                break;
            case 1: {
                double x1 = -56, x2 = 71, D = 127, sn = 15.0 / 127.0, cs = sqrt(1 - sn * sn), ph = asin(sn);
                nm = "capsule"; d.shape = PSYGFX_CAPSULE; d.w = 160; d.shape_p[0] = 24; d.shape_p[1] = 9;
                pc_arc(x1, 0, 24, PI_ / 2 - ph, PI_ + 2 * ph);
                pc_seg(x1 + 24 * sn, -24 * cs, x2 + 9 * sn, -9 * cs);
                pc_arc(x2, 0, 9, -PI_ / 2 + ph, PI_ - 2 * ph);
                pc_seg(x2 + 9 * sn, 9 * cs, x1 + 24 * sn, 24 * cs);
                (void)D;
                break;
            }
            case 2: {
                double a0 = 30 * PI_ / 180, a1 = 280 * PI_ / 180;
                nm = "arc"; d.shape = PSYGFX_ARC; d.w = 150; d.shape_p[0] = 250; d.shape_p[1] = 16; d.shape_p[2] = 30;
                pc_arc(0, 0, 75, a0, a1 - a0); pc_arc(67 * cos(a1), 67 * sin(a1), 8, a1, PI_);
                pc_arc(0, 0, 59, a1, a0 - a1); pc_arc(67 * cos(a0), 67 * sin(a0), 8, a0 + PI_, PI_);
                break;
            }
            case 3: case 4: {
                double sw = (k == 3 ? 120 : 300) * PI_ / 180, a0 = (k == 3 ? -60 : 10) * PI_ / 180;
                nm = k == 3 ? "pie 120" : "pie 300"; d.shape = PSYGFX_PIE; d.w = 150; d.shape_p[0] = k == 3 ? 120.0f : 300.0f;
                d.shape_p[2] = k == 3 ? -60.0f : 10.0f;
                pc_seg(0, 0, 75 * cos(a0), 75 * sin(a0)); pc_arc(0, 0, 75, a0, sw); pc_seg(75 * cos(a0 + sw), 75 * sin(a0 + sw), 0, 0);
                break;
            }
            case 5: {   /* a rounded hexagon: the inset polygon dilated, apart from the fillet code */
                double an = PI_ / 6, Ri = 75 - 14 / cos(an), v[12], nx, ny;
                int q;
                nm = "ngon 6 round"; d.shape = PSYGFX_NGON; d.w = 150; d.shape_p[0] = 6; d.shape_p[1] = 14;
                for (q = 0; q < 6; q++) { double a = an + 2 * an * q; v[2 * q] = Ri * sin(a); v[2 * q + 1] = Ri * cos(a); }
                for (q = 0; q < 6; q++) {
                    int qn = (q + 1) % 6;
                    double mx = 0.5 * (v[2 * q] + v[2 * qn]), my = 0.5 * (v[2 * q + 1] + v[2 * qn + 1]), ml = sqrt(mx * mx + my * my);
                    double nqx, nqy, pmx, pmy, pl, a0, a1, da;
                    int qp = (q + 5) % 6;
                    nx = mx / ml; ny = my / ml;
                    pc_seg(v[2 * q] + 14 * nx, v[2 * q + 1] + 14 * ny, v[2 * qn] + 14 * nx, v[2 * qn + 1] + 14 * ny);
                    pmx = 0.5 * (v[2 * qn] + v[2 * ((qn + 1) % 6)]); pmy = 0.5 * (v[2 * qn + 1] + v[2 * ((qn + 1) % 6) + 1]);
                    pl = sqrt(pmx * pmx + pmy * pmy); nqx = pmx / pl; nqy = pmy / pl;
                    a0 = atan2(ny, nx); a1 = atan2(nqy, nqx); da = a1 - a0;
                    while (da > PI_) da -= 2 * PI_;
                    while (da < -PI_) da += 2 * PI_;
                    pc_arc(v[2 * qn], v[2 * qn + 1], 14, a0, da);
                    (void)qp;
                }
                break;
            }
            case 6: case 7: {
                int n = k == 6 ? 5 : 7, q;
                double an = PI_ / n, rho = k == 6 ? 0.45 : 0.5, v[28];
                nm = k == 6 ? "star 5" : "star 7 round"; d.shape = PSYGFX_STAR; d.w = 160; d.shape_p[0] = (float)n; d.shape_p[1] = (float)rho;
                if (k == 7) d.shape_p[2] = 5;
                for (q = 0; q < n; q++) {
                    double ai = 2 * an * q, at = an + 2 * an * q;
                    v[4 * q] = 80 * rho * sin(ai); v[4 * q + 1] = 80 * rho * cos(ai);
                    v[4 * q + 2] = 80 * sin(at); v[4 * q + 3] = 80 * cos(at);
                }
                if (k == 6) pc_poly(v, 2 * n); else pc_fillet(v, 2 * n, 5);
                break;
            }
            case 8: case 9: {
                double a = 90, b = k == 8 ? 45 : 10;
                int q;
                nm = k == 8 ? "ellipse 2:1" : "ellipse 9:1"; d.shape = PSYGFX_ELLIPSE; d.w = (float)(2 * a); d.h = (float)(2 * b);
                for (q = 0; q < 8192; q++) {
                    double t0 = 2 * PI_ * q / 8192, t1 = 2 * PI_ * (q + 1) / 8192;
                    pc_seg(a * cos(t0), b * sin(t0), a * cos(t1), b * sin(t1));
                }
                break;
            }
            case 10: {
                static const float conc[10] = { -60, -50, 60, -50, 60, 50, 0, 0, -60, 50 };
                double v[10];
                int q;
                nm = "polygon fillet"; d.shape = PSYGFX_POLYGON; d.vertices = conc; d.shape_p[0] = 5; d.shape_p[1] = 8;
                for (q = 0; q < 10; q++) v[q] = conc[q];
                pc_fillet(v, 5, 8);
                break;
            }
            case 11: {
                static const float qb[6] = { -70, 40, 0, -80, 70, 40 };
                int q;
                nm = "qbezier"; d.shape = PSYGFX_QBEZIER; d.path = qb; d.n_path = 3; d.shape_p[0] = 10; d.cap = PSYGFX_CAP_ROUND;
                for (q = 0; q < 8192; q++) {
                    double t0 = q / 8192.0, t1 = (q + 1) / 8192.0;
                    pc_seg((1 - t0) * (1 - t0) * -70 + 2 * (1 - t0) * t0 * 0 + t0 * t0 * 70, (1 - t0) * (1 - t0) * 40 + 2 * (1 - t0) * t0 * -80 + t0 * t0 * 40,
                           (1 - t1) * (1 - t1) * -70 + 2 * (1 - t1) * t1 * 0 + t1 * t1 * 70, (1 - t1) * (1 - t1) * 40 + 2 * (1 - t1) * t1 * -80 + t1 * t1 * 40);
                }
                PC_OPEN = 1; PC_HW = 5;
                break;
            }
            }
            s = psygfx_shape(&d);
            if (k == 11) {   /* the Bezier: chords within 0.005 px of the curve */
                S5.t2_qb_cov = maxd(S5.t2_qb_cov, truth_check(&s, 1.5, &S5.t2_qb_dist));
                continue;
            }
            w = truth_check(&s, 1.5, &S5.t2_dist);
            if (w > S5.t2_cov) { S5.t2_cov = w; snprintf(S5.t2_worst, sizeof S5.t2_worst, "%s o%d", nm, o); }
        }
    /* T3: a union's reflex crease (an L of two rects), against the L's
     * Euclidean distance */
    {
        static psygfx_prim pr[2];
        psygfx_compound_desc cd;
        static const double L[12] = { -60, -20, 20, -20, 20, -80, 60, -80, 60, 20, -60, 20 };
        int i, j;
        memset(pr, 0, sizeof pr);
        pr[0].shape = PSYGFX_RECT; pr[0].x = 0; pr[0].y = 0; pr[0].w = 120; pr[0].h = 40;
        pr[1].shape = PSYGFX_RECT; pr[1].x = 40; pr[1].y = -30; pr[1].w = 40; pr[1].h = 100; pr[1].op = PSYGFX_OP_UNION;
        memset(&cd, 0, sizeof cd);
        cd.prims = pr; cd.n = 2; cd.w = 120; cd.h = 100; cd.y = 25;
        cd.edge = PSYGFX_EDGE_GAUSSIAN; cd.edge_width = 2; cd.color[0] = cd.color[1] = cd.color[2] = 1;
        s = psygfx_compound(&cd);
        pc_reset();
        pc_poly(L, 6);
        S5.t3_union = truth_check(&s, 2.0, NULL);
        S5.t3_union_pred = 0;
        for (j = 0; j <= 400; j++)   /* inside a reflex right angle: min of the two lines against the corner */
            for (i = 0; i <= 400; i++) {
                double x = i * 0.02, y = j * 0.02;
                S5.t3_union_pred = maxd(S5.t3_union_pred, cov(PSYGFX_EDGE_GAUSSIAN, 2, -sqrt(x * x + y * y)) - cov(PSYGFX_EDGE_GAUSSIAN, 2, -(x > y ? x : y)));
            }
    }
    /* T3: smooth unions, gradient-normalized, against the distance to the
     * blended shape's own boundary (its zero set, sampled at 0.05 px) */
    {
        static psygfx_prim pr[2];
        static float blk[16 * 64];
        static double zx[60000], zy[60000];
        psygfx_compound_desc cd;
        int q;
        for (q = 0; q < 2; q++) {
            double ext[2], D = q ? 90.0 : 80.0, worst = 0;
            const char* err;
            int clip, nz = 0, i, j;
            memset(pr, 0, sizeof pr);
            pr[0].shape = PSYGFX_CIRCLE; pr[0].x = (float)(-D / 2); pr[0].w = 80;
            pr[1].shape = PSYGFX_CIRCLE; pr[1].x = (float)(D / 2); pr[1].w = 80; pr[1].op = PSYGFX_OP_SMOOTH_UNION; pr[1].k = 20;
            memset(&cd, 0, sizeof cd);
            cd.prims = pr; cd.n = 2; cd.w = 200; cd.h = 100;
            cd.edge = PSYGFX_EDGE_GAUSSIAN; cd.edge_width = 2; cd.color[0] = cd.color[1] = cd.color[2] = 1;
            s = psygfx_compound(&cd);
            psygfx__vpack(&R->g, &s, blk, ext, &clip, &err);
            for (j = 0; j < 2400 && nz < 59000; j++) {   /* sign changes along rows, 0.05 px */
                double y = -60 + j * 0.05, prev = 0, f0[4], f1[4];
                for (i = 0; i < 3600; i++) {
                    double x = -90 + i * 0.05;
                    psygfx__vfield(blk, x, y, f0, f1);
                    if (i && (f0[0] > 0) != (prev > 0) && nz < 59000) { zx[nz] = x - 0.05 * f0[0] / (f0[0] - prev); zy[nz++] = y; }
                    prev = f0[0];
                }
            }
            for (i = 0; i < 3600 && nz < 59000; i++) {   /* and along columns */
                double x = -90 + i * 0.05, prev = 0, f0[4], f1[4];
                for (j = 0; j < 2400; j++) {
                    double y = -60 + j * 0.05;
                    psygfx__vfield(blk, x, y, f0, f1);
                    if (j && (f0[0] > 0) != (prev > 0) && nz < 59000) { zy[nz] = y - 0.05 * f0[0] / (f0[0] - prev); zx[nz++] = x; }
                    prev = f0[0];
                }
            }
            gl_frame(&s, 1);
            for (j = 0; j < H; j++)
                for (i = 0; i < W; i++) {
                    float lx, ly;
                    double f0[4], f1[4], best = 1e300;
                    int z;
                    psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                    psygfx__vfield(blk, lx, ly, f0, f1);
                    if (fabs(f0[0]) > 14) continue;
                    for (z = 0; z < nz; z++) best = fmin(best, (lx - zx[z]) * (lx - zx[z]) + (ly - zy[z]) * (ly - zy[z]));
                    best = sqrt(best) * (f0[0] > 0 ? 1 : -1);
                    if (fabs(best) > 12) continue;
                    worst = maxd(worst, fabs(R->scene[(j * W + i) * 4] - cov(PSYGFX_EDGE_GAUSSIAN, 2, best)));
                }
            if (q) S5.t3_connect = worst; else S5.t3_smooth = worst;
        }
    }
    psygfx_close(&R->g);
}

/* The shader's main with every effect, in double: premultiplied rgba. */
static void fx_ref(const float* b, const psygfx_stim* s, int i, int j, double out[4]) {
    float lx, ly;
    double f0[4], f1[4], d, d1, hw = b[15], c = b[11], cm, w = b[22], acc[4], col[3];
    int prof = (int)b[21], fl = (int)b[23], k;
    const float* vd = b + 36;
    psygfx_local(&R->g, s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
    psygfx__vfield(b, lx, ly, f0, f1);
    d = f0[0] - b[10]; d1 = f1[0] - b[10];
    cm = hw > 0 ? cov(prof, w, d - c - hw) - cov(prof, w, d - c + hw) : cov(prof, w, d);
    for (k = 0; k < 3; k++) col[k] = b[12 + k];
    for (k = 0; k < 3; k++) acc[k] = col[k] * cm;
    acc[3] = cm;
    if (fl & 32) {
        int o = (int)b[32];
        double r[4] = { 0, 0, 0, 0 }, t[4], a;
        const float* Dp = vd + 4 * o;
#define OVER_(T) do { int q_; for (q_ = 0; q_ < 4; q_++) r[q_] = (T)[q_] + r[q_] * (1 - (T)[3]); } while (0)
#define GAUSS_(x, sg) ((sg) <= 0 ? ((x) <= 0 ? 1.0 : 0.0) : 0.5 * erfc((x) / ((sg) * sqrt(2.0))))
        if (Dp[2] > 0) {
            if (fl & 64) {
                double hx = vd[8] + Dp[1], hy = vd[9] + Dp[1], sg = Dp[0], px = lx - b[28], py = ly - b[29];
                a = (0.5 * erfc(-(hx - px) / (sg * sqrt(2.0))) - 0.5 * erfc(-(-hx - px) / (sg * sqrt(2.0)))) *
                    (0.5 * erfc(-(hy - py) / (sg * sqrt(2.0))) - 0.5 * erfc(-(-hy - py) / (sg * sqrt(2.0))));
            } else a = GAUSS_(d1 - Dp[1], Dp[0]);
            a *= Dp[2];
            for (k = 0; k < 3; k++) r[k] = Dp[4 + k] * a;
            r[3] = a;
        }
        if (Dp[10] > 0) { a = GAUSS_(d - Dp[9], Dp[8]) * Dp[10]; for (k = 0; k < 3; k++) t[k] = Dp[12 + k] * a; t[3] = a; OVER_(t); }
        OVER_(acc);
        if (Dp[18] > 0) { a = cov(prof, w, d) * GAUSS_(-(d1 + Dp[17]), Dp[16]) * Dp[18]; for (k = 0; k < 3; k++) t[k] = Dp[20 + k] * a; t[3] = a; OVER_(t); }
        for (k = 0; k < 2; k++) {
            const float* B = Dp + 24 + 8 * k;
            if (B[2] > 0) { int q; a = (cov(prof, w, d - B[1]) - cov(prof, w, d - B[0])) * B[2]; for (q = 0; q < 3; q++) t[q] = B[4 + q] * a; t[3] = a; OVER_(t); }
        }
        for (k = 0; k < 4; k++) acc[k] = r[k];
#undef OVER_
#undef GAUSS_
    }
    for (k = 0; k < 4; k++) out[k] = acc[k] * b[8] * b[9];
}

static void gl_v03_fx(stats* st) {
    static float blk[16 * 64];
    static float a1[W * H * 4];
    psygfx_shape_desc d;
    psygfx_fx fx;
    psygfx_stim s;
    double ext[2];
    const char* err;
    int clip, i, j, k;
    (void)st;
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    /* T6: every effect in one pass against the same layers on the CPU */
    memset(&fx, 0, sizeof fx);
    fx.dx = 7; fx.dy = 5;
    fx.drop.sigma = 4; fx.drop.spread = 1; fx.drop.opacity = 0.6f; fx.drop.color[2] = 0.3f;
    fx.glow.sigma = 3; fx.glow.spread = 2; fx.glow.opacity = 0.5f; fx.glow.color[0] = 0.9f; fx.glow.color[1] = 0.7f;
    fx.inner.sigma = 2; fx.inner.opacity = 0.7f;
    fx.band[0].a1 = -2; fx.band[0].a2 = 0; fx.band[0].opacity = 1; fx.band[0].color[0] = fx.band[0].color[1] = fx.band[0].color[2] = 1;
    fx.band[1].a1 = 4; fx.band[1].a2 = 6; fx.band[1].opacity = 0.5f; fx.band[1].color[1] = 1;
    memset(&d, 0, sizeof d);
    d.shape = PSYGFX_RRECT; d.w = 140; d.h = 90; d.shape_p[0] = d.shape_p[1] = d.shape_p[2] = d.shape_p[3] = 16;
    d.color[0] = 0.3f; d.color[1] = 0.35f; d.color[2] = 0.4f; d.opacity = 0.9f;
    d.edge = PSYGFX_EDGE_COSINE; d.edge_width = 2; d.fx = &fx;
    for (k = 0; k < 2; k++) {
        d.ori = k ? 20.0f : 0.0f;
        s = psygfx_shape(&d);
        psygfx__vpack(&R->g, &s, blk, ext, &clip, &err);
        gl_frame(&s, 1);
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                double want[4];
                int c;
                fx_ref(blk, &s, i, j, want);
                for (c = 0; c < 3; c++) S5.fx = maxd(S5.fx, fabs(R->scene[(j * W + i) * 4 + c] - want[c]));
            }
    }
    /* T4: the exact blur of a sharp rect, and F(d)'s shadow against it */
    for (k = 0; k < 2; k++) {
        memset(&fx, 0, sizeof fx);
        fx.dx = 40; fx.dy = 25; fx.drop.sigma = 5; fx.drop.opacity = 1; fx.drop.color[0] = fx.drop.color[1] = fx.drop.color[2] = 1;
        fx.flags = k ? 0u : PSYGFX_FX_EXACT_BLUR;
        memset(&d, 0, sizeof d);
        d.shape = PSYGFX_RECT; d.w = 100; d.h = 60; d.x = -30; d.y = -20; d.fx = &fx;   /* black, hard: it hides the shadow under it */
        s = psygfx_shape(&d);
        gl_frame(&s, 1);
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                float lx, ly;
                double px, py, b;
                psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                if (fabs(lx) <= 51 && fabs(ly) <= 31) continue;   /* the fill */
                px = lx - 40; py = ly - 25;
                b = (Phi((50 - px) / 5) - Phi((-50 - px) / 5)) * (Phi((30 - py) / 5) - Phi((-30 - py) / 5));
                if (k) S5.t4_fd = maxd(S5.t4_fd, fabs(R->scene[(j * W + i) * 4] - b));
                else S5.t4_exact = maxd(S5.t4_exact, fabs(R->scene[(j * W + i) * 4] - b));
            }
    }
    psygfx_close(&R->g);
    /* T9: MULTIPLY and ADD on a gray background, against the exact forms */
    if (!gl_open(1, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    for (k = 0; k < 2; k++) {
        static const float c3[3] = { 0.2f, 0.6f, 1.0f };
        memset(&d, 0, sizeof d);
        d.shape = PSYGFX_RECT; d.w = 80; d.h = 60; d.opacity = 0.8f; memcpy(d.color, c3, sizeof c3);
        d.blend = k ? PSYGFX_BLEND_MODE_ADD : PSYGFX_BLEND_MODE_MULTIPLY;
        s = psygfx_shape(&d);
        gl_frame(&s, 1);
        for (i = 0; i < 3; i++) {
            double got = R->scene[(H / 2 * W + W / 2) * 4 + i];
            double want = k ? 0.5 + 0.8 * c3[i] : 0.5 * (1 - 0.8 + 0.8 * c3[i]);
            S5.blend = maxd(S5.blend, fabs(got - want));
        }
        S5.blend = maxd(S5.blend, fabs(R->scene[0] - 0.5));   /* outside: the background */
    }
    {   /* a modulation cannot take a blend */
        psygfx_gabor_desc gd;
        memset(&gd, 0, sizeof gd);
        gd.sigma = 10; gd.sf = 0.1f;
        s = psygfx_gabor(&gd);
        s.blend = PSYGFX_BLEND_MODE_MULTIPLY;
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        S5.refused_n++; S5.refused_ok += psygfx_draw(&R->g, &s) == PSYGFX_ERR_ARG;
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
    }
    /* T12: stimuli with extension blocks among batched v0.2 ones, against
     * one draw each */
    {
        static psygfx_stim many[40];
        static psygfx_prim pr[6];
        psygfx_compound_desc cd;
        for (k = 0; k < 40; k++) {
            memset(&d, 0, sizeof d);
            d.shape = (psygfx_shape_kind)(k % 3 == 0 ? PSYGFX_CIRCLE : PSYGFX_RECT); d.w = 20; d.h = 14;
            d.x = (float)(k % 10 * 28 - 130); d.y = (float)(k / 10 * 40 - 70); d.edge = PSYGFX_EDGE_COSINE; d.edge_width = 2;
            d.color[0] = 0.1f * (k % 7); d.color[1] = 0.5f; d.opacity = 0.7f;
            many[k] = psygfx_shape(&d);
        }
        memset(pr, 0, sizeof pr);
        for (k = 0; k < 6; k++) { pr[k].shape = PSYGFX_CIRCLE; pr[k].x = (float)(k * 20 - 50); pr[k].w = 26; pr[k].op = PSYGFX_OP_SMOOTH_UNION; pr[k].k = 8; }
        memset(&cd, 0, sizeof cd);
        cd.prims = pr; cd.n = 6; cd.w = 150; cd.h = 40; cd.color[0] = 1; cd.edge = PSYGFX_EDGE_GAUSSIAN; cd.edge_width = 1;
        many[13] = psygfx_compound(&cd);
        cd.y = 50; many[27] = psygfx_compound(&cd);
        gl_frame(many, 40);
        memcpy(a1, R->scene, sizeof a1);
        R->g.max_batch = 1;
        gl_frame(many, 40);
        R->g.max_batch = PSYGFX__BATCH;
        for (i = 0; i < W * H * 4; i++) S5.batch = maxd(S5.batch, fabs(a1[i] - R->scene[i]));
    }
    /* T7: a dash offset by one period draws the same; a snapped circle has
     * a whole number of dashes; trim keeps its stretch of the boundary */
    {
        float P;
        memset(&d, 0, sizeof d);
        d.shape = PSYGFX_CIRCLE; d.w = 150; d.stroke = 4; d.dash[0] = 13; d.dash[1] = 7; d.dash_snap = true; d.color[0] = 1;
        d.edge = PSYGFX_EDGE_COSINE; d.edge_width = 1.5f;
        s = psygfx_shape(&d);
        psygfx__vpack(&R->g, &s, blk, ext, &clip, &err);
        P = blk[16] + blk[17];
        gl_frame(&s, 1);
        memcpy(a1, R->scene, sizeof a1);
        s.dash_offset = P;
        gl_frame(&s, 1);
        for (i = 0; i < W * H * 4; i++) S5.dash_period = maxd(S5.dash_period, fabs(a1[i] - R->scene[i]));
        {   /* count dashes around the ring: rises of the red channel at r = 75 */
            int rises = 0, prev = -1, want = (int)floor(psygfx_length(&R->g, &s) / 20.0 + 0.5);
            for (k = 0; k < 4000; k++) {
                double a = 2 * 3.14159265358979323846 * k / 4000.0;
                int x = (int)floor(W / 2 + 75 * cos(a)), y = (int)floor(H / 2 + 75 * sin(a)), on = R->scene[(y * W + x) * 4] > 0.5;
                if (prev == 0 && on) rises++;
                prev = on;
            }
            S5.dash_count_bad += abs(rises - want) > 1;
            if (abs(rises - want) > 1) fprintf(stderr, "  dashes %d, want %d\n", rises, want);
        }
        d.dash[0] = 0; d.trim[0] = 0.0f; d.trim[1] = 0.5f; d.edge = PSYGFX_EDGE_HARD;
        s = psygfx_shape(&d);
        gl_frame(&s, 1);
        for (k = 0; k < 360; k++) {   /* clockwise from +x: the first half on, the second off */
            double a = (k + 0.5) * 3.14159265358979323846 / 180.0;
            int x = (int)floor(W / 2 + 75 * cos(a)), y = (int)floor(H / 2 + 75 * sin(a)), on = R->scene[(y * W + x) * 4] > 0.5;
            if (k == 0 || k == 179 || k == 180 || k == 359) continue;
            S5.trim_bad += on != (k < 180);
        }
    }
    {   /* refusals: dashes on a fill, on a compound; 57 prims; MITER outside a polyline */
        static psygfx_prim pr[57];
        psygfx_compound_desc cd;
        psygfx_stim r[4];
        memset(&d, 0, sizeof d);
        d.shape = PSYGFX_CIRCLE; d.w = 50; d.dash[0] = 5; d.dash[1] = 5;
        r[0] = psygfx_shape(&d);
        memset(pr, 0, sizeof pr);
        for (k = 0; k < 57; k++) { pr[k].shape = PSYGFX_CIRCLE; pr[k].w = 10; }
        memset(&cd, 0, sizeof cd);
        cd.prims = pr; cd.n = 57;
        r[1] = psygfx_compound(&cd);
        cd.n = 2; r[2] = psygfx_compound(&cd); r[2].stroke = 2; r[2].dash[0] = 4; r[2].dash[1] = 4;
        d.dash[0] = 0; d.shape = PSYGFX_STAR; d.shape_p[0] = 5; d.join = PSYGFX_JOIN_MITER;
        r[3] = psygfx_shape(&d);
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        for (k = 0; k < 4; k++) { S5.refused_n++; S5.refused_ok += psygfx_draw(&R->g, &r[k]) == PSYGFX_ERR_ARG; }
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
    }
    psygfx_close(&R->g);
}

/* Oklab from linear device RGB through a calibration's XYZ (white Y = 1),
 * Ottosson's published matrices, in double: written apart from the header. */
static void ok_from_rgb(const psygfx_cal* c, const double rgb[3], double lab[3]) {
    static const double m1[9] = { 0.8189330101, 0.3618667424, -0.1288597137, 0.0329845436, 0.9293118715, 0.0361456387,
                                  0.0482003018, 0.2643662691, 0.6338517070 };
    static const double m2[9] = { 0.2104542553, 0.7936177850, -0.0040720468, 1.9779984951, -2.4285922050, 0.4505937099,
                                  0.0259040371, 0.7827717662, -0.8086757660 };
    double xyz[3], lms[3], yw = c->rgb_to_xyz[3] + c->rgb_to_xyz[4] + c->rgb_to_xyz[5];
    int i, k;
    for (i = 0; i < 3; i++) { xyz[i] = 0; for (k = 0; k < 3; k++) xyz[i] += c->rgb_to_xyz[3 * i + k] * rgb[k] / yw; }
    for (i = 0; i < 3; i++) { lms[i] = 0; for (k = 0; k < 3; k++) lms[i] += m1[3 * i + k] * xyz[k]; lms[i] = cbrt(lms[i]); }
    for (i = 0; i < 3; i++) { lab[i] = 0; for (k = 0; k < 3; k++) lab[i] += m2[3 * i + k] * lms[k]; }
}

/* ... and back, by Newton on the forward map (no inverse matrices shared
 * with the header). */
static void ok_to_rgb(const psygfx_cal* c, const double lab[3], double rgb[3]) {
    int it, i, j;
    rgb[0] = rgb[1] = rgb[2] = 0.5;
    for (it = 0; it < 30; it++) {
        double f[3], J[9], inv[9], det, h = 1e-7;
        ok_from_rgb(c, rgb, f);
        for (j = 0; j < 3; j++) {
            double r2[3], f2[3];
            memcpy(r2, rgb, sizeof r2);
            r2[j] += h;
            ok_from_rgb(c, r2, f2);
            for (i = 0; i < 3; i++) J[3 * i + j] = (f2[i] - f[i]) / h;
        }
        det = J[0] * (J[4] * J[8] - J[5] * J[7]) - J[1] * (J[3] * J[8] - J[5] * J[6]) + J[2] * (J[3] * J[7] - J[4] * J[6]);
        inv[0] = (J[4] * J[8] - J[5] * J[7]) / det; inv[1] = (J[2] * J[7] - J[1] * J[8]) / det; inv[2] = (J[1] * J[5] - J[2] * J[4]) / det;
        inv[3] = (J[5] * J[6] - J[3] * J[8]) / det; inv[4] = (J[0] * J[8] - J[2] * J[6]) / det; inv[5] = (J[2] * J[3] - J[0] * J[5]) / det;
        inv[6] = (J[3] * J[7] - J[4] * J[6]) / det; inv[7] = (J[1] * J[6] - J[0] * J[7]) / det; inv[8] = (J[0] * J[4] - J[1] * J[3]) / det;
        for (i = 0; i < 3; i++) {
            double e = 0;
            for (j = 0; j < 3; j++) e += inv[3 * i + j] * (f[j] - lab[j]);
            rgb[i] -= e;
        }
    }
}

static void gl_v03_paint(stats* st) {
    static psygfx_cal cal, cal2;
    static const float xy[4][2] = { { 0.64f, 0.33f }, { 0.30f, 0.60f }, { 0.15f, 0.06f }, { 0.3127f, 0.3290f } };
    psygfx_desc gd;
    psygfx_shape_desc d;
    psygfx_paint pt;
    psygfx_stim s;
    int i, j, k, sp;
    (void)st;
    CHECK(psygfx_cal_nominal(&cal, xy, 80.0f, 2.2) == PSYGFX_OK);
    {   /* a calibration with spectra for DKL: Psychtoolbox's B_monitor */
        float r[81], g[81], b[81];
        char err[200];
        for (i = 0; i < 81; i++) { r[i] = b_monitor[i][0]; g[i] = b_monitor[i][1]; b[i] = b_monitor[i][2]; }
        psygfx_cal_init(&cal2);
        psygfx_cal_add(&cal2, PSYGFX_GUN_BLACK, 0, 0, 0, 0);
        for (k = 0; k < 3; k++) psygfx_cal_add(&cal2, k, 1, 1, 0, 0);
        psygfx_cal_set_spectra(&cal2, 380, 5, 81, r, g, b, NULL);
        CHECK(psygfx_cal_derive(&cal2, err, sizeof err) == PSYGFX_OK);
    }
    for (sp = 0; sp < 3; sp++) {
        memset(&gd, 0, sizeof gd);
        gd.screen = &R->scr;
        gd.background[0] = 0.4f; gd.background[1] = 0.5f; gd.background[2] = 0.3f;
        gd.cal = sp == 2 ? &cal2 : (sp == 1 ? &cal : NULL);
        psygfx__test_scene32 = 1;
        if (!psygfx_open(&R->g, &gd)) { fprintf(stderr, "paint open: %s\n", psygfx_error(&R->g)); g_failures++; return; }
        for (k = 0; k < 3; k++) {   /* LINEAR, RADIAL, ANGULAR */
            memset(&pt, 0, sizeof pt);
            pt.kind = (uint8_t)(PSYGFX_PAINT_LINEAR + k); pt.space = (uint8_t)sp; pt.n = 3;
            if (k == 0) { pt.x0 = -60; pt.y0 = -20; pt.x1 = 60; pt.y1 = 20; }
            if (k == 1) { pt.x0 = 5; pt.y0 = -3; pt.x1 = 70; }
            if (k == 2) { pt.x0 = 0; pt.y0 = 0; pt.x1 = 30; }
            pt.stops[0].t = 0; pt.stops[0].color[0] = 0.55f; pt.stops[0].color[1] = 0.45f; pt.stops[0].color[2] = 0.25f;
            pt.stops[1].t = 0.4f; pt.stops[1].color[0] = 0.30f; pt.stops[1].color[1] = 0.55f; pt.stops[1].color[2] = 0.35f;
            pt.stops[2].t = 1; pt.stops[2].color[0] = 0.40f; pt.stops[2].color[1] = 0.45f; pt.stops[2].color[2] = 0.45f;
            memset(&d, 0, sizeof d);
            d.shape = PSYGFX_RECT; d.w = 160; d.h = 120; d.paint = &pt; d.ori = 10;
            s = psygfx_shape(&d);
            gl_frame(&s, 1);
            for (j = 0; j < H; j += 3)
                for (i = 0; i < W; i += 3) {
                    float lx, ly;
                    double t, a[3], b[3], m[3], want[3], u;
                    int q, seg;
                    psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                    if (fabs(lx) > 79 || fabs(ly) > 59) continue;
                    if (k == 0) t = ((lx + 60) * 120 + (ly + 20) * 40) / (120.0 * 120 + 40 * 40);
                    else if (k == 1) t = sqrt((lx - 5) * (lx - 5) + (ly + 3) * (ly + 3)) / 70;
                    else { t = (atan2(ly, lx) - 30 * 3.14159265358979323846 / 180) / (2 * 3.14159265358979323846); t -= floor(t); }
                    if (k == 2 && (fabs(t) < 1e-3 || fabs(t - 1) < 1e-3)) continue;   /* the angular seam */
                    t = t < 0 ? 0 : (t > 1 ? 1 : t);
                    seg = t < 0.4 ? 0 : 1;
                    u = seg ? (t - 0.4) / 0.6 : t / 0.4;
                    for (q = 0; q < 3; q++) { a[q] = pt.stops[seg].color[q]; b[q] = pt.stops[seg + 1].color[q]; }
                    if (sp == 0) for (q = 0; q < 3; q++) want[q] = a[q] + (b[q] - a[q]) * u;
                    else if (sp == 1) {
                        double la[3], lb[3];
                        ok_from_rgb(&cal, a, la); ok_from_rgb(&cal, b, lb);
                        for (q = 0; q < 3; q++) m[q] = la[q] + (lb[q] - la[q]) * u;
                        ok_to_rgb(&cal, m, want);
                    } else {   /* DKL polar about the background, through psygfx_cal_dir_dkl */
                        double ea[3], eb[3], dk[3], inv[9], M[9];
                        float dir[3] = { 0, 0, 0 }, unit[3];   /* newer gcc cannot see the call fill it */
                        int c2, r2;
                        for (c2 = 0; c2 < 3; c2++) {   /* the RGB increment of each DKL unit axis, then its inverse */
                            unit[0] = unit[1] = unit[2] = 0; unit[c2] = 1;
                            psygfx_cal_dir_dkl(&cal2, gd.background, unit, dir);
                            for (r2 = 0; r2 < 3; r2++) M[3 * r2 + c2] = dir[r2];
                        }
                        psygfx__inv3(M, inv);
                        for (q = 0; q < 2; q++) {
                            double* e3 = q ? eb : ea;
                            const double* cc = q ? b : a;
                            double inc[3], v[3], rr;
                            for (r2 = 0; r2 < 3; r2++) inc[r2] = cc[r2] - gd.background[r2];
                            for (r2 = 0; r2 < 3; r2++) v[r2] = inv[3 * r2] * inc[0] + inv[3 * r2 + 1] * inc[1] + inv[3 * r2 + 2] * inc[2];
                            rr = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                            e3[0] = asin(v[0] / rr); e3[1] = atan2(v[2], v[1]); e3[2] = rr;
                        }
                        while (eb[1] - ea[1] > 3.14159265358979323846) eb[1] -= 2 * 3.14159265358979323846;
                        while (eb[1] - ea[1] < -3.14159265358979323846) eb[1] += 2 * 3.14159265358979323846;
                        for (q = 0; q < 3; q++) m[q] = ea[q] + (eb[q] - ea[q]) * u;
                        dk[0] = m[2] * sin(m[0]); dk[1] = m[2] * cos(m[0]) * cos(m[1]); dk[2] = m[2] * cos(m[0]) * sin(m[1]);
                        for (q = 0; q < 3; q++) want[q] = gd.background[q] + M[3 * q] * dk[0] + M[3 * q + 1] * dk[1] + M[3 * q + 2] * dk[2];
                    }
                    for (q = 0; q < 3; q++) {
                        double e = fabs(R->scene[(j * W + i) * 4 + q] - want[q]);
                        if (sp == 0) S5.paint_rgb = maxd(S5.paint_rgb, e);
                        else if (sp == 1) S5.paint_oklab = maxd(S5.paint_oklab, e);
                        else S5.paint_dkl = maxd(S5.paint_dkl, e);
                    }
                }
        }
        if (sp == 0) {
            /* VERTEX on a triangle: Wachspress coordinates are barycentric there */
            static const float tri[6] = { -70, 50, 80, 40, -10, -60 };
            static const float vc[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            memset(&pt, 0, sizeof pt);
            pt.kind = PSYGFX_PAINT_VERTEX; pt.vertex_colors = vc;
            memset(&d, 0, sizeof d);
            d.shape = PSYGFX_POLYGON; d.vertices = tri; d.shape_p[0] = 3; d.paint = &pt;
            s = psygfx_shape(&d);
            gl_frame(&s, 1);
            for (j = 0; j < H; j++)
                for (i = 0; i < W; i++) {
                    float lx, ly;
                    double l0, l1, l2, den;
                    psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                    den = (tri[3] - tri[5]) * (tri[0] - tri[4]) + (tri[4] - tri[2]) * (tri[1] - tri[5]);
                    l0 = ((tri[3] - tri[5]) * (lx - tri[4]) + (tri[4] - tri[2]) * (ly - tri[5])) / den;
                    l1 = ((tri[5] - tri[1]) * (lx - tri[4]) + (tri[0] - tri[4]) * (ly - tri[5])) / den;
                    l2 = 1 - l0 - l1;
                    if (l0 < 0.01 || l1 < 0.01 || l2 < 0.01) continue;   /* inside, off the edge */
                    S5.paint_vertex = maxd(S5.paint_vertex, fabs(R->scene[(j * W + i) * 4] - l0));
                    S5.paint_vertex = maxd(S5.paint_vertex, fabs(R->scene[(j * W + i) * 4 + 1] - l1));
                    S5.paint_vertex = maxd(S5.paint_vertex, fabs(R->scene[(j * W + i) * 4 + 2] - l2));
                }
            /* no calibration: OKLAB and DKL_POLAR are refused */
            memset(&pt, 0, sizeof pt);
            pt.kind = PSYGFX_PAINT_LINEAR; pt.space = PSYGFX_SPACE_OKLAB; pt.n = 2; pt.x1 = 10;
            memset(&d, 0, sizeof d);
            d.shape = PSYGFX_RECT; d.w = 20; d.h = 20; d.paint = &pt;
            s = psygfx_shape(&d);
            CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
            CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
            S5.refused_n++; S5.refused_ok += psygfx_draw(&R->g, &s) == PSYGFX_ERR_REFUSED;
            pt.space = PSYGFX_SPACE_DKL_POLAR;
            S5.refused_n++; S5.refused_ok += psygfx_draw(&R->g, &s) == PSYGFX_ERR_REFUSED;
            CHECK(psygfx_end(&R->g) == PSYGFX_OK);
            CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
        }
        psygfx_close(&R->g);
    }
}

/* An MSDF texture whose three channels hold the same distance (to a rounded
 * box): the median is then that distance, and the GPU's decoding is
 * checked against the CPU's bilinear of the same texels. */
static void gl_v03_msdf(stats* st) {
    enum { TW = 96, TH = 48 };
    static float tex[TW * TH * 4];
    psygfx_texture_desc td;
    psygfx_tex t;
    psygfx_shape_desc d;
    psygfx_stim s;
    int i, j, k;
    const double range = 16;
    (void)st;
    for (j = 0; j < TH; j++)
        for (i = 0; i < TW; i++) {
            /* two glyphs side by side: boxes in each 48 x 48 half */
            double x = i + 0.5 - (i < 48 ? 24 : 72), y = j + 0.5 - 24, dd, v;
            double hx = i < 48 ? 10 : 14, hy = i < 48 ? 14 : 8, qx = fabs(x) - hx + 3, qy = fabs(y) - hy + 3;
            dd = sqrt(fmax(qx, 0) * fmax(qx, 0) + fmax(qy, 0) * fmax(qy, 0)) + fmin(fmax(qx, qy), 0) - 3;
            v = 0.5 - dd / range;
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            /* three channels apart, their median the distance (red); alpha too */
            tex[(j * TW + i) * 4] = (float)v;
            tex[(j * TW + i) * 4 + 1] = (float)(v + 0.2 > 1 ? 1 : v + 0.2);
            tex[(j * TW + i) * 4 + 2] = (float)(v - 0.15 < 0 ? 0 : v - 0.15);
            tex[(j * TW + i) * 4 + 3] = (float)v;
        }
    if (!gl_open(0, PSYGFX_RGBA32F, PSYGFX_DITHER_NONE)) return;
    memset(&td, 0, sizeof td);
    td.w = TW; td.h = TH; td.format = PSYGFX_RGBA32F; td.data = tex; td.sdf = PSYGFX_SDF_MSDF; td.sdf_range = (float)range;
    t = psygfx_texture(&R->g, &td);
    CHECK(t.id != 0);
    for (k = 0; k < 2; k++) {   /* the second glyph by its src rectangle, at 2 px per texel */
        double sc = 2.0, sx0 = 48;
        memset(&d, 0, sizeof d);
        d.shape = PSYGFX_MASK_TEX; d.mask = t; d.w = (float)(48 * sc); d.h = (float)(48 * sc); d.ori = k ? 15.0f : 0.0f; d.x = 1.25f;
        d.edge = PSYGFX_EDGE_GAUSSIAN; d.edge_width = 1.5f; d.color[0] = 1;
        s = psygfx_shape(&d);
        s.src[0] = 48; s.src[1] = 0; s.src[2] = 48; s.src[3] = 48;
        if (k) { s.stroke = 3; s.join = PSYGFX_JOIN_MITER; }
        gl_frame(&s, 1);
        for (j = 0; j < H; j++)
            for (i = 0; i < W; i++) {
                float lx, ly;
                double tx, ty, fx, fy, wx, wy, c00, c10, c01, c11, m, dd, want, a1v, a2v;
                int x0, y0, x1, y1;
                psygfx_local(&R->g, &s, (float)i + 0.5f, (float)j + 0.5f, &lx, &ly);
                if (fabs(lx) > 48 || fabs(ly) > 48) continue;
                tx = sx0 + (lx + 48) / sc; ty = (ly + 48) / sc;
                fx = tx - 0.5; fy = ty - 0.5; x0 = (int)floor(fx); y0 = (int)floor(fy); wx = fx - x0; wy = fy - y0;
                x1 = x0 + 1; y1 = y0 + 1;
                if (x0 < 48) x0 = 48;
                if (x1 > 95) x1 = 95;
                if (x0 > 95) x0 = 95;
                if (x1 < 48) x1 = 48;
                if (y0 < 0) y0 = 0;
                if (y1 > 47) y1 = 47;
                if (y0 > 47) y0 = 47;
                if (y1 < 0) y1 = 0;
                c00 = tex[(y0 * TW + x0) * 4]; c10 = tex[(y0 * TW + x1) * 4]; c01 = tex[(y1 * TW + x0) * 4]; c11 = tex[(y1 * TW + x1) * 4];
                m = (c00 + (c10 - c00) * wx) * (1 - wy) + (c01 + (c11 - c01) * wx) * wy;
                dd = (0.5 - m) * range * sc;
                if (k) { a1v = -1.5; a2v = 1.5; want = cov(2, 1.5, dd - a2v) - cov(2, 1.5, dd - a1v); }
                else want = cov(2, 1.5, dd);
                S5.msdf = maxd(S5.msdf, fabs(R->scene[(j * W + i) * 4] - want));
            }
    }
    {   /* the range rule at 1 px per texel and range 16: a Gaussian reaches
         * 5 SD, which must stay within 8 - 1.5 = 6.5 px */
        int rc[3];
        memset(&d, 0, sizeof d);
        d.shape = PSYGFX_MASK_TEX; d.mask = t; d.w = 96; d.h = 48; d.edge = PSYGFX_EDGE_GAUSSIAN; d.color[0] = 1;
        CHECK(psyscr_begin(&R->scr, &R->f) == PSYSCR_OK);
        CHECK(psygfx_begin(&R->g, &R->f) == PSYGFX_OK);
        d.edge_width = 1.25f; s = psygfx_shape(&d); rc[0] = psygfx_draw(&R->g, &s);          /* 6.25 <= 6.5 at 1 px per texel */
        d.edge_width = 1.4f; s = psygfx_shape(&d); rc[1] = psygfx_draw(&R->g, &s);           /* 7 > 6.5 */
        if (rc[1] == PSYGFX_ERR_RANGE) CHECK(strstr(psygfx_error(&R->g), "range") != NULL);
        d.edge_width = 0; d.stroke = 2; s = psygfx_shape(&d); rc[2] = psygfx_draw(&R->g, &s); /* MSDF, ROUND join */
        CHECK(psygfx_end(&R->g) == PSYGFX_OK);
        CHECK(psyscr_flip(&R->scr) == PSYSCR_OK);
        S5.refused_n += 3;
        S5.refused_ok += (rc[0] == PSYGFX_OK) + (rc[1] == PSYGFX_ERR_RANGE) + (rc[2] == PSYGFX_ERR_ARG);
    }
    {   /* a glyph run of 3 against the same glyphs drawn one by one */
        static psygfx_glyph gl3[3];
        static float a1[W * H * 4];
        psygfx_glyphs_desc gd;
        psygfx_buf b;
        psygfx_stim one[3];
        memset(gl3, 0, sizeof gl3);
        for (k = 0; k < 3; k++) { gl3[k].x = (float)(k * 50); gl3[k].y = (float)(k * 7); gl3[k].sx = (float)(k % 2 * 48); gl3[k].sw = 48; gl3[k].sh = 48; }
        b = psygfx_buffer(&R->g, sizeof gl3);
        CHECK(psygfx_buffer_update(&R->g, b, 0, gl3, sizeof gl3) == PSYGFX_OK);
        memset(&gd, 0, sizeof gd);
        gd.atlas = t; gd.buf = b; gd.count = 3; gd.scale = 2; gd.place = PSYGFX_TOP_LEFT; gd.anchor = PSYGFX_TOP_LEFT;
        gd.x = 20; gd.y = 30; gd.w = 150; gd.h = 62; gd.edge = PSYGFX_EDGE_COSINE; gd.edge_width = 2; gd.color[1] = 1;
        s = psygfx_glyphs(&gd);
        gl_frame(&s, 1);
        memcpy(a1, R->scene, sizeof a1);
        for (k = 0; k < 3; k++) {
            memset(&d, 0, sizeof d);
            d.shape = PSYGFX_MASK_TEX; d.mask = t; d.w = 96; d.h = 96; d.place = PSYGFX_TOP_LEFT; d.anchor = PSYGFX_TOP_LEFT;
            d.x = 20.0f + gl3[k].x; d.y = 30.0f + gl3[k].y; d.edge = PSYGFX_EDGE_COSINE; d.edge_width = 2; d.color[1] = 1;
            one[k] = psygfx_shape(&d);
            one[k].src[0] = gl3[k].sx; one[k].src[2] = 48; one[k].src[3] = 48;
        }
        gl_frame(one, 3);
        for (i = 0; i < W * H * 4; i++) S5.glyph = maxd(S5.glyph, fabs(a1[i] - R->scene[i]));
    }
    psygfx_close(&R->g);
}

/* v0.3's CPU half: field bindings, the new tables, refusals before GL. */
static void test_v03_cpu(void) {
    int n = 0, i, j;
    const psygfx_param* t;
    psygfx_prim pr[2];
    psygfx_fx fx;
    psygfx_bind b[3];
    float v[3] = { 12.0f, 0.25f, 7.0f };
    memset(pr, 0, sizeof pr);
    memset(&fx, 0, sizeof fx);
    memset(b, 0, sizeof b);
    b[0].field = &pr[1].x; b[0].channel = 0;
    b[1].field = &fx.drop.opacity; b[1].channel = 1;
    b[2].stim = NULL; b[2].param = 0; b[2].channel = 2;   /* bad: no stim, no field */
    CHECK(psygfx_apply(b, 3, v) == PSYGFX_ERR_ARG && pr[1].x == 0.0f);
    CHECK(psygfx_apply(b, 2, v) == 2 && pr[1].x == 12.0f && fx.drop.opacity == 0.25f);
    t = psygfx_prim_params(&n);
    CHECK(n == 11);
    for (i = 0; i < n; i++) {
        CHECK(t[i].name && t[i].offset + sizeof(float) <= sizeof(psygfx_prim) && t[i].min <= t[i].def && t[i].def <= t[i].max);
        for (j = 0; j < i; j++) CHECK(strcmp(t[i].name, t[j].name) != 0);
    }
    t = psygfx_fx_params(&n);
    CHECK(n == 32);
    for (i = 0; i < n; i++) {
        CHECK(t[i].name && t[i].offset + sizeof(float) <= sizeof(psygfx_fx));
        for (j = 0; j < i; j++) CHECK(strcmp(t[i].name, t[j].name) != 0);
    }
    CHECK(t[2].offset == offsetof(psygfx_fx, drop.sigma) && t[31].offset == offsetof(psygfx_fx, band[1].opacity));
    t = psygfx_paint_params(&n);
    CHECK(n == 28);
    for (i = 0; i < n; i++) CHECK(t[i].name && t[i].offset + sizeof(float) <= sizeof(psygfx_paint));
    t = psygfx_params(&n);
    CHECK(t[PSYGFX_P_TRIM_END].offset == offsetof(psygfx_stim, trim[1]));
    {   /* Oklab's L is relative to white: a round trip cannot see a lost
         * white scale, so the white itself must map to L = 1, a = b = 0 */
        static psygfx_gfx gw;
        static psygfx_cal cw;
        static const float xy[4][2] = { { 0.64f, 0.33f }, { 0.30f, 0.60f }, { 0.15f, 0.06f }, { 0.3127f, 0.3290f } };
        static const float white[3] = { 1.0f, 1.0f, 1.0f };
        double lab[3];
        memset(&gw, 0, sizeof gw);
        CHECK(psygfx_cal_nominal(&cw, xy, 80.0f, 2.2) == PSYGFX_OK);
        psygfx__paint_spaces(&gw, &cw);
        CHECK(gw.has_oklab);
        psygfx__to_space(&gw, PSYGFX_SPACE_OKLAB, white, lab);
        CHECK(fabs(lab[0] - 1.0) < 1e-3 && fabs(lab[1]) < 1e-3 && fabs(lab[2]) < 1e-3);
    }
}

static int gl_suite(const char* name, psygfx_hl_device dev) {
    psyscr_desc d;
    stats st;
    char line[400];
    memset(&st, 0, sizeof st);
    memset(R, 0, sizeof *R);
    R->hl.w = W; R->hl.h = H; R->hl.device = dev;
    memset(&d, 0, sizeof d);
    d.backend = PSYSCR_BACKEND_CUSTOM;
    d.presenter = &psygfx_headless_presenter;
    d.presenter_ctx = &R->hl;
    d.sim_period_ns = 1000000;
    if (!psyscr_open(&R->scr, &d)) {
        printf("GL %s: not available: %s\n", name, psyscr_error(&R->scr));
        return 0;
    }
    g_where = name;
    printf("GL %s: %s\n", name, R->hl.renderer);
    {
        static void (*const parts[])(stats*) = { gl_shapes, gl_gratings, gl_gabors, gl_dots, gl_images,
                                                 gl_noise, gl_output, gl_user_batch_rows,
                                                 gl_strokes, gl_masks, gl_sprites, gl_tint, gl_groups, gl_targets,
                                                 gl_alpha_passes, gl_edge_truth, gl_v03_kinds, gl_v03_truth, gl_v03_fx, gl_v03_paint, gl_v03_msdf };
        static const char* const names[] = { "shapes", "gratings", "gabors", "dots", "images", "noise",
                                             "output", "user+batch+rows", "strokes", "masks", "sprites", "tint",
                                             "groups", "targets", "alpha+passes", "edge truth", "v0.3 kinds", "v0.3 truth", "v0.3 fx", "v0.3 paint", "v0.3 msdf" };
        int k;
        memset(&S2, 0, sizeof S2);
        memset(&S3, 0, sizeof S3);
        memset(&S4, 0, sizeof S4);
        memset(&S5, 0, sizeof S5);
        printf("  seconds:");
        for (k = 0; k < (int)(sizeof parts / sizeof parts[0]); k++) {
            uint64_t t0 = psyrt_now_ns();
            const char* only = getenv("PSYGFX_TEST_PARTS");   /* a development seam: run parts by name */
            if (only && only[0] && !strstr(only, names[k])) continue;
            parts[k](&st);
            printf(" %s %.1f", names[k], (double)(psyrt_now_ns() - t0) * 1e-9);
            fflush(stdout);
        }
        printf("\n");
    }
    if (gl_open(0, PSYGFX_RGBA16F, PSYGFX_DITHER_NONE)) {
        psygfx_describe(&R->g, line, sizeof line);
        printf("  %s\n", line);
        psygfx_close(&R->g);
    }
    psyscr_close(&R->scr);
    printf("  measured: shapes soft-edge max %.2e, hard-edge wrong pixels %ld, hit vs GPU wrong %ld (ties skipped %ld)\n",
           st.shape_soft, st.hard_bad, st.hit_bad, st.ties);
    printf("  measured: grating max %.2e, square wave max %.2e, gabor max %.2e, user = grating %.2e\n",
           st.grating, st.square_bad, st.gabor, st.user);
    printf("  measured: dots max %.2e, image max %.2e, noise wrong %ld, identity codes wrong %ld, CLUT codes wrong %ld\n",
           st.dots, st.image, st.noise_bad, st.out_bad, st.lut_bad);
    printf("  measured: ordered dither wrong %ld, noise dither mean - target %+.4f, anchor change %.2e, "
           "overlap - sum %.2e, batched - single %.2e\n",
           st.dither_bad, st.dither_mean, st.anchor_diff, st.overlap, st.batch_diff);
    /* Tolerances: about 3x the largest error measured on a renderer of the
     * same kind (docs/psy_gfx.md has the table per renderer); SwiftShader's
     * transcendentals are about 10x coarser and get their own; exact where
     * the arithmetic is exact. */
    {
        const int ss = dev == PSYGFX_HL_SWIFTSHADER;
        CHECK_LE(st.shape_soft, ss ? 3e-4 : 5e-5);
        CHECK(st.hard_bad == 0);
        CHECK(st.hit_bad == 0);
        CHECK_LE(st.grating, ss ? 4e-4 : 5e-5);
        CHECK_LE(st.square_bad, ss ? 1.5e-4 : 2e-5);
        CHECK_LE(st.gabor, ss ? 3e-4 : 4e-6);
        CHECK_LE(st.user, 1e-6);
        CHECK_LE(st.dots, ss ? 3e-4 : 5e-5);
        CHECK_LE(st.image, 2.2e-7);
    }
    CHECK(st.noise_bad == 0);
    CHECK(st.out_bad == 0);
    CHECK(st.lut_bad == 0);
    CHECK(st.dither_bad == 0);
    CHECK_LE(fabs(st.dither_mean), 0.005);   /* the same on every renderer: the hash is exact */
    CHECK_LE(st.anchor_diff, 1e-6);
    CHECK_LE(st.overlap, 4e-7);
    CHECK(st.batch_diff == 0.0);
    printf("  v0.2: stroke soft max %.2e, hard wrong %ld, hit wrong %ld, miter wrong %ld; mask GPU max %.2e (distance "
           "texture vs the disc %.3f texel, vs brute force wrong %ld); sprites wrong %ld, bleed %ld, linear max %.2e; tint max %.2e\n",
           S2.stroke_soft, S2.stroke_hard_bad, S2.stroke_hit_bad, S2.miter_bad, S2.mask_gpu, S2.mask_sdf_cpu, S2.edt_bad,
           S2.sprite_bad, S2.bleed, S2.sprite_lin, S2.tint);
    printf("  v0.2: group vs alone max %.2e, group hit wrong %ld; target vs direct max %.2e, target wrong %ld, "
           "persist wrong %ld, ADD vs direct max %.2e, CLUT codes wrong %ld; refusals %ld of %ld; epoch wrong %ld "
           "(control without the epoch: %ld wrong); new context %ld of 4, then wrong %ld\n"
           "  v0.2: OVER with alpha against v0.1's, scene rgb and codes wrong %ld (control: target alpha differs at %ld "
           "texels); pass limit wrong %ld\n",
           S2.group_diff, S2.group_hit_bad, S2.target_diff, S2.target_bad, S2.persist_bad, S2.add_diff, S2.clut_bad,
           S2.refused_ok, S2.refused_n, S2.epoch_bad, S2.epoch_control_bad, S2.lost_ok, S2.lost_bad,
           S2.alpha_scene_bad, S2.alpha_control, S2.passes_bad);
    {
        const int ss = dev == PSYGFX_HL_SWIFTSHADER;
        CHECK_LE(S2.stroke_soft, ss ? 3e-4 : 5e-5);
        CHECK(S2.stroke_hard_bad == 0);
        CHECK(S2.stroke_hit_bad == 0);
        CHECK(S2.miter_bad == 0);
        CHECK_LE(S2.mask_gpu, ss ? 3e-4 : 5e-5);
        /* a binary mask places its boundary only to within half a texel's
         * diagonal (0.71), whatever the transform */
        CHECK_LE(S2.mask_sdf_cpu, 0.75);
        CHECK(S2.edt_bad == 0);
        CHECK(S2.sprite_bad == 0);
        CHECK(S2.bleed == 0);
        CHECK_LE(S2.sprite_lin, 2.5e-6);
        CHECK_LE(S2.tint, 2e-7);
        CHECK_LE(S2.group_diff, 2e-7);
        CHECK(S2.group_hit_bad == 0);
        CHECK(S2.target_diff == 0.0);
        CHECK(S2.target_bad == 0);
        CHECK(S2.persist_bad == 0);
        CHECK_LE(S2.add_diff, 8e-4);   /* the target holds RGBA16F */
        CHECK(S2.clut_bad == 0);
        CHECK(S2.refused_ok == S2.refused_n);
        CHECK(S2.epoch_bad == 0);
        CHECK(S2.alpha_scene_bad == 0);
        CHECK(S2.alpha_control > 0);
        CHECK(S2.passes_bad == 0);
#if PSYGFX__EPOCH
        CHECK(S2.epoch_control_bad > 0);
        CHECK(S2.lost_ok == 4);
        CHECK(S2.lost_bad == 0);
#endif
    }
    printf("  edge model against truth: 90 deg vertex, F(d) - Gaussian blur max %.4f (analytic 0.25); disc R/sigma 5 %.4f "
           "(analytic %.4f), 20 %.4f (%.4f); MITER field - F(Euclidean d) max %.4f (analytic %.4f)\n",
           S3.vtx_blur, S3.disc_blur[0], S3.disc_pred[0], S3.disc_blur[1], S3.disc_pred[1], S3.miter_bound, S3.miter_pred);
    {
        const double tol = dev == PSYGFX_HL_SWIFTSHADER ? 3e-4 : 5e-5;
        /* the pixel grid samples the vertex exactly; the disc and the
         * corner field only near their worst point */
        CHECK_LE(S3.vtx_blur, 0.25 + tol);
        CHECK(S3.vtx_blur >= 0.25 - tol);
        CHECK_LE(S3.disc_blur[0], S3.disc_pred[0] + tol);
        CHECK(S3.disc_blur[0] >= 0.9 * S3.disc_pred[0]);
        CHECK_LE(S3.disc_blur[1], S3.disc_pred[1] + tol);
        CHECK(S3.disc_blur[1] >= 0.9 * S3.disc_pred[1]);
        CHECK_LE(S3.miter_bound, S3.miter_pred + tol);
        CHECK(S3.miter_bound >= 0.5 * S3.miter_pred);
    }
    printf("  v0.3 kinds (%ld stimuli): soft max %.2e (%s), with dashes or trim %.2e, hard wrong %ld, hit wrong %ld (ties %ld)\n",
           S4.n_shapes, S4.soft, S4.worst, S4.dash_soft, S4.hard_bad, S4.hit_bad, S4.ties);
    {
        const int ss = dev == PSYGFX_HL_SWIFTSHADER;
        CHECK_LE(S4.soft, ss ? 3e-4 : 5e-5);
        CHECK_LE(S4.dash_soft, ss ? 1e-2 : 1.5e-3);
    }
    printf("  v0.3 truth: QBEZIER (chords) GPU - F(brute force) %.2e, distance error %.4f px\n", S5.t2_qb_cov, S5.t2_qb_dist);
    printf("  v0.3 truth: exact kinds, GPU - F(brute-force Euclidean d) max %.2e (%s), the CPU form's distance error %.2e px\n"
           "  v0.3 bounds: union crease %.4f (analytic %.4f), smooth union settled %.4f, connecting %.4f\n"
           "  v0.3 blur: EXACT_BLUR rect shadow - erf product %.2e; F(d) shadow - blur %.4f\n"
           "  v0.3 fx in one pass - CPU layers %.2e; blend %.2e; batched with extension blocks - single %.2e; "
           "dash offset by a period %.2e, dash count wrong %ld, trim wrong %ld; refusals %ld of %ld\n",
           S5.t2_cov, S5.t2_worst, S5.t2_dist, S5.t3_union, S5.t3_union_pred, S5.t3_smooth, S5.t3_connect,
           S5.t4_exact, S5.t4_fd, S5.fx, S5.blend, S5.batch, S5.dash_period, S5.dash_count_bad, S5.trim_bad,
           S5.refused_ok, S5.refused_n);
    {
        const int ss = dev == PSYGFX_HL_SWIFTSHADER;
        /* the exact kinds' brute force has its own sampling error: ellipses
         * and the Bezier as 8192 chords, 1e-5 px */
        CHECK_LE(S5.t2_cov, ss ? 3e-4 : 6e-5);
        CHECK_LE(S5.t2_dist, 1e-3);
        CHECK_LE(S5.t2_qb_dist, 0.0055);
        CHECK_LE(S5.t2_qb_cov, 2e-3);
        CHECK_LE(S5.t3_union, S5.t3_union_pred + (ss ? 3e-4 : 5e-5));
        CHECK(S5.t3_union >= 0.5 * S5.t3_union_pred);
        CHECK_LE(S5.t3_smooth, 0.03);
        CHECK_LE(S5.t3_connect, 0.2);
        CHECK(S5.t3_connect >= 0.03);
        CHECK_LE(S5.t4_exact, ss ? 3e-4 : 5e-5);
        CHECK(S5.t4_fd > 0.1);
        CHECK_LE(S5.fx, ss ? 3e-4 : 5e-5);
        CHECK_LE(S5.blend, 2e-7);
        CHECK(S5.batch == 0.0);
        CHECK_LE(S5.dash_period, ss ? 2e-2 : 2e-3);
        CHECK(S5.dash_count_bad == 0);
        CHECK(S5.trim_bad == 0);
        CHECK(S5.refused_ok == S5.refused_n);
    }
    printf("  v0.3 paint: RGB %.2e, OKLAB against Ottosson through the calibration %.2e, DKL_POLAR against "
           "psygfx_cal_dir_dkl %.2e, VERTEX against barycentric %.2e\n"
           "  v0.3 MSDF against the CPU's bilinear median %.2e; glyph run against single draws %.2e\n",
           S5.paint_rgb, S5.paint_oklab, S5.paint_dkl, S5.paint_vertex, S5.msdf, S5.glyph);
    {
        const int ss = dev == PSYGFX_HL_SWIFTSHADER;
        CHECK_LE(S5.paint_rgb, 2e-6);
        CHECK_LE(S5.paint_oklab, ss ? 1e-4 : 2e-5);
        CHECK_LE(S5.paint_dkl, ss ? 1e-3 : 1e-4);
        CHECK_LE(S5.paint_vertex, 2e-5);
        CHECK_LE(S5.msdf, ss ? 3e-4 : 5e-5);
        CHECK_LE(S5.glyph, 1e-5);
        CHECK(S4.hard_bad == 0);
        CHECK(S4.hit_bad == 0);
    }
    g_where = "cpu";
    return 1;
}

#if defined(__SANITIZE_ADDRESS__)
    #define SANITIZED 1
#elif defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define SANITIZED 1
    #endif
#endif
#ifndef SANITIZED
    #define SANITIZED 0
#endif

static int sanitized(void) { return SANITIZED; }   /* not a constant: C4127 */

static int wanted(const char* name) {
    const char* e = getenv("PSYGFX_TEST_DEVICES");
    /* Under ASan, Mesa leaves 112 bytes from a module it unloads, which a
     * suppression cannot name: GL runs only when asked for by name. */
    if (sanitized() && (!e || !e[0])) return 0;
    return !e || !e[0] || strstr(e, name) != NULL;
}

int main(void) {
    int ran = 0;
    const char* req = getenv("PSYGFX_TEST_REQUIRE_GL");
    test_noise_cpu();
    test_cal_srgb();
    test_cones_dkl();
    test_tables();
    test_coordinates();
    test_null_and_ring();
    test_v03_cpu();
    R = (rig*)calloc(1, sizeof *R);
    if (!R) return 1;
#if defined(_WIN32)
    if (wanted("hardware")) ran += gl_suite("ANGLE D3D11 hardware", PSYGFX_HL_HARDWARE);
    if (wanted("warp")) ran += gl_suite("ANGLE D3D11 WARP", PSYGFX_HL_WARP);
    if (wanted("swiftshader")) ran += gl_suite("ANGLE Vulkan SwiftShader", PSYGFX_HL_SWIFTSHADER);
#else
    if (wanted("mesa")) ran += gl_suite("Mesa llvmpipe", PSYGFX_HL_MESA_SOFTWARE);
#endif
    free(R);
    if (!ran) {
        printf("no GL ES 3.0 renderer opened%s: the GL checks did not run\n",
               sanitized() ? " (a sanitizer build runs GL only with PSYGFX_TEST_DEVICES)" : "");
        if (req && req[0] == '1') { fprintf(stderr, "psy_gfx_test: FAIL: PSYGFX_TEST_REQUIRE_GL=1 and no GL\n"); g_failures++; }
    }
    if (g_failures) { fprintf(stderr, "psy_gfx_test: %d failure(s)\n", g_failures); return 1; }
    printf("psy_gfx_test: all checks passed (%d renderer(s))\n", ran);
    return 0;
}
