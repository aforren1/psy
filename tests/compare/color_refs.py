"""Reference values for tests/adapt/psy_color_test.c from colour-science.

    uv run --with colour-science --with numpy python tests/compare/color_refs.py

Prints C arrays. The test holds a copy with the colour-science version it
came from; run this again and paste when the version changes. The sRGB,
Display P3 and Rec. 2020 matrices are derived from their primaries and
white (colour's derived matrices), not the 4-digit printed sRGB matrix.
"""
import warnings

warnings.filterwarnings("ignore")
import numpy as np  # noqa: E402
import colour  # noqa: E402

D65 = colour.CCS_ILLUMINANTS["CIE 1931 2 Degree Standard Observer"]["D65"]
W65 = colour.xy_to_XYZ(D65)


def space(name):
    s = colour.RGB_COLOURSPACES[name].copy()
    s.use_derived_transformation_matrices(True)
    return s


SRGB, P3, R2020 = space("sRGB"), space("Display P3"), space("ITU-R BT.2020")

# colour's BT.2020 decoding rounds alpha and beta to the 10-bit values
# (1.099, 0.018); psy_color.h, like CSS Color 4, takes the exact ones.
B2020_A, B2020_B = 1.09929682680944, 0.018053968510807


def decode_2020(e):
    return np.where(e < 4.5 * B2020_B, e / 4.5, ((e + B2020_A - 1) / B2020_A) ** (1 / 0.45))


def g(v):
    return ", ".join("%.17g" % x for x in v)


HEX = ["#3366cc", "#ff8800", "#808080", "#000000", "#ffffff", "#ff0000", "#00ff00",
       "#0000ff", "#123456", "#fedcba", "#00ffff", "#7f7f80"]
print("/* colour-science %s: hex, then sRGB values, XYZ (D65, white Y = 1), xyY," % colour.__version__)
print(" * CIELAB, CIELCh, CIELUV, CIELChuv (white D65), Oklab, OkLCh */")
print("static const struct ref_srgb { const char* hex; double srgb[3], xyz[3], xyy[3], lab[3], lch[3], luv[3], lchuv[3], ok[3], oklch[3]; } ref_srgb[] = {")
for h in HEX:
    e = colour.notation.HEX_to_RGB(h)
    xyz = SRGB.matrix_RGB_to_XYZ @ SRGB.cctf_decoding(e)
    lab = colour.XYZ_to_Lab(xyz, D65)
    luv = colour.XYZ_to_Luv(xyz, D65)
    ok = colour.XYZ_to_Oklab(xyz)
    oklch = colour.models.Oklab_to_Oklch(ok)
    xyy = colour.XYZ_to_xyY(xyz) if xyz.sum() > 0 else np.array([D65[0], D65[1], 0.0])
    print('    { "%s", { %s }, { %s }, { %s }, { %s }, { %s }, { %s }, { %s }, { %s }, { %s } },' % (
        h, g(e), g(xyz), g(xyy), g(lab), g(colour.Lab_to_LCHab(lab)), g(luv), g(colour.Luv_to_LCHuv(luv)), g(ok), g(oklch)))
print("};")

ENC = [[0.2, 0.5, 0.9], [1.0, 0.0, 0.0], [0.05, 0.01, 0.6], [0.7, 0.7, 0.7]]
print("/* Display P3 and Rec. 2020 (exact alpha and beta) encoded values to XYZ (D65, white Y = 1) */")
print("static const struct ref_wide { double enc[3], p3[3], r2020[3]; } ref_wide[] = {")
for e in ENC:
    e = np.array(e)
    p3 = P3.matrix_RGB_to_XYZ @ P3.cctf_decoding(e)
    r = R2020.matrix_RGB_to_XYZ @ decode_2020(e)
    print("    { { %s }, { %s }, { %s } }," % (g(e), g(p3), g(r)))
print("};")

W2 = colour.xy_to_XYZ(np.array([0.300, 0.320]))
M = colour.adaptation.matrix_chromatic_adaptation_VonKries(W65, W2, transform="Bradford")
print("/* Bradford, D65 to xy (0.300, 0.320): XYZ of #3366cc adapted */")
xyz = SRGB.matrix_RGB_to_XYZ @ SRGB.cctf_decoding(colour.notation.HEX_to_RGB("#3366cc"))
print("static const double ref_white2[3] = { %s };" % g(W2))
print("static const double ref_bradford_3366cc[3] = { %s };" % g(M @ xyz))

L = colour.colorimetry.MSDS_CMFS_LMS["Stockman & Sharpe 10 Degree Cone Fundamentals"]
print("/* Stockman & Sharpe 10 degree rows (CVRL linss10e_1): 440, 570 nm */")
print("static const double ref_ss10_440[3] = { %s }, ref_ss10_570[3] = { %s };" % (g(L[440]), g(L[570])))

print("/* CIELAB near the CIE's epsilon (216/24389 = 0.008856): a D65 gray at Y = 0.0089 and 0.0088 */")
for y in (0.0089, 0.0088):
    print("static const double ref_dark_%d[6] = { %s, %s };" % (int(y * 10000), g(W65 * y), g(colour.XYZ_to_Lab(W65 * y, D65))))
