#!/usr/bin/env python3
"""Edge's wrapping, selection rectangles and carets for editor_items.txt.

    uv run --no-project --with playwright python tests/layout/editor_ref.py

Writes tests/layout/editor_ref.txt, which psy_layout_test.c compares with
Skribidi (docs/psy_layout.md, "Editing against Edge"). Microsoft Edge (the
installed browser, Playwright channel "msedge") lays each item out in a div
that loads the item's font files through @font-face, at 100 px per em, with
white-space: pre-wrap (a text box keeps its spaces). Two models:

  layout  a plain div, overflow-wrap: normal: psylay_layout() (Skribidi's
          SKB_WRAP_WORD), which breaks only at break opportunities;
  editor  a contenteditable div, overflow-wrap: break-word, the model of a
          textarea: Skribidi's editor (SKB_WRAP_WORD_CHAR), which breaks
          inside a word only when the word does not fit a line. A textarea
          itself gives no Range rectangles, so the div stands in for it.

Per item, 10 widths from the widest word to the whole line. For each width
and model: the logical offset where each line starts, and each line's words
in visual order (the run order). For user_bidi and the corpus bidi lines on
one line: the selection rectangles of Range.getClientRects() for every
logical range [s, e) of user_bidi and for every one-codepoint range of the
others (merged into x intervals per line), and the caret of a collapsed
Range at every offset. Positions are font units, x from the box's left edge.
Every item is BMP-only, so a UTF-16 offset is a codepoint offset.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import struct
import sys
import tempfile

from playwright.sync_api import sync_playwright

HERE = pathlib.Path(__file__).resolve().parent
SIZE = 100.0   # px per em
WIDTHS = 10

JS = r"""
([items, models]) => {
  const out = {};
  const box = document.getElementById('box');
  function setup(it, model, width) {
    box.removeAttribute('contenteditable');
    if (model === 'editor') box.setAttribute('contenteditable', 'true');
    box.style.fontFamily = it.families;
    box.style.overflowWrap = model === 'editor' ? 'break-word' : 'normal';
    box.style.whiteSpace = width > 0 ? 'pre-wrap' : 'pre';
    box.style.width = width > 0 ? width + 'px' : 'max-content';
    box.dir = it.dir; box.lang = it.lang;
    box.textContent = it.text;
    return box.firstChild;
  }
  function rects(node, s, e) {
    const r = document.createRange();
    r.setStart(node, s); r.setEnd(node, e);
    return Array.from(r.getClientRects()).map(q => [q.left, q.right, q.top, q.bottom]);
  }
  function chars(node, n) {
    const res = [];
    for (let i = 0; i < n; i++) res.push(rects(node, i, i + 1)[0] || null);
    return res;
  }
  for (const it of items) {
    const n = it.text.length;
    const o = { widths: [], wrap: {}, sel: [], caret: [] };
    // the whole line and the widest word, unwrapped
    let node = setup(it, 'layout', 0);
    const left0 = box.getBoundingClientRect().left;
    const full = box.getBoundingClientRect().width;
    let widest = 0, s = 0;
    for (let i = 0; i <= n; i++) {
      if (i === n || it.text[i] === ' ') {
        if (i > s) {
          const rs = rects(node, s, i);
          let lo = 1e9, hi = -1e9;
          for (const q of rs) { lo = Math.min(lo, q[0]); hi = Math.max(hi, q[1]); }
          widest = Math.max(widest, hi - lo);
        }
        s = i + 1;
      }
    }
    for (let j = 0; j < %(widths)d; j++) o.widths.push(Math.round((widest + (full - widest) * j / %(last)d) * 100) / 100 + 0.5);
    for (const model of models) {
      o.wrap[model] = [];
      for (const w of o.widths) {
        node = setup(it, model, w);
        const left = box.getBoundingClientRect().left;
        o.wrap[model].push(chars(node, n).map(q => q ? [q[0] - left, q[1] - left, (q[2] + q[3]) / 2] : null));
      }
    }
    // selections and carets on one line, in the editor model
    node = setup(it, 'editor', 0);
    const left = box.getBoundingClientRect().left;
    const pairs = [];
    if (it.all_pairs) { for (let a = 0; a < n; a++) for (let b = a + 1; b <= n; b++) pairs.push([a, b]); }
    else { for (let a = 0; a < n; a++) pairs.push([a, a + 1]); }
    for (const [a, b] of pairs) o.sel.push([a, b, rects(node, a, b).map(q => [q[0] - left, q[1] - left, (q[2] + q[3]) / 2])]);
    for (let k = 0; k <= n; k++) {
      const r = document.createRange();
      r.setStart(node, k); r.collapse(true);
      const q = r.getClientRects()[0] || r.getBoundingClientRect();
      o.caret.push(q ? q.left - left : null);
    }
    // the same on wrapped lines, where rule L1 moves line-end spaces
    o.wrapped = [];
    if (it.all_pairs) {
      for (const wi of [1, 4]) {
        const w = o.widths[wi];
        node = setup(it, 'editor', w);
        const lft = box.getBoundingClientRect().left;
        const ws = { width: w, sel: [], caret: [] };
        for (const [a, b] of pairs) ws.sel.push([a, b, rects(node, a, b).map(q => [q[0] - lft, q[1] - lft, (q[2] + q[3]) / 2])]);
        for (let k = 0; k <= n; k++) {
          const r = document.createRange();
          r.setStart(node, k); r.collapse(true);
          const q = r.getClientRects()[0] || r.getBoundingClientRect();
          ws.caret.push(q ? [q.left - lft, (q.top + q.bottom) / 2] : null);
        }
        o.wrapped.push([wi, ws]);
      }
    }
    out[it.id] = o;
  }
  return out;
}
""" % {"widths": WIDTHS, "last": WIDTHS - 1}


def upem_of(data, face=0):
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


def read_items(path):
    items = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        iid, fonts, d, lang, text = line.split("|", 4)
        if any(ord(c) > 0xffff for c in text):
            sys.exit("%s: not BMP-only" % iid)
        items.append(dict(id=iid, fonts=fonts.split(","), dir=d, lang=lang, text=text))
    return items


def lines_of(chars):
    """Line index per codepoint from rect centers: a new line where the
    center moves down by more than a third of an em."""
    centers = sorted({round(c[2], 1) for c in chars if c})
    rows = []
    for y in centers:
        if not rows or y - rows[-1] > SIZE / 3:
            rows.append(y)
    def row(y):
        return min(range(len(rows)), key=lambda k: abs(rows[k] - y))
    return [row(c[2]) if c else -1 for c in chars]


def is_mark(c):
    """The combining marks of the items (Hebrew points, Arabic harakat): a
    Range over one alone measures its base, so ink extents skip them."""
    o = ord(c)
    return 0x0591 <= o <= 0x05C7 or 0x064B <= o <= 0x065F or o == 0x0670


def wrap_summary(text, chars, scale):
    """Line starts; per line, the words (logical index) in visual order; per
    line, the ink extent (the leftmost and rightmost edge of its characters
    other than spaces and marks), font units."""
    li = lines_of(chars)
    starts, prev = [], -1
    for i, l in enumerate(li):
        if l > prev:
            starts.append(i); prev = l
    words, s = [], 0
    for i in range(len(text) + 1):
        if i == len(text) or text[i] == " ":
            if i > s:
                words.append((s, i))
            s = i + 1
    per_line = {}
    for w, (a, b) in enumerate(words):
        xs = [chars[k][0] for k in range(a, b) if chars[k]]
        per_line.setdefault(li[a], []).append((min(xs), w))
    order = "|".join(",".join(str(w) for _, w in sorted(per_line[k])) for k in sorted(per_line))
    ink = {}
    for i, c in enumerate(text):
        if c == " " or is_mark(c) or not chars[i]:
            continue
        lo, hi = ink.get(li[i], (1e30, -1e30))
        ink[li[i]] = (min(lo, chars[i][0]), max(hi, chars[i][1]))
    inks = "|".join("%.1f:%.1f" % (ink[k][0] * scale, ink[k][1] * scale) for k in sorted(ink))
    return starts, order, inks


def row_centers(chars):
    """The center height of each line of a laid-out item, top first."""
    rows = []
    for y in sorted({round(c[2], 1) for c in chars if c}):
        if not rows or y - rows[-1] > SIZE / 3:
            rows.append(y)
    return rows


def row_of(y, rows):
    return min(range(len(rows)), key=lambda k: abs(rows[k] - y))


def merge(rs, scale, rows=None):
    """Rects (left, right, center y) to x intervals per line, font units.
    Lines are the item's (rows) when given, else counted in rs."""
    out = []
    li = [row_of(r[2], rows) for r in rs] if rows else lines_of(rs)
    for line, r in sorted(zip(li, rs)):
        x0, x1 = r[0] * scale, r[1] * scale
        if x1 - x0 < 1e-6:
            continue
        if out and out[-1][0] == line and x0 <= out[-1][2] + 1e-3:
            out[-1][2] = max(out[-1][2], x1)
        else:
            out.append([line, x0, x1])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts", default="C:/Windows/Fonts")
    ap.add_argument("--items", default=str(HERE / "editor_items.txt"))
    ap.add_argument("--out", default=str(HERE / "editor_ref.txt"))
    a = ap.parse_args()
    items = read_items(pathlib.Path(a.items))
    files = sorted({f for it in items for f in it["fonts"]})
    fam = {f: "f%d" % i for i, f in enumerate(files)}
    css = ["@font-face { font-family: %s; src: url('%s'); }" % (fam[f], pathlib.Path(a.fonts, f).resolve().as_uri())
           for f in files]
    css.append("body { margin: 0; } #box { font-size: %gpx; line-height: normal; padding: 0; margin: 40px; "
               "text-spacing-trim: space-all; outline: none; }" % SIZE)
    for it in items:
        it["families"] = ", ".join(fam[f] for f in it["fonts"])
        it["all_pairs"] = it["id"] == "user_bidi"
    upem = {}
    sha = {}
    for f in files:
        data = pathlib.Path(a.fonts, f).read_bytes()
        upem[f] = upem_of(data)
        sha[f] = hashlib.sha256(data).hexdigest()
    with tempfile.TemporaryDirectory() as tmp:
        page = pathlib.Path(tmp, "editor.html")
        page.write_text("<!doctype html><html><head><meta charset='utf-8'><style>%s</style></head>"
                        "<body><div id='box'></div><div style='font-family: %s'>%s</div></body></html>"
                        % ("\n".join(css), ", ".join(fam.values()), "&#x0627;&#x0644;&#x05e9; abc &#x53c2;"),
                        encoding="utf-8")
        with sync_playwright() as p:
            browser = p.chromium.launch(channel="msedge")
            version = browser.version
            pg = browser.new_page(viewport={"width": 4000, "height": 1200})
            pg.goto(page.as_uri())
            # Faces load when text first needs them; measuring right after
            # setting the text would measure the fallback font. Load all.
            n_loaded = pg.evaluate("Promise.all([...document.fonts].map(f => f.load())).then(a => a.length)")
            if n_loaded != len(files):
                sys.exit("%d of %d fonts loaded" % (n_loaded, len(files)))
            pg.evaluate("document.fonts.ready")
            data = pg.evaluate(JS, [items, ["layout", "editor"]])
            browser.close()
    lines = ["# Edge's wrapping, selections and carets for editor_items.txt: Microsoft Edge %s, %s, editor_ref.py."
             % (version, datetime.date.today().isoformat()),
             "# font <file> <sha256> <units per em>",
             "# wrap <id> <layout|editor> <width px> <line starts> <words per line, visual order, | between lines> <ink x0:x1 per line>",
             "# sel <id> <s> <e> <line:x0:x1 ...>   caret <id> <k> <x>   (font units, x from the box's left)",
             "# wsel <id> <width> <s> <e> <line:x0:x1 ...>   wcaret <id> <width> <k> <line> <x>   (the same, wrapped)"]
    for f in files:
        lines.append("font %s %s %d" % (f, sha[f], upem[f]))
    for it in items:
        o = data[it["id"]]
        scale = upem[it["fonts"][0]] / SIZE
        for model in ("layout", "editor"):
            for w, chars in zip(o["widths"], o["wrap"][model]):
                starts, order, inks = wrap_summary(it["text"], chars, scale)
                lines.append("wrap %s %s %.2f %s %s %s" % (it["id"], model, w, ",".join(map(str, starts)), order, inks))
        if it["dir"] == "rtl" or it["id"] in ("user_bidi", "arabic_in_ltr"):
            for s, e, rs in o["sel"]:
                iv = merge(rs, scale)
                lines.append("sel %s %d %d %s" % (it["id"], s, e, " ".join("%d:%.2f:%.2f" % (l, x0, x1) for l, x0, x1 in iv)))
            for k, x in enumerate(o["caret"]):
                lines.append("caret %s %d %.2f" % (it["id"], k, x * scale))
        for wi, ws in o["wrapped"]:
            rows = row_centers(o["wrap"]["editor"][wi])
            for s, e, rs in ws["sel"]:
                iv = merge(rs, scale, rows)
                lines.append("wsel %s %.2f %d %d %s" % (it["id"], ws["width"], s, e,
                             " ".join("%d:%.2f:%.2f" % (l, x0, x1) for l, x0, x1 in iv)))
            for k, c in enumerate(ws["caret"]):
                lines.append("wcaret %s %.2f %d %d %.2f" % (it["id"], ws["width"], k, row_of(c[1], rows), c[0] * scale))
    pathlib.Path(a.out).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print("wrote", a.out, "Edge", version, len(lines), "lines")


if __name__ == "__main__":
    main()
