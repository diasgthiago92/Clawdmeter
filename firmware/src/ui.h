#pragma once
#include "data.h"
#include "ble.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_AGENDA,
    SCREEN_HISTORY,
    SCREEN_ACTIONS,          // Últimas Ações (Claude, Kiro, Gemini)
    SCREEN_ROUTINES,
    SCREEN_CRYPTO,
    SCREEN_STOCKS,
    SCREEN_FIIS,             // fundos imobiliários
    SCREEN_VASCO,
    SCREEN_LIVE,             // only reachable while a Vasco match is on
    SCREEN_MEETING,          // only while a meeting starts within 5 minutes
    SCREEN_ROUTINE_ALERT,    // only while a routine failure is unacknowledged
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_update_history_bars(const char* claude, const char* kiro, const char* ag,
                            uint64_t tokens, float kiro_credits, uint64_t ag_tokens);
void ui_update_antigravity(uint64_t tokens_today, int pct_of_peak, int responses);
void ui_set_action(int index, const ActionRow& row);
void ui_actions_received(int total);   // after a chunk of ui_set_action calls
enum quote_table_t { QUOTES_CRYPTO, QUOTES_STOCKS, QUOTES_FIIS, QUOTE_TABLE_COUNT };
void ui_update_quotes(quote_table_t table, const QuoteRow* rows, int offset, int count, int total);
void ui_update_stock_index(const char* value, float change_pct);
void ui_update_agenda(const AgendaRow* rows, int offset, int count, int total, bool needs_login);
// Daemon answer to a rerun request: ok = the LaunchAgent was started.
void ui_rerun_ack(const char* name, bool ok);
void ui_update_routines(const RoutineRow* rows, int offset, int count, int total);
void ui_update_games(const GameRow* rows, int offset, int count, int total);
void ui_update_kiro(int percent, int reset_days, int credits_used, int credit_limit);  // percent < 0 = hide
void ui_update_live(const LiveMatch* match);
// Meeting alert: title, "HH:MM", seconds to start, room/link. title == nullptr clears it.
void ui_update_meeting(const char* title, const char* hhmm, int seconds, const char* where);   // nullptr = match over

void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging);
void ui_update_peripherals(int mouse_pct, int keyboard_pct);   // -1 = unknown
