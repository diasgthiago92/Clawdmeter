#include "sim_platform.h"
#include <SDL.h>
#include <Arduino.h>
#include <stdlib.h>

static bool quit = false;

static bool     pwr_down = false;
static uint32_t pwr_down_ms = 0;
static bool     pwr_long_fired = false;
static bool     edge_pressed = false, edge_long = false, edge_released = false;

static int  battery = 87;
static bool charging = false;

static bool take(bool* f) { bool v = *f; *f = false; return v; }
bool sim_take_pwr_pressed(void)  { return take(&edge_pressed); }
bool sim_take_pwr_long(void)     { return take(&edge_long); }
bool sim_take_pwr_released(void) { return take(&edge_released); }
int  sim_battery_pct(void) { return battery; }
bool sim_charging(void)    { return charging; }
bool sim_should_quit(void) { return quit; }
void sim_request_quit(void) { quit = true; }

static int      tap_x = 0, tap_y = 0;
static int      drag_y0 = 0, drag_y1 = 0;
static uint32_t drag_start = 0, drag_ms = 0;
static uint32_t tap_until = 0;
void sim_inject_tap(int x, int y) { tap_x = x; tap_y = y; drag_ms = 0; tap_until = millis() + 80; }
void sim_inject_drag(int x, int y0, int y1, int ms) {
    tap_x = x; drag_y0 = y0; drag_y1 = y1;
    drag_ms = ms > 0 ? ms : 1;
    drag_start = millis();
    tap_until = drag_start + drag_ms;
}
bool sim_injected_touch(int* x, int* y) {
    *x = tap_x;
    *y = tap_y;
    const uint32_t now = millis();
    if (drag_ms && now - drag_start <= drag_ms) {
        *y = drag_y0 + (int)((int64_t)(drag_y1 - drag_y0) * (now - drag_start) / drag_ms);
        return true;
    }
    if (drag_ms && now - drag_start > drag_ms) drag_ms = 0;
    return tap_until && now < tap_until;
}

// Matches the AXP2101 long-press threshold main.cpp's pair gesture expects.
#define PWR_LONG_MS 1500

void sim_pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit = true;
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Keycode k = e.key.keysym.sym;
            switch (k) {
            case SDLK_ESCAPE: quit = true; break;
            case SDLK_SPACE:  sim_playback_toggle(); break;
            case SDLK_LEFT:   sim_playback_step(-1); break;
            case SDLK_RIGHT:  sim_playback_step(+1); break;
            case SDLK_d:      sim_playback_toggle_link(); break;
            case SDLK_s:      sim_display_screenshot(NULL); break;
            case SDLK_c:      charging = !charging; break;
            case SDLK_MINUS:  battery = battery < 5 ? 0 : battery - 5; break;
            case SDLK_EQUALS: battery = battery > 95 ? 100 : battery + 5; break;
            case SDLK_p:
                pwr_down = true;
                pwr_down_ms = millis();
                pwr_long_fired = false;
                break;
            default:
                if (k >= SDLK_1 && k <= SDLK_9) sim_playback_jump(k - SDLK_1);
                break;
            }
        }
        if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_p && pwr_down) {
            pwr_down = false;
            if (!pwr_long_fired) edge_pressed = true;
            edge_released = true;
        }
    }
    if (pwr_down && !pwr_long_fired && millis() - pwr_down_ms >= PWR_LONG_MS) {
        pwr_long_fired = true;
        edge_long = true;
    }

    sim_display_record_tick();

    // Headless CI hook: SIM_AUTOSHOT_MS=<ms> → screenshot + exit.
    static long autoshot_ms = -2;
    if (autoshot_ms == -2) {
        const char* v = getenv("SIM_AUTOSHOT_MS");
        autoshot_ms = v ? atol(v) : -1;
    }
    if (autoshot_ms >= 0 && millis() >= (uint32_t)autoshot_ms) {
        const char* p = getenv("SIM_AUTOSHOT_PATH");
        sim_display_screenshot(p ? p : "sim-autoshot.bmp");
        quit = true;
        autoshot_ms = -1;
    }
}
