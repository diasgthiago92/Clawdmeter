#pragma once
#include <stdint.h>
#include <lvgl.h>

// Initialize splash module. Creates the canvas widget inside `parent` and
// allocates the 480x480 pixel buffer (PSRAM).
void splash_init(lv_obj_t *parent);

// Advance animation frame if hold time elapsed. Call from main loop.
void splash_tick(void);

// Cycle to the next animation in the catalog.
void splash_next(void);

// Show/hide the splash container.
void splash_show(void);
void splash_hide(void);

// Pick the next animation matching the current usage-rate group.
// Called automatically by splash_show(); also exposed so other modules can
// trigger a re-pick when the rate group changes mid-display.
void splash_pick_for_current_rate(void);

// True when splash is currently rendering (used to gate re-picks).
bool splash_is_active(void);

// True while the Kiro ghost (plain or Vasco shirt) is the character on screen:
// its splash turn when the splash shows, its corner turn otherwise.
bool splash_kiro_on_screen(void);

// Root container (so ui.cpp can attach a click event).
lv_obj_t* splash_get_root(void);

// Mini animated creature for embedding elsewhere (e.g. the idle screen).
// Renders the named official animation (e.g. "cloud") at ~px×px
// inside `parent`; returns the canvas object (position it with lv_obj_align) or
// NULL if the animation isn't found / allocation fails. Drive it with
// splash_mini_tick(). One mini creature at a time.
lv_obj_t* splash_mini_create(lv_obj_t *parent, const char *anim_name, int px);
void splash_mini_tick(void);

// Kiro ghost flying side to side (stands in for the cloud rider on the idle
// screen during Kiro's turn). Uses the corner's current variant (plain/Vasco).
// Returns the image object, hidden until splash_flyer_tick() draws it.
lv_obj_t* splash_flyer_create(lv_obj_t *parent, int px);
// left/right = x travel limits (px, parent coords), center_y = vertical center.
void splash_flyer_tick(int left, int right, int center_y);

// Corner mascot (usage screen, PSRAM boards): the still Clawd idles in the
// logo slot, does occasional acts, and takes walk-off/lurk/walk-back trips.
// feet_y = px of the art's ground line; cell = px per art cell in the corner.
lv_obj_t* splash_mascot_create(lv_obj_t *parent, int slot_x, int feet_y, int cell);
void splash_mascot_tick(void);
void splash_mascot_set_visible(bool v);
