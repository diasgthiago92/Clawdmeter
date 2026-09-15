#include "ui.h"
#include "splash.h"
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <time.h>
#include "logo.h"
#include "clawd_still.h"
#include "icons.h"
#include "periph_icons.h"
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
    {SCREEN_USAGE,   48000},   // 40%
    {SCREEN_AGENDA,  12000},
    {SCREEN_HISTORY, 12000},   // 10%
    {SCREEN_ACTIONS, 12000},   // 10%
    {SCREEN_ROUTINES, 12000},
    {SCREEN_CRYPTO,  24000},   // 20%
    {SCREEN_STOCKS,  12000},   // 10%
    {SCREEN_FIIS,    12000},
    {SCREEN_VASCO,   12000},   // 10%
};
#define ROTATION_COUNT        (sizeof(ROTATION) / sizeof(ROTATION[0]))
#ifndef ROTATION_OFFCYCLE_MS
#define ROTATION_OFFCYCLE_MS  10000   // dwell for screens outside the cycle (splash)
#endif
#define ROTATION_TAP_PAUSE_MS 60000   // a tap holds the chosen screen this long
static uint32_t screen_shown_ms = 0;
static uint32_t tap_ms = 0;
static bool     tap_hold = false;

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
static lv_obj_t* panel_ag = nullptr;      // Antigravity - Daily (large layout)
static lv_obj_t* lbl_ag_pct;
static lv_obj_t* lbl_ag_label;
static lv_obj_t* bar_ag;
static lv_obj_t* lbl_ag_reset;

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
    lv_label_set_text(lbl_title, "Consumo");
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    fit_screen_title(lbl_title, "Consumo");

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

    // Large layout: Claude Daily, Antigravity, Kiro Monthly and Claude Weekly
    // share a scrollable box (three visible at a time).
    lv_obj_t* panels = usage_group;
    int py0 = L.content_y;
    if (L.kiro_panel) {
        panels = make_usage_scroll_box(usage_group, L.content_y, L.scr_w, 3, L.usage_panel_h + L.usage_panel_gap);
        py0 = 0;
    }

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

    panel_weekly = make_usage_panel(panels,
                     py0 + 3 * (L.usage_panel_h + L.usage_panel_gap), "Claude - Weekly",
                     &lbl_weekly_pct, &lbl_weekly_label,
                     &bar_weekly, &lbl_weekly_reset);
    // Recolor enabled so enterprise period box can color pace and reset separately
    lv_label_set_recolor(lbl_weekly_reset, true);

    if (L.kiro_panel) {
        panel_kiro = make_usage_panel(panels,
                         py0 + 2 * (L.usage_panel_h + L.usage_panel_gap), "Kiro - Monthly",
                         &lbl_kiro_pct, &lbl_kiro_label, &bar_kiro, &lbl_kiro_reset);
        lv_label_set_text(lbl_kiro_reset, "Sem dados do Kiro");
        panel_ag = make_usage_panel(panels,
                         py0 + L.usage_panel_h + L.usage_panel_gap, "Gemini - Daily",
                         &lbl_ag_pct, &lbl_ag_label, &bar_ag, &lbl_ag_reset);
        lv_label_set_text(lbl_ag_pct, "---%");
        lv_label_set_text(lbl_ag_reset, "Sem uso do Gemini hoje");
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
static lv_obj_t* lbl_history_ag;
static lv_obj_t* lbl_history_ag_unit;
static int       history_plot_h = 0;
static lv_obj_t* lbl_history_now;
static lv_obj_t* lbl_history_peak;
static lv_obj_t* lbl_history_now_unit;   // "tokens do Claude", under the Claude number
static lv_obj_t* lbl_history_peak_unit;  // "requisições do Kiro", under the Kiro number
static lv_obj_t* lbl_history_empty;

// Últimas Ações: a header per AI followed by its newest tool calls.
static lv_obj_t* actions_container;
static lv_obj_t* lbl_actions_empty;
#define ACTION_LIST_ROWS (ACTIONS_MAX + 3)   // actions + one header per AI
#define ACTION_VISIBLE_ROWS 9
static lv_obj_t*  action_time[ACTION_LIST_ROWS];
static lv_obj_t*  action_text[ACTION_LIST_ROWS];
static ActionRow* actions = nullptr;   // PSRAM: internal RAM is reserved for the panel
static int        actions_total = 0;

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

// Unit captions sit under each number: "tokens" left, "requisições" right.
static void history_place_units(void) {
    const int y = lv_font_get_line_height(L.reset_font) - 2;
    lv_obj_align(lbl_history_now, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(lbl_history_now_unit, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_align(lbl_history_ag, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_align(lbl_history_ag_unit, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_align(lbl_history_peak, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_align(lbl_history_peak_unit, LV_ALIGN_TOP_RIGHT, 0, y);
}

static void init_history_screen(lv_obj_t* scr) {
    history_container = make_screen_container(scr, "Consumo - 24\xC2\xA0horas");   // no-break space keeps "24 horas" together

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(history_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;

    // Header: Claude tokens (orange, left), Antigravity tokens (light blue, center)
    // and Kiro requests (purple, right), each with its unit underneath.
    lbl_history_now = lv_label_create(panel);
    lv_label_set_text(lbl_history_now, "---");
    lv_obj_set_style_text_font(lbl_history_now, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_history_now, COL_ACCENT, 0);
    lv_obj_set_pos(lbl_history_now, 0, 0);

    lbl_history_peak = lv_label_create(panel);
    lv_label_set_text(lbl_history_peak, "---");
    lv_obj_set_style_text_font(lbl_history_peak, L.reset_font, 0);

    lbl_history_ag = lv_label_create(panel);
    lv_label_set_text(lbl_history_ag, "---");
    lv_obj_set_style_text_font(lbl_history_ag, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_history_ag, COL_AG, 0);

    lbl_history_ag_unit = lv_label_create(panel);
    lv_label_set_text(lbl_history_ag_unit, "tokens");
    lv_obj_set_style_text_font(lbl_history_ag_unit, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_history_ag_unit, COL_TEXT, 0);
    lv_obj_set_style_text_color(lbl_history_peak, COL_KIRO, 0);

    lbl_history_now_unit = lv_label_create(panel);
    lv_label_set_text(lbl_history_now_unit, "tokens");
    lv_obj_set_style_text_font(lbl_history_now_unit, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_history_now_unit, COL_TEXT, 0);

    lbl_history_peak_unit = lv_label_create(panel);
    lv_label_set_text(lbl_history_peak_unit, "créditos (est.)");
    lv_obj_set_style_text_font(lbl_history_peak_unit, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_history_peak_unit, COL_TEXT, 0);
    history_place_units();

    const int axis_h  = lv_font_get_line_height(L.axis_font);
    const int chart_y = lv_font_get_line_height(L.reset_font) - 2 + axis_h + L.panel_pad_y / 2;
    const int chart_h = inner_h - chart_y - axis_h - 6;

    lv_obj_t* plot = lv_obj_create(panel);
    lv_obj_set_pos(plot, 0, chart_y);
    lv_obj_set_size(plot, inner_w, chart_h);
    lv_obj_set_style_bg_color(plot, COL_BG, 0);
    lv_obj_set_style_bg_opa(plot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(plot, 0, 0);
    lv_obj_set_style_radius(plot, 6, 0);
    lv_obj_set_style_pad_all(plot, 0, 0);
    lv_obj_clear_flag(plot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(plot, LV_OBJ_FLAG_EVENT_BUBBLE);

    const int pad = 6;
    history_plot_h = chart_h - 2 * pad;
    const int slot = (inner_w - 2 * pad) / HISTORY_HOURS;
    const int bar_w = slot - slot / 4;
    const int left = (inner_w - slot * HISTORY_HOURS) / 2 + (slot - bar_w) / 2;
    for (int i = 0; i < HISTORY_HOURS; i++) {
        for (int k = 0; k < 3; k++) {
            lv_obj_t* b = lv_obj_create(plot);
            lv_obj_set_style_bg_color(b, k == 0 ? COL_ACCENT : k == 1 ? COL_KIRO : COL_AG, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_radius(b, 0, 0);
            lv_obj_set_style_pad_all(b, 0, 0);
            lv_obj_clear_flag(b, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_set_pos(b, left + i * slot, pad + history_plot_h);
            lv_obj_set_size(b, bar_w, 0);
            lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
            (k == 0 ? history_claude_bar : k == 1 ? history_kiro_bar : history_ag_bar)[i] = b;
        }
    }

    lbl_history_empty = make_dim_label(panel, L.reset_font, "Coletando dados...");
    lv_obj_align_to(lbl_history_empty, plot, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* a0 = make_dim_label(panel, L.axis_font, "-24h");
    lv_obj_align(a0, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t* legend = make_dim_label(panel, L.axis_font, "");
    lv_label_set_recolor(legend, true);
    lv_label_set_text_fmt(legend, "#%s Claude#  #%s Gemini#  #%s Kiro#", COL_HEX_CLAUDE, COL_HEX_AG, COL_HEX_KIRO);
    lv_obj_align(legend, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* a2 = make_dim_label(panel, L.axis_font, "agora");
    lv_obj_align(a2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
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
    if (cur >= max_y) {
        if (!l->end_ms) l->end_ms = now;
        return now - l->end_ms < LIST_END_HOLD_MS && max_y > 0;
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

static ScrollList actions_list;
static ScrollList usage_list;

static lv_obj_t* make_usage_scroll_box(lv_obj_t* parent, int y, int w, int visible_rows, int row_h) {
    lv_obj_t* box = scroll_list_create(&usage_list, parent, 0, y, w, visible_rows, row_h);
    usage_list.rows = 4;
    return box;
}

void ui_update_antigravity(uint64_t tokens_today, int pct_of_peak, int responses) {
    if (!panel_ag) return;
    // Like the other panels: the big number is a percentage (today vs. the
    // busiest day of the last 30), the token count goes in the line below.
    char buf[48];
    lv_label_set_text_fmt(lbl_ag_pct, "%d%%", pct_of_peak);
    lv_bar_set_value(bar_ag, pct_of_peak, LV_ANIM_ON);
    if (responses <= 0) {
        lv_label_set_text(lbl_ag_reset, "Sem uso do Gemini hoje");
        return;
    }
    format_tokens(tokens_today, buf, sizeof(buf));
    lv_label_set_text_fmt(lbl_ag_reset, "%s tokens hoje \xC2\xB7 %d %s",
                          buf, responses, responses == 1 ? "resposta" : "respostas");
}

static void init_actions_screen(lv_obj_t* scr) {
    actions_container = make_screen_container(scr, "Últimas Ações");

    const int panel_h = L.scr_h - L.content_y - L.margin;
    lv_obj_t* panel = make_panel(actions_container, L.margin, L.content_y, L.content_w, panel_h);
    const int inner_w = L.content_w - 2 * L.panel_pad_x;
    const int inner_h = panel_h - 2 * L.panel_pad_y;

    const lv_font_t* font = L.table_font_dense;
    const int axis_h = lv_font_get_line_height(L.axis_font);
    const int line_h = lv_font_get_line_height(font);
    const int row_h  = (inner_h - axis_h - 6) / ACTION_VISIBLE_ROWS;
    const int time_w = inner_w * 17 / 100;
    lv_obj_t* box = scroll_list_create(&actions_list, panel, 0, 0, inner_w, ACTION_VISIBLE_ROWS, row_h);

    for (int i = 0; i < ACTION_LIST_ROWS; i++) {
        action_time[i] = lv_label_create(box);
        lv_obj_set_style_text_font(action_time[i], font, 0);
        lv_obj_set_pos(action_time[i], 0, i * row_h + (row_h - line_h) / 2);
        lv_obj_add_flag(action_time[i], LV_OBJ_FLAG_EVENT_BUBBLE);

        action_text[i] = lv_label_create(box);
        lv_obj_set_style_text_font(action_text[i], font, 0);
        lv_obj_set_style_text_color(action_text[i], COL_TEXT, 0);
        lv_label_set_long_mode(action_text[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(action_text[i], time_w, i * row_h + (row_h - line_h) / 2);
        lv_obj_set_size(action_text[i], inner_w - time_w, line_h);   // one line, ends in "..."
        lv_obj_add_flag(action_text[i], LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_add_flag(action_time[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(action_text[i], LV_OBJ_FLAG_HIDDEN);
    }

    const size_t actions_size = ACTIONS_MAX * sizeof(ActionRow);
    actions = (ActionRow*)heap_caps_malloc(actions_size, MALLOC_CAP_SPIRAM);
    if (!actions) actions = (ActionRow*)malloc(actions_size);   // boards without PSRAM
    if (actions) memset(actions, 0, actions_size);

    lbl_actions_empty = make_dim_label(panel, L.reset_font, "Buscando ações...");
    lv_obj_align(lbl_actions_empty, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* note = make_dim_label(panel, L.axis_font, "Últimas 24h \xC2\xB7 neste Mac");
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
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
            used[0] += snprintf(text[0] + used[0], sizeof(text[0]) - used[0], "%s#%s %s#", sep, COL_HEX_KIRO, row.name);
        else
            used[0] += snprintf(text[0] + used[0], sizeof(text[0]) - used[0], "%s%s", sep, row.name);
        used[1] += snprintf(text[1] + used[1], sizeof(text[1]) - used[1], "%s%s", sep, row.time);
        used[2] += snprintf(text[2] + used[2], sizeof(text[2]) - used[2], "%s%d\xC3\x97", sep, row.runs);
        used[3] += snprintf(text[3] + used[3], sizeof(text[3]) - used[3], "%s#%s %s#", sep,
                            row.ok ? COL_HEX_KIRO : COL_HEX_RED, row.ok ? "ok" : "erro");
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
            if (strcmp(routines[j].name, rows[i].name) == 0 && strcmp(routines[j].time, rows[i].time) == 0) {
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
static bool      meeting_active = false;
static uint32_t  meeting_start_ms = 0;       // lv_tick at which the meeting starts
static uint32_t  meeting_rx_ms = 0;
static int       meeting_shown_secs = -99999;

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
    lv_obj_align(lbl_meeting_title, LV_ALIGN_CENTER, 0, 20);

    lbl_meeting_when = make_dim_label(panel, L.reset_font, "");
    lv_obj_align(lbl_meeting_when, LV_ALIGN_BOTTOM_MID, 0, -lv_font_get_line_height(L.axis_font) - 10);

    lbl_meeting_where = lv_label_create(panel);
    lv_label_set_long_mode(lbl_meeting_where, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_meeting_where, inner_w);
    lv_obj_set_style_text_font(lbl_meeting_where, L.axis_font, 0);
    lv_obj_set_style_text_color(lbl_meeting_where, accent_color, 0);
    lv_obj_set_style_text_align(lbl_meeting_where, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_meeting_where, "");
    lv_obj_align(lbl_meeting_where, LV_ALIGN_BOTTOM_MID, 0, 0);
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

void ui_update_meeting(const char* title, const char* hhmm, int seconds, const char* where) {
    if (!meeting_container) return;
    if (!title) {
        if (!meeting_active) return;
        meeting_active = false;
        if (current_screen == SCREEN_MEETING) ui_show_screen(live_is_active() ? SCREEN_LIVE : ROTATION[0].screen);
        return;
    }
    const uint32_t now = lv_tick_get();
    meeting_rx_ms = now;
    meeting_start_ms = now + (uint32_t)(seconds > 0 ? seconds : 0) * 1000;
    meeting_shown_secs = -99999;
    lv_label_set_text(lbl_meeting_title, title);
    lv_label_set_text_fmt(lbl_meeting_when, "às %s", hhmm);
    lv_label_set_text(lbl_meeting_where, where);
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
        ui_update_meeting(nullptr, "", 0, "");
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
        if (live_goals[i] >= 0 && goals[i] > live_goals[i]) live_goal_ms[i] = now;
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
    games_container = make_screen_container(scr, "Próximos Jogos do Vasco");

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
void ui_update_history_bars(const char* claude, const char* kiro, const char* ag,
                            uint64_t tokens, float kiro_credits, uint64_t ag_tokens) {
    if (!lbl_history_now) return;
    static const char B64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char* series[3] = {claude, ag, kiro};                 // stacked bottom to top
    lv_obj_t** bars[3] = {history_claude_bar, history_ag_bar, history_kiro_bar};
    const size_t len[3] = {strlen(claude), strlen(ag), strlen(kiro)};
    const int bottom = 6 + history_plot_h;
    bool any = false;
    for (int i = 0; i < HISTORY_HOURS; i++) {
        int y = bottom;
        const int x = lv_obj_get_x(history_claude_bar[i]);
        for (int k = 0; k < 3; k++) {
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
    format_credits(kiro_credits, buf, sizeof(buf));
    lv_label_set_text(lbl_history_peak, buf);
    history_place_units();
    lv_label_set_text(lbl_history_empty, "Sem uso nas últimas 24h");
    if (any) lv_obj_add_flag(lbl_history_empty, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_clear_flag(lbl_history_empty, LV_OBJ_FLAG_HIDDEN);
}

// Rows arrive grouped by AI (Claude, Kiro, Gemini); each group gets a header in
// the AI's color. Times use the AI's color, action text is white.
static void render_actions(void) {
    static const char* const NAMES[3] = {"Claude", "Kiro", "Gemini"};
    const lv_color_t colors[3] = {COL_ACCENT, COL_KIRO, COL_AG};
    int row = 0, last_ai = -1;
    for (int i = 0; i < actions_total && row < ACTION_LIST_ROWS; i++) {
        const ActionRow& a = actions[i];
        if (!a.text[0] || a.ai > 2) continue;   // chunk not arrived yet
        if (a.ai != last_ai) {                   // group header: AI name across the row
            last_ai = a.ai;
            lv_label_set_text(action_time[row], NAMES[a.ai]);
            lv_obj_set_style_text_color(action_time[row], colors[a.ai], 0);
            lv_obj_clear_flag(action_time[row], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(action_text[row], LV_OBJ_FLAG_HIDDEN);
            if (++row >= ACTION_LIST_ROWS) break;
        }
        lv_label_set_text(action_time[row], a.time);
        lv_obj_set_style_text_color(action_time[row], colors[a.ai], 0);
        lv_label_set_text(action_text[row], a.text);
        lv_obj_set_style_text_color(action_text[row], a.time[0] ? COL_TEXT : COL_DIM, 0);
        lv_obj_clear_flag(action_time[row], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(action_text[row], LV_OBJ_FLAG_HIDDEN);
        row++;
    }
    for (int i = row; i < ACTION_LIST_ROWS; i++) {
        lv_obj_add_flag(action_time[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(action_text[i], LV_OBJ_FLAG_HIDDEN);
    }
    actions_list.rows = row;
    lv_label_set_text(lbl_actions_empty, "Nenhuma ação registrada");
    if (row > 0) lv_obj_add_flag(lbl_actions_empty, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_clear_flag(lbl_actions_empty, LV_OBJ_FLAG_HIDDEN);
}

void ui_set_action(int index, const ActionRow& row) {
    if (actions && index >= 0 && index < ACTIONS_MAX) actions[index] = row;
}

void ui_actions_received(int total) {
    if (!actions_container || !actions) return;
    actions_total = total > ACTIONS_MAX ? ACTIONS_MAX : total;
    for (int j = actions_total; j < ACTIONS_MAX; j++) actions[j] = ActionRow{};
    render_actions();
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
    init_actions_screen(scr);
    init_market_screens(scr);
    init_agenda_screen(scr);
    init_routines_screen(scr);
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

    if (data->enterprise) {
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
    }

    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);

    if (data->enterprise) {
        // Period box: time % + dynamic pace color + "Resets <date>" label
        lv_label_set_text(lbl_weekly_label, "Período");
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", data->time_pct);
        lv_bar_set_value(bar_weekly, data->time_pct, LV_ANIM_ON);
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
        format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_weekly_reset, buf);
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

static bool finger_down(void) {
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED)
            return true;
    }
    return false;
}

static void rotation_tick(void) {
    const uint32_t now = lv_tick_get();
    // A finger on the glass (e.g. dragging a table) never lets the screen change;
    // the usual tap pause starts counting once it lifts.
    if (finger_down()) {
        tap_ms = now;
        tap_hold = true;
        return;
    }
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
    if (now - screen_shown_ms < rotation_dwell_ms(current_screen)) return;
    if (list_gliding) return;                 // let a slow list finish before moving on
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
    case SCREEN_USAGE:    l = &usage_list; break;
    case SCREEN_ACTIONS:  l = &actions_list; break;
    case SCREEN_ROUTINES: l = &routines_list; break;
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

    // The title is fixed ("Consumo"); the daemon's wall-clock time is not shown.

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
// Clawd → Uso → Agenda → Consumo 24h → Últimas Ações → Rotinas Automáticas → Criptomoedas → Bovespa → FIIs
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
    lv_obj_add_flag(actions_container, LV_OBJ_FLAG_HIDDEN);
    for (auto& t : quote_tables) lv_obj_add_flag(t.container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(games_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(routines_container, LV_OBJ_FLAG_HIDDEN);
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
    case SCREEN_ACTIONS: lv_obj_clear_flag(actions_container, LV_OBJ_FLAG_HIDDEN); scroll_list_show(&actions_list); break;
    case SCREEN_ROUTINES: lv_obj_clear_flag(routines_container, LV_OBJ_FLAG_HIDDEN); scroll_list_show(&routines_list); break;
    case SCREEN_CRYPTO:
    case SCREEN_STOCKS:
    case SCREEN_FIIS: {
        QuoteTable& t = quote_tables[screen == SCREEN_CRYPTO ? QUOTES_CRYPTO
                                     : screen == SCREEN_STOCKS ? QUOTES_STOCKS : QUOTES_FIIS];
        lv_obj_clear_flag(t.container, LV_OBJ_FLAG_HIDDEN);
        scroll_list_show(&t.list);
        break;
    }
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
