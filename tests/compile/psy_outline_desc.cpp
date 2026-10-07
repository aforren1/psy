/* Compile check: psy_outline.h's desc structs set by designated
 * initializers, as the manual's USAGE shows them, in C++20 (the first C++
 * with them, and only in declaration order). CMake builds this one target as
 * C++20, and with -Wno-missing-field-initializers on g++, which warns about
 * every field a designated initializer leaves out even though zero is the
 * default. Built as an earlier C++, it compiles to an empty program. */
#define PSY_OUTLINE_IMPLEMENTATION
#include "psy_outline.h"

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
int main() {
    psyol_ctx cx;
    psyol_path p, o;
    psyol_box b;
    psyol_cset s;
    int ok = psyol_init(&cx, nullptr) == PSYOL_OK;
    psyol_path_init(&p, &cx);
    psyol_path_init(&o, &cx);
    psyol_rect(&p, 0, 0, 1, 1, 0.1, 0.1);
    ok = ok && psyol_path_end(&p) == PSYOL_OK;
    psyol_stroke_desc sd = { .width = 0.1, .join = PSYOL_JOIN_MITER, .mode = PSYOL_BOLD };
    ok = ok && psyol_stroke(&cx, &p, &o, &sd) == PSYOL_OK;
    psyol_raster_desc rd = { .scale = 16, .x = 1, .y = 1 };
    ok = ok && psyol_raster(&cx, &o, &rd, &b) == PSYOL_OK && b.x1 > b.x0;
    psyol_cset_desc cd = { .n_glyphs = 1, .nh = 2, .nv = 2 };
    ok = ok && psyol_cset_init(&s, &cx, &cd) == PSYOL_OK && psyol_cset_add(&s, 0, &o) == PSYOL_OK;
    psyol_glyph_desc gd = { .size = 24, .y_up = true };
    psyol_svg_desc svd = { .tol = 0.01, .max_layers = 0 };
    (void)gd; (void)svd;
    psyol_cset_free(&s);
    psyol_path_free(&p);
    psyol_path_free(&o);
    psyol_free(&cx);
    return ok ? 0 : 1;
}
#else
int main() { return 0; }
#endif
