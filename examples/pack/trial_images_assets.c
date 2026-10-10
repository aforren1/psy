/* trial_images_assets.c - the source files of trial_images' pack: PNG
 * images made here, the committed conditions CSV, and the source
 * description for `ypak build` (docs/pack.md 5.2).
 *
 * The build runs it, then ypak, so the repository holds no binary image:
 * every pixel is in this file, reviewable as text, and the same on every
 * platform (integer arithmetic and one table of the 5 x 7 font). The PNGs
 * use stored (uncompressed) deflate blocks, as tests/pack/tool_test.c's do:
 * lodepng in the pack tool decodes them like any PNG.
 *
 *   img/navon_XY.png       a Navon figure (Navon 1977): global letter X
 *                          made of local letters Y, for X, Y in H, S;
 *                          8-bit gray, 60 x 112
 *   img/letter_R.png       R for mental rotation (Shepard and Metzler
 *   img/letter_R_mirror.png  1971), and its mirror image; 8-bit gray,
 *                          112 x 112
 *   img/mask.png           a graded mask: a smooth disc window times a
 *                          ramp from 0.3 at the left to 1 at the right;
 *                          RGBA 8-bit, 128 x 128, gray in rgb, opaque
 *   conditions.csv         copied from the path given
 *   trial_images.json      the source description; every texture
 *                          "linear": the values are coverage and gain, not
 *                          sRGB photographs
 *
 * Usage: trial_images_assets OUTDIR CONDITIONS.csv
 * Exit code: 0, 1 when a file could not be written, 2 for a bad argument.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* H, S and R of gfx_gallery.c's 5 x 7 font, rows top first, bit 4 the left column. */
static const uint8_t glyph_H[7] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
static const uint8_t glyph_S[7] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
static const uint8_t glyph_R[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };

static int failed;
static char outdir[1024];

static uint32_t crc32_of(uint32_t crc, const uint8_t* d, size_t n) {
    size_t i;
    int k;
    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc ^= d[i];
        for (k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void put32(FILE* fp, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    fwrite(b, 1, 4, fp);
}

static void chunk(FILE* fp, const char* type, const uint8_t* d, size_t n) {
    put32(fp, (uint32_t)n);
    fwrite(type, 1, 4, fp);
    if (n) fwrite(d, 1, n, fp);
    put32(fp, crc32_of(crc32_of(0, (const uint8_t*)type, 4), d, n));
}

static FILE* open_out(const char* name) {
    char path[1200];
    FILE* fp;
    snprintf(path, sizeof path, "%s/%s", outdir, name);
    fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "trial_images_assets: cannot write %s\n", path); failed = 1; }
    return fp;
}

/* An 8-bit PNG of color type ct (0 gray, 6 RGBA) from tight rows. */
static void png(const char* name, int w, int h, int ct, const uint8_t* pix) {
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    int chans = ct == 6 ? 4 : 1, y;
    size_t rb = (size_t)w * (size_t)chans, raw_n = (rb + 1) * (size_t)h, at = 0, zn = 0, i;
    uint8_t *raw = (uint8_t*)malloc(raw_n), *z = (uint8_t*)malloc(raw_n + raw_n / 65535 * 5 + 16), ih[13];
    uint32_t a = 1, b = 0;
    FILE* fp = open_out(name);
    if (!fp || !raw || !z) { free(raw); free(z); if (fp) fclose(fp); failed = 1; return; }
    for (y = 0; y < h; y++) { raw[(size_t)y * (rb + 1)] = 0; memcpy(raw + (size_t)y * (rb + 1) + 1, pix + (size_t)y * rb, rb); }
    for (i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    z[zn++] = 0x78; z[zn++] = 0x01;
    while (at < raw_n) {   /* stored blocks of at most 65535 bytes */
        size_t k = raw_n - at > 65535 ? 65535 : raw_n - at;
        z[zn++] = at + k == raw_n ? 1 : 0;
        z[zn++] = (uint8_t)k; z[zn++] = (uint8_t)(k >> 8);
        z[zn++] = (uint8_t)~k; z[zn++] = (uint8_t)(~k >> 8);
        memcpy(z + zn, raw + at, k);
        zn += k; at += k;
    }
    z[zn++] = (uint8_t)(b >> 8); z[zn++] = (uint8_t)b; z[zn++] = (uint8_t)(a >> 8); z[zn++] = (uint8_t)a;
    fwrite(sig, 1, 8, fp);
    ih[0] = 0; ih[1] = 0; ih[2] = (uint8_t)(w >> 8); ih[3] = (uint8_t)w;
    ih[4] = 0; ih[5] = 0; ih[6] = (uint8_t)(h >> 8); ih[7] = (uint8_t)h;
    ih[8] = 8; ih[9] = (uint8_t)ct; ih[10] = ih[11] = ih[12] = 0;
    chunk(fp, "IHDR", ih, 13);
    chunk(fp, "IDAT", z, zn);
    chunk(fp, "IEND", NULL, 0);
    if (fclose(fp) != 0) failed = 1;
    free(raw); free(z);
}

static int on(const uint8_t* g, int col, int row) { return (g[row] >> (4 - col)) & 1; }

/* A global letter of 5 x 7 cells, each a local letter of 5 x 7 px drawn 2x
 * with a 1 px margin: 60 x 112. */
static void navon(const char* name, const uint8_t* global, const uint8_t* local) {
    static uint8_t pix[60 * 112];
    int gr, gc, r, c;
    memset(pix, 0, sizeof pix);
    for (gr = 0; gr < 7; gr++)
        for (gc = 0; gc < 5; gc++)
            if (on(global, gc, gr))
                for (r = 0; r < 14; r++)
                    for (c = 0; c < 10; c++)
                        if (on(local, c / 2, r / 2)) pix[(gr * 16 + 1 + r) * 60 + gc * 12 + 1 + c] = 255;
    png(name, 60, 112, 0, pix);
}

/* R at 16 px per font pixel, centered in 112 x 112, or its mirror image. */
static void letter(const char* name, int mirror) {
    static uint8_t pix[112 * 112];
    int x, y;
    for (y = 0; y < 112; y++)
        for (x = 0; x < 112; x++) {
            int fx = (x - 16) / 16, fy = y / 16;
            if (mirror) fx = 4 - fx;
            pix[y * 112 + x] = (uint8_t)(x >= 16 && x < 96 && on(glyph_R, fx, fy) ? 255 : 0);
        }
    png(name, 112, 112, 0, pix);
}

/* m = (1 - (r / 64)^2)^2 inside r < 64, times 0.3 + 0.7 x / 127. The
 * window is in r squared, with no sqrt or cos: IEEE basic operations only,
 * so every C runtime gives the same bytes (docs/pack.md 7). */
static void mask(void) {
    static uint8_t pix[128 * 128 * 4];
    int x, y;
    for (y = 0; y < 128; y++)
        for (x = 0; x < 128; x++) {
            double dx = x - 63.5, dy = y - 63.5, r = dx * dx + dy * dy, m = 0;
            if (r < 64.0 * 64.0) {
                double t = r / (64.0 * 64.0);
                m = (1 - t) * (1 - t);
            }
            m *= 0.3 + 0.7 * x / 127.0;
            pix[(y * 128 + x) * 4 + 0] = pix[(y * 128 + x) * 4 + 1] = pix[(y * 128 + x) * 4 + 2] = (uint8_t)(m * 255 + 0.5);
            pix[(y * 128 + x) * 4 + 3] = 255;
        }
    png("img/mask.png", 128, 128, 6, pix);
}

int main(int argc, char** argv) {
    static const char* const json =
        "{\n"
        "  \"format\": \"ysp-pack-source\",\n"
        "  \"version\": 1,\n"
        "  \"resources\": [\n"
        "    {\"kind\": \"table\", \"source\": \"conditions.csv\", \"types\": {\"ori\": \"number\"}},\n"
        "    {\"kind\": \"texture\", \"source\": \"img/navon_HH.png\", \"encoding\": \"linear\"},\n"
        "    {\"kind\": \"texture\", \"source\": \"img/navon_HS.png\", \"encoding\": \"linear\"},\n"
        "    {\"kind\": \"texture\", \"source\": \"img/navon_SH.png\", \"encoding\": \"linear\"},\n"
        "    {\"kind\": \"texture\", \"source\": \"img/navon_SS.png\", \"encoding\": \"linear\"},\n"
        "    {\"kind\": \"texture\", \"source\": \"img/letter_R.png\", \"encoding\": \"linear\"},\n"
        "    {\"kind\": \"texture\", \"source\": \"img/letter_R_mirror.png\", \"encoding\": \"linear\"},\n"
        "    {\"kind\": \"texture\", \"source\": \"img/mask.png\", \"encoding\": \"linear\"}\n"
        "  ]\n"
        "}\n";
    char buf[4096], path[1200];
    size_t n;
    FILE *in, *fp;
    if (argc != 3) { fprintf(stderr, "usage: trial_images_assets OUTDIR CONDITIONS.csv\n"); return 2; }
    snprintf(outdir, sizeof outdir, "%s", argv[1]);
    navon("img/navon_HH.png", glyph_H, glyph_H);
    navon("img/navon_HS.png", glyph_H, glyph_S);
    navon("img/navon_SH.png", glyph_S, glyph_H);
    navon("img/navon_SS.png", glyph_S, glyph_S);
    letter("img/letter_R.png", 0);
    letter("img/letter_R_mirror.png", 1);
    mask();
    in = fopen(argv[2], "rb");
    fp = open_out("conditions.csv");
    if (!in) { fprintf(stderr, "trial_images_assets: cannot read %s\n", argv[2]); failed = 1; }
    while (in && fp && (n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, fp);
    if (in) fclose(in);
    if (fp && fclose(fp) != 0) failed = 1;
    if ((fp = open_out("trial_images.json")) != NULL) {
        fputs(json, fp);
        if (fclose(fp) != 0) failed = 1;
    }
    snprintf(path, sizeof path, "%s/trial_images.json", outdir);
    if (!failed) printf("trial_images_assets: wrote %s and its 7 images\n", path);
    return failed;
}
