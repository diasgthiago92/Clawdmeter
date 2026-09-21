#pragma once
#include "data.h"
#include "ble.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_AGENDA,
    SCREEN_HISTORY,
    SCREEN_ROUTINES,
    SCREEN_POSTS,            // cronograma de posts: Instagram e TikTok
    SCREEN_CRYPTO,
    SCREEN_STOCKS,
    SCREEN_RATES,            // juros futuros (DI1)
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
void ui_update_antigravity(int used_pct, int reset_mins);
void ui_update_antigravity_daily(uint64_t tokens_today, int pct_of_peak, int responses);
void ui_update_codex_daily(int used_pct, int reset_mins, uint64_t tokens);
void ui_update_codex_weekly(int used_pct, int reset_mins, uint64_t tokens);
enum quote_table_t { QUOTES_CRYPTO, QUOTES_STOCKS, QUOTES_FIIS, QUOTE_TABLE_COUNT };
void ui_update_quotes(quote_table_t table, const QuoteRow* rows, int offset, int count, int total);
void ui_update_rates(const RatePoint* points, int offset, int count, int total);
void ui_update_stock_index(const char* value, float change_pct);
void ui_update_agenda(const AgendaRow* rows, int offset, int count, int total, bool needs_login);
// Daemon answer to a rerun request: ok = the LaunchAgent was started.
void ui_rerun_ack(const char* name, bool ok);
void ui_update_routines(const RoutineRow* rows, int offset, int count, int total);
void ui_update_posts(const PostRow* rows, int offset, int count, int total);
void ui_update_games(const GameRow* rows, int offset, int count, int total);
void ui_update_kiro(int percent, int reset_days, int credits_used, int credit_limit);  // percent < 0 = hide
void ui_update_live(const LiveMatch* match);
// Meeting alert: title, "HH:MM", seconds to start, room/link. title == nullptr clears it.
// joinable = the daemon can open its video link ("Começar" button).
void ui_update_meeting(const char* title, const char* hhmm, int seconds, const char* where, bool joinable);
void ui_meeting_join_ack(bool ok);   // daemon answer to "Começar"

// Short message pushed by the Mac (autopost, scripts): balloon on the top
// layer for 10 s, green border when ok, red when it failed.
void ui_show_notice(const char* text, bool ok);

void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging);
void ui_update_peripherals(int mouse_pct, int keyboard_pct);   // -1 = unknown
