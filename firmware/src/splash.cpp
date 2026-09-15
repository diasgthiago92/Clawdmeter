#include "splash.h"
#include "splash_animations.h"
#include "kiro_ghost.h"
#include "almirante.h"
#include "splash_geometry.h"
#include "theme.h"
#include "usage_rate.h"
#include "hal/board_caps.h"
#include "hal/display_hal.h"
#include <Arduino.h>
#include <string.h>
#include <stdint.h>
#include <esp_heap_caps.h>

// 60×60 stage. CELL sized so the canvas fits the smaller display dimension —
// the canvas is square and centered, so on portrait or letterboxed panels
// it leaves vertical margin rather than cropping. On PSRAM-less boards the
// buffer is rendered tiny (cell == 1) and LVGL scales it up to fill the panel;
// the geometry decision lives in splash_compute_geometry() (splash_geometry.h).
//
// Animations are stored as bounding-box crops of the official 55×37 art stage
// (see tools/convert_official_clawd.js); compose_stage() places the current
// frame centered on the 60×60 stage. The oversized stage leaves room to later
// translate animations across the screen (walks, lurking).
#define GRID         SPLASH_GRID
static int  cell      = 8;         // recomputed in splash_init()
static int  canvas_w  = GRID * 8;
static int  canvas_h  = GRID * 8;

// Splash background: true black (matches THEME_BG and palette index 0
// emitted by tools/convert_official_clawd.js). Used for the stage margins
// and as palette fallback.
#define COL_EMPTY    0x0000

LV_FONT_DECLARE(font_styrene_28);

static lv_obj_t *splash_container = NULL;
static lv_obj_t *canvas = NULL;
static lv_obj_t *label_status = NULL;     // shown only when no animations loaded
static uint16_t *canvas_buf = NULL;        // 480x480 RGB565 (PSRAM)

static uint16_t cur_anim = 0;
static uint16_t cur_frame = 0;
static uint32_t frame_started_ms = 0;
static uint32_t last_pick_ms = 0;
static bool active = false;

// While splash is showing, auto-cycle to the next animation in the current
// rate-driven group every this many ms.
#ifndef SPLASH_ROTATE_INTERVAL_MS
#define SPLASH_ROTATE_INTERVAL_MS 20000
#endif

// Usage-rate animation groups: 4 groups × up to 4 animations each.
// Filled at init by matching literal names from splash_anims[].
// (jumping is the only unassigned animation — still reachable via splash_next.)
#define GROUP_COUNT 4
#define GROUP_MAX   4
static int8_t  group_lists[GROUP_COUNT][GROUP_MAX];
static uint8_t group_size[GROUP_COUNT] = {0};
static uint8_t group_rotation[GROUP_COUNT] = {0};

static const char* GROUP_NAMES[GROUP_COUNT][GROUP_MAX] = {
    // Group 0 — idle / sleepy (calm, investigative). Magnifier first: it's
    // the boot pick, and lurking-first would boot to a near-empty screen.
    { "magnifier", "walking", "pointing", "lurking" },
    // Group 1 — normal pace
    { "crab walking", "waving", "trumpet", "basketball" },
    // Group 2 — active (typing along with you)
    { "laptop", "dancing", "skateboard", "soccer" },
    // Group 3 — heavy burn (high-energy rides + the most exuberant jump)
    { "racing car", "cloud", "sailing scene", "jumping happy" },
};

// Scratch stage: the current animation frame composed centered onto the full
// 60×60 grid (index 0 = background elsewhere). 3.6 KB of static RAM.
static uint8_t stage_cells[GRID * GRID];

// ─── Costumes: Vasco match days and holidays ─────────────────────────────────
// The daemon picks the day's costume ("cos"): Vasco shirt on match days, Santa
// at Christmas, passista at Carnaval, witch at Halloween. The Kiro ghost swaps
// to its pre-drawn outfit; Clawd gets his painted over any official frame at
// render time. Clawd's body is always the same orange, so the painter finds the
// torso below the eyes (outfit) and the head's top row (hat / headdress, drawn
// into the empty cells above).
#define CLAWD_BODY_565 0xDBAA
#define COSTUME_PAD    11      // rows of headroom above Clawd for hats and feathers
static int costume = SPLASH_COSTUME_NONE;
// Mask values index these per costume (0 = keep the art's own color).
static const uint16_t COSTUME_COLORS[SPLASH_COSTUME_COUNT][5] = {
    {0, 0, 0, 0, 0},
    {0, 0x3186, 0xFFFF, 0xD003, 0},          // Vasco: shirt (lifted black), sash, cross
    {0, 0xD8A2, 0xBDF7, 0x2104, 0xFE60},     // Natal: red, fur trim, belt, buckle
    {0, 0xFE60, 0xF8B2, 0x2FE4, 0xFFE0},     // Carnaval: gold, pink, green, yellow
    {0, 0x923F, 0xFC00, 0x4010, 0},          // Halloween: purple cape, orange, dark hat
};

static bool costume_has_hat(void) { return costume >= SPLASH_COSTUME_NATAL; }

static const splash_anim_def_t *kiro_outfit(void) {
    switch (costume) {
    case SPLASH_COSTUME_VASCO:     return &kiro_vasco_anim;
    case SPLASH_COSTUME_NATAL:     return &kiro_santa_anim;
    case SPLASH_COSTUME_CARNAVAL:  return &kiro_carnaval_anim;
    case SPLASH_COSTUME_HALLOWEEN: return &kiro_halloween_anim;
    default:                       return &kiro_ghost_anim;
    }
}

static int body_code_of(const uint16_t *palette) {
    if (!palette) return -1;
    for (int i = 1; i < SPLASH_PALETTE_SIZE; i++) if (palette[i] == CLAWD_BODY_565) return i;
    return -1;
}

// Fills mask (w*h) for one frame; false when there is no Clawd (or no costume).
static bool costume_mask(const uint8_t *cells, int w, int h, const uint16_t *palette, uint8_t *mask) {
    if (costume == SPLASH_COSTUME_NONE) return false;
    const int body = body_code_of(palette);
    if (body < 0 || h > 256) return false;
    memset(mask, 0, (size_t)w * h);
    static int counts[256];
    int max_count = 0, head = -1;
    for (int y = 0; y < h; y++) {
        int c = 0;
        for (int x = 0; x < w; x++) c += cells[y * w + x] == body;
        counts[y] = c;
        if (c > max_count) max_count = c;
    }
    if (max_count < 6) return false;
    int t0 = -1, t1 = -1;                              // torso rows (legs are much narrower)
    for (int y = 0; y < h; y++) {
        if (counts[y] * 20 >= max_count * 11) { if (t0 < 0) t0 = y; t1 = y; }
    }
    for (int y = 0; y <= t0 && head < 0; y++) if (counts[y] * 4 >= max_count) head = y;
    int eyes = -1;                                     // last eye row: holes in the upper half of the torso
    for (int y = t0; y <= t0 + (t1 - t0) / 2; y++) {
        int bx0 = w, bx1 = -1;
        for (int x = 0; x < w; x++) if (cells[y * w + x] == body) { if (x < bx0) bx0 = x; bx1 = x; }
        for (int x = bx0 + 1; x < bx1; x++) {
            const uint8_t c = cells[y * w + x];
            if (c != body && c != 0) { eyes = y; break; }
        }
    }
    int y0 = eyes >= 0 ? eyes + 1 : t0 + (t1 - t0) / 2;
    if (y0 < t0 + (t1 - t0) * 2 / 5) y0 = t0 + (t1 - t0) * 2 / 5;   // outfit never climbs above the chest
    const int y1 = t1;
    if (y1 - y0 < 1) return false;
    int cx0 = w, cx1 = -1;                             // torso core width from the bottom row (no arms)
    for (int x = 0; x < w; x++) if (cells[y1 * w + x] == body) { if (x < cx0) cx0 = x; cx1 = x; }
    if (cx1 - cx0 < 3) return false;
    int hx0 = w, hx1 = -1;                             // head top width
    for (int x = 0; x < w; x++) if (cells[head * w + x] == body) { if (x < hx0) hx0 = x; hx1 = x; }
    const int hw = hx1 - hx0 + 1;

    auto torso = [&](int y, int x) { return y >= y0 && y <= y1 && cells[y * w + x] == body; };
    auto mark = [&](int y, int x, uint8_t v) { if (y >= 0 && y < h && x >= 0 && x < w) mask[y * w + x] = v; };
    const int ym = y0 + (y1 - y0) / 2;

    switch (costume) {
    case SPLASH_COSTUME_VASCO: {
        for (int y = y0; y <= y1; y++) for (int x = 0; x < w; x++) if (torso(y, x)) mark(y, x, 1);
        const int sw = (cx1 - cx0 + 1) / 6 > 2 ? (cx1 - cx0 + 1) / 6 : 2;
        auto sash_x = [&](int y) { return cx0 + (y - y0) * (cx1 - cx0 + 1 - sw) / (y1 - y0); };
        for (int y = y0; y <= y1; y++)
            for (int x = sash_x(y); x < sash_x(y) + sw && x < w; x++) if (mask[y * w + x]) mark(y, x, 2);
        const int cy = y0 + (y1 - y0) / 3, cxc = sash_x(cy) + sw / 2;
        const int arm = sw / 2 > 1 ? sw / 2 : 1;
        for (int d = -arm; d <= arm; d++) {
            if (cy + d >= y0 && cy + d <= y1 && mask[(cy + d) * w + cxc]) mark(cy + d, cxc, 3);
            if (cxc + d >= 0 && cxc + d < w && mask[cy * w + cxc + d]) mark(cy, cxc + d, 3);
        }
        break;
    }
    case SPLASH_COSTUME_NATAL: {
        const int cxm = (cx0 + cx1) / 2;
        for (int y = y0; y <= y1; y++)
            for (int x = 0; x < w; x++)
                if (torso(y, x)) mark(y, x, y == y1 ? 2 : y == ym ? ((x >= cxm - 1 && x <= cxm + 1) ? 4 : 3) : 1);
        for (int x = hx0 - 1; x <= hx1 + 1; x++) mark(head, x, 2);      // fur brim
        const int hh = hw / 2;
        int tip = hx1;
        for (int k = 1; k <= hh; k++) {                                   // red cone leaning right
            const int l = hx0 + k * hw / (2 * hh) + k / 2, r = hx1 - k * hw / (2 * hh) + k;
            for (int x = l; x <= (r > l ? r : l); x++) mark(head - k, x, 1);
            tip = r > l ? r : l;
        }
        mark(head - hh - 1, tip + 1, 2); mark(head - hh, tip + 1, 2); mark(head - hh - 1, tip + 2, 2);
        break;
    }
    case SPLASH_COSTUME_CARNAVAL: {
        for (int y = y0; y <= y1; y++)
            for (int x = 0; x < w; x++)
                if (torso(y, x)) mark(y, x, y < ym ? (((x + y) & 1) ? 1 : 2) : y == ym ? 3 : ((x & 1) ? 4 : 2));
        for (int x = hx0 - 1; x <= hx1 + 1; x++) mark(head, x, 1);      // gold band
        static const uint8_t plume_color[5] = {2, 4, 3, 4, 2};
        const int tall = hw / 2 + 3;
        for (int i = 0; i < 5; i++) {
            const int bx = hx0 + i * (hw - 2) / 4, lean = i - 2;
            const int height = tall - (lean < 0 ? -lean : lean);
            for (int k = 1; k <= height; k++) {
                const int x = bx + lean * k / 4;
                mark(head - k, x, plume_color[i]);
                if (hw >= 10 && k < height - 1) mark(head - k, x + 1, plume_color[i]);
            }
        }
        break;
    }
    case SPLASH_COSTUME_HALLOWEEN: {
        for (int y = y0; y <= y1; y++) for (int x = 0; x < w; x++) if (torso(y, x)) mark(y, x, y == y0 ? 2 : 1);
        for (int x = hx0 - 2; x <= hx1 + 2; x++) mark(head, x, 3);      // wide brim
        for (int x = hx0 + 1; x <= hx1 - 1; x++) mark(head - 1, x, 2);  // orange band
        const int hh = hw / 2 + 1;
        for (int k = 0; k < hh; k++) {                                    // pointed hat, tip bent right
            const int inset = 2 + k * (hw - 4) / (2 * hh);
            const int l = hx0 + inset + k / 3, r = hx1 - inset + k / 2;
            for (int x = l; x <= (r > l ? r : l); x++) mark(head - 2 - k, x, 3);
        }
        break;
    }
    default: break;
    }
    return true;
}

static inline uint16_t costume_color(uint8_t m, uint16_t base) {
    return m ? COSTUME_COLORS[costume][m] : base;
}

static uint8_t stage_costume[GRID * GRID];
static bool    stage_has_costume = false;

// The official 55×37 art stage sits at a fixed anchor on the 60×60 grid, and
// every animation is placed at its authored stage offset (ox/oy) — never
// centered per-animation. All animations share one idle-Clawd position
// (x 15..38, y 21..36 in stage cells), so transitions between them are
// seamless; centering per-crop would make the still pose jump around.
#define STAGE_ANCHOR_X ((GRID - 55) / 2)
#define STAGE_ANCHOR_Y ((GRID - 37) / 2)

// ─── Playback: intro → loop → outro ─────────────────────────────────────────
// Every animation carries a loop region (converter-detected gait cycles and
// scene middles; whole file when nothing repeats). Playback holds the loop
// until released — walkers release on arrival at their target x, scenes after
// SCENE_LOOP_MS — then the outro (pack-away, gait exit) plays and the
// animation completes on its idle bookend. Rotation never hard-cuts: it
// releases the loop and switches after the outro, so transitions always
// happen from the shared idle pose.
static bool     pb_done = false;        // completed; holding idle frame 0
static bool     in_loop = false;
static bool     loop_release = false;
static uint32_t loop_entered_ms = 0;
static bool     pending_pick = false;   // rotate requested; honor at completion
#define SCENE_LOOP_MS 6000

// ─── Walk translation ────────────────────────────────────────────────────────
// The walk gaits animate in place; screen travel is ours, locked to the feet:
// per-frame movement equals the measured backward drift of the planted feet,
// so planted feet stay put on screen.
//   crab walking (8-frame scuttle loop [1..8]): surges of 1 cell entering
//     frames 4, 5, 8 and the cycle wrap — 4 cells / 640 ms (6.25 cells/s).
//   walking (5-frame waddle loop [2..6]): 1,1,1,1,2 cells → 6 cells / 450 ms
//     (~13.3 cells/s).
// walk_begin(target) plays intro → gait loop, clamps to land exactly on the
// target, then releases the loop so the gait exits and Clawd stands. When
// walking left the frame is mirrored (eyes lead); facing persists standing.
// DEMO: until the BLE-driven state machine exists, a choreography loops
// stand → right edge → off-screen left → re-enter home.
enum WalkKind { WALK_NONE, WALK_CRAB, WALK_FRONT };
static WalkKind walk_kind = WALK_NONE;
static bool    walk_active = false;
static int     walk_x = 0;         // stage x of the frame origin, may be < 0
static int     walk_dir = 0;       // -1 left, +1 right, 0 standing
static int     walk_target = 0;
static uint8_t walk_phase = 0;
static uint32_t walk_phase_started = 0;
static int     walk_home_x = 0;    // authored position to return to
static int     walk_face = +1;     // facing, kept while standing (-1 = left)

// Cells the body moves when the gait advances INTO `frame` (see banner).
static int walk_gait_cells_k(WalkKind kind, uint16_t frame, bool from_loop) {
    if (kind == WALK_CRAB) {
        if (frame == 1) return from_loop ? 1 : 0;     // cycle wrap, mid-surge
        return (frame == 4 || frame == 5 || frame == 8) ? 1 : 0;
    }
    if (kind == WALK_FRONT) {
        if (frame < 2 || frame > 6) return 0;         // idle / wind-up / outro
        if (frame == 2 && !from_loop) return 0;       // first plant
        return (frame == 6) ? 2 : 1;
    }
    return 0;
}
static int walk_gait_cells(uint16_t frame, bool from_loop) {
    return walk_gait_cells_k(walk_kind, frame, from_loop);
}

static void anim_reset(const splash_anim_def_t *a) {
    pb_done = false;
    in_loop = false;
    loop_release = false;
    pending_pick = false;
    walk_active = false;
    walk_kind = WALK_NONE;
    if (strcmp(a->name, "crab walking") == 0) walk_kind = WALK_CRAB;
    else if (strcmp(a->name, "walking") == 0) walk_kind = WALK_FRONT;
    else return;
    walk_active = true;
    walk_home_x = STAGE_ANCHOR_X + a->ox;
    walk_x = walk_home_x;
    walk_dir = 0;
    walk_face = +1;
    walk_phase = 0;
    walk_phase_started = millis();
    pb_done = true;    // walkers start standing; the choreography sets off
}

static const uint8_t* compose_stage(const splash_anim_def_t *a, uint16_t frame);
static void render_frame(const uint8_t *cells, const uint16_t *palette);

// Start walking toward `target` (stage x of the frame origin).
static void walk_begin(int target) {
    if (target == walk_x) return;          // already there; stay standing
    walk_target = target;
    walk_dir = (target > walk_x) ? +1 : -1;
    walk_face = walk_dir;
    cur_frame = 0;
    frame_started_ms = millis();
    pb_done = false;
    loop_release = false;
    in_loop = false;
}

// Demo choreography: advance phases whenever the current walk has completed.
static void walk_choreo(const splash_anim_def_t *a) {
    if (!pb_done) return;
    const uint32_t now = millis();
    switch (walk_phase) {
        case 0:  // standing at home
            if (now - walk_phase_started > 1200) { walk_phase = 1; walk_begin(GRID - a->w); }
            break;
        case 1:  // arrived at the right edge
            walk_phase = 2; walk_phase_started = now;
            break;
        case 2:  // standing at the edge
            if (now - walk_phase_started > 1200) { walk_phase = 3; walk_begin(-a->w); }
            break;
        case 3:  // fully off-screen left
            walk_phase = 4; walk_phase_started = now;
            break;
        case 4:  // hold off-screen (empty stage)
            if (now - walk_phase_started > 800) { walk_phase = 5; walk_begin(walk_home_x); }
            break;
        case 5:  // back home
            walk_phase = 0; walk_phase_started = now;
            break;
    }
}

static const uint8_t* compose_stage(const splash_anim_def_t *a, uint16_t frame) {
    memset(stage_cells, 0, sizeof(stage_cells));
    // Horizontal edge snap: art touching its canvas's left/right edge was
    // designed to hang off that edge (lurking peeks in from the left), so it
    // goes to the true screen edge instead of the anchored stage edge. Not
    // applied vertically — every animation touches the stage bottom, and
    // vertical placement should stay anchored (rounded panel corners).
    int ax = STAGE_ANCHOR_X + a->ox;
    if (a->ox == 0)           ax = 0;
    if (a->ox + a->w == 55)   ax = GRID - a->w;
    if (walk_active)          ax = walk_x;
    const bool mirror = walk_active && walk_face < 0;
    const int ay = STAGE_ANCHOR_Y + a->oy;
    const uint8_t *src = &a->frames[(size_t)frame * a->w * a->h];
    for (int r = 0; r < a->h; r++) {
        const int dy = ay + r;
        if (dy < 0 || dy >= GRID) continue;
        int c0 = 0, c1 = a->w;                 // clip for partial off-screen x
        if (ax + c0 < 0)     c0 = -ax;
        if (ax + c1 > GRID)  c1 = GRID - ax;
        if (c0 >= c1) continue;
        if (mirror) {
            for (int c = c0; c < c1; c++)
                stage_cells[dy * GRID + ax + c] = src[r * a->w + (a->w - 1 - c)];
        } else {
            memcpy(&stage_cells[dy * GRID + ax + c0], &src[r * a->w + c0], c1 - c0);
        }
    }
    return stage_cells;
}

static void resolve_group_lists(void) {
    for (int g = 0; g < GROUP_COUNT; g++) {
        group_size[g] = 0;
        for (int s = 0; s < GROUP_MAX; s++) {
            group_lists[g][s] = -1;
            const char* want = GROUP_NAMES[g][s];
            if (!want) continue;
            for (int i = 0; i < SPLASH_ANIM_COUNT; i++) {
                if (strcmp(splash_anims[i].name, want) == 0) {
                    group_lists[g][group_size[g]++] = (int8_t)i;
                    break;
                }
            }
        }
    }
}

static uint16_t *row_buf = NULL;   // scratch row, sized to canvas_w (PSRAM path)

// ─── Two render paths ────────────────────────────────────────────────────────
// PSRAM boards (S3) draw the pixel art into an LVGL canvas at native size and
// let LVGL flush it — they have the RAM and cores to spare, no transform needed.
//
// PSRAM-less boards (C6) can't hold a 480×480 canvas. The prior approach (tiny
// 20×20 canvas + LVGL image-scale) made LVGL software-transform the whole
// upscaled frame on every redraw — measured ~0.76 µs/output-px, i.e. 100–220 ms
// per frame on the single-core C6, and partial invalidation of a transformed
// image both fails to clip the transform and smears. Instead we upscale the
// stage cells ourselves with trivial nearest-neighbour replication and push only
// the *changed* cells straight to the panel via the display HAL, bypassing LVGL.
// That removes the transform cost (leaving just the QSPI flush) and the
// dirty-rect is exact, so no smearing.
#ifndef BOARD_HAS_PSRAM
#  define SPLASH_DIRECT_DRAW 1
#else
#  define SPLASH_DIRECT_DRAW 0
#endif

#if SPLASH_DIRECT_DRAW
static uint16_t*       strip_buf = NULL;   // one grid-row band: (GRID*scr_cell)×scr_cell
static int             scr_cell  = 24;     // on-screen px per grid cell
static int             scr_offx  = 0;      // centering offsets (square art on panel)
static int             scr_offy  = 0;
static uint8_t         prev_cells[GRID * GRID];
static const uint16_t* prev_palette = NULL;
static bool            prev_valid   = false;
static bool            force_full   = false;  // repaint everything on the next render

// Upscale grid cells [gx0..gx1]×[gy0..gy1] and push them to the panel, one
// grid-row band at a time so the scratch buffer stays (GRID*scr_cell × scr_cell).
static void blit_cells(const uint8_t* cells, const uint16_t* palette,
                       int gx0, int gy0, int gx1, int gy1) {
    if (!strip_buf) return;
    const int spc = scr_cell;
    const int bw  = (gx1 - gx0 + 1) * spc;          // band width, px
    const int px  = scr_offx + gx0 * spc;
    for (int gy = gy0; gy <= gy1; gy++) {
        for (int gx = gx0; gx <= gx1; gx++) {       // expand one source row across
            uint8_t code = cells[gy * GRID + gx];
            uint16_t color = (palette && code < SPLASH_PALETTE_SIZE) ? palette[code] : COL_EMPTY;
            if (stage_has_costume) color = costume_color(stage_costume[gy * GRID + gx], color);
            uint16_t* p = &strip_buf[(gx - gx0) * spc];
            for (int i = 0; i < spc; i++) p[i] = color;
        }
        for (int dy = 1; dy < spc; dy++)             // replicate that row down
            memcpy(&strip_buf[dy * bw], strip_buf, bw * 2);
        display_hal_draw_bitmap(px, scr_offy + gy * spc, bw, spc, strip_buf);
    }
}

static void render_frame(const uint8_t *cells, const uint16_t *palette) {
    if (!strip_buf) return;
    if (!active) return;          // never draw to the panel while not shown
    stage_has_costume = costume_mask(cells, GRID, GRID, palette, stage_costume);
    bool full = force_full || !prev_valid || palette != prev_palette;
    force_full = false;

    int gx0 = 0, gy0 = 0, gx1 = GRID - 1, gy1 = GRID - 1;
    if (!full) {                                     // bounding box of changed cells
        gx0 = GRID; gy0 = GRID; gx1 = -1; gy1 = -1;
        for (int gy = 0; gy < GRID; gy++)
            for (int gx = 0; gx < GRID; gx++)
                if (cells[gy * GRID + gx] != prev_cells[gy * GRID + gx]) {
                    if (gx < gx0) gx0 = gx;
                    if (gx > gx1) gx1 = gx;
                    if (gy < gy0) gy0 = gy;
                    if (gy > gy1) gy1 = gy;
                }
        if (gx1 < 0) return;                         // identical frame, nothing to do
    }

    blit_cells(cells, palette, gx0, gy0, gx1, gy1);

    memcpy(prev_cells, cells, GRID * GRID);
    prev_palette = palette;
    prev_valid   = true;
}

#else  // ── PSRAM: LVGL canvas render (unchanged) ──

static void render_frame(const uint8_t *cells, const uint16_t *palette) {
    if (!row_buf || !canvas_buf) return;
    stage_has_costume = costume_mask(cells, GRID, GRID, palette, stage_costume);
    for (int gy = 0; gy < GRID; gy++) {
        for (int gx = 0; gx < GRID; gx++) {
            uint8_t code = cells[gy * GRID + gx];
            uint16_t color = (palette && code < SPLASH_PALETTE_SIZE) ? palette[code] : COL_EMPTY;
            if (stage_has_costume) color = costume_color(stage_costume[gy * GRID + gx], color);
            uint16_t *p = &row_buf[gx * cell];
            for (int i = 0; i < cell; i++) p[i] = color;
        }
        for (int dy = 0; dy < cell; dy++) {
            memcpy(&canvas_buf[(gy * cell + dy) * canvas_w], row_buf, canvas_w * 2);
        }
    }
    if (canvas) lv_obj_invalidate(canvas);
}
#endif

// ---- Mini creature: a small animated creature for embedding in other screens
//      (e.g. the idle "sleeping" indicator). Self-contained — its own canvas and
//      buffer, independent of the full-screen splash above. ----
static lv_obj_t  *mini_canvas = NULL;
static uint16_t  *mini_buf = NULL;
static int        mini_cell = 0;
static int        mini_w = 0;      // canvas px, mini_anim->w * mini_cell
static int        mini_h = 0;
static const splash_anim_def_t *mini_anim = NULL;
static uint16_t   mini_frame = 0;
static uint32_t   mini_started = 0;

static void mini_render(void) {
    if (!mini_buf || !mini_anim) return;
    const int aw = mini_anim->w, ah = mini_anim->h + COSTUME_PAD;   // headroom rows on top
    static uint8_t cells[64 * 80], mask[64 * 80];
    if (aw * ah > (int)sizeof(cells)) return;
    memset(cells, 0, (size_t)aw * COSTUME_PAD);
    memcpy(cells + aw * COSTUME_PAD, &mini_anim->frames[(size_t)mini_frame * aw * mini_anim->h],
           (size_t)aw * mini_anim->h);
    const uint16_t *pal = mini_anim->palette;
    const bool dressed = costume_mask(cells, aw, ah, pal, mask);
    for (int gy = 0; gy < ah; gy++) {
        for (int gx = 0; gx < aw; gx++) {
            uint8_t code = cells[gy * aw + gx];
            uint16_t color = (pal && code < SPLASH_PALETTE_SIZE) ? pal[code] : COL_EMPTY;
            if (dressed) color = costume_color(mask[gy * aw + gx], color);
            for (int dy = 0; dy < mini_cell; dy++) {
                uint16_t *dst = &mini_buf[(gy * mini_cell + dy) * mini_w + gx * mini_cell];
                for (int dx = 0; dx < mini_cell; dx++) dst[dx] = color;
            }
        }
    }
    if (mini_canvas) lv_obj_invalidate(mini_canvas);
}

lv_obj_t* splash_mini_create(lv_obj_t *parent, const char *anim_name, int px) {
    mini_anim = NULL;
    for (int i = 0; i < SPLASH_ANIM_COUNT; i++) {
        if (strcmp(splash_anims[i].name, anim_name) == 0) { mini_anim = &splash_anims[i]; break; }
    }
    if (!mini_anim) return NULL;
    const int amax = (mini_anim->w > mini_anim->h) ? mini_anim->w : mini_anim->h;
    mini_cell = px / amax;
    if (mini_cell < 1) mini_cell = 1;
    mini_w = mini_anim->w * mini_cell;
    mini_h = (mini_anim->h + COSTUME_PAD) * mini_cell;   // headroom for costume hats
#ifdef BOARD_HAS_PSRAM
    const uint32_t caps = MALLOC_CAP_SPIRAM;
#else
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#endif
    mini_buf = (uint16_t*)heap_caps_malloc(mini_w * mini_h * 2, caps);
    if (!mini_buf) return NULL;
    mini_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(mini_canvas, mini_buf, mini_w, mini_h, LV_COLOR_FORMAT_RGB565);
    mini_frame = 0;
    mini_started = millis();
    mini_render();
    return mini_canvas;
}

void splash_mini_tick(void) {
    if (!mini_buf || !mini_anim || mini_anim->frame_count == 0) return;
    if (millis() - mini_started < mini_anim->holds[mini_frame]) return;
    mini_started = millis();
    mini_frame = (mini_frame + 1) % mini_anim->frame_count;
    mini_render();
}

// ─── Corner mascot (usage screen) ────────────────────────────────────────────
// The corner logo slot, alive: the still Clawd idles, occasionally does a
// small act (waving, dancing, pointing) in place, and every few acts walks
// off the left edge, does the full-size lurking animation over the screen,
// and walks back into the slot. PSRAM boards only (ui.cpp falls back to the
// static clawd_still.h icon on the C6); driven by splash_mascot_tick() from
// the main loop, independent of the splash screen itself.
static lv_obj_t *mas_img = NULL;
static lv_obj_t *mas_lurk_img = NULL;
static uint8_t  *mas_buf = NULL;       // planar RGB565A8, sized for largest act
static uint8_t  *mas_lurk_buf = NULL;
static lv_image_dsc_t mas_dsc, mas_lurk_dsc;
static int  mas_cell = 3;
static int  mas_slot_x = 0;            // px of the slot (walk-in target)
static int  mas_feet_y = 0;            // px feet line (all art is bottom-anchored)
static int  mas_lurk_cell = 8;
static int  mas_screen_w = 480;
static bool mas_visible = false;

enum MasMode { MAS_STILL, MAS_ACT, MAS_WALK_OFF, MAS_LURK, MAS_WALK_IN, MAS_KIRO, MAS_ALMIRANTE };
// The corner slot alternates Clawd with the Kiro ghost. Turns only hand over
// while Clawd is idle, so an act or lurk trip always finishes first.
#ifndef MAS_CLAWD_TURN_MS
#define MAS_CLAWD_TURN_MS 30000
#endif
#ifndef MAS_KIRO_TURN_MS
#define MAS_KIRO_TURN_MS  25000
#endif
// On Vasco match days the Almirante takes a turn after the ghost, standing in
// the slot with his own animation (fist pump, blink, breathing).
#ifndef MAS_ALMIRANTE_TURN_MS
#define MAS_ALMIRANTE_TURN_MS 20000
#endif
static uint32_t mas_turn_started = 0;
// Ghost trip inside its turn: float in the slot, drift off left, peek in big
// from the right edge, then float back into the slot.
enum KiroPhase { KP_FLOAT, KP_OFF, KP_PEEK_IN, KP_PEEK_HOLD, KP_PEEK_OUT, KP_IN, KP_REST };
#ifndef KP_FLOAT_MS
#define KP_FLOAT_MS     6000
#endif
#define KP_GLIDE_PX_S   140
#define KP_PEEK_SLIDE_MS 600
#define KP_PEEK_HOLD_MS 2200
static KiroPhase kp = KP_FLOAT;
static uint32_t  kp_started = 0, kp_moved_ms = 0;
static int       kp_slot_x = 0;
static MasMode mas_mode = MAS_STILL;
static const splash_anim_def_t *mas_anim = NULL;
static uint16_t mas_frame = 0;
static uint32_t mas_frame_started = 0;
static uint32_t mas_mode_started = 0;
static int  mas_x = 0;                 // widget x, px (may be off-screen)
static int  mas_face = +1;
static uint8_t mas_act_idx = 0;
static bool mas_from_loop = false;

// The corner mascot mirrors the splash's excitement: per usage-rate group,
// how long he idles between acts and which acts he does. "lurking" means the
// walk-off / full-size-lurk / walk-back trip. Acts must fit the 28×21-cell
// buffer (jumps are too tall for the corner).
static const char* MAS_ACTS_BY_RATE[4][4] = {
    { "pointing", "lurking", NULL,       NULL      },   // idle: sparse, sneaky
    { "waving",   "lurking", "pointing", NULL      },   // normal
    { "waving",   "dancing", "lurking",  NULL      },   // active
    { "dancing",  "waving",  "dancing",  "lurking" },   // heavy: can't sit still
};
static const uint16_t MAS_STILL_MS_BY_RATE[4] = { 10000, 7000, 5000, 3500 };

static const splash_anim_def_t* anim_by_name(const char *n) {
    for (int i = 0; i < SPLASH_ANIM_COUNT; i++)
        if (strcmp(splash_anims[i].name, n) == 0) return &splash_anims[i];
    return NULL;
}

// Render one frame into a planar RGB565A8 image (alpha 0 outside the art) and
// anchor the widget on the shared feet line.
static void mas_render(const splash_anim_def_t *a, uint16_t frame, bool mirror,
                       lv_image_dsc_t *dsc, uint8_t *buf, lv_obj_t *img,
                       int cell, int x, int feet_y) {
    // Hats need headroom: Clawd frames get COSTUME_PAD empty rows on top.
    const int pad = (costume_has_hat() && body_code_of(a->palette) >= 0) ? COSTUME_PAD : 0;
    const int gw = a->w, gh = a->h + pad;
    static uint8_t cells[64 * 80], mask[64 * 80];
    if (gw * gh > (int)sizeof(cells)) return;
    memset(cells, 0, (size_t)gw * pad);
    memcpy(cells + gw * pad, &a->frames[(size_t)frame * a->w * a->h], (size_t)a->w * a->h);
    const bool dressed = costume_mask(cells, gw, gh, a->palette, mask);
    const int w = gw * cell, h = gh * cell;
    uint16_t *color = (uint16_t*)buf;
    uint8_t  *alpha = buf + (size_t)w * h * 2;
    const uint8_t *src = cells;
    for (int gy = 0; gy < gh; gy++) {
        for (int gx = 0; gx < gw; gx++) {
            const int sx = mirror ? gw - 1 - gx : gx;
            uint8_t code = src[gy * gw + sx];
            const uint8_t m = dressed ? mask[gy * gw + sx] : 0;
            uint16_t c = (code && code < SPLASH_PALETTE_SIZE) ? a->palette[code] : 0;
            if (m) c = costume_color(m, c);
            uint8_t  al = (code || m) ? 255 : 0;
            for (int dy = 0; dy < cell; dy++) {
                uint16_t *cp = &color[(gy * cell + dy) * w + gx * cell];
                uint8_t  *ap = &alpha[(gy * cell + dy) * w + gx * cell];
                for (int dx = 0; dx < cell; dx++) { cp[dx] = c; ap[dx] = al; }
            }
        }
    }
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = buf;
    dsc->data_size = (size_t)w * h * 3;
    lv_image_set_src(img, dsc);
    lv_obj_set_pos(img, x, feet_y - h);
    lv_obj_invalidate(img);
}

static void mas_show_still(void) {
    mas_anim = anim_by_name("walking");     // frame 0 == the official still pose
    mas_frame = 0;
    mas_mode = MAS_STILL;
    mas_mode_started = millis();
    mas_x = mas_slot_x;
    mas_face = +1;
    if (mas_anim)
        mas_render(mas_anim, 0, false, &mas_dsc, mas_buf, mas_img,
                   mas_cell, mas_x, mas_feet_y);
}

lv_obj_t* splash_mascot_create(lv_obj_t *parent, int slot_x, int feet_y, int cell) {
    mas_cell = cell;
    mas_slot_x = slot_x;
    mas_feet_y = feet_y;
    mas_screen_w = board_caps().width;
    // Buffer for the largest act bbox (pointing, 28×21 cells) plus hat headroom.
    const size_t mas_bytes = (size_t)(28 * cell) * ((21 + COSTUME_PAD) * cell) * 3;
    const splash_anim_def_t *lurk = anim_by_name("lurking");
    const BoardCaps& c = board_caps();
    int mind = (c.width < c.height) ? c.width : c.height;
    mas_lurk_cell = mind / SPLASH_GRID;
    if (mas_lurk_cell < 1) mas_lurk_cell = 1;
    size_t lurk_bytes = lurk ?
        (size_t)(lurk->w * mas_lurk_cell) * ((lurk->h + COSTUME_PAD) * mas_lurk_cell) * 3 : 0;
    const size_t ghost_bytes =                          // the Kiro ghost peeks in with this buffer too
        (size_t)(kiro_ghost_anim.w * mas_lurk_cell) * (kiro_ghost_anim.h * mas_lurk_cell) * 3;
    if (lurk_bytes && ghost_bytes > lurk_bytes) lurk_bytes = ghost_bytes;
    const int alm_big = mas_lurk_cell * 5 / 8 > 0 ? mas_lurk_cell * 5 / 8 : 1;   // alm_big_cell()
    const size_t alm_bytes =                            // the Almirante's big poses share it
        (size_t)(almirante_celebrate_anim.w * alm_big) * (almirante_celebrate_anim.h * alm_big) * 3;
    if (lurk_bytes && alm_bytes > lurk_bytes) lurk_bytes = alm_bytes;
    mas_buf      = (uint8_t*)heap_caps_malloc(mas_bytes,  MALLOC_CAP_SPIRAM);
    mas_lurk_buf = lurk_bytes ? (uint8_t*)heap_caps_malloc(lurk_bytes, MALLOC_CAP_SPIRAM) : NULL;
    if (!mas_buf) return NULL;
    mas_img = lv_image_create(parent);
    if (mas_lurk_buf) {
        mas_lurk_img = lv_image_create(parent);
        lv_obj_add_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
    }
    mas_show_still();
    return mas_img;
}


static void mas_start_kiro(uint32_t now) {
    mas_anim = kiro_outfit();                 // the day's costume (plain on ordinary days)
    mas_frame = 0;
    mas_frame_started = now;
    mas_from_loop = false;
    mas_face = +1;
    const splash_anim_def_t *still = anim_by_name("walking");   // center on Clawd's still pose
    kp_slot_x = mas_slot_x + (still ? (still->w - kiro_ghost_anim.w) * mas_cell / 2 : 0);
    mas_x = kp_slot_x;
    mas_mode = MAS_KIRO;
    mas_turn_started = now;
    kp = KP_FLOAT;
    kp_started = kp_moved_ms = now;
    mas_render(mas_anim, 0, false, &mas_dsc, mas_buf, mas_img, mas_cell, mas_x, mas_feet_y);
}

// The Almirante is drawn finer than Clawd (2 px cells on the large layout) so
// his face and hat read at the corner size; same feet line, centered on the slot.
static int alm_cell(void) { return mas_cell > 2 ? mas_cell - 1 : 1; }
// Big poses (peek at the right edge, goal celebration) use a smaller cell than
// the Kiro ghost's so his taller sprite fits the shared lurk buffer.
static int alm_big_cell(void) { return mas_lurk_cell * 5 / 8 > 0 ? mas_lurk_cell * 5 / 8 : 1; }
static int alm_slot_x(void) {
    const splash_anim_def_t *still = anim_by_name("walking");
    const int still_w = still ? still->w * mas_cell : 0;
    return mas_slot_x + (still_w - almirante_anim.w * alm_cell()) / 2;
}

// His corner turn: idle in the slot, walk off left, pop up big at the right
// edge celebrating, walk back in from the right, idle until the turn ends.
enum AlmPhase { AP_IDLE, AP_WALK_OFF, AP_PEEK_IN, AP_PEEK_HOLD, AP_PEEK_OUT, AP_WALK_IN, AP_REST };
#define ALM_IDLE_MS       6000
#define ALM_WALK_PX_S     55
#define ALM_PEEK_SLIDE_MS 500
#define ALM_PEEK_HOLD_MS  3200
static AlmPhase ap = AP_IDLE;
static uint32_t ap_started = 0, ap_moved_ms = 0;
static const splash_anim_def_t *alm_anim = &almirante_anim;
// Goal celebration (live match): big jumping Almirante in the bottom-left corner.
#define ALM_GOAL_MS 7000
static uint32_t alm_goal_until = 0;

static void alm_set_anim(const splash_anim_def_t *a, uint32_t now) {
    if (alm_anim == a) return;
    alm_anim = a;
    mas_frame = 0;
    mas_frame_started = now;
}
static bool alm_advance(uint32_t now) {        // true when the frame changed
    if (now - mas_frame_started < alm_anim->holds[mas_frame % alm_anim->frame_count]) return false;
    mas_frame = (mas_frame + 1) % alm_anim->frame_count;
    mas_frame_started = now;
    return true;
}
static void mas_render_almirante(void) {
    mas_render(alm_anim, mas_frame % alm_anim->frame_count, mas_face < 0, &mas_dsc, mas_buf, mas_img,
               alm_cell(), mas_x, mas_feet_y);
}
static void alm_draw_big(int x, int feet_y, bool mirror) {
    mas_render(alm_anim, mas_frame % alm_anim->frame_count, mirror, &mas_lurk_dsc, mas_lurk_buf,
               mas_lurk_img, alm_big_cell(), x, feet_y);
}
static void mas_start_almirante(uint32_t now) {
    alm_anim = &almirante_anim;
    mas_anim = &almirante_anim;
    mas_frame = 0;
    mas_frame_started = now;
    mas_face = +1;
    mas_x = alm_slot_x();
    mas_mode = MAS_ALMIRANTE;
    mas_turn_started = now;
    ap = AP_IDLE;
    ap_started = ap_moved_ms = now;
    mas_render_almirante();
}
static void alm_end_turn(uint32_t now) {
    if (mas_lurk_img) lv_obj_add_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
    mas_show_still();                           // back to Clawd
    mas_turn_started = now;
}
static void mas_almirante_tick(uint32_t now) {
    if (costume != SPLASH_COSTUME_VASCO) { alm_end_turn(now); return; }
    const uint32_t in_phase = now - ap_started;
    auto next = [&](AlmPhase p) { ap = p; ap_started = now; ap_moved_ms = now; };
    auto walk = [&](int target) -> bool {       // true once arrived
        const int step = (int)((now - ap_moved_ms) * ALM_WALK_PX_S / 1000);
        if (step > 0) {
            ap_moved_ms = now;
            const int d = target - mas_x;
            mas_x = (step >= abs(d)) ? target : mas_x + (d > 0 ? step : -step);
        }
        return mas_x == target;
    };
    const int big_w = almirante_celebrate_anim.w * alm_big_cell();
    const int big_h = almirante_celebrate_anim.h * alm_big_cell();
    const int peek_x = mas_screen_w - big_w * 3 / 4;       // three quarters of him shows
    const int big_feet = board_caps().height / 2 + big_h / 2;
    bool changed = alm_advance(now);

    switch (ap) {
    case AP_IDLE:
        alm_set_anim(&almirante_anim, now);
        if (in_phase >= ALM_IDLE_MS && mas_lurk_img && mas_lurk_buf) next(AP_WALK_OFF);
        break;
    case AP_WALK_OFF:
        alm_set_anim(&almirante_walk_anim, now);
        mas_face = -1;
        if (walk(-almirante_anim.w * alm_cell())) {
            lv_obj_add_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(mas_lurk_img);
            alm_set_anim(&almirante_celebrate_anim, now);
            next(AP_PEEK_IN);
        }
        changed = true;
        break;
    case AP_PEEK_IN:
    case AP_PEEK_OUT: {
        uint32_t t = in_phase > ALM_PEEK_SLIDE_MS ? ALM_PEEK_SLIDE_MS : in_phase;
        if (ap == AP_PEEK_OUT) t = ALM_PEEK_SLIDE_MS - t;
        alm_draw_big(mas_screen_w - (int)((mas_screen_w - peek_x) * t / ALM_PEEK_SLIDE_MS), big_feet, true);
        if (in_phase >= ALM_PEEK_SLIDE_MS) {
            if (ap == AP_PEEK_IN) {
                next(AP_PEEK_HOLD);
            } else {
                lv_obj_add_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
                mas_x = mas_screen_w;
                alm_set_anim(&almirante_walk_anim, now);
                next(AP_WALK_IN);
            }
        }
        return;
    }
    case AP_PEEK_HOLD:
        if (changed) alm_draw_big(peek_x, big_feet, true);
        if (in_phase >= ALM_PEEK_HOLD_MS) next(AP_PEEK_OUT);
        return;
    case AP_WALK_IN:
        mas_face = -1;
        if (walk(alm_slot_x())) {
            mas_face = +1;
            alm_set_anim(&almirante_anim, now);
            next(AP_REST);
        }
        changed = true;
        break;
    case AP_REST:
        alm_set_anim(&almirante_anim, now);
        if (now - mas_turn_started >= MAS_ALMIRANTE_TURN_MS) { alm_end_turn(now); return; }
        break;
    }
    if (changed) mas_render_almirante();
}

// Goal: the Almirante jumps big in the bottom-left corner for a few seconds,
// over whatever the corner mascot was doing (it restarts clean afterwards).
void splash_almirante_goal(void) {
    if (!mas_lurk_img || !mas_lurk_buf) return;
    const uint32_t now = millis();
    alm_goal_until = now ? now + ALM_GOAL_MS : 1;
    alm_anim = &almirante_celebrate_anim;
    mas_frame = 0;
    mas_frame_started = now;
    lv_obj_clear_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(mas_lurk_img);
}
static bool alm_goal_tick(uint32_t now) {       // true while the celebration owns the mascots
    if (!alm_goal_until) return false;
    if (now >= alm_goal_until) {
        alm_goal_until = 0;
        lv_obj_add_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
        alm_anim = &almirante_anim;
        mas_show_still();
        mas_turn_started = now;
        return false;
    }
    // Bottom-left: the scores sit on the right of the live panel.
    if (alm_advance(now) || mas_frame == 0)
        alm_draw_big(12, board_caps().height - 8, false);
    return true;
}

static const int KP_BIG_FEET_Y_DIV = 2;   // big ghost centered vertically
static void kp_draw_big(int x) {
    const splash_anim_def_t *a = mas_anim;
    const int h = a->h * mas_lurk_cell;
    const int feet = board_caps().height / KP_BIG_FEET_Y_DIV + h / 2;
    mas_render(a, mas_frame, true, &mas_lurk_dsc, mas_lurk_buf, mas_lurk_img,
               mas_lurk_cell, x, feet);
}

static void mas_kiro_tick(uint32_t now) {
    const splash_anim_def_t *a = mas_anim;
    const int big_w = a->w * mas_lurk_cell;
    const int peek_x = mas_screen_w - big_w * 3 / 5;       // 60% of the big ghost shows
    const uint32_t in_phase = now - kp_started;
    bool new_frame = false;
    if (now - mas_frame_started >= a->holds[mas_frame]) {
        mas_frame = (mas_frame + 1) % a->frame_count;
        mas_frame_started = now;
        new_frame = true;
    }
    auto next = [&](KiroPhase p) { kp = p; kp_started = now; kp_moved_ms = now; };
    auto glide = [&](int target) -> bool {                  // true once arrived
        int step = (int)((now - kp_moved_ms) * KP_GLIDE_PX_S / 1000);
        if (step > 0) {
            kp_moved_ms = now;
            const int d = target - mas_x;
            mas_x = (step >= abs(d)) ? target : mas_x + (d > 0 ? step : -step);
        }
        return mas_x == target;
    };

    switch (kp) {
    case KP_FLOAT:
        if (in_phase >= KP_FLOAT_MS && mas_lurk_img && mas_lurk_buf) next(KP_OFF);
        break;
    case KP_OFF:
        if (glide(-a->w * mas_cell)) {
            lv_obj_add_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(mas_lurk_img);
            next(KP_PEEK_IN);
        }
        break;
    case KP_PEEK_IN:
    case KP_PEEK_OUT: {
        uint32_t t = in_phase > KP_PEEK_SLIDE_MS ? KP_PEEK_SLIDE_MS : in_phase;
        if (kp == KP_PEEK_OUT) t = KP_PEEK_SLIDE_MS - t;
        kp_draw_big(mas_screen_w - (int)((mas_screen_w - peek_x) * t / KP_PEEK_SLIDE_MS));
        if (in_phase >= KP_PEEK_SLIDE_MS) {
            if (kp == KP_PEEK_IN) {
                next(KP_PEEK_HOLD);
            } else {
                lv_obj_add_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
                mas_x = mas_screen_w;
                next(KP_IN);
            }
        }
        return;
    }
    case KP_PEEK_HOLD:
        if (new_frame) kp_draw_big(peek_x);
        if (in_phase >= KP_PEEK_HOLD_MS) next(KP_PEEK_OUT);
        return;
    case KP_IN:
        if (glide(kp_slot_x)) next(KP_REST);
        break;
    case KP_REST:
        if (now - mas_turn_started >= MAS_KIRO_TURN_MS) {
            if (costume == SPLASH_COSTUME_VASCO) {   // match day: the Almirante is next
                mas_start_almirante(now);
                return;
            }
            mas_show_still();                   // hand the slot back to Clawd
            mas_turn_started = now;
            return;
        }
        break;
    }
    mas_render(a, mas_frame, false, &mas_dsc, mas_buf, mas_img, mas_cell, mas_x, mas_feet_y);
}

void splash_mascot_set_visible(bool v) {
    mas_visible = v;
    if (!mas_img) return;
    if (v) {
        lv_obj_clear_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
        // The mascot walks over everything — keep him above later-created
        // siblings (battery icon, labels) whenever he's shown.
        lv_obj_move_foreground(mas_img);
        if (mas_lurk_img) lv_obj_move_foreground(mas_lurk_img);
        if (alm_goal_until) return;             // celebrating a goal: leave it on screen
        if (mas_mode == MAS_ALMIRANTE) {        // screen change mid-turn: keep him in the slot
            if (mas_lurk_img) lv_obj_add_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
            if (ap != AP_IDLE) ap = AP_REST;
            mas_x = alm_slot_x();
            mas_face = +1;
            alm_anim = &almirante_anim;
            mas_render_almirante();
            return;
        }
        if (mas_mode == MAS_KIRO) {             // screen change mid-turn: keep the ghost, in the slot
            if (mas_lurk_img) lv_obj_add_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
            if (kp != KP_FLOAT && kp != KP_REST) kp = KP_REST;
            mas_x = kp_slot_x;
            mas_render(mas_anim, mas_frame, false, &mas_dsc, mas_buf, mas_img,
                       mas_cell, mas_x, mas_feet_y);
            return;
        }
        mas_show_still();                       // restart clean at the slot
    } else {
        lv_obj_add_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
        if (mas_lurk_img) lv_obj_add_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
    }
}

void splash_mascot_tick(void) {
    if (!mas_img || !mas_visible || !mas_anim) return;
    const uint32_t now = millis();
    if (alm_goal_tick(now)) return;
    if (mas_mode == MAS_KIRO) { mas_kiro_tick(now); return; }
    if (mas_mode == MAS_ALMIRANTE) { mas_almirante_tick(now); return; }

    if (mas_mode == MAS_STILL) {
        if (now - mas_turn_started >= MAS_CLAWD_TURN_MS) {
            mas_start_kiro(now);
            return;
        }
        int g = usage_rate_group();
        if (g < 0 || g > 3) g = 0;
        if (now - mas_mode_started < MAS_STILL_MS_BY_RATE[g]) return;
        uint8_t count = 0;
        while (count < 4 && MAS_ACTS_BY_RATE[g][count]) count++;
        if (count == 0) { mas_mode_started = now; return; }
        const char *act = MAS_ACTS_BY_RATE[g][mas_act_idx++ % count];
        mas_frame = 0;
        mas_frame_started = now;
        mas_from_loop = false;
        if (strcmp(act, "lurking") == 0 && mas_lurk_img) {   // the lurk trip
            mas_anim = anim_by_name("walking");
            mas_face = -1;
            mas_mode = MAS_WALK_OFF;
        } else {
            const splash_anim_def_t *a = anim_by_name(act);
            if (!a) { mas_mode_started = now; return; }
            mas_anim = a;
            mas_face = +1;
            mas_mode = MAS_ACT;
        }
        return;
    }

    const splash_anim_def_t *a = mas_anim;
    if (now - mas_frame_started < a->holds[mas_frame]) return;
    mas_frame_started = now;

    uint16_t next = mas_frame + 1;
    const bool walking_mode = (mas_mode == MAS_WALK_OFF || mas_mode == MAS_WALK_IN);
    if (walking_mode && mas_frame == a->loop_end)
        next = a->loop_start;                       // walk: hold the gait loop

    if (next >= a->frame_count) {                   // act / lurk finished
        if (mas_mode == MAS_LURK) {
            lv_obj_add_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
            mas_anim = anim_by_name("walking");
            mas_frame = 0;
            mas_from_loop = false;
            mas_face = -1;                          // he lurked on the right,
            mas_x = mas_screen_w;                   // so he re-enters from it
            mas_mode = MAS_WALK_IN;
            lv_obj_clear_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        mas_show_still();                           // acts end on the idle pose
        return;
    }

    const bool from_loop = mas_from_loop;
    mas_frame = next;
    mas_from_loop = walking_mode &&
        mas_frame >= a->loop_start && mas_frame <= a->loop_end;

    if (walking_mode && mas_from_loop) {
        const int step = walk_gait_cells_k(WALK_FRONT, mas_frame, from_loop) * mas_cell;
        // Walk-off always exits left; walk-in heads toward the slot from
        // whichever side he's on (right, after the lurk trip).
        const int dir = (mas_mode == MAS_WALK_OFF) ? -1
                        : (mas_x < mas_slot_x ? +1 : -1);
        mas_face = (mas_mode == MAS_WALK_OFF) ? -1 : dir;
        mas_x += dir * step;
        if (mas_mode == MAS_WALK_OFF && mas_x <= -a->w * mas_cell) {
            // Fully off: hide the corner sprite, run the full-size lurk.
            lv_obj_add_flag(mas_img, LV_OBJ_FLAG_HIDDEN);
            const splash_anim_def_t *lurk = anim_by_name("lurking");
            if (lurk && mas_lurk_img && mas_lurk_buf) {
                mas_anim = lurk;
                mas_frame = 0;
                mas_mode = MAS_LURK;
                lv_obj_clear_flag(mas_lurk_img, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(mas_lurk_img);
                // He left stage left, so he peeks in from the RIGHT edge —
                // mirrored at render time (the art is authored left-edge).
                mas_render(lurk, 0, true, &mas_lurk_dsc, mas_lurk_buf,
                           mas_lurk_img, mas_lurk_cell,
                           mas_screen_w - lurk->w * mas_lurk_cell,
                           (STAGE_ANCHOR_Y + lurk->oy + lurk->h) * mas_lurk_cell);
            } else {
                mas_mode = MAS_WALK_IN;             // no lurk asset: turn back
                mas_face = +1;
            }
            return;
        }
        if (mas_mode == MAS_WALK_IN &&
            ((dir > 0 && mas_x >= mas_slot_x) || (dir < 0 && mas_x <= mas_slot_x))) {
            mas_show_still();                       // arrived: settle in the slot
            return;
        }
    }

    if (mas_mode == MAS_LURK) {
        mas_render(a, mas_frame, true, &mas_lurk_dsc, mas_lurk_buf, mas_lurk_img,
                   mas_lurk_cell, mas_screen_w - a->w * mas_lurk_cell,
                   (STAGE_ANCHOR_Y + a->oy + a->h) * mas_lurk_cell);
    } else {
        mas_render(a, mas_frame, mas_face < 0, &mas_dsc, mas_buf, mas_img,
                   mas_cell, mas_x, mas_feet_y);
    }
}

static void show_placeholder() {
    // Solid dark background + centered status label. On the direct-draw path
    // there's no canvas; the black container is the background and the LVGL
    // label shows over it.
#if !SPLASH_DIRECT_DRAW
    if (canvas_buf) {
        for (int i = 0; i < canvas_w * canvas_h; i++) canvas_buf[i] = COL_EMPTY;
    }
    if (canvas) lv_obj_invalidate(canvas);
#endif
    if (label_status) lv_obj_clear_flag(label_status, LV_OBJ_FLAG_HIDDEN);
}

void splash_init(lv_obj_t *parent) {
    const BoardCaps& c = board_caps();

    // Shared full-screen black container — the splash background.
    splash_container = lv_obj_create(parent);
    lv_obj_set_size(splash_container, c.width, c.height);
    lv_obj_set_pos(splash_container, 0, 0);
    lv_obj_set_style_bg_color(splash_container, THEME_BG, 0);
    lv_obj_set_style_bg_opa(splash_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash_container, 0, 0);
    lv_obj_set_style_pad_all(splash_container, 0, 0);
    lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_SCROLLABLE);   // ui.cpp makes it draggable on PSRAM boards

#if SPLASH_DIRECT_DRAW
    // Direct-to-panel path (no PSRAM): no LVGL canvas. Compute on-screen cell
    // size + centering, and a scratch band buffer sized for one grid-row strip
    // across the square art (GRID*scr_cell × scr_cell). On the C6 that's
    // 480×24×2 ≈ 23 KB of internal SRAM.
    int mind = (c.width < c.height) ? c.width : c.height;
    scr_cell = mind / GRID;
    int side = GRID * scr_cell;
    scr_offx = (c.width  - side) / 2;
    scr_offy = (c.height - side) / 2;
    strip_buf = (uint16_t*)heap_caps_malloc((size_t)side * scr_cell * 2,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!strip_buf) {
        Serial.println("splash: strip buffer alloc failed");
        return;
    }
#else
    // PSRAM path: render into an LVGL canvas at native size (no transform).
    SplashGeometry geo = splash_compute_geometry(c.width, c.height, true);
    cell                = geo.cell;
    canvas_w            = geo.canvas_dim;
    canvas_h            = geo.canvas_dim;
    const int img_scale = geo.scale;

    canvas_buf = (uint16_t*)heap_caps_malloc(canvas_w * canvas_h * 2, MALLOC_CAP_SPIRAM);
    row_buf    = (uint16_t*)heap_caps_malloc(canvas_w * 2,            MALLOC_CAP_SPIRAM);
    if (!canvas_buf || !row_buf) {
        Serial.println("splash: failed to alloc canvas buffer");
        return;
    }

    canvas = lv_canvas_create(splash_container);
    lv_canvas_set_buffer(canvas, canvas_buf, canvas_w, canvas_h, LV_COLOR_FORMAT_RGB565);
    if (img_scale != SPLASH_SCALE_UNITY) {
        lv_image_set_antialias(canvas, false);
        lv_image_set_pivot(canvas, canvas_w / 2, canvas_h / 2);
        lv_image_set_scale(canvas, img_scale);
    }
    lv_obj_center(canvas);
#endif

    // Placeholder label (visible only when no animations are loaded)
    label_status = lv_label_create(splash_container);
    lv_label_set_text(label_status,
        "no animations loaded\n\n"
        "run tools/convert_official_clawd.js");
    lv_obj_set_style_text_font(label_status, &font_styrene_28, 0);
    lv_obj_set_style_text_color(label_status, lv_color_hex(0xb0aea5), 0);
    lv_obj_set_style_text_align(label_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_status);

    resolve_group_lists();

    if (SPLASH_ANIM_COUNT == 0) {
        show_placeholder();
    } else {
        lv_obj_add_flag(label_status, LV_OBJ_FLAG_HIDDEN);
#if !SPLASH_DIRECT_DRAW
        // PSRAM path pre-renders frame 0 into the canvas buffer. The direct
        // path draws nothing here — render_frame() bails while inactive, so the
        // splash never paints to the panel before it's actually shown.
        const splash_anim_def_t *a = &splash_anims[0];
        render_frame(compose_stage(a, 0), a->palette);
#endif
        frame_started_ms = millis();
    }

    lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
}

// ─── Kiro turn on the splash ────────────────────────────────────────────────
// Rotation alternates Clawd's animations with the Kiro ghost. The ghost stands
// on the same ground line as Clawd, bobs through its own frames, and glides
// through the walk demo route: home → right edge → off left → home.
#define KIRO_GLIDE_MS_PER_CELL 45
#define ALM_STAGE_MS_PER_CELL  110     // the Almirante walks the same route, slower
#define ALM_STAGE_CELEBRATE_MS 3200    // jump when he's back home
static bool     kiro_on = false;          // the ghost owns the splash stage
static bool     kiro_next = false;        // next rotation goes to the ghost
static bool     alm_on = false;           // the stage guest is the Almirante (match days)
static bool     alm_next = false;         // he follows the ghost's turn
static int      kiro_x = 0, kiro_home_x = 0, kiro_target = 0;
static int      kiro_face = +1;
static uint8_t  kiro_phase = 0;
static uint32_t kiro_phase_ms = 0, kiro_step_ms = 0;
static uint16_t kiro_frame = 0;
static uint32_t kiro_frame_ms = 0;

static const splash_anim_def_t *kiro_anim = &kiro_ghost_anim;   // current outfit (see kiro_outfit)

static const uint8_t* compose_kiro(void) {
    const splash_anim_def_t *a = kiro_anim;
    memset(stage_cells, 0, sizeof(stage_cells));
    const int ay = STAGE_ANCHOR_Y + 37 - a->h;      // Clawd's ground line
    const uint8_t *src = &a->frames[(size_t)kiro_frame * a->w * a->h];
    for (int r = 0; r < a->h; r++) {
        const int dy = ay + r;
        if (dy < 0 || dy >= GRID) continue;
        for (int c = 0; c < a->w; c++) {
            const int dx = kiro_x + c;
            if (dx < 0 || dx >= GRID) continue;
            stage_cells[dy * GRID + dx] = src[r * a->w + (kiro_face < 0 ? a->w - 1 - c : c)];
        }
    }
    return stage_cells;
}

static void kiro_splash_start(bool almirante = false) {
    kiro_on = true;
    alm_on = almirante;
    kiro_anim = almirante ? &almirante_anim : kiro_outfit();   // ghost wears the day's costume
    kiro_home_x = (GRID - kiro_anim->w) / 2;
    kiro_x = kiro_target = kiro_home_x;
    kiro_face = +1;
    kiro_phase = 0;
    kiro_frame = 0;
    kiro_phase_ms = kiro_frame_ms = kiro_step_ms = millis();
    last_pick_ms = kiro_phase_ms;
    render_frame(compose_kiro(), kiro_anim->palette);
}

// Next rotation step: Clawd pick → ghost turn → Almirante (Vasco match days) → Clawd.
static void splash_rotate(void) {
    if (kiro_next) {
        kiro_next = false;
        alm_next = costume == SPLASH_COSTUME_VASCO;
        kiro_splash_start();
        return;
    }
    if (alm_next && costume == SPLASH_COSTUME_VASCO) {
        alm_next = false;
        kiro_splash_start(true);
        return;
    }
    alm_next = false;
    kiro_next = true;
    kiro_on = false;
    alm_on = false;
    splash_pick_for_current_rate();
}

static void kiro_splash_tick(uint32_t now) {
    const splash_anim_def_t *a = kiro_anim;
    bool dirty = false;
    if (now - kiro_frame_ms >= a->holds[kiro_frame]) {
        kiro_frame = (kiro_frame + 1) % a->frame_count;
        kiro_frame_ms = now;
        dirty = true;
    }
    const bool arrived = kiro_x == kiro_target;
    if (alm_on) {                              // walks the route; celebrates once back home
        const splash_anim_def_t *want = !arrived ? &almirante_walk_anim
            : (kiro_phase == 6 && now - kiro_phase_ms < ALM_STAGE_CELEBRATE_MS) ? &almirante_celebrate_anim
            : &almirante_anim;
        if (want != kiro_anim) {
            kiro_anim = want;
            kiro_frame = 0;
            kiro_frame_ms = now;
            dirty = true;
        }
    }
    const uint32_t step_ms = alm_on ? ALM_STAGE_MS_PER_CELL : KIRO_GLIDE_MS_PER_CELL;
    if (!arrived && now - kiro_step_ms >= step_ms) {
        kiro_x += (kiro_target > kiro_x) ? 1 : -1;
        kiro_step_ms = now;
        dirty = true;
    }
    if (arrived) {
        const uint32_t held = now - kiro_phase_ms;
        auto glide = [&](int target, uint8_t next) {
            kiro_target = target;
            kiro_face = (target >= kiro_x) ? +1 : -1;
            kiro_phase = next;
            kiro_step_ms = now;
        };
        switch (kiro_phase) {
            case 0: if (held > 2500) glide(GRID - a->w, 1); break;               // float / stand at home
            case 1: kiro_phase = 2; kiro_phase_ms = now; break;                  // reached right edge
            case 2: if (held > 1200) glide(-a->w, 3); break;                     // float at the edge
            case 3: kiro_phase = 4; kiro_phase_ms = now; break;                  // off-screen left
            case 4: if (held > 800) glide(kiro_home_x, 5); break;                // empty stage
            case 5: kiro_phase = 6; kiro_phase_ms = now; kiro_face = +1; dirty = true; break;
            case 6: if (now - last_pick_ms >= SPLASH_ROTATE_INTERVAL_MS) { splash_rotate(); return; } break;
        }
    }
    if (dirty) render_frame(compose_kiro(), kiro_anim->palette);
}

void splash_tick(void) {
    if (!active || SPLASH_ANIM_COUNT == 0) return;
    const uint32_t now = millis();

#if SPLASH_DIRECT_DRAW
    // Deferred full repaint after a (re)show — runs now that LVGL has drawn the
    // black background this loop iteration.
    if (force_full) {
        const splash_anim_def_t *fa = &splash_anims[cur_anim];
        if (kiro_on)              render_frame(compose_kiro(), kiro_anim->palette);
        else if (fa->frame_count) render_frame(compose_stage(fa, cur_frame), fa->palette);
    }
#endif

    if (kiro_on) { kiro_splash_tick(now); return; }

    const splash_anim_def_t *a = &splash_anims[cur_anim];
    if (a->frame_count == 0) return;

    if (walk_active) walk_choreo(a);

    // Scenes: hold the loop for SCENE_LOOP_MS, then let the outro play.
    if (!walk_active && in_loop && !loop_release &&
        now - loop_entered_ms >= SCENE_LOOP_MS)
        loop_release = true;

    // Auto-rotate — never a hard cut. Walkers switch only while standing at
    // home; everything else releases its loop and switches after the outro.
    if (now - last_pick_ms >= SPLASH_ROTATE_INTERVAL_MS) {
        if (walk_active) {
            if (walk_phase == 0 && pb_done) splash_rotate();
        } else {
            loop_release = true;
            pending_pick = true;
            last_pick_ms = now;    // don't re-fire while the outro plays
        }
    }

    if (pb_done) return;                       // holding the idle frame
    if (now - frame_started_ms < a->holds[cur_frame]) return;

    // Advance one frame through intro → loop → outro.
    const bool from_loop = in_loop;
    uint16_t next = cur_frame + 1;
    if (cur_frame == a->loop_end && !loop_release)
        next = a->loop_start;

    if (next >= a->frame_count) {              // completed the file
        if (pending_pick) {
            pending_pick = false;
            splash_rotate();
            return;
        }
        if (walk_active) {                     // walk finished: stand
            cur_frame = 0;
            frame_started_ms = now;
            pb_done = true;
            render_frame(compose_stage(a, 0), a->palette);
            return;
        }
        next = 0;                              // replay from the intro
        loop_release = false;
    }

    cur_frame = next;
    frame_started_ms = now;
    const bool now_in = cur_frame >= a->loop_start && cur_frame <= a->loop_end;
    if (now_in && !from_loop) loop_entered_ms = now;
    in_loop = now_in;

    // Walk translation, locked to gait frames; clamp to land exactly on the
    // target, then release the loop so the gait exits.
    if (walk_active && walk_dir != 0 && in_loop) {
        walk_x += walk_dir * walk_gait_cells(cur_frame, from_loop);
        if ((walk_dir > 0 && walk_x >= walk_target) ||
            (walk_dir < 0 && walk_x <= walk_target)) {
            walk_x = walk_target;
            walk_dir = 0;
            loop_release = true;
        }
    }

    render_frame(compose_stage(a, cur_frame), a->palette);
}

void splash_next(void) {
    if (SPLASH_ANIM_COUNT == 0) return;
    kiro_on = false;
    alm_on = false;
    cur_anim = (cur_anim + 1) % SPLASH_ANIM_COUNT;
    cur_frame = 0;
    frame_started_ms = millis();
    last_pick_ms = frame_started_ms;
    const splash_anim_def_t *a = &splash_anims[cur_anim];
    anim_reset(a);
    render_frame(compose_stage(a, 0), a->palette);
    Serial.printf("splash: -> %s\n", a->name);
}

void splash_pick_for_current_rate(void) {
    if (SPLASH_ANIM_COUNT == 0) return;
    int g = usage_rate_group();
    if (g < 0 || g >= GROUP_COUNT) g = 0;
    if (group_size[g] == 0) return;

    uint8_t slot = group_rotation[g] % group_size[g];
    group_rotation[g]++;
    int8_t idx = group_lists[g][slot];
    if (idx < 0) return;

    cur_anim = (uint16_t)idx;
    cur_frame = 0;
    frame_started_ms = millis();
    last_pick_ms = frame_started_ms;
    const splash_anim_def_t *a = &splash_anims[cur_anim];
    anim_reset(a);
    render_frame(compose_stage(a, 0), a->palette);
}

// ─── Flying ghost (idle screen, Kiro's turn) ─────────────────────────────────
#define FLYER_PX_PER_S 90
static lv_obj_t      *flyer_img = NULL;
static lv_image_dsc_t flyer_dsc;
static uint8_t       *flyer_buf = NULL;
static int            flyer_cell = 4, flyer_x = INT32_MIN, flyer_dir = +1;
static uint16_t       flyer_frame = 0;
static uint32_t       flyer_frame_ms = 0, flyer_move_ms = 0;
static const splash_anim_def_t *flyer_anim = NULL;

lv_obj_t* splash_flyer_create(lv_obj_t *parent, int px) {
    flyer_cell = px / kiro_ghost_anim.h;
    if (flyer_cell < 1) flyer_cell = 1;
    const size_t bytes = (size_t)(kiro_ghost_anim.w * flyer_cell) * (kiro_ghost_anim.h * flyer_cell) * 3;
#ifdef BOARD_HAS_PSRAM
    flyer_buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#else
    flyer_buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    if (!flyer_buf) return NULL;
    flyer_img = lv_image_create(parent);
    lv_obj_add_flag(flyer_img, LV_OBJ_FLAG_HIDDEN);
    return flyer_img;
}

void splash_flyer_tick(int left, int right, int center_y) {
    if (!flyer_img || !flyer_buf) return;
    const uint32_t now = millis();
    // Same outfit as the corner ghost's current turn.
    const splash_anim_def_t *a = (mas_mode == MAS_KIRO && mas_anim) ? mas_anim : &kiro_ghost_anim;
    const int w = a->w * flyer_cell, h = a->h * flyer_cell;
    bool redraw = a != flyer_anim;
    flyer_anim = a;
    if (flyer_x == INT32_MIN) { flyer_x = left; flyer_move_ms = now; redraw = true; }
    if (now - flyer_frame_ms >= a->holds[flyer_frame % a->frame_count]) {
        flyer_frame = (flyer_frame + 1) % a->frame_count;
        flyer_frame_ms = now;
        redraw = true;
    }
    const int step = (int)((now - flyer_move_ms) * FLYER_PX_PER_S / 1000);
    if (step > 0) {
        flyer_move_ms = now;
        flyer_x += flyer_dir * step;
        if (flyer_x >= right - w) { flyer_x = right - w; flyer_dir = -1; redraw = true; }
        if (flyer_x <= left)      { flyer_x = left;      flyer_dir = +1; redraw = true; }
        if (!redraw) lv_obj_set_pos(flyer_img, flyer_x, center_y - h / 2);
    }
    if (redraw)
        mas_render(a, flyer_frame, flyer_dir < 0, &flyer_dsc, flyer_buf, flyer_img,
                   flyer_cell, flyer_x, center_y + h / 2);
}

bool splash_is_active(void) { return active; }

void splash_set_costume(int c) {
    if (c < 0 || c >= SPLASH_COSTUME_COUNT || c == costume) return;
    costume = c;
    // Redraw now so outfits appear/disappear without waiting for a new frame.
    if (active) {
#if SPLASH_DIRECT_DRAW
        force_full = true;
#else
        const splash_anim_def_t *a = &splash_anims[cur_anim];
        if (kiro_on) {
            kiro_anim = alm_on ? &almirante_anim : kiro_outfit();
            render_frame(compose_kiro(), kiro_anim->palette);
        } else if (a->frame_count) {
            render_frame(compose_stage(a, cur_frame), a->palette);
        }
#endif
    }
    if (mas_img && mas_anim) {
        if (mas_mode == MAS_ALMIRANTE && c != SPLASH_COSTUME_VASCO) mas_show_still();   // no match: no Almirante
        if (mas_mode == MAS_KIRO) mas_anim = kiro_outfit();
        if (mas_mode == MAS_STILL || mas_mode == MAS_KIRO)
            mas_render(mas_anim, mas_frame, mas_face < 0, &mas_dsc, mas_buf, mas_img, mas_cell, mas_x, mas_feet_y);
    }
    if (mini_buf && mini_anim) mini_render();
}

bool splash_kiro_on_screen(void) {
    if (active) return kiro_on && !alm_on;
    return mas_img && mas_visible && mas_mode == MAS_KIRO;
}

void splash_show(void) {
    kiro_on = false;
    alm_on = false;
    alm_next = false;
    kiro_next = true;                 // Clawd opens; the ghost gets the next turn
    splash_pick_for_current_rate();   // select animation; direct path defers the draw
    if (splash_container) lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    active = true;
#if SPLASH_DIRECT_DRAW
    // LVGL fills the container black once on unhide; that would erase a creature
    // drawn now. Defer the full repaint to the next splash_tick(), which runs
    // after lv_timer_handler() in the main loop.
    force_full = true;
#endif
}

void splash_hide(void) {
    if (splash_container) lv_obj_add_flag(splash_container, LV_OBJ_FLAG_HIDDEN);
    active = false;
}

lv_obj_t* splash_get_root(void) {
    return splash_container;
}
