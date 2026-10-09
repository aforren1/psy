"""Rename the library from psy to ysp and move its headers into include/ysp/.

Run once, from a clean checkout of the pre-rename tree (commit 3f975bb):

    uv run python tools/rename_psy_to_ysp.py

The script moves files first, then rewrites the contents of every tracked
text file (third_party/ is vendored: only its CMakeLists.txt and README.md
are ours). It is deterministic and refuses to run twice. The committed rename
is the script's output plus a few edits made afterwards by hand: the trials
v0.1 pins regenerated (their hash folds in the error text, whose prefix
changed), and layout prose in README.md, the binding READMEs and MANIFEST.in
files, parallel.h, serial.h, trials_pin_gen.c, and a few docs that describe
the old flat layout or quote paths from before the rename.

Naming: the symbol root psy becomes y (psyscr_ -> yscr_, PSYGFX_ -> YGFX_),
the library root psy becomes ysp (psy_X.h -> ysp/X.h, PSY_BUILD_* ->
YSP_BUILD_*, psy::psy -> ysp::ysp, the Python namespace psy. -> ysp.), and the
gp module becomes aep (psy_gp.h -> ysp/aep.h, psygp_ -> yaep_). Words that
only start with "psy" (psychophysics, Psychtoolbox, PsychoPy, psync, the
.psycal/.psylum/.psyseq/.psyvi/.psyprog file names and the binary magics) are
not identifiers of this library and are left alone.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Old module stem -> new module stem (top-level psy_<old>.h).
MODULES = {
    "rt": "rt", "timeline": "timeline", "table": "table", "quest": "quest",
    "stair": "stair", "gp": "aep", "trials": "trials", "serial": "serial",
    "parallel": "parallel", "input": "input", "response": "response",
    "screen": "screen", "gfx": "gfx", "color": "color", "outline": "outline",
    "rdk": "rdk", "video": "video", "audio": "audio",
}
NEW_MODULES = sorted(MODULES.values())

# Old function-prefix tag (after "psy") -> new prefix. The last four name
# headers that the specs plan but the tree does not have yet (psy_device.h,
# psy_box.h, psy_net.h, and the color wasm shim psycolw_).
TAGS = {
    "rt": "yrt", "tl": "ytl", "tb": "ytb", "q": "yqst", "st": "yst",
    "gp": "yaep", "tr": "ytr", "s": "yser", "p": "ypar", "in": "yin",
    "rsp": "yrsp", "scr": "yscr", "gfx": "ygfx", "col": "ycol", "ol": "yol",
    "rdk": "yrdk", "vid": "yvid", "au": "yau", "lay": "ylay",
    "dev": "ydev", "box": "ybox", "net": "ynet", "colw": "ycolw",
}

# MEX sources (bindings/mex/psy_<x>.c), by new stem. A bare "psy_<x>.c"
# outside tests/compile/ names one of these.
MEX = {"color", "aep", "parallel", "quest", "serial", "stair", "trials"}

# The gp examples are found by CMake as examples/<lib>_*.c, so they follow
# the lib name.
GP_EXAMPLES = ["async", "audiometric", "bench", "optimize", "pairwise", "sim"]

TEST_DIRS = ["adapt", "compile", "fuzz", "loopback", "layout", "compare"]

# Left as they are: paths on the development machine that other tools and
# sessions share (the measurement quiet flag and lock, the notes folder), and
# the demo text the gallery draws.
PROTECT = [
    ("C:/tmp/psy-quiet", None), ("/tmp/psy-quiet", None), ("C:\\tmp\\psy-quiet", None),
    ("psy-measure.lock", None), ("C:\\tmp\\psy-work\\", None),
    ('"PSY"', "examples/gfx/gallery.c"),
]


# --- helpers -------------------------------------------------------------------

def git_ls_files():
    r = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True)
    return [p for p in r.stdout.decode("utf-8").split("\0") if p]


def move(old, new, moves):
    src, dst = os.path.join(ROOT, old), os.path.join(ROOT, new)
    if not os.path.exists(src):
        raise SystemExit("missing: " + old)
    if os.path.exists(dst):
        raise SystemExit("exists: " + new)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    os.rename(src, dst)
    moves.append((old, new))


def mstem(old):
    return MODULES.get(old, old)


def gp2aep(name):
    """psy_gp... file names -> psy_aep..., gp_* examples -> aep_*."""
    return re.sub(r"^psy_gp(?![a-z0-9])", "psy_aep", name)


# --- file moves ------------------------------------------------------------------

def plan_and_move(tracked):
    moves = []
    tracked_set = set(tracked)

    # Top-level headers.
    for old, new in sorted(MODULES.items()):
        move("psy_%s.h" % old, "include/ysp/%s.h" % new, moves)

    # The layout glue.
    move("pack/psy_layout.h", "pack/ysp/layout.h", moves)
    move("pack/psy_layout.c", "pack/layout.c", moves)

    # Design notes.
    for p in sorted(tracked):
        m = re.fullmatch(r"docs/psy_(\w+)\.md", p)
        if m:
            move(p, "docs/%s.md" % mstem(m.group(1)), moves)

    # Tests: drop the psy_ prefix; gp -> aep.
    for p in sorted(tracked):
        m = re.fullmatch(r"tests/(%s)/(psy_[^/]+)" % "|".join(TEST_DIRS), p)
        if m:
            base = gp2aep(m.group(2))[len("psy_"):]
            move(p, "tests/%s/%s" % (m.group(1), base), moves)

    # gp examples.
    for s in GP_EXAMPLES:
        move("examples/gp_%s.c" % s, "examples/aep_%s.c" % s, moves)

    # MEX bindings.
    for p in sorted(tracked):
        m = re.fullmatch(r"bindings/mex/psy_(\w+)\.(c|h)", p)
        if m:
            move(p, "bindings/mex/ysp_%s.%s" % (mstem(m.group(1)), m.group(2)), moves)
    move("bindings/mex/example_gp.m", "bindings/mex/example_aep.m", moves)

    # Python bindings: the package dir (with whatever gitignored build output
    # is in it), then the files inside it.
    pydirs = sorted({p.split("/")[2] for p in tracked if p.startswith("bindings/python/psy_")})
    for d in pydirs:
        old = d[len("psy_"):]
        new = mstem(old)
        nd = "bindings/python/ysp_%s" % new
        move("bindings/python/%s" % d, nd, moves)
        move("%s/psy_%s_ext.c" % (nd, old), "%s/ysp_%s_ext.c" % (nd, new), moves)
        move("%s/psy" % nd, "%s/ysp" % nd, moves)
        if old != new and os.path.exists(os.path.join(ROOT, nd, "tests", "test_%s.py" % old)):
            move("%s/tests/test_%s.py" % (nd, old), "%s/tests/test_%s.py" % (nd, new), moves)

    # Map every tracked path to where it is now.
    def where(p):
        for old, new in moves:
            if p == old:
                return new
            if p.startswith(old + "/"):
                return new + p[len(old):]
        return p

    # Python packages move their contents in two steps (dir, then file).
    def where_all(p):
        q = where(p)
        while True:
            r = where(q)
            if r == q:
                return q
            q = r

    now = [where_all(p) for p in tracked]
    for p in now:
        if not os.path.exists(os.path.join(ROOT, p)):
            raise SystemExit("lost track of " + p)
    return moves, now, tracked_set


# --- content rewrite ---------------------------------------------------------------

# Start of an identifier: not after a word character, except right after a
# -D//D compiler flag or a \n, \t, \r escape in a string literal.
B = r"(?:(?<![A-Za-z0-9_])|(?<=-D)|(?<=/D)|(?<=\\n)|(?<=\\t)|(?<=\\r))"
# End of a tag: an underscore or the end of the word.
E = r"(?=_|\b)"


def alt(words):
    return "|".join(re.escape(w) for w in sorted(words, key=lambda w: (-len(w), w)))


def compile_rules(test_stems):
    R = []

    def add(pat, repl, flags=0):
        R.append((re.compile(pat, flags), repl))

    # 1. gp -> aep, where gp is the module stem.
    add(r"(?<![A-Za-z0-9])psy_gp(?![A-Za-z0-9])", "psy_aep")
    add(B + r"PSY_GP_", "PSY_AEP_")
    add(B + r"psy([./-])gp(?![A-Za-z0-9_])", r"psy\1aep")
    add(r"\bPyInit_gp\b", "PyInit_aep")
    add(r"\bgp\.(abi3|pyd)\b", r"aep.\1")
    add(r"\btest_gp\.py\b", "test_aep.py")
    add(r"\bexample_gp\b", "example_aep")
    add(r"\b(PSY_[A-Z]+_)gp\b", r"\1aep")
    add(r"\bgp_(%s)\b" % alt(GP_EXAMPLES), r"aep_\1")

    # 2. Markdown anchors of headings that name a header: GitHub drops the
    #    "/" and "." of "ysp/rt.h", so #depends-on-psy_rth -> #depends-on-ysprth.
    def anchor(m):
        stem = m.group(2)
        stem = "aep" if stem == "gp" else stem
        return m.group(1) + "ysp" + stem + "h"
    add(r"(\]\([^)\s]*#[a-z0-9-]*?)psy_([a-z0-9]+)h(?=[-)])", anchor)

    # 3. Header paths.
    mods = alt(NEW_MODULES)
    add(r"(?<=\]\()psy_(%s)\.h\b" % mods, r"include/ysp/\1.h")          # root links
    add(r"((?:\.\.[/\\])+)psy_(%s)\.h\b" % mods, r"\1include/ysp/\2.h")  # ../ paths
    add(B + r"psy_(\$\{\w+\}|\$\w+|<\w+>|\*)\.h\b", r"ysp/\1.h")         # templates
    add(B + r"psy_([a-z0-9]+)\.h\b", r"ysp/\1.h")                        # every header

    # 4. Moved test files and test/target names.
    add(r"(tests[/\\](?:%s)[/\\])psy_" % "|".join(TEST_DIRS), r"\1")
    add(B + r"psy_(%s)\b" % alt(test_stems), r"\1")
    add(B + r"psy_(\$\{\w+\}|\$\w+|<\w+>)_loopback\b", r"\1_loopback")
    add(B + r"psy_(%s)\.cpp\b" % mods, r"\1.cpp")
    add(B + r"psy_(\w+)\.md\b", r"\1.md")
    add(r"\b(test|compile)_psy_", r"\1_")
    add(r"\bpack/psy_layout\b(?!\.)", "pack/layout")

    # 5. psy as a suffix in compare-script names.
    add(r"\b(fit|run|rgb)_psy\b", r"\1_ysp")
    add(r"\bbuild_truth_psygp\b", "build_truth_yaep")

    # 6. The naming-rule placeholders.
    add(B + r"PSY<X>", "Y<X>")
    add(B + r"psy<x>", "y<x>")

    # 7. Function and macro prefixes, longest tag first.
    low = {"psy" + k: v for k, v in TAGS.items()}
    up = {"PSY" + k.upper(): v.upper() for k, v in TAGS.items()}
    add(B + r"(%s)%s" % (alt(low), E), lambda m: low[m.group(1)])
    add(B + r"(%s)%s" % (alt(up), E), lambda m: up[m.group(1)])

    # 8. The library root.
    add(B + r"PSY_", "YSP_")
    add(B + r"psy_", "ysp_")
    add(B + r"psy\b", "ysp")
    add(B + r"PSY\b", "YSP")

    # 9. Include flags: the headers moved from the root to include/.
    add(r"(?<![\w.\-/])(-I|/I)\.(?=[\s\"'\],;)]|$)", r"\1include", re.M)
    add(r"(-I)((?:\.\./)+\.\.)(?=\s)", r"\1\2/include")
    add(r"(/I)((?:\.\.\\)+\.\.)(?=\s)", r"\1\2\\include")
    return R


def c_file_rule(path):
    """A bare psy_<x>.c is a compile check in tests/compile/ and pack/'s
    glue, and a MEX source elsewhere when a MEX module has that name."""
    in_compile = path.startswith("tests/compile/")
    mods = alt(NEW_MODULES + ["layout"])
    pat = re.compile(B + r"psy_(%s)\.c\b" % mods)

    def repl(m):
        s = m.group(1)
        if s in MEX and not in_compile:
            return "ysp_%s.c" % s
        return "%s.c" % s
    return pat, repl


# File-specific edits, on the original text (before the rules). Each must
# match exactly the stated number of times.
PRE = {
    "CMakeLists.txt": [
        ("target_include_directories(psy INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})",
         "target_include_directories(psy INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)", 1),
        ("set(PSY_LIBS rt parallel serial stair quest gp table",
         "set(PSY_LIBS rt parallel serial stair quest aep table", 1),
        ("# adaptive methods (stair, quest, gp), tables", "# adaptive methods (stair, quest, aep), tables", 1),
        ("quest and gp have an opt-in async", "quest and aep have an opt-in async", 1),
        ("# which the include path above resolves from the repository root, so the",
         "# which the include path above resolves from include/, so the", 1),
    ],
    ".github/workflows/ci.yml": [
        ("(stair, quest, gp, trials,        #", "(stair, quest, aep, trials,       #", 1),
        ("for h in stair quest gp table", "for h in stair quest aep table", 1),
        ("test_gp_async", "test_aep_async", 2),
        ("# the extension compiles must sit next to setup.py:",
         "# the extension compiles must sit in include/ysp/ next to setup.py:", 1),
        ("run: cp ${{ matrix.package }}.h psy_rt.h psy_table.h bindings/python/${{ matrix.package }}/",
         "run: p=${{ matrix.package }} && d=bindings/python/$p/include/ysp && mkdir -p $d && "
         "cp include/ysp/${p#ysp_}.h include/ysp/rt.h include/ysp/table.h $d/", 1),
    ],
    "README.md": [
        ("class in quest and gp that runs", "class in quest and aep that runs", 1),
    ],
    "bindings/mex/README.md": [
        ("`nocross` and `numeric` (gp)", "`nocross` and `numeric` (aep)", 1),
        ("(quest, gp). The binding", "(quest, aep). The binding", 1),
        ("`{'gp', h}`", "`{'aep', h}`", 1),
        ("(or quest, gp).", "(or quest, aep).", 1),
        ("(and for gp,", "(and for aep,", 1),
        ("The build adds `-I` for the repository root, where the headers live.",
         "The build adds `-I` for include/ under the repository root, where the headers live.", 1),
    ],
    "bindings/mex/ysp_trials.c": [
        ("{'gp', h}", "{'aep', h}", 1),
        ("{'stair'|'quest'|'gp', handle}", "{'stair'|'quest'|'aep', handle}", 2),
        ('pm_lower_eq(mod, "gp")', 'pm_lower_eq(mod, "aep")', 1),
        (" * gp) through mexCallMATLABWithTrap", " * aep) through mexCallMATLABWithTrap", 1),
    ],
    "bindings/mex/test_mex.m": [
        ("test_gp()", "test_aep()", 2),
    ],
    "bindings/mex/build.m": [
        ("root = fullfile(here, '..', '..');           % repo root holds the headers",
         "root = fullfile(here, '..', '..', 'include'); % include/ysp/ holds the headers", 1),
    ],
    ".gitignore": [
        ("# header staged into a python binding for isolated/CI builds\nbindings/python/*/psy_*.h",
         "# headers staged into a python binding for isolated/CI builds\nbindings/python/*/include/", 1),
    ],
    "tests/mutate/mutate.py": [
        ("    for f in os.listdir(REPO):\n"
         "        if f.endswith(\".h\"):\n"
         "            shutil.copy2(os.path.join(REPO, f), hdr)\n",
         "    os.makedirs(os.path.join(hdr, \"ysp\"), exist_ok=True)\n"
         "    for f in os.listdir(os.path.join(REPO, \"include\", \"ysp\")):\n"
         "        if f.endswith(\".h\"):\n"
         "            shutil.copy2(os.path.join(REPO, \"include\", \"ysp\", f), os.path.join(hdr, \"ysp\"))\n", 1),
        ("open(os.path.join(REPO, f), encoding=\"utf-8\")",
         "open(os.path.join(REPO, \"include\", f), encoding=\"utf-8\")", 1),
    ],
}

# File-specific edits on the renamed text (after the rules), as regexes.
# (path regex, pattern, replacement, expected count per file or None).
POST = [
    # setup.py: the dev-tree include root is include/, the staged one is
    # include/ under the package.
    (r"bindings/python/ysp_\w+/setup\.py",
     r'HEADER_DIR = REPO_ROOT if os\.path\.exists\(os\.path\.join\(REPO_ROOT, "ysp/(\w+)\.h"\)\) else HERE',
     r'HEADER_DIR = os.path.join(REPO_ROOT, "include") if os.path.exists(os.path.join(REPO_ROOT, "include", "ysp", "\1.h")) else os.path.join(HERE, "include")', 1),
    (r"bindings/python/ysp_\w+/setup\.py", r"lives at the repo root;", r"lives in include/ under the repo root;", 1),
    (r"bindings/python/ysp_\w+/setup\.py", r"staged next to this file", r"staged in include/ysp/ next to this file", 1),
    (r"bindings/python/ysp_\w+/MANIFEST\.in", r"(?m)^include ysp/(\w+)\.h(?=\r?$)", r"include include/ysp/\1.h", None),
    (r"bindings/python/ysp_\w+/MANIFEST\.in", r"live at the repository root", r"live in include/ysp/ under the repository root", None),
    # Staging commands: the copies go to include/ysp/ under the package.
    (r"bindings/python/ysp_\w+/(MANIFEST\.in|README\.md|pyproject\.toml)",
     r"(?m)\bcp ((?:\.\./\.\./\.\./include/ysp/\w+\.h )+)\.(?=\r?$)", r"mkdir -p include/ysp && cp \1include/ysp/", None),
    # The examples no longer have the repository root on the include path.
    (r"examples/(gfx_bench|gfx_rdk_bench)\.c", r'#include "tests/adapt/', r'#include "../tests/adapt/', 1),
]


def protect(text, path):
    keys = []
    for i, (s, only) in enumerate(PROTECT):
        if only and path != only:
            continue
        k = "\x00P%d\x00" % i
        if s in text:
            text = text.replace(s, k)
            keys.append((k, s))
    return text, keys


PSYCH = re.compile(r"(?i)psych")


def rewrite(path, text, rules):
    for old, new, n in PRE.get(path, []):
        if "\n" in old and "\r\n" in text:   # a CRLF checkout
            old, new = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
        c = text.count(old)
        if c != n:
            raise SystemExit("%s: pre-edit found %d times, want %d: %r" % (path, c, n, old))
        text = text.replace(old, new)
    text, keys = protect(text, path)
    cpat, crepl = c_file_rule(path)
    for pat, repl in rules[:_C_RULE_AT]:
        text = pat.sub(repl, text)
    text = cpat.sub(crepl, text)
    for pat, repl in rules[_C_RULE_AT:]:
        text = pat.sub(repl, text)
    for k, s in keys:
        text = text.replace(k, s)
    for prx, pat, repl, n in POST:
        if re.fullmatch(prx, path):
            text, c = re.subn(pat, repl, text)
            if n is not None and c != n:
                raise SystemExit("%s: post-edit matched %d times, want %d: %s" % (path, c, n, pat))
    return text


_C_RULE_AT = None


def self_test(rules):
    global _C_RULE_AT
    cases = [
        ('#include "psy_screen.h"', '#include "ysp/screen.h"'),
        ("#include <psy_gp.h>", "#include <ysp/aep.h>"),
        ("psyscr_open(PSYSCR_OK) psyst_x PSYST_Y psys_open PSYS_OK", "yscr_open(YSCR_OK) yst_x YST_Y yser_open YSER_OK"),
        ("psyp__set_error PSYP_NO_THREADS psyq_ PSYQ_ASYNC", "ypar__set_error YPAR_NO_THREADS yqst_ YQST_ASYNC"),
        ("psygp_open PSYGP_ASYNC PSY_GP_IMPLEMENTATION", "yaep_open YAEP_ASYNC YSP_AEP_IMPLEMENTATION"),
        ("set(PSY_PREFIX_screen PSYSCR)", "set(YSP_PREFIX_screen YSCR)"),
        ("set(PSY_DEPS_gp x)", "set(YSP_DEPS_aep x)"),
        ("-DPSY_FETCH_MINIAUDIO=ON -DPSYSCR_NO_SDL", "-DYSP_FETCH_MINIAUDIO=ON -DYSCR_NO_SDL"),
        ('printf("\\npsygfx_x")', 'printf("\\nygfx_x")'),
        ("import psy.gp as pg; psy.trials", "import ysp.aep as pg; ysp.trials"),
        ("pip install psy-gp psy-serial", "pip install ysp-aep ysp-serial"),
        ("psy/gp.abi3.so PyInit_gp", "ysp/aep.abi3.so PyInit_aep"),
        ("project(psy LANGUAGES C) psy::psy", "project(ysp LANGUAGES C) ysp::ysp"),
        ("tests/adapt/psy_gp_test.c tests/compile/psy_${lib}.c", "tests/adapt/aep_test.c tests/compile/${lib}.c"),
        ("test_psy_gp compile_psy_rt_c psy_screen_loopback", "test_aep compile_rt_c screen_loopback"),
        ("[psy_rt.h](psy_rt.h) [x](../psy_rt.h) docs/psy_gfx.md", "[ysp/rt.h](include/ysp/rt.h) [x](../include/ysp/rt.h) docs/gfx.md"),
        ("psy_<name>.h psy<x>_ PSY<X>_ PSY_<NAME>_IMPLEMENTATION", "ysp/<name>.h y<x>_ Y<X>_ YSP_<NAME>_IMPLEMENTATION"),
        ("psy_quest('version') psy_gp:busy", "ysp_quest('version') ysp_aep:busy"),
        ("cc -O2 -I. -o x x.c; cl /O2 /I. x.c", "cc -O2 -Iinclude -o x x.c; cl /O2 /Iinclude x.c"),
        ("[a](#depends-on-psy_rth) [b](#what-psy_gph-leaves)", "[a](#depends-on-ysprth) [b](#what-yspaeph-leaves)"),
        ("examples/gp_sim.c gp_async: psygp_async", "examples/aep_sim.c aep_async: yaep_async"),
        ("psy_trials_pins.h psy_gfx_headless.h", "trials_pins.h gfx_headless.h"),
        ("psy_mex_util.h psy_add_program psy_layout", "ysp_mex_util.h ysp_add_program ysp_layout"),
        ("pack/psy_layout.h psycolw_ psydev_ PSYNET_", "pack/ysp/layout.h ycolw_ ydev_ YNET_"),
    ]
    survive = ("psychophysics psychometric Psychtoolbox PsychoPy AEPsych jsPsych PSYCHOMETRIC "
               "psync psyscr_slot_psync .psycal .psylum .psyseq .psyvi .psyprog PSYCAL PSYLUM "
               "PSYVSEQ1 PSYVIDX1 PSYPROG1 PSYF PSYC psyscreen")
    for src, want in cases + [(survive, survive.replace("psyscr_slot", "yscr_slot"))]:
        got = rewrite("x.txt", src, rules)
        if got != want:
            raise SystemExit("self-test:\n  in   %r\n  got  %r\n  want %r" % (src, got, want))
    got = rewrite("tests/compile/x.cpp", '#include "psy_gp.c"', rules)
    assert got == '#include "aep.c"', got
    got = rewrite("docs/x.md", "psy_gp.c psy_rt.c pack/psy_layout.c", rules)
    assert got == "ysp_aep.c rt.c pack/layout.c", got


def main():
    if os.path.exists(os.path.join(ROOT, "include", "ysp")):
        raise SystemExit("include/ysp/ exists: the rename has already run")
    tracked = git_ls_files()
    for p in tracked:
        if not os.path.exists(os.path.join(ROOT, p)):
            raise SystemExit("tracked file missing (tree not clean?): " + p)

    # Stems of the moved test files that are not plain module names
    # (trials_pins, gfx_headless, screen_loopback, aep_test, ...).
    stems = set()
    for p in tracked:
        m = re.fullmatch(r"tests/(?:%s)/(psy_\w+)\.\w+" % "|".join(TEST_DIRS), p)
        if m:
            s = gp2aep(m.group(1))[len("psy_"):]
            if "_" in s:
                stems.add(s)
    rules = compile_rules(sorted(stems))
    global _C_RULE_AT
    # The .c rule needs the test-stem rule before it and the generic rules after.
    _C_RULE_AT = next(i for i, (p, _) in enumerate(rules) if p.pattern.endswith(r"_loopback\b"))
    self_test(rules)

    moves, now, _ = plan_and_move(tracked)

    third_ok = {"third_party/CMakeLists.txt", "third_party/README.md"}
    changed = skipped = 0
    for p in sorted(now):
        if p.startswith("third_party/") and p not in third_ok:
            continue
        if p == "tools/rename_psy_to_ysp.py":
            continue
        full = os.path.join(ROOT, p)
        with open(full, "rb") as f:
            raw = f.read()
        if b"\0" in raw:
            skipped += 1
            continue
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            raise SystemExit("not UTF-8: " + p)
        new = rewrite(p, text, rules)
        if PSYCH.findall(new) != PSYCH.findall(text):
            raise SystemExit("%s: a psych... word changed" % p)
        if new != text:
            with open(full, "wb") as f:
                f.write(new.encode("utf-8"))
            changed += 1

    print("moved %d paths, rewrote %d files, skipped %d binary files" % (len(moves), changed, skipped))


if __name__ == "__main__":
    sys.exit(main())
