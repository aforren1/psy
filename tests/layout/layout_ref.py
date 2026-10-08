#!/usr/bin/env python3
"""Chromium's glyphs for tests/layout/corpus.txt, into corpus_ref.txt.

    uv run --no-project --with playwright --with pypdf python tests/layout/layout_ref.py

The method of docs/layout_probe.md 2.1: Microsoft Edge (Playwright, channel
"msedge", the installed browser) prints a page that loads each corpus font
through @font-face, one item a page at 100 px per em with no wrapping. Skia's
PDF backend writes each glyph id as an Identity-H CID with CIDToGIDMap
/Identity and positions the glyphs with text-space moves and the font's /W
widths; a small content-stream interpreter reads every glyph id and position
back. Positions are written in font units, y down, relative to the first glyph
in visual order (by x, then y, then id), as layout_test.c compares them.
Each item records the SHA-256 of its font file: a different font version has
different glyph ids, and the test skips it. Edge trims the space of adjacent
CJK punctuation by default, so the page sets text-spacing-trim: space-all.
"""
import argparse
import datetime
import hashlib
import html
import pathlib
import struct
import sys
import tempfile

from playwright.sync_api import sync_playwright
from pypdf import PdfReader
from pypdf.generic import ContentStream

HERE = pathlib.Path(__file__).resolve().parent


def read_corpus(path):
    items = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        iid, file, face, direction, lang, text = line.split("|", 5)
        items.append(dict(id=iid, file=file, face=int(face), dir=direction, lang=lang, text=text))
    return items


def upem_of(data, face):
    off = 0
    if data[:4] == b"ttcf":
        off = struct.unpack(">I", data[12 + 4 * face:16 + 4 * face])[0]
    n = struct.unpack(">H", data[off + 4:off + 6])[0]
    for i in range(n):
        rec = off + 12 + 16 * i
        if data[rec:rec + 4] == b"head":
            t = struct.unpack(">I", data[rec + 8:rec + 12])[0]
            return struct.unpack(">H", data[t + 18:t + 20])[0]
    raise ValueError("no head table")


def page_html(items, fonts_dir):
    faces = {}
    css = []
    for it in items:
        key = it["file"]
        if key not in faces:
            if it["face"] != 0:
                sys.exit("@font-face cannot select face %d of %s" % (it["face"], key))
            faces[key] = "f%d" % len(faces)
            url = pathlib.Path(fonts_dir, key).resolve().as_uri()
            css.append("@font-face { font-family: %s; src: url('%s'); }" % (faces[key], url))
    css.append("@page { size: 4000px 400px; margin: 0; }")
    css.append("body { margin: 0; }")
    css.append("div { font-size: 100px; white-space: pre; padding: 100px 40px; "
               "text-spacing-trim: space-all; break-after: page; }")
    body = []
    for it in items:
        direction = "" if it["dir"] == "auto" else ' dir="%s"' % it["dir"]
        body.append('<div lang="%s"%s style="font-family: %s">%s</div>' % (
            it["lang"], direction, faces[it["file"]], html.escape(it["text"])))
    return ("<!doctype html><html><head><meta charset='utf-8'><style>%s</style></head><body>%s</body></html>"
            % ("\n".join(css), "\n".join(body)))


def mul(a, b):
    """Affine matrices as (a, b, c, d, e, f): a, then b."""
    return (a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],
            a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
            a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5])


def font_widths(font):
    desc = font["/DescendantFonts"][0].get_object()
    dw = float(desc.get("/DW", 1000))
    w = {}
    arr = desc.get("/W", [])
    i = 0
    while i < len(arr):
        first = int(arr[i])
        nxt = arr[i + 1]
        if isinstance(nxt, list):
            for k, v in enumerate(nxt):
                w[first + k] = float(v)
            i += 2
        else:
            for g in range(first, int(nxt) + 1):
                w[g] = float(arr[i + 2])
            i += 3
    return w, dw


def raw_bytes(s):
    if hasattr(s, "original_bytes"):
        return s.original_bytes
    return bytes(s)


def glyphs_of_page(page, reader):
    """[(gid, x, y, scale_x, scale_y)] in user space for every glyph shown."""
    res = page["/Resources"]
    fonts = {k: v.get_object() for k, v in res.get("/Font", {}).items()}
    cs = ContentStream(page.get_contents(), reader)
    ctm = (1, 0, 0, 1, 0, 0)
    stack = []
    out = []
    tm = tlm = (1, 0, 0, 1, 0, 0)
    tfs, tc, th, trise, tl = 1.0, 0.0, 1.0, 0.0, 0.0
    widths = None
    for operands, op in cs.operations:
        op = op.decode() if isinstance(op, bytes) else op
        if op == "q":
            stack.append(ctm)
        elif op == "Q":
            ctm = stack.pop()
        elif op == "cm":
            ctm = mul(tuple(float(v) for v in operands), ctm)
        elif op == "BT":
            tm = tlm = (1, 0, 0, 1, 0, 0)
        elif op == "Tf":
            f = fonts[operands[0]]
            if f.get("/Subtype") != "/Type0" or f.get("/Encoding") != "/Identity-H":
                sys.exit("unexpected font %s %s" % (f.get("/Subtype"), f.get("/Encoding")))
            widths = font_widths(f)
            tfs = float(operands[1])
        elif op == "Tc":
            tc = float(operands[0])
        elif op == "Tz":
            th = float(operands[0]) / 100
        elif op == "Ts":
            trise = float(operands[0])
        elif op == "TL":
            tl = float(operands[0])
        elif op in ("Td", "TD"):
            tlm = mul((1, 0, 0, 1, float(operands[0]), float(operands[1])), tlm)
            tm = tlm
            if op == "TD":
                tl = -float(operands[1])
        elif op == "Tm":
            tlm = tm = tuple(float(v) for v in operands)
        elif op == "T*":
            tlm = mul((1, 0, 0, 1, 0, -tl), tlm)
            tm = tlm
        elif op in ("Tj", "TJ"):
            parts = [operands[0]] if op == "Tj" else operands[0]
            for part in parts:
                if isinstance(part, (int, float)) or type(part).__name__ in ("FloatObject", "NumberObject"):
                    tm = mul((1, 0, 0, 1, -float(part) / 1000 * tfs * th, 0), tm)
                    continue
                b = raw_bytes(part)
                for k in range(0, len(b), 2):
                    gid = b[k] << 8 | b[k + 1]
                    m = mul((tfs * th, 0, 0, tfs, 0, trise), mul(tm, ctm))
                    out.append((gid, m[4], m[5], (m[0] ** 2 + m[1] ** 2) ** 0.5, (m[2] ** 2 + m[3] ** 2) ** 0.5))
                    w = widths[0].get(gid, widths[1])
                    tm = mul((1, 0, 0, 1, (w / 1000 * tfs + tc) * th, 0), tm)
        elif op in ("'", '"'):
            sys.exit("text operator %s not handled" % op)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts", default="C:/Windows/Fonts")
    ap.add_argument("--corpus", default=str(HERE / "corpus.txt"))
    ap.add_argument("--out", default=str(HERE / "corpus_ref.txt"))
    a = ap.parse_args()
    items = read_corpus(pathlib.Path(a.corpus))
    with tempfile.TemporaryDirectory() as tmp:
        page = pathlib.Path(tmp, "corpus.html")
        page.write_text(page_html(items, a.fonts), encoding="utf-8")
        pdf = pathlib.Path(tmp, "corpus.pdf")
        with sync_playwright() as p:
            browser = p.chromium.launch(channel="msedge")
            version = browser.version
            pg = browser.new_page()
            pg.goto(page.as_uri())
            pg.evaluate("document.fonts.ready")
            pg.pdf(path=str(pdf), prefer_css_page_size=True, print_background=False)
            browser.close()
        reader = PdfReader(str(pdf))
        if len(reader.pages) != len(items):
            sys.exit("%d pages for %d items" % (len(reader.pages), len(items)))
        lines = ["# Chromium's glyphs for corpus.txt: Microsoft Edge %s, %s, layout_ref.py." % (
                     version, datetime.date.today().isoformat()),
                 "# item <id> <font file> <face> <font sha256> <glyphs> then gid:x:y per glyph in visual",
                 "# order (x, then y, then id), font units, y down, from the first glyph."]
        for it, pdfpage in zip(items, reader.pages):
            data = pathlib.Path(a.fonts, it["file"]).read_bytes()
            sha = hashlib.sha256(data).hexdigest()
            upem = upem_of(data, it["face"])
            gl = glyphs_of_page(pdfpage, reader)
            gl = [(g, x / sx * upem, -y / sy * upem) for g, x, y, sx, sy in gl]
            gl.sort(key=lambda t: (round(t[1], 3), round(t[2], 3), t[0]))
            x0, y0 = gl[0][1], gl[0][2]
            cells = " ".join("%d:%.2f:%.2f" % (g, x - x0, y - y0) for g, x, y in gl)
            lines.append("item %s %s %d %s %d %s" % (it["id"], it["file"], it["face"], sha, len(gl), cells))
            print("%-18s %3d glyphs" % (it["id"], len(gl)))
    pathlib.Path(a.out).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print("wrote", a.out, "Edge", version)


if __name__ == "__main__":
    main()
