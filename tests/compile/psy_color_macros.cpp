/* Compile check: psy_color.h's value macros (PSYCOL_DKL(...) and the rest)
 * as C++20. They are designated initializers in a functional cast, which
 * C++ has from C++20 on, and only in declaration order. CMake builds this
 * one target as C++20, and with -Wno-missing-field-initializers on g++,
 * which warns about every field a designated initializer leaves out even
 * though zero is the default. Built as an earlier C++, it compiles to an
 * empty program. */
#define PSY_COLOR_IMPLEMENTATION
#include "psy_color.h"

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
int main() {
    psycol_color c[] = {
        PSYCOL_DKL(.elev = 0, .azim = 90, .contrast = 0.1),
        PSYCOL_DKL_CART(.lm = 0.05),
        PSYCOL_RGB(0.5, 0.5, 0.5),
        PSYCOL_OKLCH(.L = 0.7, .C = 0.1, .h = 30),
        PSYCOL_CIELAB(.L = 50),
        PSYCOL_CONE(.l = 0.1, .m = -0.1),
        PSYCOL_SRGB(1, 0.5, 0),
        PSYCOL_MB(.l = 0.7, .s = 0.02, .lum = 1),
    };
    return c[0].space == PSYCOL_SPACE_DKL && c[0].u.dkl.azim == 90 && c[1].u.dkl_cart.lm == 0.05 &&
           c[3].u.lch.h == 30 && c[7].u.mb.lum == 1 ? 0 : 1;
}
#else
int main() { return 0; }
#endif
