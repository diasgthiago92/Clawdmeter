#pragma once
#include <Arduino.h>

struct UsageData {
    float session_pct;       // utilization 0-100 (5h window Pro/Max; spending % Enterprise)
    int session_reset_mins;  // minutes until reset
    float weekly_pct;        // 7-day utilization (Pro/Max only; 0 for Enterprise)
    int weekly_reset_mins;   // minutes until weekly reset (Pro/Max only)
    char status[16];         // "allowed", "limited", etc.
    bool chime;              // play the session-reset chime; false unless daemon opts in
    bool enterprise;         // true = Enterprise spending-limit account
    int time_pct;            // 0-100: fraction of billing period elapsed (Enterprise)
    int period_days;         // total billing period length in days (Enterprise)
    char reset_date[12];     // formatted reset date e.g. "Jul 1" (Enterprise)
    long clock_epoch;        // local wall-clock epoch (s) from daemon; 0 = not provided
    int  clock_fmt;          // 12 or 24 (hour format from daemon); defaults to 24
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
};

// Session % over the last 24h, oldest first, one char per 15-min bin:
// base64 alphabet index = round(pct * 63 / 100), '-' = no sample.
#define HISTORY_BINS 96

// Últimas Ações: newest tool calls per AI, grouped Claude → Kiro → Gemini.
#define ACTIONS_MAX 24
struct ActionRow {
    uint8_t ai;              // 0 Claude, 1 Kiro, 2 Gemini
    char    time[6];         // "HH:MM"; empty = the AI had no action in the window
    char    text[72];        // UTF-8, already clipped by the daemon
};

// One row of the crypto / B3 tables: pre-formatted cells + change %.
// A table holds up to QUOTE_TABLE_ROWS and shows QUOTE_PAGE_ROWS at a time.
#define QUOTE_PAGE_ROWS  8
#define QUOTE_TABLE_ROWS 48
#define QUOTE_CELLS      4

#define GAMES_MAX 10
struct GameRow {
    char opponent[20];
    bool home;
    char kickoff[20];        // "Sáb 19/09 20:30", local time from the daemon
    char competition[20];
};
struct QuoteRow {
    char  cells[QUOTE_CELLS][12];  // crypto: symbol, BRL, USD · stocks: ticker, name, BRL, P/VP
    float change_pct;
};

// Vasco match in progress ({"l": [...]} from the daemon). While one is on the
// device holds SCREEN_LIVE and pauses auto-rotation.
struct LiveMatch {
    char home[16];
    char away[16];
    int  home_goals;
    int  away_goals;
    char clock[12];          // "67'", "45'+2'"; empty at half-time
    char phase[28];          // "1º tempo", "Intervalo", ...
    char competition[20];
};

// Today's Kiro routines, as announced by the leo-dias bot in Slack.
#define ROUTINES_MAX 24
struct RoutineRow {
    char name[28];           // UTF-8, up to 22 characters
    char time[6];            // "HH:MM" of the latest run
    bool ok;                 // latest run succeeded
    int  runs;               // runs today
    bool rerunnable;         // the daemon knows how to start it again
};

// Today's Google Calendar meetings.
#define AGENDA_MAX 24
struct AgendaRow {
    char start[6];           // "HH:MM" or "Dia" (all-day)
    char end[6];
    char title[48];          // UTF-8, up to 30 characters
    uint8_t state;           // 0 over, 1 now, 2 upcoming
};
