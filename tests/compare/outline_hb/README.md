# ysp/outline.h against HarfBuzz

A local check, not a CI job. It needs a font file and uharfbuzz, and the
fonts it is meant for are system fonts that CI machines do not have.

## What it checks

For every glyph of one font face, it compares two outlines bit for bit:

- `yol_font_glyph()`, ysp/outline.h's own reader;
- HarfBuzz's `hb_font_draw_glyph()` at the font's units per em (HarfBuzz
  14.6.0, from uharfbuzz 0.56.3), put through ysp/outline.h's path
  builder in em units with y down.

It also builds two curve sets, one with `yol_cset_add_font()` and one
with `yol_cset_add()` from HarfBuzz's outlines, and compares their words
and texels.

Glyph positions come from HarfBuzz (through Skribidi), so the outlines must
sit where HarfBuzz puts them. This check found the placement rule that
ysp/outline.h v0.2.1 adopted (docs/outline.md, "Placement against
HarfBuzz").

Contours are compared as cycles of segments, so a contour can start at a
different point. Zero-length segments and one-point contours are skipped:
HarfBuzz passes them on, ysp/outline.h drops them, and neither has an area.

## How to run it

From the repository root, with any font file and an optional face index
for a collection (`.ttc`):

```sh
uv run tests/compare/outline_hb/hbdump.py FONT dump.txt [FACE]
gcc -std=c11 -O2 -Iinclude -o hbcmp tests/compare/outline_hb/hbcmp.c -lm
./hbcmp FONT dump.txt [FACE [GLYPH]]
```

With MSVC: `cl /nologo /O2 /Iinclude tests\compare\outline_hb\hbcmp.c`.

`hbdump.py` pins uharfbuzz in its inline script header, so `uv run`
needs no project. Give the same face index to both programs. GLYPH prints
both outlines of that glyph in font units.

## Output

```text
C:/Windows/Fonts/arial.ttf face 0: 4651 glyphs, 4651 equal bit for bit, 0 differ, largest difference 0 em
curve sets: equal (590432 and 590432 words, 127874 and 127874 texels)
```

The exit code is 0 when every glyph and both sets are equal, 1 when they
are not (the first 10 differing glyphs are listed), and 2 on a usage or
file error.

A variable font is compared at its default instance. ysp/outline.h
refuses CFF2 outlines, so a CFF2 font stops with exit code 2.
