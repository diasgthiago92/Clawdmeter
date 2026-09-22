#include "ui.h"
#include "splash.h"
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <time.h>
#include "logo.h"
#include "clawd_still.h"
#include "icons.h"
#include "periph_icons.h"
#include "social_icons.h"
#include "hal/board_caps.h"

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);
LV_FONT_DECLARE(font_mono_32);
LV_FONT_DECLARE(font_mono_18);

// Layout values computed from the active board's geometry. Populated once
// in ui_init() and treated as const for the rest of the program. Adding a
// new display size means extending compute_layout() with another
// breakpoint — never editing the screen-builder functions below.
struct Layout {
    int16_t scr_w, scr_h;
    int16_t margin;
    int16_t title_y;
    int16_t content_y;
    int16_t content_w;

    // Usage screen
    int16_t usage_panel_h;
    int16_t usage_panel_gap;
    int16_t usage_bar_y;
    int16_t usage_reset_y;
    int16_t usage_bar_h;
    const lv_font_t* usage_reset_font;
    bool    kiro_panel;              // room for a third (Kiro) usage panel; else Kiro goes on the status line
    // Proposta B rows (large layout): 4 provider rows with mini-bars.
    int16_t urow_h;                  // height of one provider row
    int16_t urow_gap;                // gap between rows
    int16_t urow_label_w;            // width reserved for the provider name (left)
    int16_t urow_minibar_h;          // thin mini-bar height
    const lv_font_t* urow_name_font; // provider name (Claude/Kiro/...)
    const lv_font_t* urow_pct_font;  // per-bar % number
    const lv_font_t* urow_win_font;  // "Daily"/"Weekly" caption + reset hint
    int16_t bar_h;
    int16_t panel_pad_x, panel_pad_y;
    int16_t pill_pad_x, pill_pad_y;
    const lv_font_t* title_font;     // screen title / clock
    const lv_font_t* pct_font;       // big percentage number
    const lv_font_t* ent_pct_font;   // enterprise spending number
    const lv_font_t* pill_font;      // "Current" / "Weekly" pill
    const lv_font_t* reset_font;     // "Resets in ..." line
    const lv_font_t* pace_font;      // enterprise "Under/On/Over pace" line
    const lv_font_t* anim_font;      // animated status line
    const lv_font_t* axis_font;      // chart axis + footnotes on history/models screens
    const lv_font_t* table_font;     // quote table rows (crypto)
    const lv_font_t* table_font_dense; // one step smaller, for tables with a name column (B3)
    int16_t anim_y;                  // status line offset from bottom
    bool    small_icons;             // 40px logo + 24px battery (vs 80/48) on small screens
    int16_t title_nudge;             // title x-shift balancing the corner logo
    int16_t logo_y;                  // logo top edge
    int16_t batt_y;                  // battery icon top edge
    int16_t batt_w;                  // battery icon width, for position math

    // Pairing hint / idle screen
    int16_t pair_y1, pair_y2, pair_y3;
    int16_t idle_px;                 // sleeping-creature size on the idle screen

    // Bluetooth screen
    int16_t bt_info_panel_h;
    int16_t bt_reset_zone_h;
    const lv_font_t* bt_title_font;
    const lv_font_t* bt_status_font;
    const lv_font_t* bt_device_font;
    const lv_font_t* bt_credit_1_font;
    const lv_font_t* bt_credit_2_font;
};
static Layout L = {};

// Pick layout values from the active board's pixel dimensions. The two
// existing boards happen to land on the two breakpoints below; new ports
// inherit the closer one — visually OK, may need a polish pass for
// pixel-perfect alignment but never blocks the port from booting.
static void compute_layout(const BoardCaps& c) {
    L.scr_w = c.width;
    L.scr_h = c.height;
    L.margin = 20;
    L.title_y = 30;

    // Values shared by the two original breakpoints; the small branch below
    // overrides them wholesale.
    L.bar_h = 24;
    L.usage_bar_h = 24;
    L.usage_reset_font = &font_styrene_28;
    L.kiro_panel = false;
    L.panel_pad_x = 16;
    L.panel_pad_y = 12;
    L.pill_pad_x = 18;
    L.pill_pad_y = 6;
    L.title_font   = &font_tiempos_56;
    L.pct_font     = &font_styrene_48;
    L.ent_pct_font = &font_tiempos_56;
    L.pill_font    = &font_styrene_28;
    L.reset_font   = &font_styrene_28;
    L.pace_font    = &font_styrene_16;
    L.anim_font    = &font_mono_32;
    L.anim_y = -15;
    L.small_icons = false;
    L.title_nudge = 16;
    L.logo_y = L.title_y - 10;
    L.batt_y = L.title_y;
    L.batt_w = ICON_BATTERY_W;
    L.pair_y1 = 40;
    L.pair_y2 = 120;
    L.pair_y3 = 160;
    L.idle_px = 160;

    if (c.height >= 460) {
        // Large layout — tuned for 480x480 (AMOLED-2.16).
        L.content_y = 100;
        L.axis_font = &font_styrene_20;
        L.table_font = &font_styrene_24;
        L.table_font_dense = &font_styrene_20;
        // Three shorter panels (Sessão, Semanal, Kiro) fill the space the
        // status line used to share.
        L.usage_panel_h = 112;
        L.usage_panel_gap = 10;
        L.usage_bar_y = 50;
        L.usage_reset_y = 64;
        L.usage_bar_h = 12;
        L.usage_reset_font = &font_styrene_20;
        L.kiro_panel = true;
        // Proposta B: 4 provider rows fill the space between content_y and the
        // status line. content_y=100, status line near the bottom → ~340px for
        // rows. 4 rows of 78 + 3 gaps of 10 = 342.
        L.urow_h = 78;
        L.urow_gap = 10;
        L.urow_label_w = 92;
        L.urow_minibar_h = 10;
        L.urow_name_font = &font_styrene_24;
        L.urow_pct_font = &font_styrene_24;
        L.urow_win_font = &font_styrene_14;
        L.bt_info_panel_h = 160;
        L.bt_reset_zone_h = 110;
        L.bt_title_font    = &font_tiempos_56;
        L.bt_status_font   = &font_styrene_48;
        L.bt_device_font   = &font_styrene_28;
        L.bt_credit_1_font = &font_styrene_24;
        L.bt_credit_2_font = &font_styrene_20;
    } else if (c.height >= 300) {
        // Compact layout — tuned for 368x448 (AMOLED-1.8).
        L.content_y = 85;
        L.axis_font = &font_styrene_16;
        L.table_font = &font_styrene_20;
        L.table_font_dense = &font_styrene_16;
        L.usage_panel_h = 130;
        L.usage_panel_gap = 12;
        L.usage_bar_y = 48;
        L.usage_reset_y = 78;
        L.bt_info_panel_h = 140;
        L.bt_reset_zone_h = 90;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_28;
        L.bt_device_font   = &font_styrene_20;
        L.bt_credit_1_font = &font_styrene_16;
        L.bt_credit_2_font = &font_styrene_14;
    } else {
        // Small layout — tuned for 240x240 (LCD-1.54 and similar square TFTs).
        // Everything shrinks: fonts two steps down, panels ~half height, and
        // the corner logo/battery switch to the 40px/24px small assets.
        L.margin = 8;
        L.title_y = 4;
        L.content_y = 44;
        L.axis_font = &font_styrene_12;
        L.table_font = &font_styrene_12;
        L.table_font_dense = &font_styrene_12;
        L.usage_panel_h = 74;
        L.usage_panel_gap = 6;
        L.usage_bar_y = 30;
        L.usage_reset_y = 46;
        L.bar_h = 12;
        L.panel_pad_x = 10;
        L.panel_pad_y = 6;
        L.pill_pad_x = 8;
        L.pill_pad_y = 2;
        L.title_font   = &font_tiempos_34;
        L.pct_font     = &font_styrene_24;
        L.ent_pct_font = &font_tiempos_34;
        L.pill_font    = &font_styrene_14;
        L.reset_font   = &font_styrene_14;
        L.pace_font    = &font_styrene_12;
        L.anim_font    = &font_mono_18;
        // Center the status line in the strip below the weekly panel; flush
        // against the bottom edge it reads as unevenly spaced.
        L.anim_y = -10;
        L.small_icons = true;
        L.title_nudge = 8;
        L.logo_y = 2;
        L.batt_y = 10;
        L.batt_w = ICON_BATTERY_SMALL_W;
        L.pair_y1 = 12;
        L.pair_y2 = 56;
        L.pair_y3 = 80;
        L.idle_px = 96;
        L.bt_info_panel_h = 90;
        L.bt_reset_zone_h = 60;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_20;
        L.bt_device_font   = &font_styrene_14;
        L.bt_credit_1_font = &font_styrene_12;
        L.bt_credit_2_font = &font_styrene_12;
    }

    L.content_w = L.scr_w - 2 * L.margin;
}

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG
#define COL_KIRO      lv_color_hex(0x9046FF)   // Kiro brand purple; Claude stays COL_ACCENT
static const char* const COL_HEX_CLAUDE = "d97757";
// Theme accent follows the character on screen: Claude orange, Kiro purple.
// Data colors (Claude vs Kiro series) stay fixed and don't use these.
static lv_color_t  accent_color = lv_color_hex(0xd97757);
static const char* accent_hex   = "d97757";
static const char* const COL_HEX_KIRO   = "9046ff";
#define COL_AG        lv_color_hex(0x64B5F6)   // Antigravity: light blue primary, white secondary
static const char* const COL_HEX_AG     = "64b5f6";
static const char* const COL_HEX_CODEX  = "10a37f";
#define COL_CODEX     lv_color_hex(0x10A37F)   // Codex: green primary, white secondary

// ---- Usage screen widgets (single non-splash view) ----
static lv_obj_t* usage_container;
static lv_obj_t* lbl_title;
static void fit_screen_title(lv_obj_t* t, const char* text);
// Clock fed by the daemon: base epoch (local wall-clock seconds) + the lv_tick at
// which it landed, so the title ticks forward locally between 60s payloads.
static long     clock_base_epoch = 0;
static uint32_t clock_base_ms = 0;
static int      clock_fmt = 24;   // 12 or 24, set from the daemon payload
static int      clock_last_min = -1;   // last rendered minute; avoids redrawing the title every tick
static lv_obj_t* usage_group;   // the two usage panels — shown when connected
static lv_obj_t* pair_group;    // pairing hint — shown when disconnected
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_label;
static lv_obj_t* lbl_session_reset;
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_label;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* panel_session = nullptr;
static lv_obj_t* panel_weekly = nullptr;
// Enterprise-only widgets inside panel_session
static lv_obj_t* lbl_session_pct_sym = nullptr;  // "%" in smaller font
static lv_obj_t* lbl_spending_desc = nullptr;     // "do orçamento mensal"
static lv_obj_t* lbl_spending_status = nullptr;   // "Abaixo do ritmo" / "No ritmo" / "Acima do ritmo"
static lv_obj_t* lbl_anim;      // status line: connection state + whimsical idle, or Kiro credits
static int kiro_pct = -1;       // Kiro credit usage from the daemon; < 0 = none
static int kiro_reset_days = 0;
static lv_obj_t* panel_kiro = nullptr;   // third usage panel (large layout only)
static lv_obj_t* lbl_kiro_pct;
static lv_obj_t* lbl_kiro_label;
static lv_obj_t* bar_kiro;
static lv_obj_t* lbl_kiro_reset;

// ---- Battery indicator (shared, on top) ----
static lv_obj_t* battery_img;
static lv_obj_t* periph_box;          // mouse / keyboard battery, top-right corner
static lv_obj_t* periph_icon[2];
static lv_obj_t* periph_pct[2];
static lv_image_dsc_t periph_dscs[2];
static bool      periph_known = false;
static void apply_battery_visibility(void);
static lv_obj_t* logo_img;
static lv_image_dsc_t battery_dscs[5];  // empty, low, medium, full, charging

// ---- Live-data freshness → which usage sub-view to show ----
// usage panels when data is flowing, an idle "Zzz" screen when the host is
// connected but no usage update landed within DATA_FRESH_MS, the pairing hint
// when BLE is down. Re-evaluated every loop in ui_tick_anim().
static lv_obj_t* idle_group;            // the "Zzz" idle screen
static uint32_t  last_data_ms = 0;      // lv_tick when the last valid usage update landed
static bool      data_received = false; // any valid update since boot
static bool      data_ok = true;        // last payload's ok flag; a {"ok":false} beat = "no fresh data"
static int       view_state = -1;       // -1 unknown / 0 pair / 1 idle / 2 usage
static const uint32_t DATA_FRESH_MS = 90000;  // usage counts as "live" within this window (daemon sends ~60s)

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static screen_t current_screen = SCREEN_USAGE;

// Auto-rotation: each screen's share of a 2-minute cycle. Clawd (splash) is
// not in the cycle — it only shows for a moment after boot or when tapped to.
struct RotationStep { screen_t screen; uint32_t ms; };
static const RotationStep ROTATION[] = {
    {SCREEN_USAGE,    90000},   // every screen stays at least 90 s
    {SCREEN_AGENDA,   90000},
    {SCREEN_HISTORY,  90000},
    {SCREEN_ROUTINES, 90000},
    {SCREEN_POSTS,    90000},
    {SCREEN_CRYPTO,   90000},
    {SCREEN_STOCKS,   90000},
    {SCREEN_RATES,    90000},
    {SCREEN_FIIS,     90000},
    {SCREEN_VASCO,    90000},
};
#define ROTATION_COUNT        (sizeof(ROTATION) / sizeof(ROTATION[0]))
#ifndef ROTATION_OFFCYCLE_MS
#define ROTATION_OFFCYCLE_MS  10000   // dwell for screens outside the cycle (splash)
#endif
#define ROTATION_TAP_PAUSE_MS 60000   // a tap holds the chosen screen this long
static uint32_t screen_shown_ms = 0;
static uint32_t tap_ms = 0;
static bool     tap_hold = false;
// Holding a finger still on the center for 2 s pauses auto-rotation; another
// 2 s hold resumes it, and so does 1 h of pause. Alerts (meeting, failed routine, live match) still show.
#define ROTATION_LOCK_HOLD_MS 2000
#ifndef ROTATION_LOCK_MAX_MS
#define ROTATION_LOCK_MAX_MS  3600000     // a pause left on for 1 h resumes by itself
#endif
static bool       rotation_locked = false;
static uint32_t   rotation_locked_ms = 0; // when the pause started
static uint32_t   hold_start_ms = 0;      // 0 = no touch; UINT32_MAX = touch can't toggle
static lv_point_t hold_point;
static bool       hold_toggled = false;   // this touch toggled the lock; its release isn't a tap
static lv_obj_t*  lock_toast = nullptr;
static uint32_t   lock_toast_ms = 0;

static uint32_t rotation_dwell_ms(screen_t screen) {
    for (const auto& step : ROTATION) {
        if (step.screen == screen) return step.ms;
    }
    return ROTATION_OFFCYCLE_MS;
}
static bool     s_ble_connected = false;   // cached BLE connection state
static uint32_t connected_at_ms = 0;       // when we last entered CONNECTED ("Connected" dwell)

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Pensando", "Ruminando", "Matutando",
    "Cozinhando", "Fermentando", "Maquinando",
    "Calculando", "Arquitetando", "Ponderando",
    "Refletindo", "Elucubrando", "Filosofando",
    "Tramando", "Tricotando", "Processando",
    "Destrinchando", "Garimpando", "Lapidando",
    "Decifrando", "Conjurando", "Imaginando",
    "Incubando", "Idealizando", "Burilando",
    "Temperando", "Marinando", "Refogando",
    "Forjando", "Moldando", "Esculpindo",
    "Clauding", "Chocando", "Germinando",
    "Deliberando", "Cogitando", "Especulando",
    "Sintetizando", "Compilando", "Depurando",
    "Vagueando", "Divagando", "Perambulando",
    "Borbulhando", "Fervilhando", "Remexendo",
    "Desvendando", "Investigando", "Farejando",
    "Rabiscando", "Esboçando", "Tecendo",
    "Aprontando", "Caprichando", "Lustrando",
    "Mastigando", "Digerindo", "Saboreando",
    "Zanzando", "Cirandando", "Rodopiando",
    "Encantando", "Enfeitiçando", "Alquimiando",
    "Trabalhando", "Labutando", "Batucando",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))


static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Reinicia em %d min", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Reinicia em %dh %02dmin", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Reinicia em %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

// Compact reset hint for the per-bar line in the Proposta B rows:
// "faltam 55m", "faltam 4h", "faltam 2d". mins < 0 = unknown ("--").
static void format_reset_short(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "--");
    } else if (mins < 60) {
        snprintf(buf, len, "faltam %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "faltam %dh", mins / 60);
    } else {
        snprintf(buf, len, "faltam %dd", mins / 1440);
    }
}

// Same, but from a day count (Kiro reports its monthly reset in days).
static void format_reset_short_days(int days, char* buf, size_t len) {
    if (days < 0)       snprintf(buf, len, "--");
    else if (days == 0) snprintf(buf, len, "hoje");
    else                snprintf(buf, len, "faltam %dd", days);
}

// Forward decls — callbacks defined near ui_show_screen below
static void global_click_cb(lv_event_t* e);
static void make_screen_draggable(lv_obj_t* c);
static void screen_pressed_cb(lv_event_t* e);

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_right(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_top(panel, L.panel_pad_y, 0);
    lv_obj_set_style_pad_bottom(panel, L.panel_pad_y, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, L.pill_font, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_right(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_top(lbl, L.pill_pad_y, 0);
    lv_obj_set_style_pad_bottom(lbl, L.pill_pad_y, 0);
    return lbl;
}

static void init_battery_icons(void) {
    if (L.small_icons) {
        init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_SMALL_W, ICON_BATTERY_SMALL_H, icon_battery_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_SMALL_W, ICON_BATTERY_LOW_SMALL_H, icon_battery_low_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_SMALL_W, ICON_BATTERY_MEDIUM_SMALL_H, icon_battery_medium_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_SMALL_W, ICON_BATTERY_FULL_SMALL_H, icon_battery_full_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_SMALL_W, ICON_BATTERY_CHARGING_SMALL_H, icon_battery_charging_small_data);
        return;
    }
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
}

// ======== Usage Screen ========

static lv_obj_t* make_usage_panel(lv_obj_t* parent, int y, const char* pill_text,
                                  lv_obj_t** out_pct, lv_obj_t** out_pill,
                                  lv_obj_t** out_bar, lv_obj_t** out_reset) {
    lv_obj_t* panel = make_panel(parent, L.margin, y, L.content_w, L.usage_panel_h);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, L.pct_font, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, L.kiro_panel ? -6 : 0);

    *out_pill = make_pill(panel, pill_text);
    lv_obj_align(*out_pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    *out_bar = make_bar(panel, 0, L.usage_bar_y,
                        L.content_w - 2 * L.panel_pad_x, L.usage_bar_h);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, L.usage_reset_font, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, L.usage_reset_y);

    return panel;
}

// ---- Proposta B: provider row with up to two mini-bar cells ----
// A cell creates a %/bar pair (returned via out params so ui_update_* can drive
// them) plus a small window caption ("Daily") and a compact reset hint.
// x/w are relative to the row's inner content area.
static void make_minibar_cell(lv_obj_t* row, int x, int w, lv_color_t color,
                              const char* win_text, lv_obj_t** out_pct,
                              lv_obj_t** out_bar, lv_obj_t** out_hint) {
    // Window caption (top-left of the cell).
    lv_obj_t* win = lv_label_create(row);
    lv_label_set_text(win, win_text);
    lv_obj_set_style_text_font(win, L.urow_win_font, 0);
    lv_obj_set_style_text_color(win, COL_DIM, 0);
    lv_obj_set_pos(win, x, 2);

    // % number (top-right of the cell).
    lv_obj_t* pct = lv_label_create(row);
    lv_label_set_text(pct, "--%");
    lv_obj_set_style_text_font(pct, L.urow_pct_font, 0);
    lv_obj_set_style_text_color(pct, color, 0);
    lv_obj_set_width(pct, w);
    lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(pct, x, 0);

    // Thin bar under the caption/number.
    lv_obj_t* bar = make_bar(row, x, 30, w, L.urow_minibar_h);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);

    // Compact reset hint under the bar.
    lv_obj_t* hint = lv_label_create(row);
    lv_label_set_text(hint, "--");
    lv_obj_set_style_text_font(hint, L.urow_win_font, 0);
    lv_obj_set_style_text_color(hint, COL_DIM, 0);
    lv_obj_set_pos(hint, x, 44);

    if (out_pct) *out_pct = pct;
    if (out_bar) *out_bar = bar;
    if (out_hint) *out_hint = hint;
}

// One provider row: colored name on the left, one or two mini-bar cells on the right.
static lv_obj_t* make_provider_row(lv_obj_t* parent, int y, const char* name, lv_color_t color) {
    lv_obj_t* row = make_panel(parent, L.margin, y, L.content_w, L.urow_h);
    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_font(lbl, L.urow_name_font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    return row;
}

// Pairing hint — shown when disconnected so the screen isn't empty. Pairing
// from the host's Bluetooth settings works on every board, buttons or not.
static void build_pair_group(lv_obj_t* parent) {
    pair_group = lv_obj_create(parent);
    lv_obj_set_size(pair_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(pair_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(pair_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pair_group, 0, 0);
    lv_obj_set_style_pad_all(pair_group, 0, 0);
    lv_obj_clear_flag(pair_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l1 = lv_label_create(pair_group);
    lv_label_set_text(l1, "Para parear");
    lv_obj_set_style_text_font(l1, L.bt_status_font, 0);
    lv_obj_set_style_text_color(l1, COL_TEXT, 0);
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, L.pair_y1);

    lv_obj_t* l2 = lv_label_create(pair_group);
    lv_label_set_text(l2, "conecte ao Clawdmeter");
    lv_obj_set_style_text_font(l2, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l2, COL_DIM, 0);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, L.pair_y2);

    lv_obj_t* l3 = lv_label_create(pair_group);
    lv_label_set_text(l3, "no Bluetooth do computador");
    lv_obj_set_style_text_font(l3, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l3, COL_DIM, 0);
    lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, L.pair_y3);

    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);  // ui_update_ble_status decides
}

// Idle "Zzz" screen — shown when the host is connected but no usage update has
// landed recently (token expired, daemon down, host asleep…). Full-screen, like
// the pairing hint, so we never render hours-old numbers as if they were live.
static lv_obj_t* idle_cloud = nullptr;
static lv_obj_t* idle_flyer = nullptr;

static void build_idle_group(lv_obj_t* parent) {
    idle_group = lv_obj_create(parent);
    lv_obj_set_size(idle_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(idle_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(idle_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_group, 0, 0);
    lv_obj_set_style_pad_all(idle_group, 0, 0);
    lv_obj_clear_flag(idle_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    // A shrunk-down resting creature (the official cloud-ride animation)
    // sits between the header and the status line; the animated "Listening…"
    // status line carries the words, so no extra text is needed here.
    idle_cloud = splash_mini_create(idle_group, "cloud", L.idle_px);
    if (idle_cloud) lv_obj_align(idle_cloud, LV_ALIGN_CENTER, 0, -20);
    // During Kiro's turn the cloud rider gives way to the ghost flying across.
    idle_flyer = splash_flyer_create(idle_group, L.idle_px);

    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);  // update_view_state decides
}

static lv_obj_t* make_usage_scroll_box(lv_obj_t* parent, int y, int w, int visible_rows, int row_h);
static lv_obj_t* panel_ag = nullptr;      // Gemini - Weekly (large layout)
static lv_obj_t* lbl_ag_pct;
static lv_obj_t* lbl_ag_label;
static lv_obj_t* bar_ag;
static lv_obj_t* lbl_ag_reset;
static lv_obj_t* panel_ag_day = nullptr;  // Gemini - Daily (large layout)
static lv_obj_t* lbl_ag_day_pct;
static lv_obj_t* lbl_ag_day_label;
static lv_obj_t* bar_ag_day;
static lv_obj_t* lbl_ag_day_reset;
static lv_obj_t* panel_codex_day = nullptr;
static lv_obj_t* lbl_codex_day_pct;
static lv_obj_t* lbl_codex_day_label;
static lv_obj_t* bar_codex_day;
static lv_obj_t* lbl_codex_day_reset;
static lv_obj_t* panel_codex_week = nullptr;
static lv_obj_t* lbl_codex_week_pct;
static lv_obj_t* lbl_codex_week_label;
static lv_obj_t* bar_codex_week;
static lv_obj_t* lbl_codex_week_reset;

// Proposta B (large layout): one row per provider, each with up to two mini-bars
// (Daily / Weekly). Each mini-bar carries a small "faltam Xh" reset hint under it.
// These reuse the existing %/bar objects above; only the compact hints are new.
static bool      usage_rows_layout = false;   // true = Proposta B rows (large layout)
static lv_obj_t* hint_session = nullptr;      // Claude Daily
static lv_obj_t* hint_weekly = nullptr;       // Claude Weekly
static lv_obj_t* hint_kiro = nullptr;         // Kiro Monthly
static lv_obj_t* hint_ag = nullptr;           // Gemini Weekly
static lv_obj_t* hint_ag_day = nullptr;       // Gemini Daily
static lv_obj_t* hint_codex_day = nullptr;    // Codex Daily
static lv_obj_t* hint_codex_week = nullptr;   // Codex Weekly

// Sets a mini-bar's compact reset hint (Proposta B only; no-op elsewhere).
static void set_reset_hint(lv_obj_t* hint, int mins) {
    if (!hint) return;
    char buf[16];
    format_reset_short(mins, buf, sizeof(buf));
    lv_label_set_text(hint, buf);
}

// Large layout: panels are sorted by usage %, highest on top. Ties and panels
// without data (-1, at the bottom) keep the default order below.
enum { UP_CLAUDE_WEEK, UP_KIRO, UP_AG_WEEK, UP_CLAUDE_DAY, UP_AG_DAY,
       UP_CODEX_DAY, UP_CODEX_WEEK, UP_COUNT };
static int usage_pct[UP_COUNT] = {-1, -1, -1, -1, -1, -1, -1};

static void sort_usage_panels(void) {
    // Proposta B rows are fixed (one per provider); never reposition them.
    if (usage_rows_layout) return;
    if (!L.kiro_panel) return;
    lv_obj_t* panel[UP_COUNT] = {panel_weekly, panel_kiro, panel_ag, panel_session, panel_ag_day,
                                 panel_codex_day, panel_codex_week};
    int order[UP_COUNT];
    for (int i = 0; i < UP_COUNT; i++) {
        int j = i;
        while (j > 0 && usage_pct[order[j - 1]] < usage_pct[i]) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    for (int i = 0; i < UP_COUNT; i++) {
        if (panel[order[i]]) lv_obj_set_y(panel[order[i]], i * (L.usage_panel_h + L.usage_panel_gap));
    }
}

// Proposta B shows every provider at once, so nothing is ever hidden and the
// screen never needs to glide. (Legacy scroll layout kept for reference.)
static bool usage_hidden_in_use(void) {
    if (usage_rows_layout) return false;
    int in_use = 0;
    for (int i = 0; i < UP_COUNT; i++) in_use += usage_pct[i] > 0;
    return in_use > 3;
}

static void set_usage_pct(int which, int pct) {
    if (usage_pct[which] == pct) return;
    usage_pct[which] = pct;
    sort_usage_panels();
}

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    make_screen_draggable(usage_container);
    lv_obj_add_event_cb(usage_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Consumo Atual");
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    fit_screen_title(lbl_title, "Consumo Atual");

    // Usage panels (shown when connected) live in a transparent full-size group
    // so they can be toggled against the pairing hint as one unit.
    usage_group = lv_obj_create(usage_container);
    lv_obj_set_size(usage_group, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_group, 0, 0);
    lv_obj_set_style_bg_opa(usage_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_group, 0, 0);
    lv_obj_set_style_pad_all(usage_group, 0, 0);
    lv_obj_clear_flag(usage_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Large layout uses Proposta B: 4 provider rows (Claude, Kiro, Gemini, Codex),
    // each with one or two mini-bars. Small/compact layouts keep the two tall
    // panels (Claude Daily on top, Weekly below). No scroll box in either case.
    usage_rows_layout = L.kiro_panel;
    lv_obj_t* panels = usage_group;
    int py0 = L.content_y;
    const int row = L.usage_panel_h + L.usage_panel_gap;

    if (usage_rows_layout) {
        // ---- Proposta B rows ----
        const int rh = L.urow_h + L.urow_gap;
        const int inner_w = L.content_w - 2 * L.panel_pad_x;
        const int lw = L.urow_label_w;              // left band for the provider name
        const int cell_gap = 16;
        const int cells_x = lw;
        const int cells_w = inner_w - lw;
        const int cell_w = (cells_w - cell_gap) / 2;
        const int cell2_x = cells_x + cell_w + cell_gap;

        // Claude — Daily + Weekly. panel_session/panel_weekly point at this row
        // so the enterprise overlays and hidden-flag logic keep working.
        lv_obj_t* row_claude = make_provider_row(panels, py0, "Claude", COL_ACCENT);
        panel_session = row_claude;
        panel_weekly = row_claude;
        make_minibar_cell(row_claude, cells_x, cell_w, COL_ACCENT, "Daily",
                          &lbl_session_pct, &bar_session, &hint_session);
        make_minibar_cell(row_claude, cell2_x, cell_w, COL_ACCENT, "Weekly",
                          &lbl_weekly_pct, &bar_weekly, &hint_weekly);

        // Enterprise-only overlays inside the Claude row — hidden until enterprise data arrives.
        lbl_session_pct_sym = lv_label_create(row_claude);
        lv_label_set_text(lbl_session_pct_sym, "%");
        lv_obj_set_style_text_font(lbl_session_pct_sym, L.urow_win_font, 0);
        lv_obj_set_style_text_color(lbl_session_pct_sym, COL_TEXT, 0);
        lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lbl_spending_desc = lv_label_create(row_claude);
        lv_label_set_text(lbl_spending_desc, "do orçamento mensal");
        lv_obj_set_style_text_font(lbl_spending_desc, L.urow_win_font, 0);
        lv_obj_set_style_text_color(lbl_spending_desc, COL_DIM, 0);
        lv_obj_add_flag(lbl_spending_desc, LV_OBJ_FLAG_HIDDEN);
        lbl_spending_status = lv_label_create(row_claude);
        lv_label_set_text(lbl_spending_status, "");
        lv_obj_set_style_text_font(lbl_spending_status, L.urow_win_font, 0);
        lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);
        // Reset labels still exist (used by the small layout / enterprise recolor)
        // but stay off-screen in row mode; the compact hints carry the reset info.
        lbl_session_reset = lv_label_create(row_claude);
        lbl_weekly_reset = lv_label_create(row_claude);
        lv_label_set_recolor(lbl_weekly_reset, true);
        lv_obj_add_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_weekly_reset, LV_OBJ_FLAG_HIDDEN);
        lbl_session_label = lv_label_create(row_claude);
        lbl_weekly_label = lv_label_create(row_claude);
        lv_obj_add_flag(lbl_session_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_weekly_label, LV_OBJ_FLAG_HIDDEN);

        // Kiro — one mini-bar (Mês). The 5h/24h Kiro figures live on the history
        // screen, so the row shows the monthly credit budget across the full width.
        lv_obj_t* row_kiro = make_provider_row(panels, py0 + rh, "Kiro", COL_KIRO);
        panel_kiro = row_kiro;
        make_minibar_cell(row_kiro, cells_x, cells_w, COL_KIRO, "Mês",
                          &lbl_kiro_pct, &bar_kiro, &hint_kiro);
        lbl_kiro_label = lv_label_create(row_kiro);
        lbl_kiro_reset = lv_label_create(row_kiro);
        lv_obj_add_flag(lbl_kiro_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_kiro_reset, LV_OBJ_FLAG_HIDDEN);

        // Gemini — Daily + Weekly.
        lv_obj_t* row_ag = make_provider_row(panels, py0 + 2 * rh, "Gemini", COL_AG);
        panel_ag = row_ag;
        panel_ag_day = row_ag;
        make_minibar_cell(row_ag, cells_x, cell_w, COL_AG, "Daily",
                          &lbl_ag_day_pct, &bar_ag_day, &hint_ag_day);
        make_minibar_cell(row_ag, cell2_x, cell_w, COL_AG, "Weekly",
                          &lbl_ag_pct, &bar_ag, &hint_ag);
        lbl_ag_label = lv_label_create(row_ag);
        lbl_ag_day_label = lv_label_create(row_ag);
        lbl_ag_reset = lv_label_create(row_ag);
        lbl_ag_day_reset = lv_label_create(row_ag);
        lv_obj_add_flag(lbl_ag_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ag_day_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ag_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_ag_day_reset, LV_OBJ_FLAG_HIDDEN);

        // Codex — Daily + Weekly.
        lv_obj_t* row_codex = make_provider_row(panels, py0 + 3 * rh, "Codex", COL_CODEX);
        panel_codex_day = row_codex;
        panel_codex_week = row_codex;
        make_minibar_cell(row_codex, cells_x, cell_w, COL_CODEX, "Daily",
                          &lbl_codex_day_pct, &bar_codex_day, &hint_codex_day);
        make_minibar_cell(row_codex, cell2_x, cell_w, COL_CODEX, "Weekly",
                          &lbl_codex_week_pct, &bar_codex_week, &hint_codex_week);
        lbl_codex_day_label = lv_label_create(row_codex);
        lbl_codex_week_label = lv_label_create(row_codex);
        lbl_codex_day_reset = lv_label_create(row_codex);
        lbl_codex_week_reset = lv_label_create(row_codex);
        lv_obj_add_flag(lbl_codex_day_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_codex_week_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_codex_day_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_codex_week_reset, LV_OBJ_FLAG_HIDDEN);
    } else {
        // ---- Small / compact layouts: two tall panels, no scroll, no Kiro panel ----
        panel_session = make_usage_panel(panels, py0, "Claude - Daily",
                         &lbl_session_pct, &lbl_session_label,
                         &bar_session, &lbl_session_reset);

        // Enterprise-only overlays inside panel_session — hidden until enterprise data arrives
        lbl_session_pct_sym = lv_label_create(panel_session);
        lv_label_set_text(lbl_session_pct_sym, "%");
        lv_obj_set_style_text_font(lbl_session_pct_sym, L.reset_font, 0);
        lv_obj_set_style_text_color(lbl_session_pct_sym, COL_TEXT, 0);
        lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);

        lbl_spending_desc = lv_label_create(panel_session);
        lv_label_set_text(lbl_spending_desc, "do orçamento mensal");
        lv_obj_set_style_text_font(lbl_spending_desc, L.reset_font, 0);
        lv_obj_set_style_text_color(lbl_spending_desc, COL_DIM, 0);
        lv_obj_set_pos(lbl_spending_desc, 0, L.usage_reset_y);
        lv_obj_add_flag(lbl_spending_desc, LV_OBJ_FLAG_HIDDEN);

        lbl_spending_status = lv_label_create(panel_session);
        lv_label_set_text(lbl_spending_status, "");
        lv_obj_set_style_text_font(lbl_spending_status, L.pace_font, 0);
        lv_obj_set_pos(lbl_spending_status, 0, L.usage_reset_y + 20);
        lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);

        panel_weekly = make_usage_panel(panels, py0 + row, "Claude - Weekly",
                         &lbl_weekly_pct, &lbl_weekly_label,
                         &bar_weekly, &lbl_weekly_reset);
        lv_label_set_recolor(lbl_weekly_reset, true);
    }

    // Brand colors: the number and bar in the tool's primary color, text in white.
    lv_obj_set_style_text_color(lbl_session_pct, COL_ACCENT, 0);
    lv_obj_set_style_text_color(lbl_weekly_pct, COL_ACCENT, 0);
    lv_obj_set_style_text_color(lbl_session_reset, COL_TEXT, 0);
    lv_obj_set_style_text_color(lbl_weekly_reset, COL_TEXT, 0);
    lv_obj_set_style_bg_color(bar_session, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_weekly, COL_ACCENT, LV_PART_INDICATOR);
    if (panel_ag) {
        lv_obj_set_style_text_color(lbl_ag_pct, COL_AG, 0);
        lv_obj_set_style_text_color(lbl_ag_reset, COL_TEXT, 0);
        lv_obj_set_style_bg_color(bar_ag, COL_AG, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(lbl_ag_day_pct, COL_AG, 0);
        lv_obj_set_style_text_color(lbl_ag_day_reset, COL_TEXT, 0);
        lv_obj_set_style_bg_color(bar_ag_day, COL_AG, LV_PART_INDICATOR);
    }
    if (panel_codex_day) {
        lv_obj_set_style_text_color(lbl_codex_day_pct, COL_CODEX, 0);
        lv_obj_set_style_text_color(lbl_codex_day_reset, COL_TEXT, 0);
        lv_obj_set_style_bg_color(bar_codex_day, COL_CODEX, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(lbl_codex_week_pct, COL_CODEX, 0);
        lv_obj_set_style_text_color(lbl_codex_week_reset, COL_TEXT, 0);
        lv_obj_set_style_bg_color(bar_codex_week, COL_CODEX, LV_PART_INDICATOR);
    }
    if (panel_kiro) {
        lv_obj_set_style_text_color(lbl_kiro_pct, COL_KIRO, 0);
        lv_obj_set_style_text_color(lbl_kiro_reset, COL_TEXT, 0);
        lv_obj_set_style_bg_color(bar_kiro, COL_KIRO, LV_PART_INDICATOR);
    }

    build_pair_group(usage_container);
    build_idle_group(usage_container);

    // Status line — always visible on the usage view. Driven by ui_tick_anim().
    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, L.anim_font, 0);
    lv_obj_set_style_text_color(lbl_anim, accent_color, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, L.anim_y);
}

// ======== History / Models screens ========

static lv_obj_t* history_container;
// Stacked hourly bars: Claude (bottom, orange) + Kiro (top, purple).
#define HISTORY_HOURS 24
static lv_obj_t* history_claude_bar[HISTORY_HOURS];
static lv_obj_t* history_kiro_bar[HISTORY_HOURS];
static lv_obj_t* history_ag_bar[HISTORY_HOURS];
static lv_obj_t* history_codex_bar[HISTORY_HOURS];
static lv_obj_t* lbl_history_codex;
static lv_obj_t* lbl_history_codex_unit;
static lv_obj_t* lbl_history_ag;
static lv_obj_t* lbl_history_ag_unit;
static int       history_plot_h = 0;
static lv_obj_t* lbl_history_now;
static lv_obj_t* lbl_history_peak;
static lv_obj_t* lbl_history_now_unit;   // "tokens do Claude", under the Claude number
static lv_obj_t* lbl_history_peak_unit;  // "requisições do Kiro", under the Kiro number
static lv_obj_t* lbl_history_empty;


// Titles sit between the mascot (left) and the battery (right). One that is too
// wide for the title font drops to Tiempos 34 inside that band, wrapping onto
// two lines if it still doesn't fit.
#define TITLE_MASCOT_CELLS 28   // widest corner-mascot act (pointing), in cells
#define TITLE_MASCOT_GAP   12
#define TITLE_BAND_MID     61   // vertical center of the title band (mascot height)
// Titles are left-aligned just past the farthest point the corner mascots reach
// while acting in place, in Tiempos 34; one wider than the band wraps.
static void fit_screen_title(lv_obj_t* t, const char* text) {
    (void)text;
    const int left  = L.margin + TITLE_MASCOT_CELLS * (L.small_icons ? 2 : 3) + TITLE_MASCOT_GAP;
    const int right = L.scr_w - L.margin - (board_caps().has_battery ? L.batt_w + 10 : 0);
    const lv_font_t* font = &font_tiempos_34;
    const int mid_y = L.title_y < TITLE_BAND_MID ? TITLE_BAND_MID : L.title_y + lv_font_get_line_height(font) / 2;
    lv_obj_set_style_text_font(t, font, 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(t, -6, 0);   // keep two lines clear of the panel below
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, right - left);
    lv_obj_update_layout(t);
    lv_obj_set_pos(t, left, mid_y - lv_obj_get_height(t) / 2);
}

// Project rule: every screen drags up and down with a finger. Screens whose
// content fits just stretch elastically and spring back; lists scroll first and
// hand the drag to the screen at their ends (LVGL scroll chaining).
static lv_point_t press_point;           // where the current touch started

static void screen_pressed_cb(lv_event_t* e) {
    (void)e;
    lv_indev_t* indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &press_point);
}

static void make_screen_draggable(lv_obj_t* c) {
    lv_obj_add_flag(c, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                       LV_OBJ_FLAG_SCROLL_MOMENTUM));
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    // LVGL only drags an object that has some scroll range: a 1 px taller
    // invisible spacer gives every screen that range, so a swipe rubber-bands
    // the screen instead of falling through as a tap.
    lv_obj_t* spacer = lv_obj_create(c);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, L.scr_h + 1);
    lv_obj_set_pos(spacer, 0, 0);
    lv_obj_clear_flag(spacer, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_event_cb(c, screen_pressed_cb, LV_EVENT_PRESSED, NULL);
}

static lv_obj_t* make_screen_container(lv_obj_t* scr, const char* title) {
    lv_obj_t* c = lv_obj_create(scr);
    lv_obj_set_size(c, L.scr_w, L.scr_h);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    make_screen_draggable(c);
    lv_obj_add_event_cb(c, global_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* t = lv_label_create(c);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, COL_TEXT, 0);
    fit_screen_title(t, title);

    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    return c;
}

static lv_obj_t* make_dim_label(lv_obj_t* parent, const lv_font_t* font, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, COL_DIM, 0);
    return l;
}

// Pt-BR compact count: 950, 12 mil, 3,4 mi, 1,2 bi.
// Kiro credits, pt-BR with one decimal: "7,4". "~" marks an estimate.
static void format_credits(float c, char* buf, size_t len) {
    snprintf(buf, len, "%.1f", c);
    for (char* p = buf; *p; p++) if (*p == '.') *p = ',';
}

// Integer with pt-BR thousands separator: "2.000".
static void format_int_ptbr(int v, char* buf, size_t len) {
    if (v >= 1000) snprintf(buf, len, "%d.%03d", v / 1000, v % 1000);
    else           snprintf(buf, len, "%d", v);
}

static void format_tokens(uint64_t t, char* buf, size_t len) {
    if (t >= 1000000000ULL)   snprintf(buf, len, "%.1f bi", t / 1e9);
    else if (t >= 1000000ULL) snprintf(buf, len, "%.1f mi", t / 1e6);
    else if (t >= 1000ULL)    snprintf(buf, len, "%llu mil", (unsigned long long)(t / 1000));
    else                      snprintf(buf, len, "%llu", (unsigned long long)t);
    for (char* p = buf; *p; p++) if (*p == '.') *p = ',';
}

// V5 layout: the four provider totals live in a compact strip along the bottom
// of the card — one cell each (Claude, Kiro, Gemini, Codex), the colored name
// on top and the total below. The chart fills the whole area above the strip.
static int history_strip_y = 0;   // top of the bottom strip, inside the card
static int history_cell_w = 0;    // width of one strip cell (set at init)
static void history_place_units(void) {
    const int inner_w   = L.content_w - 2 * L.panel_pad_x;
    const int name_h    = lv_font_get_line_height(L.axis_font);
    const int cell_gap  = 6;
    const int cell_w    = history_cell_w ? history_cell_w : (inner_w - 3 * cell_gap) / 4;
    // Order across the strip: Claude, Kiro, Gemini, Codex.
    lv_obj_t* name[4] = {lbl_history_now_unit, lbl_history_peak_unit, lbl_history_ag_unit, lbl_history_codex_unit};
    lv_obj_t* num[4]  = {lbl_history_now,      lbl_history_peak,      lbl_history_ag,      lbl_history_codex};
    for (int i = 0; i < 4; i++) {
        const int x = i * (cell_w + cell_gap);   // left edge of this cell
        // Each label spans the full cell width and centers its text, so a long
        // total ("49,0 créd.") stays clipped inside its own cell — never wraps.
        lv_label_set_long_mode(name[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(name[i], cell_w);
        lv_obj_set_style_text_align(name[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(name[i], x, history_strip_y);
        lv_label_set_long_mode(num[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(num[i], cell_w);
        lv_obj_set_style_text_align(num[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(num[i], x, history_strip_y + name_h);
    }
}

static void init_history_screen(lv_obj_t* scr) {
    history_container = make_screen_container(scr, "Consumo - 24\xC2\xA0horas");   // no-break space keeps "24 horas" together

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(history_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;

    // V5 layout — the 24h stacked chart fills the top of the card; a compact
    // strip of four provider totals (Claude, Kiro, Gemini, Codex) sits along
    // the bottom, each with its colored name over the total.
    const int name_h = lv_font_get_line_height(L.axis_font);
    const int num_h  = lv_font_get_line_height(L.table_font_dense);
    const int strip_h = name_h + num_h;
    history_strip_y = inner_h - strip_h;   // top of the bottom strip, in card space

    // One rounded cell behind each of the four totals.
    const int cell_gap = 6;
    const int cell_w   = (inner_w - 3 * cell_gap) / 4;
    history_cell_w = cell_w;
    for (int i = 0; i < 4; i++) {
        lv_obj_t* cell = lv_obj_create(panel);
        lv_obj_set_pos(cell, i * (cell_w + cell_gap), history_strip_y - 4);
        lv_obj_set_size(cell, cell_w, strip_h + 8);
        lv_obj_set_style_bg_color(cell, COL_BG, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_radius(cell, 10, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_add_flag(cell, LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    // Provider name labels (colored, small) — reuse the old *_unit labels.
    lbl_history_now_unit = lv_label_create(panel);      // Claude
    lv_label_set_text(lbl_history_now_unit, "Claude");
    lv_obj_set_style_text_font(lbl_history_now_unit, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_history_now_unit, COL_ACCENT, 0);

    lbl_history_peak_unit = lv_label_create(panel);     // Kiro
    lv_label_set_text(lbl_history_peak_unit, "Kiro");
    lv_obj_set_style_text_font(lbl_history_peak_unit, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_history_peak_unit, COL_KIRO, 0);

    lbl_history_ag_unit = lv_label_create(panel);       // Gemini
    lv_label_set_text(lbl_history_ag_unit, "Gemini");
    lv_obj_set_style_text_font(lbl_history_ag_unit, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_history_ag_unit, COL_AG, 0);

    lbl_history_codex_unit = lv_label_create(panel);    // Codex
    lv_label_set_text(lbl_history_codex_unit, "Codex");
    lv_obj_set_style_text_font(lbl_history_codex_unit, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_history_codex_unit, COL_CODEX, 0);

    // Provider total numbers (medium, white) — reuse the old header labels.
    // table_font (not the big reset_font) so "49,0 créd." fits inside a cell.
    lbl_history_now = lv_label_create(panel);           // Claude tokens
    lv_label_set_text(lbl_history_now, "---");
    lv_obj_set_style_text_font(lbl_history_now, L.table_font_dense, 0);
    lv_obj_set_style_text_color(lbl_history_now, COL_TEXT, 0);

    lbl_history_peak = lv_label_create(panel);          // Kiro credits
    lv_label_set_text(lbl_history_peak, "---");
    lv_obj_set_style_text_font(lbl_history_peak, L.table_font_dense, 0);
    lv_obj_set_style_text_color(lbl_history_peak, COL_TEXT, 0);

    lbl_history_ag = lv_label_create(panel);            // Gemini tokens
    lv_label_set_text(lbl_history_ag, "---");
    lv_obj_set_style_text_font(lbl_history_ag, L.table_font_dense, 0);
    lv_obj_set_style_text_color(lbl_history_ag, COL_TEXT, 0);

    lbl_history_codex = lv_label_create(panel);         // Codex tokens
    lv_label_set_text(lbl_history_codex, "---");
    lv_obj_set_style_text_font(lbl_history_codex, L.table_font_dense, 0);
    lv_obj_set_style_text_color(lbl_history_codex, COL_TEXT, 0);

    history_place_units();

    // Chart card: fills from the top of the panel down to just above the strip.
    const int axis_h  = lv_font_get_line_height(L.axis_font);
    const int chart_y = 0;
    const int chart_h = history_strip_y - chart_y - 8 - axis_h;   // leave room for the -24h/agora axis

    lv_obj_t* plot = lv_obj_create(panel);
    lv_obj_set_pos(plot, 0, chart_y);
    lv_obj_set_size(plot, inner_w, chart_h);
    lv_obj_set_style_bg_color(plot, COL_BG, 0);
    lv_obj_set_style_bg_opa(plot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(plot, 0, 0);
    lv_obj_set_style_radius(plot, 10, 0);
    lv_obj_set_style_pad_all(plot, 0, 0);
    lv_obj_clear_flag(plot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(plot, LV_OBJ_FLAG_EVENT_BUBBLE);

    const int pad = 6;
    history_plot_h = chart_h - 2 * pad;
    const int slot = (inner_w - 2 * pad) / HISTORY_HOURS;
    const int bar_w = slot - slot / 4;
    const int left = (inner_w - slot * HISTORY_HOURS) / 2 + (slot - bar_w) / 2;
    for (int i = 0; i < HISTORY_HOURS; i++) {
        for (int k = 0; k < 4; k++) {
            lv_obj_t* b = lv_obj_create(plot);
            lv_obj_set_style_bg_color(b, k == 0 ? COL_ACCENT : k == 1 ? COL_KIRO : k == 2 ? COL_AG : COL_CODEX, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_radius(b, 0, 0);
            lv_obj_set_style_pad_all(b, 0, 0);
            lv_obj_clear_flag(b, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_set_pos(b, left + i * slot, pad + history_plot_h);
            lv_obj_set_size(b, bar_w, 0);
            lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
            (k == 0 ? history_claude_bar : k == 1 ? history_kiro_bar : k == 2 ? history_ag_bar : history_codex_bar)[i] = b;
        }
    }

    lbl_history_empty = make_dim_label(panel, L.reset_font, "Coletando dados...");
    lv_obj_align_to(lbl_history_empty, plot, LV_ALIGN_CENTER, 0, 0);

    // Time axis just under the chart card.
    lv_obj_t* a0 = make_dim_label(panel, L.axis_font, "-24h");
    lv_obj_set_pos(a0, 0, chart_y + chart_h + 2);
    lv_obj_t* a2 = make_dim_label(panel, L.axis_font, "agora");
    lv_obj_update_layout(a2);
    lv_obj_set_pos(a2, inner_w - lv_obj_get_width(a2), chart_y + chart_h + 2);
}

// ======== Scrollable lists ========
// Project rule: any list that can hold more items than fit on screen is one
// finger-scrollable list (no pages, no dropped items). Rows live in a clipped
// box under a fixed header; a drag scrolls and pauses rotation, a short tap
// still bubbles up to the screen's left/right navigation. Unattended, the list
// glides from its focus row to the end within the screen's rotation dwell.
struct ScrollList {
    lv_obj_t* box;
    int       row_h;
    int       rows;          // rows currently in the list
    int       focus;         // row brought to the top when the screen opens
    int       start_y;
    uint32_t  shown_ms;
    uint32_t  touched_ms;    // last finger contact (stops the glide)
    uint32_t  glide_ms;      // last glide step
    uint32_t  glide_acc;     // sub-pixel progress, px*ms
    uint32_t  end_ms;        // when the glide reached the bottom (0 = not yet)
};
// Unattended lists glide at a slow, fixed pace; the screen waits for the end.
#define LIST_GLIDE_DELAY_MS 2500
#define LIST_GLIDE_PX_S     25
#define LIST_END_HOLD_MS    2500

static void scroll_list_pressed_cb(lv_event_t* e) {
    ScrollList* l = (ScrollList*)lv_event_get_user_data(e);
    l->touched_ms = lv_tick_get();
    tap_ms = l->touched_ms;            // reading the list pauses rotation like a tap
    tap_hold = true;
}

static lv_obj_t* scroll_list_create(ScrollList* l, lv_obj_t* parent, int x, int y, int w,
                                    int visible_rows, int row_h) {
    l->row_h = row_h;
    l->box = lv_obj_create(parent);
    lv_obj_set_pos(l->box, x, y);
    lv_obj_set_size(l->box, w, row_h * visible_rows);
    lv_obj_set_style_bg_opa(l->box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l->box, 0, 0);
    lv_obj_set_style_pad_all(l->box, 0, 0);
    lv_obj_set_scroll_dir(l->box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l->box, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_flag(l->box, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(l->box, scroll_list_pressed_cb, LV_EVENT_PRESSED, l);
    return l->box;
}

static int scroll_list_max_y(const ScrollList* l) {
    const int max_y = l->row_h * l->rows - lv_obj_get_height(l->box);
    return max_y > 0 ? max_y : 0;
}

static void scroll_list_show(ScrollList* l) {
    if (!l->box) return;
    l->shown_ms = lv_tick_get();
    l->touched_ms = 0;
    int y = l->focus * l->row_h;
    if (y > scroll_list_max_y(l)) y = scroll_list_max_y(l);
    l->start_y = y;
    l->glide_ms = l->shown_ms;
    l->glide_acc = 0;
    l->end_ms = 0;
    lv_obj_scroll_to_y(l->box, y, LV_ANIM_OFF);
}

// Returns true while the list is still gliding (the rotation should wait for it).
static bool scroll_list_tick(ScrollList* l) {
    if (!l->box) return false;
    const uint32_t now = lv_tick_get();
    if (l->touched_ms && now - l->touched_ms < ROTATION_TAP_PAUSE_MS) return false;
    const int max_y = scroll_list_max_y(l);
    const int cur = lv_obj_get_scroll_y(l->box);
    if (now - l->shown_ms < LIST_GLIDE_DELAY_MS) {
        l->glide_ms = now;
        return max_y > cur;
    }
    // End = nothing left below the viewport (the computed max_y can overshoot the real one).
    if (max_y <= 0) return false;
    if (cur >= max_y || lv_obj_get_scroll_bottom(l->box) <= 0) {
        if (!l->end_ms) l->end_ms = now;
        if (now - l->end_ms < LIST_END_HOLD_MS) return true;
        // Rested at the bottom: jump back to the top and glide again, never frozen there.
        lv_obj_scroll_to_y(l->box, 0, LV_ANIM_ON);
        l->shown_ms = now;
        l->glide_ms = now;
        l->glide_acc = 0;
        l->end_ms = 0;
        return true;
    }
    l->glide_acc += (now - l->glide_ms) * LIST_GLIDE_PX_S;
    l->glide_ms = now;
    const int step = (int)(l->glide_acc / 1000);
    if (step > 0) {
        l->glide_acc -= (uint32_t)step * 1000;
        lv_obj_scroll_to_y(l->box, cur + step > max_y ? max_y : cur + step, LV_ANIM_OFF);
    }
    return true;
}

static ScrollList usage_list;

static lv_obj_t* make_usage_scroll_box(lv_obj_t* parent, int y, int w, int visible_rows, int row_h) {
    lv_obj_t* box = scroll_list_create(&usage_list, parent, 0, y, w, visible_rows, row_h);
    usage_list.rows = 7;
    return box;
}

void ui_update_antigravity(int used_pct, int reset_mins) {
    if (!panel_ag) return;
    set_usage_pct(UP_AG_WEEK, used_pct < 0 ? -1 : used_pct);
    // Like Claude - Weekly: the big number is the share of the weekly limit
    // already used (Antigravity reports what remains), reset time below.
    if (used_pct < 0) {
        lv_label_set_text(lbl_ag_pct, "---%");
        lv_bar_set_value(bar_ag, 0, LV_ANIM_ON);
        lv_label_set_text(lbl_ag_reset, "Sem dados do Gemini");
        set_reset_hint(hint_ag, -1);
        return;
    }
    char buf[48];
    lv_label_set_text_fmt(lbl_ag_pct, "%d%%", used_pct);
    lv_bar_set_value(bar_ag, used_pct, LV_ANIM_ON);
    format_reset_time(reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_ag_reset, buf);
    set_reset_hint(hint_ag, reset_mins);
}

void ui_update_antigravity_daily(uint64_t tokens_today, int pct_of_peak, int responses) {
    if (!panel_ag_day) return;
    set_usage_pct(UP_AG_DAY, pct_of_peak);
    // No daily quota exists: the big number is today vs. the busiest day of
    // the last 30, the token count goes in the line below.
    char buf[48];
    lv_label_set_text_fmt(lbl_ag_day_pct, "%d%%", pct_of_peak);
    lv_bar_set_value(bar_ag_day, pct_of_peak, LV_ANIM_ON);
    // Daily Gemini has no reset clock (it's "today vs peak"): the hint shows
    // whether there was any use today.
    if (hint_ag_day) lv_label_set_text(hint_ag_day, responses > 0 ? "hoje" : "sem uso");
    if (responses <= 0) {
        lv_label_set_text(lbl_ag_day_reset, "Sem uso do Gemini hoje");
        return;
    }
    format_tokens(tokens_today, buf, sizeof(buf));
    lv_label_set_text_fmt(lbl_ag_day_reset, "%s tokens hoje \xC2\xB7 %d %s",
                          buf, responses, responses == 1 ? "resposta" : "respostas");
}

static void update_codex_panel(int which, lv_obj_t* pct_label, lv_obj_t* bar,
                               lv_obj_t* reset_label, lv_obj_t* hint,
                               int used_pct, int reset_mins, uint64_t tokens) {
    set_usage_pct(which, used_pct < 0 ? -1 : used_pct);
    set_reset_hint(hint, used_pct < 0 ? -1 : reset_mins);
    if (used_pct < 0) {
        lv_label_set_text(pct_label, "---%");
        lv_bar_set_value(bar, 0, LV_ANIM_ON);
        lv_label_set_text(reset_label, "Sem dados do Codex");
        return;
    }
    char buf[48], tok[16];
    lv_label_set_text_fmt(pct_label, "%d%%", used_pct);
    lv_bar_set_value(bar, used_pct, LV_ANIM_ON);
    format_reset_time(reset_mins, buf, sizeof(buf));
    format_tokens(tokens, tok, sizeof(tok));
    lv_label_set_text_fmt(reset_label, "%s \xC2\xB7 %s tokens", buf, tok);
}

void ui_update_codex_daily(int used_pct, int reset_mins, uint64_t tokens) {
    if (!panel_codex_day) return;
    update_codex_panel(UP_CODEX_DAY, lbl_codex_day_pct, bar_codex_day,
                       lbl_codex_day_reset, hint_codex_day, used_pct, reset_mins, tokens);
}

void ui_update_codex_weekly(int used_pct, int reset_mins, uint64_t tokens) {
    if (!panel_codex_week) return;
    update_codex_panel(UP_CODEX_WEEK, lbl_codex_week_pct, bar_codex_week,
                       lbl_codex_week_reset, hint_codex_week, used_pct, reset_mins, tokens);
}

// Crypto and B3 screens share one table: 3 text cells + a colored change cell.
#define QUOTE_COLS_MAX 5
#define QUOTE_CHANGE   -1        // QuoteColumn.cell value: the colored change % column

struct QuoteColumn {
    uint16_t start_pm, end_pm;   // column span in permille of the panel's inner width
    lv_text_align_t align;
    int8_t cell;                 // index into QuoteRow.cells, or QUOTE_CHANGE
};

// One label per column (rows are lines) keeps each table to a few LVGL objects —
// a label per cell exhausted LVGL's 64 KB pool at boot.
struct QuoteTable {
    lv_obj_t*   container;
    lv_obj_t*   empty;
    lv_obj_t*   note;
    lv_obj_t*   columns[QUOTE_COLS_MAX];
    const QuoteColumn* cols;
    int         ncols;
    ScrollList  list;
    QuoteRow    rows[QUOTE_TABLE_ROWS];
    int         total;
};
static QuoteTable quote_tables[QUOTE_TABLE_COUNT];

static const char* const COL_HEX_GREEN = "788c5d";   // THEME_GREEN, for recolor text
static const char* const COL_HEX_RED   = "c0392b";   // THEME_RED

static void format_change(float pct, char* buf, size_t len) {
    snprintf(buf, len, "%+.1f%%", pct);
    for (char* p = buf; *p; p++) if (*p == '.') *p = ',';
}

static lv_obj_t* make_column(lv_obj_t* parent, const lv_font_t* font, const QuoteColumn& col,
                             int inner_w, int y) {
    int x = inner_w * col.start_pm / 1000;
    int w = inner_w * col.end_pm / 1000 - x;
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, COL_TEXT, 0);
    lv_obj_set_style_text_align(l, col.align, 0);
    return l;
}

static void init_quote_screen(lv_obj_t* scr, QuoteTable* t, const char* title,
                              const char* const headers[], const QuoteColumn cols[], int ncols,
                              const lv_font_t* font, const char* note) {
    t->container = make_screen_container(scr, title);
    t->cols = cols;
    t->ncols = ncols;

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(t->container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;

    const int axis_h = lv_font_get_line_height(L.axis_font);
    const int line_h = lv_font_get_line_height(font);
    const int rows_y = axis_h + 4;
    const int row_h  = (inner_h - rows_y - axis_h - 4) / QUOTE_PAGE_ROWS;
    lv_obj_t* box = scroll_list_create(&t->list, panel, 0, rows_y, inner_w, QUOTE_PAGE_ROWS, row_h);
    const int col_y = (row_h - line_h) / 2;

    for (int c = 0; c < ncols; c++) {
        lv_obj_t* h = make_column(panel, L.axis_font, cols[c], inner_w, 0);
        lv_label_set_text(h, headers[c]);
        lv_obj_set_style_text_color(h, COL_DIM, 0);

        lv_obj_t* col = make_column(box, font, cols[c], inner_w, col_y);
        lv_obj_set_height(col, row_h);
        lv_obj_set_style_text_line_space(col, row_h - line_h, 0);
        t->columns[c] = col;
        if (cols[c].cell == QUOTE_CHANGE) lv_label_set_recolor(col, true);
    }
    lv_obj_set_style_text_color(t->columns[0], COL_DIM, 0);

    t->empty = make_dim_label(panel, L.reset_font, "Buscando cotações...");
    lv_obj_align(t->empty, LV_ALIGN_CENTER, 0, 0);

    t->note = make_dim_label(panel, L.axis_font, note);
    lv_label_set_recolor(t->note, true);
    lv_obj_align(t->note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void init_market_screens(lv_obj_t* scr) {
    static const char* const crypto_headers[4] = {"Moeda", "R$", "US$", "24h"};
    static const QuoteColumn crypto_cols[4] = {
        {0, 200, LV_TEXT_ALIGN_LEFT, 0},
        {200, 490, LV_TEXT_ALIGN_RIGHT, 1},
        {490, 770, LV_TEXT_ALIGN_RIGHT, 2},
        {770, 1000, LV_TEXT_ALIGN_RIGHT, QUOTE_CHANGE},
    };
    init_quote_screen(scr, &quote_tables[QUOTES_CRYPTO], "Criptomoedas",
                      crypto_headers, crypto_cols, 4, L.table_font, "CoinGecko · variação em 24h");

    static const char* const stock_headers[5] = {"Ação", "Empresa", "R$", "Dia", "P/VP"};
    static const QuoteColumn stock_cols[5] = {
        {0, 218, LV_TEXT_ALIGN_LEFT, 0},
        {222, 525, LV_TEXT_ALIGN_LEFT, 1},
        {525, 682, LV_TEXT_ALIGN_RIGHT, 2},
        {682, 850, LV_TEXT_ALIGN_RIGHT, QUOTE_CHANGE},
        {850, 1000, LV_TEXT_ALIGN_RIGHT, 3},
    };
    init_quote_screen(scr, &quote_tables[QUOTES_STOCKS], "Bovespa",
                      stock_headers, stock_cols, 5, L.table_font_dense,
                      "Yahoo (15 min) · Fundamentus");

    // FII prices run to three integer digits, so the name column gives way.
    static const char* const fii_headers[5] = {"Fundo", "Nome", "R$", "Dia", "P/VP"};
    static const QuoteColumn fii_cols[5] = {
        {0, 212, LV_TEXT_ALIGN_LEFT, 0},
        {224, 480, LV_TEXT_ALIGN_LEFT, 1},
        {480, 675, LV_TEXT_ALIGN_RIGHT, 2},
        {675, 850, LV_TEXT_ALIGN_RIGHT, QUOTE_CHANGE},
        {850, 1000, LV_TEXT_ALIGN_RIGHT, 3},
    };
    init_quote_screen(scr, &quote_tables[QUOTES_FIIS], "Fundos Imobiliários",
                      fii_headers, fii_cols, 5, L.table_font_dense,
                      "Yahoo (15 min) · Fundamentus");
}

static void render_quote_table(QuoteTable& t) {
    // Worst case per column: every row × (15 chars + recolor tags + newline).
    // Static: too big for the loop task's stack.
    static char text[QUOTE_COLS_MAX][QUOTE_TABLE_ROWS * 32];
    size_t used[QUOTE_COLS_MAX] = {};
    for (int r = 0; r < t.total; r++) {
        const char* sep = r ? "\n" : "";
        char chg[16];
        format_change(t.rows[r].change_pct, chg, sizeof(chg));
        for (int c = 0; c < t.ncols; c++) {
            size_t room = used[c] < sizeof(text[c]) ? sizeof(text[c]) - used[c] : 0;
            if (t.cols[c].cell == QUOTE_CHANGE) {
                used[c] += snprintf(text[c] + used[c], room, "%s#%s %s#", sep,
                                    t.rows[r].change_pct < 0 ? COL_HEX_RED : COL_HEX_GREEN, chg);
            } else {
                used[c] += snprintf(text[c] + used[c], room, "%s%s", sep, t.rows[r].cells[t.cols[c].cell]);
            }
        }
    }
    t.list.rows = t.total;
    for (int c = 0; c < t.ncols; c++) {
        text[c][used[c] < sizeof(text[c]) ? used[c] : sizeof(text[c]) - 1] = '\0';
        lv_label_set_text(t.columns[c], text[c]);
        lv_obj_set_height(t.columns[c], t.list.row_h * (t.total > 0 ? t.total : 1));
    }
    if (t.total > 0) lv_obj_add_flag(t.empty, LV_OBJ_FLAG_HIDDEN);
    else             lv_obj_clear_flag(t.empty, LV_OBJ_FLAG_HIDDEN);
}

void ui_update_quotes(quote_table_t which, const QuoteRow* rows, int offset, int count, int total) {
    QuoteTable& t = quote_tables[which];
    if (!t.container) return;
    t.total = total > QUOTE_TABLE_ROWS ? QUOTE_TABLE_ROWS : total;
    for (int i = 0; i < count; i++) {
        if (offset + i >= 0 && offset + i < t.total) t.rows[offset + i] = rows[i];
    }
    render_quote_table(t);
}

void ui_update_stock_index(const char* value, float change_pct) {
    QuoteTable& t = quote_tables[QUOTES_STOCKS];
    if (!t.note) return;
    char chg[16], buf[96];
    format_change(change_pct, chg, sizeof(chg));
    snprintf(buf, sizeof(buf), "Ibovespa %s #%s %s# · atraso 15 min",
             value, change_pct < 0 ? COL_HEX_RED : COL_HEX_GREEN, chg);
    lv_label_set_text(t.note, buf);
}

// ---- Juros Futuros: the whole DI1 curve on one screen ----
// Today's curve in the accent color over the previous settlement (dim), x by
// maturity date, with the first and last contracts spelled out below.
#define RATE_X_LABELS 20
static lv_obj_t*  rates_container;
static lv_obj_t*  rates_chart;            // plot area; lines and axis labels live inside
static lv_obj_t*  rates_line_today;
static lv_obj_t*  rates_line_prev;
static lv_obj_t*  rates_grid[3];
static lv_obj_t*  rates_y_labels[3];
static lv_obj_t*  rates_x_labels[RATE_X_LABELS];
static lv_obj_t*  rates_first;
static lv_obj_t*  rates_last;
static lv_obj_t*  rates_empty;
static lv_obj_t*  rates_note;
static RatePoint  rates[RATES_MAX];
static int        rates_total = 0;
static lv_point_precise_t rates_pts_today[RATES_MAX];
static lv_point_precise_t rates_pts_prev[RATES_MAX];
static int        rates_plot_x, rates_plot_y, rates_plot_w, rates_plot_h;
static const char* const RATE_MONTHS[12] = {"jan", "fev", "mar", "abr", "mai", "jun",
                                            "jul", "ago", "set", "out", "nov", "dez"};

static lv_obj_t* make_rate_line(lv_obj_t* parent, lv_color_t color, int width) {
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    return l;
}

static void init_rates_screen(lv_obj_t* scr) {
    rates_container = make_screen_container(scr, "Juros Futuros");
    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(rates_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;
    const int axis_h  = lv_font_get_line_height(L.axis_font);
    const int stat_h  = lv_font_get_line_height(L.table_font_dense);

    // Plot: y labels on the left, year labels under it, two stat lines and the note below.
    const int chart_h = inner_h - axis_h - 2 * stat_h - axis_h - 16;
    rates_chart = lv_obj_create(panel);
    lv_obj_remove_style_all(rates_chart);
    lv_obj_set_pos(rates_chart, 0, 0);
    lv_obj_set_size(rates_chart, inner_w, chart_h + axis_h + 4);
    lv_obj_clear_flag(rates_chart, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(rates_chart, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* probe = make_dim_label(rates_chart, L.axis_font, "14,25%");
    lv_obj_update_layout(probe);
    rates_plot_x = lv_obj_get_width(probe) + 10;
    lv_obj_delete(probe);
    rates_plot_w = inner_w - rates_plot_x - 6;
    rates_plot_y = axis_h / 2 + 2;          // room for the top y label
    rates_plot_h = chart_h - rates_plot_y - 4;

    for (int i = 0; i < 3; i++) {
        rates_grid[i] = lv_obj_create(rates_chart);
        lv_obj_remove_style_all(rates_grid[i]);
        lv_obj_set_style_bg_color(rates_grid[i], COL_BAR_BG, 0);
        lv_obj_set_style_bg_opa(rates_grid[i], LV_OPA_COVER, 0);
        lv_obj_set_size(rates_grid[i], rates_plot_w, 1);
        rates_y_labels[i] = make_dim_label(rates_chart, L.axis_font, "");
        lv_obj_add_flag(rates_grid[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rates_y_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (auto& x : rates_x_labels) {
        x = make_dim_label(rates_chart, L.axis_font, "");
        lv_obj_add_flag(x, LV_OBJ_FLAG_HIDDEN);
    }
    rates_line_prev  = make_rate_line(rates_chart, COL_DIM, 2);
    rates_line_today = make_rate_line(rates_chart, accent_color, 4);

    const int stats_y = chart_h + axis_h + 8;
    rates_first = make_dim_label(panel, L.table_font_dense, "");
    lv_label_set_recolor(rates_first, true);
    lv_obj_set_pos(rates_first, 0, stats_y);
    rates_last = make_dim_label(panel, L.table_font_dense, "");
    lv_label_set_recolor(rates_last, true);
    lv_obj_set_pos(rates_last, 0, stats_y + stat_h);

    rates_empty = make_dim_label(panel, L.reset_font, "Buscando taxas...");
    lv_obj_align(rates_empty, LV_ALIGN_CENTER, 0, 0);
    rates_note = make_dim_label(panel, L.axis_font, "");
    lv_label_set_recolor(rates_note, true);
    lv_obj_align(rates_note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static int rate_months(int yymm) { return (yymm / 100) * 12 + (yymm % 100 - 1); }

static void format_rate(int32_t milli, char* buf, size_t len) {
    snprintf(buf, len, "%d,%02d%%", (int)(milli / 1000), (int)((milli % 1000 + 5) / 10));
}

// "jan/27  13,59%  -1 bp" with the move colored like the quote tables.
static void format_rate_stat(const char* prefix, const RatePoint& p, char* buf, size_t len) {
    char rate[16];
    format_rate(p.rate, rate, sizeof(rate));
    const int bp = (p.rate - p.prev + (p.rate >= p.prev ? 5 : -5)) / 10;
    const int mm = p.yymm % 100;
    snprintf(buf, len, "%s #faf9f5 %s/%02d#  #faf9f5 %s#  #%s %+d bp#", prefix,
             RATE_MONTHS[(mm >= 1 && mm <= 12) ? mm - 1 : 0], p.yymm / 100, rate,
             bp < 0 ? "c0392b" : bp > 0 ? "788c5d" : "b0aea5", bp);
    if (bp == 0) {                            // "+0 bp" reads oddly
        char* plus = strstr(buf, "+0 bp");
        if (plus) memmove(plus, plus + 1, strlen(plus));
    }
}

static void render_rates(void) {
    if (!rates_container) return;
    lv_label_set_text_fmt(rates_note, "B3 \xC2\xB7 DI1 \xC2\xB7 #%s hoje# \xC2\xB7 ajuste anterior", accent_hex);
    const int n = rates_total;
    const bool have = n >= 2;
    if (have) lv_obj_add_flag(rates_empty, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_clear_flag(rates_empty, LV_OBJ_FLAG_HIDDEN);
    if (!have) {
        lv_obj_add_flag(rates_chart, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(rates_first, "");
        lv_label_set_text(rates_last, "");
        return;
    }
    lv_obj_clear_flag(rates_chart, LV_OBJ_FLAG_HIDDEN);

    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int i = 0; i < n; i++) {
        lo = LV_MIN(lo, LV_MIN(rates[i].rate, rates[i].prev));
        hi = LV_MAX(hi, LV_MAX(rates[i].rate, rates[i].prev));
    }
    lo = (lo / 100) * 100;                    // round out to 0,1% steps
    hi = ((hi + 99) / 100) * 100;
    if (hi <= lo) hi = lo + 100;
    const int m0 = rate_months(rates[0].yymm);
    const int span = LV_MAX(1, rate_months(rates[n - 1].yymm) - m0);
    auto px = [&](int yymm) { return rates_plot_x + (rate_months(yymm) - m0) * rates_plot_w / span; };
    auto py = [&](int32_t milli) { return rates_plot_y + (int)((int64_t)(hi - milli) * rates_plot_h / (hi - lo)); };

    for (int i = 0; i < n; i++) {
        rates_pts_today[i] = {(lv_value_precise_t)px(rates[i].yymm), (lv_value_precise_t)py(rates[i].rate)};
        rates_pts_prev[i]  = {(lv_value_precise_t)px(rates[i].yymm), (lv_value_precise_t)py(rates[i].prev)};
    }
    lv_line_set_points(rates_line_prev, rates_pts_prev, n);
    lv_line_set_points(rates_line_today, rates_pts_today, n);

    const int axis_h = lv_font_get_line_height(L.axis_font);
    for (int i = 0; i < 3; i++) {
        const int32_t v = hi - (hi - lo) * i / 2;
        const int y = py(v);
        lv_obj_set_pos(rates_grid[i], rates_plot_x, y);
        char buf[16];
        format_rate(v, buf, sizeof(buf));
        lv_label_set_text(rates_y_labels[i], buf);
        lv_obj_set_pos(rates_y_labels[i], 0, y - axis_h / 2);
        lv_obj_clear_flag(rates_grid[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(rates_y_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Year ticks at each January, spaced so the labels never touch.
    const int per_year = rates_plot_w * 12 / span;
    const int step = LV_MAX(1, (44 + per_year - 1) / LV_MAX(1, per_year));
    int shown = 0;
    const int first_year = rates[0].yymm / 100 + (rates[0].yymm % 100 > 1 ? 1 : 0);
    const int last_year = rates[n - 1].yymm / 100;
    for (int yy = first_year; yy <= last_year && shown < RATE_X_LABELS; yy += step) {
        lv_obj_t* l = rates_x_labels[shown++];
        lv_label_set_text_fmt(l, "%02d", yy);
        lv_obj_update_layout(l);
        const int x = px(yy * 100 + 1) - lv_obj_get_width(l) / 2;
        lv_obj_set_pos(l, LV_MAX(rates_plot_x, LV_MIN(x, rates_plot_x + rates_plot_w - lv_obj_get_width(l))),
                       rates_plot_y + rates_plot_h + 8);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = shown; i < RATE_X_LABELS; i++) lv_obj_add_flag(rates_x_labels[i], LV_OBJ_FLAG_HIDDEN);

    char buf[128];
    format_rate_stat("Curto", rates[0], buf, sizeof(buf));
    lv_label_set_text(rates_first, buf);
    format_rate_stat("Longo", rates[n - 1], buf, sizeof(buf));
    lv_label_set_text(rates_last, buf);
}

void ui_update_rates(const RatePoint* points, int offset, int count, int total) {
    rates_total = total > RATES_MAX ? RATES_MAX : total;
    for (int i = 0; i < count; i++) {
        if (offset + i >= 0 && offset + i < rates_total) rates[offset + i] = points[i];
    }
    render_rates();
}

// ---- Google Calendar: today's meetings ----
static const char* const COL_HEX_DIM = "b0aea5";   // THEME_DIM
#define AGENDA_VISIBLE_ROWS 8
static lv_obj_t* agenda_container;
static lv_obj_t* agenda_cols[2];
static lv_obj_t* lbl_agenda_empty;
static AgendaRow agenda[AGENDA_MAX];
static int       agenda_total = 0;
static bool      agenda_needs_login = false;
static lv_obj_t* lbl_agenda_note;
static ScrollList agenda_list;

static void init_agenda_screen(lv_obj_t* scr) {
    agenda_container = make_screen_container(scr, "Agenda de Hoje");

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(agenda_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;

    const lv_font_t* font = L.table_font_dense;
    const int axis_h = lv_font_get_line_height(L.axis_font);
    const int line_h = lv_font_get_line_height(font);
    const int rows_y = axis_h + 4;
    const int row_h  = (inner_h - rows_y - axis_h - 4) / AGENDA_VISIBLE_ROWS;
    lv_obj_t* box = scroll_list_create(&agenda_list, panel, 0, rows_y, inner_w, AGENDA_VISIBLE_ROWS, row_h);

    static const char* const headers[2] = {"Horário", "Reunião"};
    static const QuoteColumn cols[2] = {
        {0, 330, LV_TEXT_ALIGN_LEFT, 0},
        {345, 1000, LV_TEXT_ALIGN_LEFT, 0},
    };
    for (int c = 0; c < 2; c++) {
        lv_obj_t* h = make_column(panel, L.axis_font, cols[c], inner_w, 0);
        lv_label_set_text(h, headers[c]);
        lv_obj_set_style_text_color(h, COL_DIM, 0);
        lv_obj_t* col = make_column(box, font, cols[c], inner_w, (row_h - line_h) / 2);
        lv_obj_set_height(col, row_h);
        lv_obj_set_style_text_line_space(col, row_h - line_h, 0);
        lv_label_set_recolor(col, true);
        agenda_cols[c] = col;
    }

    lbl_agenda_empty = make_dim_label(panel, L.reset_font, "Buscando agenda...");
    lv_obj_align(lbl_agenda_empty, LV_ALIGN_CENTER, 0, 0);

    lbl_agenda_note = make_dim_label(panel, L.axis_font, "");
    lv_label_set_recolor(lbl_agenda_note, true);
    lv_obj_align(lbl_agenda_note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_agenda(void) {
    lv_label_set_text_fmt(lbl_agenda_note, "Google Agenda \xC2\xB7 #%s agora#", accent_hex);
    // Row colors: finished meetings dim, the current one in the accent, upcoming white.
    static char text[2][AGENDA_MAX * 72];
    size_t used[2] = {};
    int shown = 0;
    int focus = -1, next = -1;
    for (int r = 0; r < agenda_total; r++) {
        const AgendaRow& a = agenda[r];
        if (!a.title[0]) continue;               // chunk not arrived yet
        // Opening focus: the meeting on now (else the next one), one row of context above.
        if (a.end[0] && a.state == 1 && focus < 0) focus = shown;
        if (a.end[0] && a.state == 2 && next < 0) next = shown;
        const char* sep = shown++ ? "\n" : "";
        const char* hex = a.state == 0 ? COL_HEX_DIM : a.state == 1 ? accent_hex : "faf9f5";
        if (a.end[0])
            used[0] += snprintf(text[0] + used[0], sizeof(text[0]) - used[0], "%s#%s %s-%s#", sep, hex, a.start, a.end);
        else
            used[0] += snprintf(text[0] + used[0], sizeof(text[0]) - used[0], "%s#%s Dia todo#", sep, hex);
        used[1] += snprintf(text[1] + used[1], sizeof(text[1]) - used[1], "%s#%s %s#", sep, hex, a.title);
    }
    for (int c = 0; c < 2; c++) {
        text[c][used[c] < sizeof(text[c]) ? used[c] : sizeof(text[c]) - 1] = '\0';
        lv_label_set_text(agenda_cols[c], text[c]);
        lv_obj_set_height(agenda_cols[c], agenda_list.row_h * (shown > 0 ? shown : 1));
    }
    agenda_list.rows = shown;
    if (focus < 0) focus = next >= 0 ? next : shown - 1;
    agenda_list.focus = focus > 0 ? focus - 1 : 0;
    lv_label_set_text(lbl_agenda_empty, agenda_needs_login ? "Falta o login do Google" : "Nenhuma reunião hoje");
    if (shown > 0) lv_obj_add_flag(lbl_agenda_empty, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_clear_flag(lbl_agenda_empty, LV_OBJ_FLAG_HIDDEN);
}

void ui_update_agenda(const AgendaRow* rows, int offset, int count, int total, bool needs_login) {
    if (!agenda_container) return;
    agenda_total = total > AGENDA_MAX ? AGENDA_MAX : total;
    agenda_needs_login = needs_login;
    for (int i = 0; i < count; i++) {
        if (offset + i >= 0 && offset + i < agenda_total) agenda[offset + i] = rows[i];
    }
    for (int j = agenda_total; j < AGENDA_MAX; j++) agenda[j] = AgendaRow{};
    render_agenda();
}

// ---- Routine failure alert ----
// A routine that just failed takes the screen: blinking red panel, its name and
// time, and two buttons. "Rodar de novo" asks the daemon to kickstart its
// LaunchAgent; "Dispensar" (or 10 minutes) lets rotation resume.
#define ROUTINE_ALERT_MAX_MS (10 * 60 * 1000)
#define ROUTINE_BLINK_MS     500
static lv_obj_t* ralert_container;
static lv_obj_t* ralert_panel;
static lv_obj_t* lbl_ralert_name;
static lv_obj_t* lbl_ralert_when;
static lv_obj_t* lbl_ralert_status;
static lv_obj_t* btn_ralert_rerun;
static bool      ralert_active = false;
static bool      ralert_rerunnable = false;
static uint32_t  ralert_since = 0, ralert_blink_ms = 0, ralert_close_at = 0;
static bool      ralert_blink_on = false;
static char      ralert_name[28] = "";

static void ralert_close(void);

static void ralert_rerun_cb(lv_event_t* e) {
    (void)e;
    if (!ralert_active || !ralert_rerunnable) return;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "{\"rr\":\"%s\"}", ralert_name);
    ble_send_command(cmd);
    lv_label_set_text(lbl_ralert_status, "Pedido enviado ao Mac...");
    lv_obj_add_state(btn_ralert_rerun, LV_STATE_DISABLED);
}

static void ralert_dismiss_cb(lv_event_t* e) {
    (void)e;
    ralert_close();
}

static lv_obj_t* make_alert_button(lv_obj_t* parent, const char* text, lv_color_t bg, lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 18, 0);
    lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, L.reset_font, 0);
    lv_obj_set_style_text_color(l, COL_TEXT, 0);
    lv_obj_center(l);
    return b;
}

static void init_routine_alert_screen(lv_obj_t* scr) {
    ralert_container = make_screen_container(scr, "Rotina falhou");

    const int panel_h = L.scr_h - L.content_y - L.margin;
    ralert_panel = make_panel(ralert_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;

    lbl_ralert_name = lv_label_create(ralert_panel);
    lv_label_set_long_mode(lbl_ralert_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_ralert_name, inner_w);
    lv_obj_set_style_text_font(lbl_ralert_name, L.pct_font, 0);
    lv_obj_set_style_text_color(lbl_ralert_name, COL_TEXT, 0);
    lv_obj_set_style_text_align(lbl_ralert_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_ralert_name, "");
    lv_obj_align(lbl_ralert_name, LV_ALIGN_TOP_MID, 0, 6);

    lbl_ralert_when = lv_label_create(ralert_panel);
    lv_obj_set_style_text_font(lbl_ralert_when, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_ralert_when, COL_TEXT, 0);
    lv_label_set_text(lbl_ralert_when, "");
    lv_obj_align(lbl_ralert_when, LV_ALIGN_TOP_MID, 0, lv_font_get_line_height(L.pct_font) + 12);

    lbl_ralert_status = lv_label_create(ralert_panel);
    lv_label_set_long_mode(lbl_ralert_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_ralert_status, inner_w);
    lv_obj_set_style_text_font(lbl_ralert_status, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_ralert_status, COL_TEXT, 0);
    lv_obj_set_style_text_align(lbl_ralert_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_ralert_status, "leo-dias-news \xC2\xB7 Slack");
    lv_obj_align(lbl_ralert_status, LV_ALIGN_CENTER, 0, 10);

    btn_ralert_rerun = make_alert_button(ralert_panel, "Rodar de novo", COL_KIRO, ralert_rerun_cb);
    lv_obj_align(btn_ralert_rerun, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_ralert_rerun, COL_BAR_BG, LV_STATE_DISABLED);
    lv_obj_t* dismiss = make_alert_button(ralert_panel, "Dispensar", COL_BAR_BG, ralert_dismiss_cb);
    lv_obj_align(dismiss, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void ralert_open(const RoutineRow& row) {
    if (!ralert_container) return;
    strlcpy(ralert_name, row.name, sizeof(ralert_name));
    ralert_rerunnable = row.rerunnable;
    lv_label_set_text(lbl_ralert_name, row.name);
    lv_label_set_text_fmt(lbl_ralert_when, "falhou às %s", row.time);
    lv_label_set_text(lbl_ralert_status, row.rerunnable ? "leo-dias-news \xC2\xB7 Slack"
                                                         : "Sem atalho para rodar de novo");
    if (row.rerunnable) {
        lv_obj_clear_flag(btn_ralert_rerun, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(btn_ralert_rerun, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(btn_ralert_rerun, LV_OBJ_FLAG_HIDDEN);
    }
    ralert_since = ralert_blink_ms = lv_tick_get();
    ralert_close_at = 0;
    if (!ralert_active) {
        ralert_active = true;
        tap_hold = false;
        ui_show_screen(SCREEN_ROUTINE_ALERT);
    }
}

static void ralert_close(void) {
    if (!ralert_active) return;
    ralert_active = false;
    lv_obj_set_style_bg_color(ralert_panel, COL_PANEL, 0);
    if (current_screen == SCREEN_ROUTINE_ALERT) ui_show_screen(SCREEN_ROUTINES);
}

void ui_rerun_ack(const char* name, bool ok) {
    if (!ralert_active || strcmp(name, ralert_name) != 0) return;
    lv_label_set_text(lbl_ralert_status, ok ? "Rotina iniciada no Mac" : "O Mac recusou ou falhou ao iniciar");
    if (ok) ralert_close_at = lv_tick_get() + 4000;
    else    lv_obj_clear_state(btn_ralert_rerun, LV_STATE_DISABLED);
}

static void ralert_tick(void) {
    if (!ralert_active) return;
    const uint32_t now = lv_tick_get();
    if (now - ralert_since > ROUTINE_ALERT_MAX_MS || (ralert_close_at && now >= ralert_close_at)) {
        ralert_close();
        return;
    }
    if (now - ralert_blink_ms >= ROUTINE_BLINK_MS) {
        ralert_blink_ms = now;
        ralert_blink_on = !ralert_blink_on;
        lv_obj_set_style_bg_color(ralert_panel, ralert_blink_on ? COL_RED : lv_color_hex(0x5a1a14), 0);
    }
}

// ---- Kiro routines (leo-dias-news) ----
#define ROUTINE_NEW_MS (60 * 1000)   // a routine that just ran is highlighted this long
static lv_obj_t*  routines_container;
static lv_obj_t*  routines_cols[4];
static lv_obj_t*  lbl_routines_empty;
static RoutineRow routines[ROUTINES_MAX];
static int        routines_total = 0;
static bool       routines_loaded = false;          // first payload seeds, later ones highlight
static uint32_t   routines_new_ms[ROUTINES_MAX] = {};
static char       routines_new_name[ROUTINES_MAX][28] = {};
static ScrollList routines_list;
#define ROUTINES_VISIBLE_ROWS 8

static void init_routines_screen(lv_obj_t* scr) {
    routines_container = make_screen_container(scr, "Rotinas Automáticas");

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(routines_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;

    const lv_font_t* font = L.table_font_dense;
    const int axis_h = lv_font_get_line_height(L.axis_font);
    const int line_h = lv_font_get_line_height(font);
    const int rows_y = axis_h + 4;
    const int row_h  = (inner_h - rows_y - axis_h - 4) / ROUTINES_VISIBLE_ROWS;
    lv_obj_t* box = scroll_list_create(&routines_list, panel, 0, rows_y, inner_w, ROUTINES_VISIBLE_ROWS, row_h);

    static const char* const headers[4] = {"Rotina", "Última", "Hoje", ""};
    static const QuoteColumn cols[4] = {
        {0, 570, LV_TEXT_ALIGN_LEFT, 0},
        {570, 745, LV_TEXT_ALIGN_RIGHT, 0},
        {745, 865, LV_TEXT_ALIGN_RIGHT, 0},
        {865, 1000, LV_TEXT_ALIGN_RIGHT, 0},
    };
    for (int c = 0; c < 4; c++) {
        lv_obj_t* h = make_column(panel, L.axis_font, cols[c], inner_w, 0);
        lv_label_set_text(h, headers[c]);
        lv_obj_set_style_text_color(h, COL_DIM, 0);
        lv_obj_t* col = make_column(box, font, cols[c], inner_w, (row_h - line_h) / 2);
        lv_obj_set_height(col, row_h);
        lv_obj_set_style_text_line_space(col, row_h - line_h, 0);
        lv_label_set_recolor(col, true);
        routines_cols[c] = col;
    }
    // Kiro screen: purple highlights, white text.
    lv_obj_set_style_text_color(routines_cols[1], COL_TEXT, 0);
    lv_obj_set_style_text_color(routines_cols[2], COL_TEXT, 0);

    lbl_routines_empty = make_dim_label(panel, L.reset_font, "Buscando rotinas...");
    lv_obj_align(lbl_routines_empty, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* note = make_dim_label(panel, L.axis_font, "leo-dias-news \xC2\xB7 Slack");
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_routines(void) {
    const uint32_t now = lv_tick_get();
    static char text[4][ROUTINES_MAX * 48];
    size_t used[4] = {};
    int shown = 0;
    for (int r = 0; r < routines_total; r++) {
        const RoutineRow& row = routines[r];
        if (!row.name[0]) continue;              // chunk not arrived yet
        const char* sep = shown++ ? "\n" : "";
        bool fresh = false;
        for (int i = 0; i < ROUTINES_MAX; i++) {
            if (routines_new_ms[i] && now - routines_new_ms[i] < ROUTINE_NEW_MS &&
                strcmp(routines_new_name[i], row.name) == 0) fresh = true;
        }
        if (fresh)
            used[0] += snprintf(text[0] + used[0], sizeof(text[0]) - used[0], "%s#%s %s#", sep, row.codex ? COL_HEX_CODEX : COL_HEX_KIRO, row.name);
        else
            used[0] += snprintf(text[0] + used[0], sizeof(text[0]) - used[0], "%s%s", sep, row.name);
        used[1] += snprintf(text[1] + used[1], sizeof(text[1]) - used[1], "%s%s", sep, row.time);
        used[2] += snprintf(text[2] + used[2], sizeof(text[2]) - used[2], "%s%d\xC3\x97", sep, row.runs);
        used[3] += snprintf(text[3] + used[3], sizeof(text[3]) - used[3], "%s#%s %s#", sep,
                            row.ok ? (row.codex ? COL_HEX_CODEX : COL_HEX_KIRO) : COL_HEX_RED,
                            row.status == 2 ? "exec" : row.status == 3 ? "11h" : row.ok ? "ok" : "erro");
    }
    for (int c = 0; c < 4; c++) {
        text[c][used[c] < sizeof(text[c]) ? used[c] : sizeof(text[c]) - 1] = '\0';
        lv_label_set_text(routines_cols[c], text[c]);
        lv_obj_set_height(routines_cols[c], routines_list.row_h * (shown > 0 ? shown : 1));
    }
    routines_list.rows = shown;
    lv_label_set_text(lbl_routines_empty, "Nenhuma rotina hoje");
    if (routines_total > 0) lv_obj_add_flag(lbl_routines_empty, LV_OBJ_FLAG_HIDDEN);
    else                    lv_obj_clear_flag(lbl_routines_empty, LV_OBJ_FLAG_HIDDEN);
}

void ui_update_routines(const RoutineRow* rows, int offset, int count, int total) {
    if (!routines_container) return;
    const uint32_t now = lv_tick_get();
    routines_total = total > ROUTINES_MAX ? ROUTINES_MAX : total;
    for (int i = 0; i < count; i++) {
        const int at = offset + i;
        if (at < 0 || at >= routines_total) continue;
        // A new run (different latest time, or a routine not listed before) gets highlighted.
        bool is_new = true;
        for (int j = 0; j < ROUTINES_MAX; j++) {
            if (strcmp(routines[j].name, rows[i].name) == 0 && strcmp(routines[j].time, rows[i].time) == 0 &&
                routines[j].status == rows[i].status) {
                is_new = false;
                break;
            }
        }
        if (is_new && routines_loaded && !rows[i].ok) ralert_open(rows[i]);
        if (is_new && routines_loaded) {
            int slot = 0;
            for (int j = 1; j < ROUTINES_MAX; j++) if (routines_new_ms[j] < routines_new_ms[slot]) slot = j;
            routines_new_ms[slot] = now ? now : 1;
            strlcpy(routines_new_name[slot], rows[i].name, sizeof(routines_new_name[slot]));
        }
    }
    for (int i = 0; i < count; i++) {
        const int at = offset + i;
        if (at >= 0 && at < routines_total) routines[at] = rows[i];
    }
    for (int j = routines_total; j < ROUTINES_MAX; j++) routines[j] = RoutineRow{};
    if (offset + count >= routines_total) routines_loaded = true;
    render_routines();
}

// Clears the "just ran" highlight once it expires.
static void routines_tick(void) {
    static uint32_t last = 0;
    const uint32_t now = lv_tick_get();
    if (now - last < 5000) return;
    last = now;
    for (int i = 0; i < ROUTINES_MAX; i++) {
        if (routines_new_ms[i] && now - routines_new_ms[i] >= ROUTINE_NEW_MS) {
            routines_new_ms[i] = 0;
            render_routines();
        }
    }
}

// ---- Cronograma de posts (Instagram e TikTok) ----
// Uma linha por lançamento, com o logo da rede; verde = enviado, vermelho = não
// enviado, cinza = agendado. Dados: daemon/posts_schedule.py.
#define POSTS_VISIBLE_ROWS 8
static lv_obj_t*  posts_container;
static lv_obj_t*  posts_cols[3];
static lv_obj_t*  posts_icons[POSTS_MAX];
static lv_obj_t*  lbl_posts_empty;
static lv_obj_t*  lbl_posts_note;
static PostRow*   posts;                    // PSRAM: internal RAM is needed by the RGB panel's bounce buffers
static char*      posts_text;               // 3 columns x POSTS_TEXT_COL bytes, also PSRAM
#define POSTS_TEXT_COL (POSTS_MAX * 64)
static int        posts_total = 0;
static lv_image_dsc_t posts_dscs[2];        // Instagram, TikTok
static ScrollList posts_list;

static void* posts_alloc(size_t bytes) {
    void* p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);   // boards without PSRAM
}

static void init_posts_screen(lv_obj_t* scr) {
    posts = (PostRow*)posts_alloc(sizeof(PostRow) * POSTS_MAX);
    posts_text = (char*)posts_alloc(3 * POSTS_TEXT_COL);
    posts_container = make_screen_container(scr, "Cronograma");

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(posts_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;

    // One step smaller than the other lists: the title needs room next to the logo, date and status.
    const lv_font_t* font = L.table_font_dense == &font_styrene_20 ? &font_styrene_16 : L.table_font_dense;
    const int axis_h = lv_font_get_line_height(L.axis_font);
    const int line_h = lv_font_get_line_height(font);
    const int rows_y = axis_h + 4;
    const int row_h  = (inner_h - rows_y - axis_h - 4) / POSTS_VISIBLE_ROWS;
    lv_obj_t* box = scroll_list_create(&posts_list, panel, 0, rows_y, inner_w, POSTS_VISIBLE_ROWS, row_h);

    init_icon_dsc_rgb565a8(&posts_dscs[0], ICON_INSTAGRAM_W, ICON_INSTAGRAM_H, icon_instagram_data);
    init_icon_dsc_rgb565a8(&posts_dscs[1], ICON_TIKTOK_W, ICON_TIKTOK_H, icon_tiktok_data);

    static const char* const headers[3] = {"Quando", "Post", "Envio"};
    static const QuoteColumn cols[3] = {
        {90, 315, LV_TEXT_ALIGN_LEFT, 0},
        {330, 725, LV_TEXT_ALIGN_LEFT, 0},
        {730, 1000, LV_TEXT_ALIGN_RIGHT, 0},
    };
    for (int c = 0; c < 3; c++) {
        lv_obj_t* h = make_column(panel, L.axis_font, cols[c], inner_w, 0);
        lv_label_set_text(h, headers[c]);
        lv_obj_set_style_text_color(h, COL_DIM, 0);
        lv_obj_t* col = make_column(box, font, cols[c], inner_w, (row_h - line_h) / 2);
        lv_obj_set_height(col, row_h);
        lv_obj_set_style_text_line_space(col, row_h - line_h, 0);
        lv_label_set_recolor(col, true);
        posts_cols[c] = col;
    }
    // A image per row (created once, moved and shown by render_posts): the logo scrolls with the list.
    const int icon_h = row_h - 6 < ICON_INSTAGRAM_H ? row_h - 6 : ICON_INSTAGRAM_H;
    for (int i = 0; i < POSTS_MAX; i++) {
        lv_obj_t* img = lv_image_create(box);
        lv_image_set_scale(img, 256 * icon_h / ICON_INSTAGRAM_H);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);
        lv_obj_set_size(img, ICON_INSTAGRAM_W, ICON_INSTAGRAM_H);
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        posts_icons[i] = img;
    }

    lbl_posts_empty = make_dim_label(panel, L.reset_font, "Buscando cronograma...");
    lv_obj_align(lbl_posts_empty, LV_ALIGN_CENTER, 0, 0);

    lbl_posts_note = make_dim_label(panel, L.axis_font, "");
    lv_label_set_recolor(lbl_posts_note, true);
    lv_obj_align(lbl_posts_note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void render_posts(void) {
    char (*text)[POSTS_TEXT_COL] = (char (*)[POSTS_TEXT_COL])posts_text;
    size_t used[3] = {};
    int shown = 0, sent = 0, missed = 0, scheduled = 0, focus = -1;
    for (int r = 0; r < posts_total; r++) {
        const PostRow& p = posts[r];
        if (!p.title[0]) continue;               // chunk not arrived yet
        const char* sep = shown ? "\n" : "";
        const char* hex = p.state == POST_SENT ? "faf9f5" : p.state == POST_MISSED ? COL_HEX_RED : COL_HEX_DIM;
        const char* word = p.state == POST_SENT ? "enviado" : p.state == POST_MISSED ? "n\xC3\xA3o enviado" : "agendado";
        const char* wcol = p.state == POST_SENT ? COL_HEX_GREEN : hex;
        used[0] += snprintf(text[0] + used[0], sizeof(text[0]) - used[0], "%s#%s %s#", sep, hex, p.when);
        used[1] += snprintf(text[1] + used[1], sizeof(text[1]) - used[1], "%s#%s %s#", sep, hex, p.title);
        used[2] += snprintf(text[2] + used[2], sizeof(text[2]) - used[2], "%s#%s %s#", sep, wcol, word);
        if (p.state == POST_SENT) sent++;
        else if (p.state == POST_MISSED) missed++;
        else scheduled++;
        if (focus < 0 && p.state != POST_SENT) focus = shown;   // opens on the first one not sent
        lv_obj_t* img = posts_icons[shown];
        lv_image_set_src(img, &posts_dscs[p.net == POST_NET_TIKTOK ? 1 : 0]);
        lv_obj_set_pos(img, 0, shown * posts_list.row_h + (posts_list.row_h - ICON_INSTAGRAM_H) / 2);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
        shown++;
    }
    for (int i = shown; i < POSTS_MAX; i++) lv_obj_add_flag(posts_icons[i], LV_OBJ_FLAG_HIDDEN);
    for (int c = 0; c < 3; c++) {
        text[c][used[c] < sizeof(text[c]) ? used[c] : sizeof(text[c]) - 1] = '\0';
        lv_label_set_text(posts_cols[c], text[c]);
        lv_obj_set_height(posts_cols[c], posts_list.row_h * (shown > 0 ? shown : 1));
    }
    posts_list.rows = shown;
    if (focus < 0) focus = shown;
    posts_list.focus = focus > 0 ? focus - 1 : 0;
    lv_label_set_text_fmt(lbl_posts_note, "#%s %d enviados# \xC2\xB7 #%s %d n\xC3\xA3o enviados# \xC2\xB7 %d agendados",
                          COL_HEX_GREEN, sent, COL_HEX_RED, missed, scheduled);
    if (shown > 0) lv_obj_add_flag(lbl_posts_empty, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_clear_flag(lbl_posts_empty, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_posts_empty, "Nenhum post no cronograma");
}

void ui_update_posts(const PostRow* rows, int offset, int count, int total) {
    if (!posts_container || !posts) return;
    posts_total = total > POSTS_MAX ? POSTS_MAX : total;
    for (int i = 0; i < count; i++) {
        if (offset + i >= 0 && offset + i < posts_total) posts[offset + i] = rows[i];
    }
    for (int j = posts_total; j < POSTS_MAX; j++) posts[j] = PostRow{};
    if (offset + count < posts_total) return;    // redraw once, after the last chunk (avoids flicker)
    uint32_t h = 2166136261u;                    // the daemon resends the list every minute: skip if unchanged
    const uint8_t* b = (const uint8_t*)posts;
    for (size_t i = 0; i < sizeof(PostRow) * posts_total; i++) h = (h ^ b[i]) * 16777619u;
    static uint32_t last_hash = 0;
    static bool have_hash = false;
    if (have_hash && h == last_hash) return;
    last_hash = h; have_hash = true;
    render_posts();
}

static bool live_is_active(void);

// ---- Meeting alert (Google Agenda) ----
// Five minutes before a meeting the screen takes over with a countdown, like the
// live match lock; it lets go a minute after the start or when the daemon clears it.
#define MEETING_GRACE_MS  (60 * 1000)
#define MEETING_STALE_MS  (7 * 60 * 1000)   // daemon gone: don't hold a stale alert
static lv_obj_t* meeting_container;
static lv_obj_t* lbl_meeting_count;
static lv_obj_t* lbl_meeting_badge;
static lv_obj_t* lbl_meeting_title;
static lv_obj_t* lbl_meeting_when;
static lv_obj_t* lbl_meeting_where;
static lv_obj_t* btn_meeting_join;
static char      meeting_where[64] = "";
static bool      meeting_active = false;
static uint32_t  meeting_start_ms = 0;       // lv_tick at which the meeting starts
static uint32_t  meeting_rx_ms = 0;
static int       meeting_shown_secs = -99999;

// "Começar": the daemon opens this meeting's own video link on the Mac.
static void meeting_join_cb(lv_event_t* e) {
    (void)e;
    if (!meeting_active) return;
    ble_send_command("{\"mg\":1}");
    lv_label_set_text(lbl_meeting_where, "Abrindo no Mac...");
    lv_obj_add_state(btn_meeting_join, LV_STATE_DISABLED);
}

void ui_meeting_join_ack(bool ok) {
    if (!meeting_active) return;
    lv_label_set_text(lbl_meeting_where, ok ? "Reunião aberta no Mac" : "Não consegui abrir no Mac");
    if (!ok) lv_obj_clear_state(btn_meeting_join, LV_STATE_DISABLED);
}

static void init_meeting_screen(lv_obj_t* scr) {
    meeting_container = make_screen_container(scr, "Reunião");

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(meeting_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;

    lbl_meeting_badge = make_dim_label(panel, L.axis_font, "COMEÇA EM");
    lv_obj_align(lbl_meeting_badge, LV_ALIGN_TOP_MID, 0, 0);

    lbl_meeting_count = lv_label_create(panel);
    lv_label_set_text(lbl_meeting_count, "--:--");
    lv_obj_set_style_text_font(lbl_meeting_count, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_meeting_count, COL_TEXT, 0);
    lv_obj_align(lbl_meeting_count, LV_ALIGN_TOP_MID, 0, lv_font_get_line_height(L.axis_font) + 4);

    lbl_meeting_title = lv_label_create(panel);
    lv_label_set_long_mode(lbl_meeting_title, LV_LABEL_LONG_DOT);
    lv_obj_set_size(lbl_meeting_title, inner_w, lv_font_get_line_height(L.reset_font) * 2 + 4);
    lv_obj_set_style_text_font(lbl_meeting_title, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_meeting_title, COL_TEXT, 0);
    lv_obj_set_style_text_align(lbl_meeting_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_meeting_title, "");
    lv_obj_align(lbl_meeting_title, LV_ALIGN_CENTER, 0, -6);

    // "Começar" at the bottom; time and link sit just above it.
    btn_meeting_join = make_alert_button(panel, "Começar", accent_color, meeting_join_cb);
    lv_obj_align(btn_meeting_join, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_meeting_join, COL_BAR_BG, LV_STATE_DISABLED);
    lv_obj_add_flag(btn_meeting_join, LV_OBJ_FLAG_HIDDEN);
    const int btn_h = lv_font_get_line_height(L.reset_font) + 24;

    lbl_meeting_when = make_dim_label(panel, L.reset_font, "");
    lv_obj_align(lbl_meeting_when, LV_ALIGN_BOTTOM_MID, 0, -btn_h - lv_font_get_line_height(L.axis_font) - 14);

    lbl_meeting_where = lv_label_create(panel);
    lv_label_set_long_mode(lbl_meeting_where, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_meeting_where, inner_w);
    lv_obj_set_style_text_font(lbl_meeting_where, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_meeting_where, accent_color, 0);
    lv_obj_set_style_text_align(lbl_meeting_where, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_meeting_where, "");
    lv_obj_align(lbl_meeting_where, LV_ALIGN_BOTTOM_MID, 0, -btn_h - 8);
}

static void render_meeting_count(void) {
    const int32_t left_ms = (int32_t)(meeting_start_ms - lv_tick_get());
    const int secs = left_ms > 0 ? (left_ms + 999) / 1000 : -1;
    if (secs == meeting_shown_secs) return;
    meeting_shown_secs = secs;
    lv_label_set_text(lbl_meeting_badge, secs < 0 ? "COMEÇOU" : "COMEÇA EM");
    if (secs < 0) {
        lv_label_set_text(lbl_meeting_count, "Agora");
        lv_obj_set_style_text_color(lbl_meeting_count, accent_color, 0);
    } else {
        lv_label_set_text_fmt(lbl_meeting_count, "%d:%02d", secs / 60, secs % 60);
        lv_obj_set_style_text_color(lbl_meeting_count, secs <= 60 ? accent_color : COL_TEXT, 0);
    }
}

void ui_update_meeting(const char* title, const char* hhmm, int seconds, const char* where, bool joinable) {
    if (!meeting_container) return;
    if (!title) {
        if (!meeting_active) return;
        meeting_active = false;
        meeting_where[0] = '\0';
        if (current_screen == SCREEN_MEETING) ui_show_screen(live_is_active() ? SCREEN_LIVE : ROTATION[0].screen);
        return;
    }
    const uint32_t now = lv_tick_get();
    meeting_rx_ms = now;
    meeting_start_ms = now + (uint32_t)(seconds > 0 ? seconds : 0) * 1000;
    meeting_shown_secs = -99999;
    lv_label_set_text(lbl_meeting_title, title);
    lv_label_set_text_fmt(lbl_meeting_when, "às %s", hhmm);
    // A refresh keeps "Abrindo no Mac..." / the result while the same meeting is up.
    const bool same = meeting_active && strcmp(meeting_where, where) == 0;
    if (!same) {
        strlcpy(meeting_where, where, sizeof(meeting_where));
        lv_label_set_text(lbl_meeting_where, where);
        lv_obj_clear_state(btn_meeting_join, LV_STATE_DISABLED);
    }
    if (joinable) lv_obj_clear_flag(btn_meeting_join, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(btn_meeting_join, LV_OBJ_FLAG_HIDDEN);
    render_meeting_count();
    if (!meeting_active) {
        meeting_active = true;
        tap_hold = false;                    // the alert takes the screen right away
        ui_show_screen(SCREEN_MEETING);
    }
}

static void meeting_tick(void) {
    if (!meeting_active) return;
    const uint32_t now = lv_tick_get();
    if ((int32_t)(now - meeting_start_ms) > MEETING_GRACE_MS || now - meeting_rx_ms > MEETING_STALE_MS) {
        ui_update_meeting(nullptr, "", 0, "", false);
        return;
    }
    render_meeting_count();
}

static lv_obj_t* games_container;
static lv_obj_t* lbl_games_empty;
static lv_obj_t* lbl_game_match[GAMES_MAX];
static lv_obj_t* lbl_game_detail[GAMES_MAX];
static GameRow   games[GAMES_MAX];
static int       games_total = 0;
static lv_obj_t* game_rows[GAMES_MAX];
static ScrollList games_list;
#define GAMES_VISIBLE_ROWS 4

// ---- Live match ----
#define LIVE_STALE_MS  (3 * 60 * 1000)   // no update this long (daemon gone) → release the screen
#define LIVE_GOAL_MS   (60 * 1000)       // a changed score stays highlighted this long
static lv_obj_t* live_container;
static lv_obj_t* lbl_live_comp;
static lv_obj_t* lbl_live_team[2];
static lv_obj_t* lbl_live_score[2];
static lv_obj_t* lbl_live_status;
static bool      live_active = false;
static uint32_t  live_last_ms = 0;
static int       live_goals[2] = {-1, -1};
static uint32_t  live_goal_ms[2] = {0, 0};

static void init_live_screen(lv_obj_t* scr) {
    live_container = make_screen_container(scr, "Vasco ao vivo");

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(live_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;

    const int dot = lv_font_get_line_height(L.axis_font) / 2;
    lv_obj_t* live_dot = lv_obj_create(panel);   // fonts are Latin-1 only, so draw the dot
    lv_obj_set_size(live_dot, dot, dot);
    lv_obj_set_style_radius(live_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(live_dot, COL_RED, 0);
    lv_obj_set_style_border_width(live_dot, 0, 0);
    lv_obj_clear_flag(live_dot, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(live_dot, 0, dot / 2);

    lv_obj_t* badge = lv_label_create(panel);
    lv_label_set_text(badge, "AO VIVO");
    lv_obj_set_style_text_font(badge, L.axis_font, 0);
    lv_obj_set_style_text_color(badge, COL_RED, 0);
    lv_obj_set_pos(badge, dot + 6, 0);

    lbl_live_comp = make_dim_label(panel, L.axis_font, "");
    lv_obj_align(lbl_live_comp, LV_ALIGN_TOP_RIGHT, 0, 0);

    const int score_h = lv_font_get_line_height(&font_tiempos_56);
    const int score_w = inner_w / 4;
    const int axis_h  = lv_font_get_line_height(L.axis_font);
    const int rows_top = axis_h + 8;
    const int status_h = lv_font_get_line_height(L.reset_font);
    const int row_h = (inner_h - rows_top - status_h - 8) / 2;
    for (int i = 0; i < 2; i++) {
        const int y = rows_top + i * row_h + (row_h - score_h) / 2;
        lbl_live_team[i] = lv_label_create(panel);
        lv_label_set_long_mode(lbl_live_team[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_live_team[i], inner_w - score_w);
        lv_obj_set_style_text_font(lbl_live_team[i], L.reset_font, 0);
        lv_obj_set_style_text_color(lbl_live_team[i], COL_TEXT, 0);
        lv_obj_set_pos(lbl_live_team[i], 0, y + (score_h - status_h) / 2);

        lbl_live_score[i] = lv_label_create(panel);
        lv_label_set_text(lbl_live_score[i], "-");
        lv_obj_set_width(lbl_live_score[i], score_w);
        lv_obj_set_style_text_align(lbl_live_score[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(lbl_live_score[i], &font_tiempos_56, 0);
        lv_obj_set_style_text_color(lbl_live_score[i], COL_TEXT, 0);
        lv_obj_set_pos(lbl_live_score[i], inner_w - score_w, y);
    }

    lbl_live_status = lv_label_create(panel);
    lv_label_set_text(lbl_live_status, "");
    lv_obj_set_style_text_font(lbl_live_status, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_live_status, accent_color, 0);
    lv_obj_align(lbl_live_status, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void ui_update_kiro(int percent, int reset_days, int credits_used, int credit_limit) {
    kiro_pct = percent;
    kiro_reset_days = reset_days;
    if (!panel_kiro) return;
    set_usage_pct(UP_KIRO, percent < 0 ? -1 : percent);
    if (hint_kiro) {
        char hb[16];
        format_reset_short_days(percent < 0 ? -1 : reset_days, hb, sizeof(hb));
        lv_label_set_text(hint_kiro, hb);
    }
    if (percent < 0) {
        lv_label_set_text(lbl_kiro_pct, "---%");
        lv_bar_set_value(bar_kiro, 0, LV_ANIM_OFF);
        lv_label_set_text(lbl_kiro_reset, "Sem dados do Kiro");
        return;
    }
    lv_label_set_text_fmt(lbl_kiro_pct, "%d%%", percent);
    lv_bar_set_value(bar_kiro, percent > 100 ? 100 : percent, LV_ANIM_ON);
    if (credits_used >= 0 && credit_limit > 0) {
        char used[16], limit[16];
        format_int_ptbr(credits_used, used, sizeof(used));
        format_int_ptbr(credit_limit, limit, sizeof(limit));
        lv_label_set_text_fmt(lbl_kiro_reset, "%s de %s créditos \xC2\xB7 renova em %dd", used, limit, reset_days);
    } else {
        lv_label_set_text_fmt(lbl_kiro_reset, "Renova em %d %s", reset_days, reset_days == 1 ? "dia" : "dias");
    }
}

void ui_update_live(const LiveMatch* m) {
    if (!live_container) return;
    if (!m) {
        if (!live_active) return;
        live_active = false;
        live_goals[0] = live_goals[1] = -1;
        if (current_screen == SCREEN_LIVE) ui_show_screen(ROTATION[0].screen);
        return;
    }
    const uint32_t now = lv_tick_get();
    lv_label_set_text(lbl_live_comp, m->competition);
    lv_label_set_text(lbl_live_team[0], m->home);
    lv_label_set_text(lbl_live_team[1], m->away);
    const int goals[2] = {m->home_goals, m->away_goals};
    for (int i = 0; i < 2; i++) {
        if (live_goals[i] >= 0 && goals[i] > live_goals[i]) {
            live_goal_ms[i] = now;
            if (strstr(i == 0 ? m->home : m->away, "Vasco")) splash_almirante_goal();   // our goal
        }
        live_goals[i] = goals[i];
        lv_label_set_text_fmt(lbl_live_score[i], "%d", goals[i]);
    }
    if (m->clock[0]) lv_label_set_text_fmt(lbl_live_status, "%s · %s", m->phase, m->clock);
    else             lv_label_set_text(lbl_live_status, m->phase);

    live_last_ms = now;
    if (!live_active) {
        live_active = true;
        tap_hold = false;                   // a match takes the screen right away
        ui_show_screen(SCREEN_LIVE);
    }
}

static bool live_is_active(void) { return live_active; }

static void live_tick(void) {
    if (!live_active) return;
    const uint32_t now = lv_tick_get();
    if (now - live_last_ms > LIVE_STALE_MS) {
        ui_update_live(nullptr);
        return;
    }
    for (int i = 0; i < 2; i++) {
        const bool goal = live_goal_ms[i] && now - live_goal_ms[i] < LIVE_GOAL_MS;
        lv_obj_set_style_text_color(lbl_live_score[i], goal ? accent_color : COL_TEXT, 0);
    }
}

static void init_games_screen(lv_obj_t* scr) {
    games_container = make_screen_container(scr, "Jogos do Vasco");

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(games_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;

    const int axis_h  = lv_font_get_line_height(L.axis_font);
    const int match_h = lv_font_get_line_height(L.reset_font);
    const int block_h = (inner_h - axis_h - 6) / GAMES_VISIBLE_ROWS;
    lv_obj_t* box = scroll_list_create(&games_list, panel, 0, 0, inner_w, GAMES_VISIBLE_ROWS, block_h);

    for (int i = 0; i < GAMES_MAX; i++) {
        lv_obj_t* row = lv_obj_create(box);
        lv_obj_set_pos(row, 0, i * block_h);
        lv_obj_set_size(row, inner_w, block_h);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        game_rows[i] = row;

        lbl_game_match[i] = lv_label_create(row);
        lv_label_set_long_mode(lbl_game_match[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl_game_match[i], inner_w);
        lv_obj_set_pos(lbl_game_match[i], 0, 0);
        lv_obj_set_style_text_font(lbl_game_match[i], L.reset_font, 0);
        lv_obj_set_style_text_color(lbl_game_match[i], COL_TEXT, 0);

        lbl_game_detail[i] = make_dim_label(row, L.axis_font, "");
        lv_label_set_long_mode(lbl_game_detail[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl_game_detail[i], inner_w);
        lv_obj_set_pos(lbl_game_detail[i], 0, match_h + 2);
        if (i == 0) lv_obj_set_style_text_color(lbl_game_detail[i], accent_color, 0);
    }

    lbl_games_empty = make_dim_label(panel, L.reset_font, "Buscando jogos...");
    lv_obj_align(lbl_games_empty, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* note = make_dim_label(panel, L.axis_font, "Próximos jogos · ESPN");
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void ui_update_games(const GameRow* rows, int offset, int count, int total) {
    if (!games_container) return;
    games_total = total > GAMES_MAX ? GAMES_MAX : total;
    for (int i = 0; i < count; i++) {
        if (offset + i >= 0 && offset + i < games_total) games[offset + i] = rows[i];
    }

    char buf[64];
    for (int i = 0; i < GAMES_MAX; i++) {
        if (i >= games_total) {
            lv_obj_add_flag(game_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const GameRow& g = games[i];
        if (g.home) snprintf(buf, sizeof(buf), "Vasco x %s", g.opponent);
        else        snprintf(buf, sizeof(buf), "%s x Vasco", g.opponent);
        lv_label_set_text(lbl_game_match[i], buf);
        snprintf(buf, sizeof(buf), "%s · %s", g.kickoff, g.competition);
        lv_label_set_text(lbl_game_detail[i], buf);
        lv_obj_clear_flag(game_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    games_list.rows = games_total;
    lv_label_set_text(lbl_games_empty, "Nenhum jogo marcado");
    if (games_total > 0) lv_obj_add_flag(lbl_games_empty, LV_OBJ_FLAG_HIDDEN);
    else                 lv_obj_clear_flag(lbl_games_empty, LV_OBJ_FLAG_HIDDEN);
}

// {"hb": [claude, kiro], "tc": tokens, "tk": requests}: two 24-char base64
// strings, each hour's segment already scaled so the tallest stack fills the plot.
void ui_update_history_bars(const char* claude, const char* kiro, const char* ag, const char* codex,
                            uint64_t tokens, float kiro_credits, uint64_t ag_tokens, uint64_t codex_tokens) {
    if (!lbl_history_now) return;
    static const char B64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char* series[4] = {claude, ag, codex, kiro};          // stacked bottom to top
    lv_obj_t** bars[4] = {history_claude_bar, history_ag_bar, history_codex_bar, history_kiro_bar};
    const size_t len[4] = {strlen(claude), strlen(ag), strlen(codex), strlen(kiro)};
    const int bottom = 6 + history_plot_h;
    bool any = false;
    for (int i = 0; i < HISTORY_HOURS; i++) {
        int y = bottom;
        const int x = lv_obj_get_x(history_claude_bar[i]);
        for (int k = 0; k < 4; k++) {
            const char* hit = i < (int)len[k] ? strchr(B64, series[k][i]) : nullptr;
            const int h = hit && *hit ? (int)(hit - B64) * history_plot_h / 63 : 0;
            lv_obj_set_size(bars[k][i], lv_obj_get_width(bars[k][i]), h);
            lv_obj_set_pos(bars[k][i], x, y - h);
            if (h) lv_obj_clear_flag(bars[k][i], LV_OBJ_FLAG_HIDDEN);
            else   lv_obj_add_flag(bars[k][i], LV_OBJ_FLAG_HIDDEN);
            y -= h;
            any = any || h;
        }
    }
    char buf[24];
    format_tokens(tokens, buf, sizeof(buf));
    lv_label_set_text(lbl_history_now, buf);
    format_tokens(ag_tokens, buf, sizeof(buf));
    lv_label_set_text(lbl_history_ag, buf);
    format_tokens(codex_tokens, buf, sizeof(buf));
    lv_label_set_text(lbl_history_codex, buf);
    format_credits(kiro_credits, buf, sizeof(buf));
    {
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "%s créd.", buf);
        lv_label_set_text(lbl_history_peak, kbuf);
    }
    history_place_units();
    lv_label_set_text(lbl_history_empty, "Sem uso nas últimas 24h");
    if (any) lv_obj_add_flag(lbl_history_empty, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_clear_flag(lbl_history_empty, LV_OBJ_FLAG_HIDDEN);
}

// ======== Public API ========

void ui_init(void) {
    compute_layout(board_caps());

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

#ifndef BOARD_HAS_PSRAM
    // Static corner mascot (see clawd_still.h) — the animated one needs PSRAM.
    if (L.small_icons) init_icon_dsc_rgb565a8(&logo_dsc, CLAWD_STILL_SMALL_W, CLAWD_STILL_SMALL_H, clawd_still_small_data);
    else               init_icon_dsc_rgb565a8(&logo_dsc, CLAWD_STILL_W, CLAWD_STILL_H, clawd_still_data);
#endif
    init_battery_icons();

    init_usage_screen(scr);
    init_history_screen(scr);
    init_market_screens(scr);
    init_agenda_screen(scr);
    init_rates_screen(scr);
    init_routines_screen(scr);
    init_posts_screen(scr);
    init_games_screen(scr);
    init_live_screen(scr);
    init_meeting_screen(scr);
    init_routine_alert_screen(scr);
    splash_init(scr);

    if (splash_get_root()) {
#ifdef BOARD_HAS_PSRAM
        make_screen_draggable(splash_get_root());   // PSRAM-less boards draw the splash past LVGL
#else
        lv_obj_add_event_cb(splash_get_root(), screen_pressed_cb, LV_EVENT_PRESSED, NULL);
#endif
        lv_obj_add_event_cb(splash_get_root(), global_click_cb, LV_EVENT_CLICKED, NULL);
    }

    // Corner mascot in the old logo slot. The still Clawd is shorter than the
    // 80/40 px slot the spark logo used; center it vertically in that slot.
    {
        const int slot  = L.small_icons ? LOGO_SMALL_HEIGHT : LOGO_HEIGHT;
        const int art_h = L.small_icons ? CLAWD_STILL_SMALL_H : CLAWD_STILL_H;
        const int top   = L.logo_y + (slot - art_h) / 2;
#ifdef BOARD_HAS_PSRAM
        // Animated: idles, does acts, and takes walk-off/lurk trips.
        splash_mascot_create(scr, L.margin, top + art_h, L.small_icons ? 2 : 3);
#else
        logo_img = lv_image_create(scr);
        lv_image_set_src(logo_img, &logo_dsc);
        lv_obj_set_pos(logo_img, L.margin, top);
#endif
    }

    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, L.scr_w - L.batt_w - L.margin, L.batt_y);
    // Boards without battery telemetry never show the indicator (per the HAL
    // contract; previously every board drew the empty-battery glyph).
    if (!board_caps().has_battery) {
        lv_obj_del(battery_img);
        battery_img = nullptr;
    }

    // Mouse and keyboard batteries (read by the daemon), fixed in the top-right
    // corner on every screen but Clawd. Below the board battery when there is one.
    // A row of [icon pct] pairs, right-aligned in a thin strip above the title.
    periph_box = lv_obj_create(scr);
    lv_obj_remove_style_all(periph_box);
    lv_obj_set_size(periph_box, LV_SIZE_CONTENT, ICON_MOUSE_H);
    lv_obj_set_flex_flow(periph_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(periph_box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(periph_box, 4, 0);
    lv_obj_clear_flag(periph_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(periph_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(periph_box, LV_ALIGN_TOP_RIGHT, -L.margin, 3);
    init_icon_dsc_rgb565a8(&periph_dscs[0], ICON_MOUSE_W, ICON_MOUSE_H, icon_mouse_data);
    init_icon_dsc_rgb565a8(&periph_dscs[1], ICON_KEYBOARD_W, ICON_KEYBOARD_H, icon_keyboard_data);
    for (int i = 0; i < 2; i++) {
        periph_icon[i] = lv_image_create(periph_box);
        lv_image_set_src(periph_icon[i], &periph_dscs[i]);
        periph_pct[i] = lv_label_create(periph_box);
        lv_obj_set_style_text_font(periph_pct[i], &font_styrene_16, 0);
        lv_obj_set_style_text_color(periph_pct[i], COL_TEXT, 0);
        if (i == 0) lv_obj_set_style_margin_right(periph_pct[i], 10, 0);   // gap between the two pairs
    }
    lv_obj_add_flag(periph_box, LV_OBJ_FLAG_HIDDEN);
}

// Each value: battery percent, or -1 when the device isn't readable right now.
void ui_update_peripherals(int mouse_pct, int keyboard_pct) {
    if (!periph_box) return;
    const int pcts[2] = {mouse_pct, keyboard_pct};
    int shown = 0;
    for (int i = 0; i < 2; i++) {
        if (pcts[i] < 0) {
            lv_obj_add_flag(periph_icon[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(periph_pct[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_label_set_text_fmt(periph_pct[i], "%d%%", pcts[i]);
        const lv_color_t c = pcts[i] <= 20 ? THEME_RED : COL_TEXT;   // low battery turns red
        lv_obj_set_style_text_color(periph_pct[i], c, 0);
        lv_obj_set_style_image_recolor(periph_icon[i], c, 0);
        lv_obj_set_style_image_recolor_opa(periph_icon[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(periph_icon[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(periph_pct[i], LV_OBJ_FLAG_HIDDEN);
        shown++;
    }
    // No trailing gap when the keyboard is hidden.
    lv_obj_set_style_margin_right(periph_pct[0], pcts[1] < 0 ? 0 : 10, 0);
    periph_known = shown > 0;
    apply_battery_visibility();
}

void ui_update(const UsageData* data) {
    if (!data->valid) return;
    data_ok = data->ok;
    if (!data->ok) return;          // a {"ok":false} "no data" beat → fall through to idle, keep last numbers
    last_data_ms = lv_tick_get();   // a real usage update just landed
    data_received = true;

    if (data->clock_epoch > 0) {    // daemon supplied wall-clock time → drive the title clock
        clock_base_epoch = data->clock_epoch;
        clock_base_ms = last_data_ms;
        clock_fmt = data->clock_fmt;
    } else if (clock_base_epoch != 0) {   // clock turned off daemon-side
        clock_base_epoch = 0;
        clock_last_min = -1;
    }

    int s_pct = (int)(data->session_pct + 0.5f);

    if (usage_rows_layout) {
        // Proposta B: the Claude row's mini-bars are already styled; enterprise
        // spending mode isn't represented as a row. Keep the compact cells as-is.
    } else if (data->enterprise) {
        // Spending box: big number-only label + small "%" symbol + desc + pace
        lv_obj_set_style_text_font(lbl_session_pct, L.ent_pct_font, 0);
        lv_label_set_text(lbl_session_label, "Gastos");
        lv_obj_add_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status,   LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_text_font(lbl_session_pct, L.pct_font, 0);
        lv_label_set_text(lbl_session_label, "Claude - Daily");
        lv_obj_clear_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    }

    char buf[48];

    // Pace vars used in both enterprise blocks below
    const char* pace_text = "Abaixo do ritmo";
    lv_color_t  pace_color = COL_GREEN;
    const char* pace_hex   = "788c5d";   // matches THEME_GREEN
    if (data->session_pct > (float)data->time_pct + 15.0f) {
        pace_text = "Acima do ritmo";  pace_color = COL_RED;   pace_hex = "c0392b";
    } else if (data->session_pct > (float)data->time_pct - 15.0f) {
        pace_text = "No ritmo";    pace_color = COL_AMBER; pace_hex = "d97757";
    }

    if (data->enterprise) {
        lv_label_set_text_fmt(lbl_session_pct, "%d", s_pct);
        lv_obj_align_to(lbl_session_pct_sym, lbl_session_pct,
                        LV_ALIGN_OUT_RIGHT_TOP, 4, 12);
    } else {
        lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
        format_reset_time(data->session_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_session_reset, buf);
        set_reset_hint(hint_session, data->session_reset_mins);
    }

    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
    set_usage_pct(UP_CLAUDE_DAY, s_pct);

    if (data->enterprise) {
        // Period box: time % + dynamic pace color + "Resets <date>" label
        lv_label_set_text(lbl_weekly_label, "Período");
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", data->time_pct);
        lv_bar_set_value(bar_weekly, data->time_pct, LV_ANIM_ON);
        set_usage_pct(UP_CLAUDE_WEEK, data->time_pct);
        lv_color_t bar_pace = (data->session_pct <= (float)data->time_pct) ? COL_GREEN :
                              (data->session_pct <= (float)data->time_pct + 15.0f) ? COL_AMBER :
                              COL_RED;
        lv_obj_set_style_bg_color(bar_weekly, bar_pace, LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), "#%s %s# - #faf9f5 Reinicia %s#",
                 pace_hex, pace_text, data->reset_date);
        lv_label_set_text(lbl_weekly_reset, buf);
    } else {
        int w_pct = (int)(data->weekly_pct + 0.5f);
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
        lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
        set_usage_pct(UP_CLAUDE_WEEK, w_pct);
        format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_weekly_reset, buf);
        set_reset_hint(hint_weekly, data->weekly_reset_mins);
    }
}

// Pick the usage-view sub-screen: pairing hint (BLE down), the idle "Zzz" screen
// (connected but data has gone stale), or the live usage panels. Only re-lays-out
// on an actual change. The animated status line stays visible everywhere — it
// reads "Listening…" on the idle screen, keeping it alive rather than frozen.
static void update_view_state(void) {
    if (!usage_group || !pair_group || !idle_group) return;
    int v;
    if (!s_ble_connected) {
        v = 0;  // pairing hint
    } else if (data_received && data_ok && (lv_tick_get() - last_data_ms) < DATA_FRESH_MS) {
        v = 2;  // live usage
    } else {
        v = 1;  // idle / Zzz
    }
    if (v == view_state) return;
    view_state = v;
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(v == 0 ? pair_group : v == 1 ? idle_group : usage_group,
                      LV_OBJ_FLAG_HIDDEN);
}

static bool list_gliding = false;   // the visible screen's list hasn't reached its end yet

static lv_indev_t* finger_down(void) {
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED)
            return indev;
    }
    return nullptr;
}

static void show_lock_toast(void) {
    if (!lock_toast) {
        lock_toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(lock_toast, &font_styrene_28, 0);
        lv_obj_set_style_text_color(lock_toast, COL_TEXT, 0);
        lv_obj_set_style_bg_color(lock_toast, COL_PANEL, 0);
        lv_obj_set_style_bg_opa(lock_toast, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(lock_toast, 3, 0);
        lv_obj_set_style_radius(lock_toast, 16, 0);
        lv_obj_set_style_pad_hor(lock_toast, 24, 0);
        lv_obj_set_style_pad_ver(lock_toast, 14, 0);
    }
    lv_label_set_text(lock_toast, rotation_locked ? "Telas pausadas" : "Telas passando");
    lv_obj_set_style_border_color(lock_toast, accent_color, 0);
    lv_obj_align(lock_toast, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(lock_toast, LV_OBJ_FLAG_HIDDEN);
    lock_toast_ms = lv_tick_get();
}

// ---- Notice toast ----
// A short message pushed by the Mac (e.g. "Post publicado" from the @eaiproduto
// autopost). Lives on the top layer, so it shows over whatever screen is up,
// green border for success and red for failure, and clears itself after 10 s.
static lv_obj_t* notice_toast = nullptr;
static lv_obj_t* lbl_notice = nullptr;
static uint32_t  notice_ms = 0;
static const uint32_t NOTICE_TOAST_MS = 10000;
static const int NOTICE_PAD = 14, NOTICE_BORDER = 3;

void ui_show_notice(const char* text, bool ok) {
    if (!text || !*text) return;
    if (!notice_toast) {
        notice_toast = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(notice_toast);
        lv_obj_clear_flag(notice_toast, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(notice_toast, COL_PANEL, 0);
        lv_obj_set_style_bg_opa(notice_toast, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(notice_toast, NOTICE_BORDER, 0);
        lv_obj_set_style_radius(notice_toast, 16, 0);
        lv_obj_set_style_pad_all(notice_toast, NOTICE_PAD, 0);
        lv_obj_set_size(notice_toast, L.content_w, LV_SIZE_CONTENT);

        lbl_notice = lv_label_create(notice_toast);
        lv_label_set_long_mode(lbl_notice, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl_notice, L.content_w - 2 * (NOTICE_PAD + NOTICE_BORDER));
        lv_obj_set_style_text_font(lbl_notice, L.reset_font, 0);
        lv_obj_set_style_text_color(lbl_notice, COL_TEXT, 0);
        lv_obj_set_style_text_align(lbl_notice, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl_notice);
    }
    lv_label_set_text(lbl_notice, text);
    lv_obj_set_style_border_color(notice_toast, ok ? COL_GREEN : COL_RED, 0);
    lv_obj_align(notice_toast, LV_ALIGN_BOTTOM_MID, 0, -L.margin);
    lv_obj_clear_flag(notice_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(notice_toast);
    notice_ms = lv_tick_get();
}

static void notice_tick(uint32_t now) {
    if (notice_toast && !lv_obj_has_flag(notice_toast, LV_OBJ_FLAG_HIDDEN) &&
        now - notice_ms > NOTICE_TOAST_MS)
        lv_obj_add_flag(notice_toast, LV_OBJ_FLAG_HIDDEN);
}

// Holding a finger still on the middle of the screen for 2 s toggles the lock.
static void lock_hold_tick(lv_indev_t* indev, uint32_t now) {
    lv_point_t p = {0, 0};
    lv_indev_get_point(indev, &p);
    if (hold_start_ms == 0) {                 // a new touch just started
        hold_start_ms = now;
        hold_point = p;
        hold_toggled = false;
    }
    if (hold_toggled) return;
    const int cx = p.x - L.scr_w / 2, cy = p.y - L.scr_h / 2, r = L.scr_w / 4;
    const int mx = p.x - hold_point.x, my = p.y - hold_point.y;
    const bool alert = current_screen == SCREEN_MEETING || current_screen == SCREEN_ROUTINE_ALERT;
    if (alert || cx * cx + cy * cy > r * r || mx * mx + my * my > 20 * 20) {
        hold_start_ms = UINT32_MAX;           // moved or off-center: this touch can't toggle
        return;
    }
    if (hold_start_ms == UINT32_MAX || now - hold_start_ms < ROTATION_LOCK_HOLD_MS) return;
    hold_toggled = true;
    rotation_locked = !rotation_locked;
    rotation_locked_ms = now;
    tap_hold = false;                         // unlocking resumes right away, from a fresh dwell
    screen_shown_ms = now;
    show_lock_toast();
}

static void rotation_tick(void) {
    const uint32_t now = lv_tick_get();
    if (lock_toast && !lv_obj_has_flag(lock_toast, LV_OBJ_FLAG_HIDDEN) && now - lock_toast_ms > 2000)
        lv_obj_add_flag(lock_toast, LV_OBJ_FLAG_HIDDEN);
    notice_tick(now);
    // A finger on the glass (e.g. dragging a table) never lets the screen change;
    // the usual tap pause starts counting once it lifts.
    if (lv_indev_t* indev = finger_down()) {
        lock_hold_tick(indev, now);
        if (hold_toggled) return;
        tap_ms = now;
        tap_hold = true;
        return;
    }
    hold_start_ms = 0;
    hold_toggled = false;                     // the release's click (if any) already ran
    if (tap_hold) {
        if (now - tap_ms < ROTATION_TAP_PAUSE_MS) return;
        tap_hold = false;
    }
    // An imminent meeting takes precedence over everything, then a Vasco match.
    if (meeting_active) {
        if (current_screen != SCREEN_MEETING) ui_show_screen(SCREEN_MEETING);
        return;
    }
    // A failed routine waits for "Rodar de novo" / "Dispensar".
    if (ralert_active) {
        if (current_screen != SCREEN_ROUTINE_ALERT) ui_show_screen(SCREEN_ROUTINE_ALERT);
        return;
    }
    // A Vasco match freezes rotation on the live score until it ends.
    if (live_active) {
        if (current_screen != SCREEN_LIVE) ui_show_screen(SCREEN_LIVE);
        return;
    }
    if (rotation_locked && now - rotation_locked_ms >= ROTATION_LOCK_MAX_MS) {
        rotation_locked = false;              // forgotten pause: resume from a fresh dwell
        screen_shown_ms = now;
        show_lock_toast();
    }
    if (rotation_locked) return;              // paused by a 2 s hold; taps still navigate
    if (now - screen_shown_ms < rotation_dwell_ms(current_screen)) return;
    size_t next = 0;
    for (size_t i = 0; i < ROTATION_COUNT; i++) {
        if (ROTATION[i].screen == current_screen) { next = (i + 1) % ROTATION_COUNT; break; }
    }
    ui_show_screen(ROTATION[next].screen);
}

// The visible screen's list (if any) glides on its own while unattended.
static void lists_tick(void) {
    ScrollList* l = nullptr;
    switch (current_screen) {
    case SCREEN_AGENDA:   l = &agenda_list; break;
    // Consumo Atual only glides down when a hidden panel has usage; else it stays put.
    case SCREEN_USAGE:    l = usage_hidden_in_use() ? &usage_list : nullptr; break;
    case SCREEN_ROUTINES: l = &routines_list; break;
    case SCREEN_POSTS:    l = &posts_list; break;
    case SCREEN_CRYPTO:   l = &quote_tables[QUOTES_CRYPTO].list; break;
    case SCREEN_STOCKS:   l = &quote_tables[QUOTES_STOCKS].list; break;
    case SCREEN_FIIS:     l = &quote_tables[QUOTES_FIIS].list; break;
    case SCREEN_VASCO:    l = &games_list; break;
    default: break;
    }
    list_gliding = l && scroll_list_tick(l);
}

// Repaint accent-colored items when the on-screen character changes.
static void accent_tick(void) {
    static int shown = -1;                    // -1 unknown, 0 Claude, 1 Kiro
    const int kiro = splash_kiro_on_screen() ? 1 : 0;
    if (kiro == shown) return;
    shown = kiro;
    accent_color = kiro ? COL_KIRO : COL_ACCENT;
    accent_hex   = kiro ? COL_HEX_KIRO : COL_HEX_CLAUDE;
    if (lbl_anim)          lv_obj_set_style_text_color(lbl_anim, accent_color, 0);
    if (lbl_live_status)   lv_obj_set_style_text_color(lbl_live_status, accent_color, 0);
    if (lbl_meeting_where) lv_obj_set_style_text_color(lbl_meeting_where, accent_color, 0);
    meeting_shown_secs = -99999;
    if (lbl_game_detail[0]) lv_obj_set_style_text_color(lbl_game_detail[0], accent_color, 0);
    if (lbl_agenda_note)   render_agenda();
    if (rates_line_today) { lv_obj_set_style_line_color(rates_line_today, accent_color, 0); render_rates(); }
}

void ui_tick_anim(void) {
    accent_tick();
    meeting_tick();
    ralert_tick();
    live_tick();
    routines_tick();
    rotation_tick();
    lists_tick();
    if (current_screen != SCREEN_USAGE) return;
    update_view_state();
    if (view_state == 1) {                    // idle screen: cloud rider, or the flying ghost on Kiro's turn
        const bool kiro = splash_kiro_on_screen() && idle_flyer;
        if (kiro) {
            if (idle_cloud) lv_obj_add_flag(idle_cloud, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(idle_flyer, LV_OBJ_FLAG_HIDDEN);
            const int group_h = L.scr_h - L.content_y;
            splash_flyer_tick(L.margin, L.scr_w - L.margin, group_h / 2 - 20);
        } else {
            if (idle_flyer) lv_obj_add_flag(idle_flyer, LV_OBJ_FLAG_HIDDEN);
            if (idle_cloud) lv_obj_clear_flag(idle_cloud, LV_OBJ_FLAG_HIDDEN);
            splash_mini_tick();
        }
    }

    uint32_t now = lv_tick_get();

    // The title is fixed ("Consumo Atual"); the daemon's wall-clock time is not shown.

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms < spinner_ms[anim_spinner_idx]) return;
    anim_last_ms = now;
    anim_phase = (anim_phase + 1) % SPINNER_PHASES;
    anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                    : (SPINNER_PHASES - anim_phase);

    // Status text by priority. Whimsical messages only when connected & settled.
    const char* text;
    if (!s_ble_connected) {
        text = "Aguardando";              // advertising / waiting for a host connection
    } else if (view_state == 1) {      // idle — alternate so it reads as alive AND data-less
        text = (anim_msg_idx & 1) ? "Sem dados" : "Escutando";
    } else if (now - connected_at_ms < 5000) {
        text = "Conectado";
    } else {
        text = anim_messages[anim_msg_idx];
    }

    static char buf[80];
    if (panel_kiro && view_state == 2) {
        buf[0] = '\0';                     // the three panels use the status line's space
    } else if (!panel_kiro && kiro_pct >= 0 && view_state == 2 && now - connected_at_ms >= 5000) {
        // Live usage: the line carries Kiro's credit usage instead of the whimsy.
        snprintf(buf, sizeof(buf), "Kiro %d%% \xC2\xB7 renova em %dd", kiro_pct, kiro_reset_days);
        lv_obj_set_style_text_font(lbl_anim, L.reset_font, 0);
    } else {
        // All states share the whimsical style: "<glyph> <Title-case word>…"
        snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
                 spinner_frames[anim_spinner_idx], text);
        lv_obj_set_style_text_font(lbl_anim, L.anim_font, 0);
    }
    lv_label_set_text(lbl_anim, buf);
}

static screen_t prev_non_splash_screen = SCREEN_USAGE;
static void apply_battery_visibility(void) {
    if (periph_box) {
        if (current_screen == SCREEN_SPLASH || !periph_known) lv_obj_add_flag(periph_box, LV_OBJ_FLAG_HIDDEN);
        else                                                 lv_obj_clear_flag(periph_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (!battery_img) return;
    if (current_screen == SCREEN_SPLASH) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    else                                  lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
}

// Taps navigate by side: right half goes forward, left half goes back, through
// Clawd → Uso → Agenda → Consumo 24h → Rotinas Automáticas → Cronograma de Posts → Criptomoedas → Bovespa → Juros Futuros → FIIs
// → Fundos Imobiliários
// → Jogos do Vasco (→ Vasco ao vivo, only during a match) → Clawd. Long lists
// scroll with a drag instead of paging. A tap pauses auto-rotation so the
// screen can be read; during a match it returns to the live score after the pause.
static screen_t step_screen(screen_t from, int dir) {
    screen_t s = (screen_t)((from + dir + SCREEN_COUNT) % SCREEN_COUNT);
    for (int guard = 0; guard < 3; guard++) {   // skip alert screens that aren't active
        if ((s == SCREEN_LIVE && !live_active) || (s == SCREEN_MEETING && !meeting_active) ||
            (s == SCREEN_ROUTINE_ALERT && !ralert_active))
            s = (screen_t)((s + dir + SCREEN_COUNT) % SCREEN_COUNT);
    }
    return s;
}

static void global_click_cb(lv_event_t* e) {
    (void)e;
    if (hold_toggled) return;                 // lifting after the 2 s pause/resume hold
    tap_ms = lv_tick_get();
    tap_hold = true;

    lv_point_t p = {0, 0};
    lv_indev_t* indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &p);
    // A swipe that LVGL didn't turn into a scroll is still not a tap.
    const int dy = p.y - press_point.y, dx = p.x - press_point.x;
    if (dy * dy + dx * dx > 20 * 20) return;
    ui_show_screen(step_screen(current_screen, (p.x < L.scr_w / 2) ? -1 : +1));
}

void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(history_container, LV_OBJ_FLAG_HIDDEN);
    for (auto& t : quote_tables) lv_obj_add_flag(t.container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(games_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(rates_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(routines_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(posts_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(agenda_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(live_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(meeting_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ralert_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:  splash_show(); break;
    case SCREEN_USAGE:   lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); scroll_list_show(&usage_list); break;
    case SCREEN_AGENDA:  lv_obj_clear_flag(agenda_container, LV_OBJ_FLAG_HIDDEN); scroll_list_show(&agenda_list); break;
    case SCREEN_HISTORY: lv_obj_clear_flag(history_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_ROUTINES: lv_obj_clear_flag(routines_container, LV_OBJ_FLAG_HIDDEN); scroll_list_show(&routines_list); break;
    case SCREEN_POSTS:   lv_obj_clear_flag(posts_container, LV_OBJ_FLAG_HIDDEN); scroll_list_show(&posts_list); break;
    case SCREEN_CRYPTO:
    case SCREEN_STOCKS:
    case SCREEN_FIIS: {
        QuoteTable& t = quote_tables[screen == SCREEN_CRYPTO ? QUOTES_CRYPTO
                                     : screen == SCREEN_STOCKS ? QUOTES_STOCKS : QUOTES_FIIS];
        lv_obj_clear_flag(t.container, LV_OBJ_FLAG_HIDDEN);
        scroll_list_show(&t.list);
        break;
    }
    case SCREEN_RATES:   lv_obj_clear_flag(rates_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_VASCO:   lv_obj_clear_flag(games_container, LV_OBJ_FLAG_HIDDEN); scroll_list_show(&games_list); break;
    case SCREEN_LIVE:    lv_obj_clear_flag(live_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_MEETING: lv_obj_clear_flag(meeting_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_ROUTINE_ALERT: lv_obj_clear_flag(ralert_container, LV_OBJ_FLAG_HIDDEN); break;
    default: break;
    }

    splash_mascot_set_visible(screen != SCREEN_SPLASH);
    if (logo_img) {
        if (screen == SCREEN_SPLASH) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    }

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    screen_shown_ms = lv_tick_get();
    apply_battery_visibility();
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_ble_status(ble_state_t state, const char* name, const char* mac) {
    (void)name; (void)mac;
    bool was_connected = s_ble_connected;
    s_ble_connected = (state == BLE_STATE_CONNECTED);

    if (s_ble_connected && !was_connected) connected_at_ms = lv_tick_get();
    // pair / idle / usage — picked from connection + data freshness.
    update_view_state();
}

void ui_update_battery(int percent, bool charging) {
    if (!battery_img) return;
    int idx;
    if (charging) {
        idx = 4;
    } else if (percent < 0) {
        idx = 0;
    } else if (percent <= 10) {
        idx = 0;
    } else if (percent <= 35) {
        idx = 1;
    } else if (percent <= 75) {
        idx = 2;
    } else {
        idx = 3;
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();
}
