#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "data.h"
#include "ui.h"
#include "ble.h"
#include "splash.h"
#include "usage_rate.h"
#include "idle.h"
#include "idle_cfg.h"
#include "brightness.h"

#include "hal/board_caps.h"
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "hal/input_hal.h"
#include "hal/power_hal.h"
#include "hal/imu_hal.h"
#include "hal/sound_hal.h"

static UsageData usage = {};

// ---- LVGL draw buffers (partial render mode) ----
// PSRAM-equipped boards (S3) can comfortably hold larger strips. PSRAM-free
// boards (e.g. ESP32-C6) allocate from internal SRAM, so we shrink the strip
// — 480×20 RGB565 = 19 KB × 2 buffers = 38 KB, fits beside everything else.
#ifdef BOARD_HAS_PSRAM
#define BUF_LINES 40
#define LV_BUF_CAPS (MALLOC_CAP_SPIRAM)
#else
#define BUF_LINES 20
#define LV_BUF_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#endif
static uint16_t* buf1 = nullptr;
static uint16_t* buf2 = nullptr;

static uint32_t my_tick(void) { return millis(); }

static void my_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    display_hal_draw_bitmap(area->x1, area->y1, w, h, (uint16_t*)px_map);
    lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    display_hal_round_area(&area->x1, &area->y1, &area->x2, &area->y2);
}

// Touch policy is driven by IDLE_WAKE_ON_TOUCH:
//   true  → a press edge while asleep wakes the device and the first touch is
//           swallowed (mirrors the button wake-consumption); a press while
//           awake counts as activity.
//   false → touch never counts as activity and is fully swallowed while the
//           panel is dark, so pets/sleeves can't wake it overnight and LVGL
//           can't quietly toggle splash<->usage on a black panel.
static void my_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    uint16_t x, y;
    bool pressed;
    touch_hal_read(&x, &y, &pressed);
    const bool raw_pressed = pressed;

    if (IDLE_WAKE_ON_TOUCH) {
        static bool touch_was = false;
        static bool touch_wake_swallowed = false;
        if (raw_pressed && !touch_was) {
            // Press edge — consume as wake if asleep.
            if (idle_consume_wake_press()) {
                touch_wake_swallowed = true;
                pressed = false;
            }
        } else if (!raw_pressed && touch_was) {
            // Release edge.
            if (touch_wake_swallowed) {
                touch_wake_swallowed = false;
                pressed = false;
            }
        } else if (raw_pressed && touch_wake_swallowed) {
            // Held finger through wake — keep hiding until release.
            pressed = false;
        }
        touch_was = raw_pressed;
    } else if (idle_is_asleep()) {
        pressed = false;
    }

    if (pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Parse a JSON line into UsageData.
static bool parse_json(const char* json, UsageData* out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return false;
    }

    out->session_pct = doc["s"] | 0.0f;
    out->session_reset_mins = doc["sr"] | -1;
    out->weekly_pct = doc["w"] | 0.0f;
    out->weekly_reset_mins = doc["wr"] | -1;
    strlcpy(out->status, doc["st"] | "unknown", sizeof(out->status));
    out->chime = doc["c"] | false;   // absent (old daemon / chime off) → stay silent
    const char* acct = doc["acct"] | "pro";
    out->enterprise = (strcmp(acct, "ent") == 0);
    out->time_pct = doc["tp"] | 0;
    out->period_days = doc["pd"] | 30;
    strlcpy(out->reset_date, doc["rd"] | "", sizeof(out->reset_date));
    out->clock_epoch = doc["t"] | 0L;
    out->clock_fmt = doc["tf"] | 24;
    out->ok = doc["ok"] | false;
    out->valid = true;
    return true;
}

// History bars ("hb"), actions ("a"), crypto ("x"), B3 ("b") and fixtures ("v") payloads arrive as their own BLE writes so
// each stays under the host's write-without-response size. Returns true when
// the JSON was one of these, leaving UsageData untouched.
static bool handle_extra_json(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;

    if (doc["hb"].is<JsonArray>()) {         // stacked hourly bars: Claude tokens + Kiro requests
        ui_update_history_bars(doc["hb"][0] | "", doc["hb"][1] | "", doc["hb"][2] | "",
                               doc["tc"] | (uint64_t)0, doc["tk"] | 0.0f, doc["ta"] | (uint64_t)0);
        return true;
    }
    if (doc["a"].is<JsonArray>()) {          // Últimas Ações, chunked like the tables
        // One row at a time on the stack: a static row buffer here costs internal
        // RAM, and the RGB panel's DMA buffers need every KB of it at boot.
        const int offset = doc["o"] | 0;
        int n = 0;
        for (JsonArray src : doc["a"].as<JsonArray>()) {
            ActionRow row = {};
            row.ai = src[0] | 0;
            strlcpy(row.time, src[1] | "", sizeof(row.time));
            strlcpy(row.text, src[2] | "", sizeof(row.text));
            ui_set_action(offset + n++, row);
        }
        ui_actions_received(doc["n"] | (offset + n));
        return true;
    }
    if (doc["g"].is<JsonArray>()) {
        static AgendaRow rows[AGENDA_MAX];   // static: large for the loop task stack
        memset(rows, 0, sizeof(rows));
        int n = 0;
        for (JsonArray src : doc["g"].as<JsonArray>()) {
            if (n >= AGENDA_MAX) break;
            strlcpy(rows[n].start, src[0] | "", sizeof(rows[n].start));
            strlcpy(rows[n].end, src[1] | "", sizeof(rows[n].end));
            strlcpy(rows[n].title, src[2] | "", sizeof(rows[n].title));
            rows[n].state = src[3] | 2;
            n++;
        }
        ui_update_agenda(rows, doc["o"] | 0, n, doc["n"] | n, (doc["login"] | 0) != 0);
        return true;
    }
    if (doc["r"].is<JsonArray>()) {
        static RoutineRow rows[ROUTINES_MAX];
        memset(rows, 0, sizeof(rows));
        int n = 0;
        for (JsonArray src : doc["r"].as<JsonArray>()) {
            if (n >= ROUTINES_MAX) break;
            strlcpy(rows[n].name, src[0] | "?", sizeof(rows[n].name));
            strlcpy(rows[n].time, src[1] | "", sizeof(rows[n].time));
            rows[n].ok = (src[2] | 1) != 0;
            rows[n].runs = src[3] | 1;
            rows[n].rerunnable = (src[4] | 0) != 0;
            n++;
        }
        ui_update_routines(rows, doc["o"] | 0, n, doc["n"] | n);
        return true;
    }
    if (doc["v"].is<JsonArray>()) {
        static GameRow games[GAMES_MAX];
        memset(games, 0, sizeof(games));
        int n = 0;
        for (JsonArray src : doc["v"].as<JsonArray>()) {
            if (n >= GAMES_MAX) break;
            strlcpy(games[n].opponent, src[0] | "?", sizeof(games[n].opponent));
            games[n].home = strcmp(src[1] | "C", "C") == 0;
            strlcpy(games[n].kickoff, src[2] | "", sizeof(games[n].kickoff));
            strlcpy(games[n].competition, src[3] | "", sizeof(games[n].competition));
            n++;
        }
        ui_update_games(games, doc["o"] | 0, n, doc["n"] | n);
        return true;
    }
    if (!doc["cos"].isNull()) {              // the day's costume (Vasco match day / holidays)
        splash_set_costume(doc["cos"] | 0);
        return true;
    }
    if (doc["ag"].is<JsonArray>()) {          // Antigravity today: [tokens, % of busiest day, responses]
        ui_update_antigravity(doc["ag"][0] | (uint64_t)0, doc["ag"][1] | 0, doc["ag"][2] | 0);
        return true;
    }
    if (doc["pb"].is<JsonArray>()) {         // mouse / keyboard battery %, -1 = unknown
        ui_update_peripherals(doc["pb"][0] | -1, doc["pb"][1] | -1);
        return true;
    }
    if (!doc["k"].isNull()) {                // Kiro credits: [percent, days to reset] or 0
        JsonArray k = doc["k"].as<JsonArray>();
        if (k.isNull()) ui_update_kiro(-1, 0, 0, 0);
        else            ui_update_kiro(k[0] | 0, k[1] | 0, k[2] | -1, k[3] | 0);
        return true;
    }
    if (doc["rrk"].is<JsonArray>()) {         // rerun acknowledgement: [name, 1 started | 0 refused]
        ui_rerun_ack(doc["rrk"][0] | "", (doc["rrk"][1] | 0) != 0);
        return true;
    }
    if (!doc["mt"].isNull()) {               // meeting alert: [title, "HH:MM", seconds, where, joinable] or 0
        JsonArray mt = doc["mt"].as<JsonArray>();
        if (mt.isNull()) ui_update_meeting(nullptr, "", 0, "", false);
        else ui_update_meeting(mt[0] | "", mt[1] | "", mt[2] | 0, mt[3] | "", (mt[4] | 0) != 0);
        return true;
    }
    if (!doc["mgk"].isNull()) {              // answer to "Começar": 1 opened on the Mac, 0 failed
        ui_meeting_join_ack((doc["mgk"] | 0) != 0);
        return true;
    }
    if (!doc["l"].isNull()) {
        JsonArray src = doc["l"].as<JsonArray>();
        if (src.isNull()) {                 // {"l": 0}: the match ended
            ui_update_live(nullptr);
            return true;
        }
        LiveMatch m = {};
        strlcpy(m.home, src[0] | "?", sizeof(m.home));
        strlcpy(m.away, src[1] | "?", sizeof(m.away));
        m.home_goals = src[2] | 0;
        m.away_goals = src[3] | 0;
        strlcpy(m.clock, src[4] | "", sizeof(m.clock));
        strlcpy(m.phase, src[5] | "", sizeof(m.phase));
        strlcpy(m.competition, src[6] | "", sizeof(m.competition));
        ui_update_live(&m);
        return true;
    }
    static const struct { const char* key; quote_table_t table; } QUOTE_KEYS[] = {
        {"x", QUOTES_CRYPTO},
        {"b", QUOTES_STOCKS},
        {"f", QUOTES_FIIS},
    };
    for (const auto& q : QUOTE_KEYS) {
        if (!doc[q.key].is<JsonArray>()) continue;
        QuoteRow rows[QUOTE_PAGE_ROWS] = {};
        int n = 0;
        for (JsonArray src : doc[q.key].as<JsonArray>()) {
            if (n >= QUOTE_PAGE_ROWS) break;
            // [cell, ..., change %]: every element but the last is a string cell.
            const int last = (int)src.size() - 1;
            for (int c = 0; c < last && c < QUOTE_CELLS; c++) {
                strlcpy(rows[n].cells[c], src[c] | "", sizeof(rows[n].cells[c]));
            }
            rows[n].change_pct = last >= 0 ? (src[last] | 0.0f) : 0.0f;
            n++;
        }
        ui_update_quotes(q.table, rows, doc["o"] | 0, n, doc["n"] | n);
        if (doc["i"].is<JsonArray>()) {
            ui_update_stock_index(doc["i"][0] | "---", doc["i"][1] | 0.0f);
        }
        return true;
    }
    return false;
}

// ---- Serial command buffer ----
#define CMD_BUF_SIZE 64
static char cmd_buf[CMD_BUF_SIZE];
static int cmd_pos = 0;

static void send_screenshot() {
#ifndef BOARD_HAS_PSRAM
    // A full RGB565 framebuffer doesn't fit in internal SRAM on PSRAM-free
    // boards (e.g. 480×480×2 = 460 KB). Capture is unsupported there.
    Serial.println("SCREENSHOT_UNSUPPORTED");
    return;
#else
    const uint32_t w = board_caps().width;
    const uint32_t h = board_caps().height;
    const uint32_t row_bytes = w * 2;
    const uint32_t buf_size = row_bytes * h;
    uint8_t* sbuf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!sbuf) {
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, w, h, LV_COLOR_FORMAT_RGB565, row_bytes, sbuf, buf_size);

    lv_result_t res = lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565, &draw_buf);
    if (res != LV_RESULT_OK) {
        heap_caps_free(sbuf);
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    Serial.printf("SCREENSHOT_START %lu %lu %lu\n",
        (unsigned long)w, (unsigned long)h, (unsigned long)buf_size);
    Serial.flush();
    Serial.write(sbuf, buf_size);
    Serial.flush();
    Serial.println();
    Serial.println("SCREENSHOT_END");
    heap_caps_free(sbuf);
#endif
}

static void check_serial_cmd() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_pos] = '\0';
            if (strcmp(cmd_buf, "screenshot") == 0) send_screenshot();
            else if (strcmp(cmd_buf, "buzz") == 0)  sound_hal_play_reset();
            cmd_pos = 0;
        } else if (cmd_pos < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_pos++] = c;
        }
    }
}

// Each board provides this. Must bring up the shared I2C bus (Wire.begin
// with the board's SDA/SCL pins) and any board-private hardware that has
// to settle before display/touch (e.g. an IO expander gating the LCD
// reset line). Called exactly once at the start of setup().
extern "C" void board_init(void);

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("{\"ready\":true}");

    board_init();

    // Wipe what the rolled-back Wi-Fi mode stored (credentials and quote catalog).
    // Only these two NVS namespaces: BLE bonds and brightness stay intact.
    for (const char* ns : {"wifi", "wificat"}) {
        Preferences prefs;
        if (prefs.begin(ns, true)) {         // read-only open fails if it never existed
            prefs.end();
            if (prefs.begin(ns, false)) { prefs.clear(); prefs.end(); Serial.printf("nvs: cleared %s\n", ns); }
        }
    }

    // Bring the BLE controller up before the display: its interrupt allocation
    // runs on the tiny ipc0 stack, and RGB-panel interrupts landing mid-setup
    // overflowed it (boot loop on the Guition 4848S040).
    ble_init();

    display_hal_init();
    display_hal_begin();
    idle_init();        // takes over panel brightness and starts the idle timer
    brightness_init();  // load the user's saved brightness level and apply via idle

    power_hal_init();
    imu_hal_init();
    sound_hal_init();
    touch_hal_init();

    // ---- LVGL ----
    const int W = board_caps().width;
    const int H = board_caps().height;

    lv_init();
#if defined(BOARD_HAS_PSRAM) && LV_MEM_POOL_EXPAND_SIZE > 0
    // The screens outgrew LV_MEM_SIZE (scrollable lists keep every row as
    // objects). Give LVGL a second pool in PSRAM instead of eating internal RAM
    // the BLE stack needs; the built-in allocator spills into it when the
    // internal pool is full.
    {
        static const size_t LV_PSRAM_POOL = LV_MEM_POOL_EXPAND_SIZE;   // 256 KB on guition/sim
        void* pool = heap_caps_malloc(LV_PSRAM_POOL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (pool) lv_mem_add_pool(pool, LV_PSRAM_POOL);
    }
#endif
    lv_tick_set_cb(my_tick);

    buf1 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, LV_BUF_CAPS);
    buf2 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, LV_BUF_CAPS);

    lv_display_t* disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, W * BUF_LINES * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_cb);

    input_hal_init();

    ui_init();
    ui_update_ble_status(ble_get_state(), ble_get_device_name(), ble_get_mac_address());
    ui_update_battery(power_hal_battery_pct(), power_hal_is_charging());
    ui_show_screen(SCREEN_SPLASH);

    Serial.printf("Dashboard ready (%s, %dx%d), waiting for data on BLE...\n",
        board_caps().name, W, H);
}

static ble_state_t last_ble_state = BLE_STATE_INIT;

// Hold-to-pair gesture: hold the PWR button ~3s, then RELEASE → clear all BLE
// bonds and re-advertise. Clearing on *release* (not while held) is deliberate:
// holding to power the device OFF (AXP hardware shutdown at 8s) must not wipe
// the bond — a power-off hold never releases before shutdown. To stop a
// "chicken-out" release just before 8s from pairing, the gesture disarms at 6s.
//
//   ~1.5s long-press edge → PENDING
//   3.0s (+1500)          → ARMED   (release from here clears bonds)
//   6.0s (+4500)          → DISARMED (no clear; AXP powers off at 8s)
#define PAIR_ARM_AFTER_LONG_MS    1500   // 3.0s total
#define PAIR_DISARM_AFTER_LONG_MS 4500   // 6.0s total
enum pair_state_t { PAIR_IDLE, PAIR_PENDING, PAIR_ARMED };
static pair_state_t pair_state        = PAIR_IDLE;
static uint32_t     pair_long_seen_ms = 0;

static void pair_tick(void) {
    if (pair_state == PAIR_IDLE && power_hal_pwr_long_pressed()) {
        pair_state = PAIR_PENDING;
        pair_long_seen_ms = millis();
        (void)power_hal_pwr_released();  // drain any stale release edge
        Serial.println("PWR long-press: hold to ~3s then release to pair");
        return;
    }
    if (pair_state == PAIR_IDLE) return;

    if (power_hal_pwr_released()) {
        if (pair_state == PAIR_ARMED) {
            Serial.println("Pair: released in window — clearing bonds, advertising");
            ble_clear_bonds();
        } else {
            Serial.println("Pair: released too early — cancelled");
        }
        pair_state = PAIR_IDLE;
        return;
    }

    uint32_t held = millis() - pair_long_seen_ms;
    if (pair_state == PAIR_PENDING && held >= PAIR_ARM_AFTER_LONG_MS) {
        pair_state = PAIR_ARMED;
        Serial.println("Pair: armed — release to pair");
    } else if (pair_state == PAIR_ARMED && held >= PAIR_DISARM_AFTER_LONG_MS) {
        pair_state = PAIR_IDLE;  // power-off territory; don't pair
        Serial.println("Pair: disarmed (holding toward power-off)");
    }
}

void loop() {
    idle_tick();
    lv_timer_handler();
    ui_tick_anim();
    ble_tick();
    power_hal_tick();
    imu_hal_tick();
    sound_hal_tick();
    splash_tick();
    splash_mascot_tick();
    // Rotation transition (blank + ramp) would fight the idle fade — skip
    // ticks while the panel is dark. A rotation that happens during sleep
    // is detected by the next tick after wake and ramped in then.
    if (!idle_is_asleep()) display_hal_tick();

    // ---- Physical buttons ----
    //   PRIMARY   → HID Space  (Claude Code voice-mode PTT)
    //   SECONDARY → HID Shift+Tab  (mode toggle; only if the board has one)
    //   PWR       → on splash: cycle animations; on usage: cycle brightness;
    //               hold ~3s + release: pairing mode
    // First press from sleep is consumed as a wake-only event by
    // idle_consume_wake_press(); the normal action fires from the second
    // press. Activity bookkeeping happens inside idle_consume_wake_press
    // so no separate idle_note_activity() call is needed here.
    {
        static bool primary_was = false;
        static bool primary_wake_swallowed = false;
        bool primary_now = input_hal_is_held(INPUT_BTN_PRIMARY);
        if (primary_now != primary_was) {
            if (primary_now) {
                if (idle_consume_wake_press()) primary_wake_swallowed = true;
                else                            ble_keyboard_press(0x2C, 0);  // HID Space, no mods
            } else {
                if (primary_wake_swallowed) primary_wake_swallowed = false;
                else                        ble_keyboard_release();
            }
            primary_was = primary_now;
        }

        if (board_caps().button_count >= 2) {
            static bool secondary_was = false;
            static bool secondary_wake_swallowed = false;
            bool secondary_now = input_hal_is_held(INPUT_BTN_SECONDARY);
            if (secondary_now != secondary_was) {
                if (secondary_now) {
                    if (idle_consume_wake_press()) secondary_wake_swallowed = true;
                    else                            ble_keyboard_press(0x2B, 0x02);  // HID Tab + LEFT_SHIFT
                } else {
                    if (secondary_wake_swallowed) secondary_wake_swallowed = false;
                    else                          ble_keyboard_release();
                }
                secondary_was = secondary_now;
            }
        }

        if (power_hal_pwr_pressed()) {
            if (!idle_consume_wake_press()) {
                // On splash: cycle animations. On the usage view: cycle
                // screen brightness (single non-splash view, no more screens).
                if (ui_get_current_screen() == SCREEN_SPLASH) splash_next();
                else                                          brightness_cycle();
            }
        }

        pair_tick();
    }

    ble_state_t bs = ble_get_state();
    if (bs != last_ble_state) {
        last_ble_state = bs;
        ui_update_ble_status(bs, ble_get_device_name(), ble_get_mac_address());
    }

    static int  last_pct      = -2;
    static bool last_charging = false;
    int  pct      = power_hal_battery_pct();
    bool charging = power_hal_is_charging();
    if (pct != last_pct || charging != last_charging) {
        if (pct != last_pct) ble_set_battery_level(pct);
        last_pct = pct;
        last_charging = charging;
        ui_update_battery(pct, charging);
    }

    check_serial_cmd();

    if (ble_has_data()) {
        const char* json = ble_get_data();
        if (handle_extra_json(json)) {
            ble_send_ack();
        } else if (parse_json(json, &usage)) {
            int g_before = usage_rate_group();
            bool session_reset = usage_rate_sample(usage.session_pct);
            int g_after = usage_rate_group();
            // 5-hour session limit refilled → chime so the user knows they can
            // use Claude again (no-op on boards without a buzzer). Gated on the
            // daemon's opt-in `chime` config; the `buzz` serial cmd ignores it.
            if (session_reset && usage.chime) {
                Serial.println("session reset detected — chime");
                sound_hal_play_reset();
            }
            if (g_after != g_before) {
                Serial.printf("usage rate: group %d -> %d (s=%.2f%%)\n",
                    g_before, g_after, usage.session_pct);
                if (splash_is_active()) splash_pick_for_current_rate();
            }
            ui_update(&usage);
            ble_send_ack();
        } else {
            ble_send_nack();
        }
    }

    delay(5);
}
