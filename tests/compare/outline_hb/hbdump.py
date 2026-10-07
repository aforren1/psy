# /// script
# requires-python = ">=3.9"
# dependencies = ["uharfbuzz==0.56.3"]
# ///
"""Writes HarfBuzz's outline of every glyph of one font face as text, in
font units with y up, for hbcmp.c. uharfbuzz 0.56.3 bundles HarfBuzz 14.6.0.

    uv run tests/compare/outline_hb/hbdump.py FONT OUT.txt [FACE]
"""
import sys

import uharfbuzz as hb


class Pen:
    def __init__(self, out):
        self.o = out

    def moveTo(self, p):
        self.o.append("M %r %r" % p)

    def lineTo(self, p):
        self.o.append("L %r %r" % p)

    def qCurveTo(self, c, p):
        self.o.append("Q %r %r %r %r" % (c + p))

    def curveTo(self, a, b, p):
        self.o.append("C %r %r %r %r %r %r" % (a + b + p))

    def closePath(self):
        self.o.append("Z")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    path, out = sys.argv[1], sys.argv[2]
    index = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    face = hb.Face(hb.Blob.from_file_path(path), index)
    font = hb.Font(face)  # scale = units per em, no hinting
    lines = ["U %d %d" % (face.upem, face.glyph_count)]
    for g in range(face.glyph_count):
        lines.append("G %d" % g)
        font.draw_glyph_with_pen(g, Pen(lines))
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\nE\n")
    print("%s face %d: %d glyphs, HarfBuzz %s" % (path, index, face.glyph_count, hb.version_string()))


main()
