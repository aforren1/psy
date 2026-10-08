/* psy_layout.h - v0.1.0 - text layout for the pack tool and the player
 *
 *   Lays out UTF-8 text with Skribidi (vendored in third_party/, pinned, with
 *   a local bidi patch) and gives glyphs as psy_gfx.h curve-run items over
 *   psy_outline.h curve sets built from the same font bytes. Not a single
 *   header: it links the vendored Skribidi, HarfBuzz, SheenBidi, libunibreak
 *   and budouxc (CMake option PSY_BUILD_LAYOUT). The psy_*.h headers stay
 *   free of them (rig_spec 5.2, Font). docs/psy_layout.md is the manual.
 *
 *   The program compiles psy_outline.h's implementation once
 *   (PSY_OUTLINE_IMPLEMENTATION). This library uses psy_gfx.h's types only
 *   and calls nothing in it, so the pack tool runs without a GPU; uploading
 *   the curve sets (psygfx_cset_make, psygfx_cset_add) is the caller's.
 *
 *   Usage:
 *       psyol_ctx ol; psyol_init(&ol, NULL);
 *       psylay_lib* L;
 *       psylay_create(&L, &(psylay_desc){ .ol = &ol });
 *       int ui = psylay_add_font(L, bytes, n, 0, "segoeui.ttf");     // fallback order
 *       psylay_block b = { 0 };
 *       psylay_layout(L, "Hello, world", -1, &(psylay_style){ .size = 32, .width = 400 }, NULL, 0, &b);
 *       // b.runs[r]: font b.runs[r].font, items b.items[first .. first + n - 1],
 *       // one psygfx_crun each, .size = b.runs[r].size, place and anchor TOP_LEFT,
 *       // .w = b.w, .h = b.h, so every run shares the block's origin.
 *       psylay_stamp(L, &b, line, sizeof line);    // versions and font hashes
 *
 *   Threads: a psylay_lib is for one thread, with its psyol_ctx.
 */
#ifndef PSY_LAYOUT_H_INCLUDED
#define PSY_LAYOUT_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include "psy_outline.h"
#include "psy_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PSYLAY_VERSION_STRING "0.1.0"
#define PSYLAY_MAX_FONTS      64

/* Return codes: psy_outline.h's values for the same meaning. */
#define PSYLAY_OK            0
#define PSYLAY_ERR_ARG     (-1)
#define PSYLAY_ERR_FULL    (-4)
#define PSYLAY_ERR_FORMAT  (-8)
#define PSYLAY_ERR_NOMEM  (-10)

/* What lays out the text. The pack manifest records these (rig_spec 5.1). */
typedef struct psylay_versions {
    const char* psylay;           /* PSYLAY_VERSION_STRING                          */
    const char* skribidi;         /* the full commit of the vendored Skribidi       */
    const char* skribidi_patch;   /* "sha256:<hex>" of each local patch file, in
                                   * the order they apply, comma-separated, then
                                   * ",tree-differs" when the patched source does
                                   * not match third_party/MANIFEST.sha256         */
    const char* harfbuzz;         /* hb_version_string(), e.g. "14.6.0"            */
    const char* sheenbidi;        /* commit                                         */
    const char* libunibreak;      /* tag                                            */
    const char* budouxc;          /* commit                                         */
} psylay_versions;
const psylay_versions* psylay_get_versions(void);

typedef struct psylay_lib psylay_lib;

typedef struct psylay_desc {
    psyol_ctx* ol;                /* required: builds the curve sets               */
} psylay_desc;

int  psylay_create(psylay_lib** out, const psylay_desc* d);
void psylay_destroy(psylay_lib* L);
/* The message of the last call that failed ("" if none). */
const char* psylay_error(const psylay_lib* L);

/* Adds a font: face `face` of the bytes (a .ttc holds several). The bytes
 * stay the caller's and must outlive L. Fonts are tried in the order added:
 * for each run of one script, the fonts that support the script, in that
 * order, and the first that maps the codepoint (Skribidi's rule). Opens the
 * face with HarfBuzz and with psy_outline.h, hashes the bytes (SHA-256) and
 * makes an empty curve set (resolved, backward lists) whose glyphs are built
 * when a layout first uses them. Returns the font index or a code. */
int psylay_add_font(psylay_lib* L, const void* bytes, size_t n, int face, const char* name);

typedef struct psylay_font_info {
    const char*       name;
    int               face;
    const char*       sha256;          /* 64 hex digits of the file's bytes        */
    int32_t           units_per_em, n_glyphs;
    double            ascender, descender, line_gap;   /* em, hhea; descender < 0  */
    const psyol_cset* set;             /* the glyphs built so far                  */
    uint32_t          n_built;
    double            build_us;        /* time spent building them, in all         */
} psylay_font_info;
int psylay_font(const psylay_lib* L, int font, psylay_font_info* out);
int psylay_font_count(const psylay_lib* L);

/* The glyphs added to font's set since the last call, for psygfx_cset_add()
 * (or psygfx_cset_make() the first time). *glyphs is L's array and stays
 * valid until the next call that lays out. Returns the count. */
int psylay_take_new_glyphs(psylay_lib* L, int font, const uint32_t** glyphs);

enum { PSYLAY_DIR_AUTO = 0, PSYLAY_DIR_LTR, PSYLAY_DIR_RTL };
enum { PSYLAY_ALIGN_START = 0, PSYLAY_ALIGN_END, PSYLAY_ALIGN_CENTER };

typedef struct psylay_style {
    float       size;          /* units (px or deg) per em; > 0                    */
    float       width;         /* the box width in units; 0: no wrapping           */
    float       line_height;   /* times the fonts' own line; 0: 1                  */
    int         dir;           /* PSYLAY_DIR_*; AUTO: the first strong character   */
    int         align;         /* PSYLAY_ALIGN_*; START and END follow dir         */
    const char* lang;          /* BCP 47. "ja", "zh-Hans", "zh-Hant" and "th" break
                                * at BudouX phrases (Skribidi's rule); NULL: ""   */
    float       color;         /* the palette position of every item              */
} psylay_style;

/* Attributes over the text's bytes [start, end). Spans are sorted and do not
 * overlap; text outside them has the style's. */
typedef struct psylay_span {
    uint32_t    start, end;
    float       size;          /* 0: the style's                                   */
    float       color;         /* < 0: the style's                                 */
    const char* lang;          /* NULL: the style's                                */
} psylay_span;

typedef struct psylay_line {
    float    x, baseline;      /* units from the block's top-left                  */
    float    width, ascent, descent;   /* descent > 0, below the baseline           */
    uint32_t text_start, text_end;     /* codepoint offsets                         */
} psylay_line;

/* The items of one font at one size: one psygfx_crun. */
typedef struct psylay_run {
    int      font;
    float    size;             /* crun_desc.size                                   */
    uint32_t first, n;         /* into the block's items                           */
} psylay_run;

typedef struct psylay_block {
    /* items[i]: x, y the glyph's pen in units from the block's top-left (y down,
     * on the baseline), glyph its id in the run's font, color from the style or
     * span; the other fields 0, scale 1, gate 1. cluster[i]: the codepoint offset
     * where items[i]'s cluster starts. Grouped by run, visual order inside one. */
    psygfx_citem* items;   uint32_t* cluster;  uint32_t n_items, cap_items;
    psylay_run*   runs;    uint32_t n_runs,  cap_runs;
    psylay_line*  lines;   uint32_t n_lines, cap_lines;
    float         w, h;          /* the laid-out box, units                        */
    int           dir;           /* the paragraph's resolved direction             */
    uint32_t      n_missing;     /* items with glyph 0: no font has the codepoint   */
    uint64_t      fonts_used;    /* bit i: font i                                  */
    const psylay_versions* versions;
    double        layout_us;     /* the last call: Skribidi and the conversion     */
    double        build_us;      /* the last call: building new glyphs             */
    uint32_t      n_new_glyphs;  /* the last call                                  */
} psylay_block;

/* Lays out text (n bytes, or to its NUL when n < 0) into b. b's arrays grow
 * only when too small, so laying out again at the same size or smaller
 * allocates nothing here; Skribidi allocates (docs/psy_layout.md, Cost).
 * Builds the glyphs the result uses that the font sets lack. */
int  psylay_layout(psylay_lib* L, const char* text, int n, const psylay_style* st,
                   const psylay_span* spans, int n_spans, psylay_block* b);
void psylay_block_free(psylay_block* b);

/* One line for a log or a manifest: the versions, the patch hash, and
 * name#face=sha256 for each font b used (every font when b is NULL).
 * Returns the length it needed. */
int  psylay_stamp(const psylay_lib* L, const psylay_block* b, char* buf, size_t cap);

/* Skribidi access, for its editor. The collections hold L's fonts. */
struct skb_font_collection_t;
struct skb_attribute_collection_t;
struct skb_temp_alloc_t;
struct skb_layout_t;
struct skb_font_collection_t*      psylay_skb_fonts(psylay_lib* L);
struct skb_attribute_collection_t* psylay_skb_attributes(psylay_lib* L);
struct skb_temp_alloc_t*           psylay_skb_temp(psylay_lib* L);
/* Adds a Skribidi layout (an editor's paragraph) to b, offset by x, y, with
 * every item at palette position color; append 0 empties b first. Builds
 * missing glyphs as psylay_layout() does. */
int psylay_from_skb(psylay_lib* L, const struct skb_layout_t* lay, float x, float y,
                    float color, int append, psylay_block* b);

#ifdef __cplusplus
}
#endif
#endif
