#pragma once
#include <stddef.h>
#include <stdbool.h>

// Standalone Wi-Fi mode: when the Mac has been quiet for a few minutes, the
// device fetches crypto (CoinGecko), B3 stocks and FIIs (Yahoo) and Vasco
// fixtures (ESPN) by itself. Results are built as the same JSON payloads the
// daemon sends and fed back through the normal parser, so screens don't change.
// The Mac stays the source of truth for *what* to show: the ticker lists, names
// and P/VP of the last daemon sync are kept in flash.

using wifi_payload_handler = void (*)(const char* json);

void wifi_mode_init(void);
// {"wifi": [ssid, password]} from the daemon; stored in NVS, (re)connects.
void wifi_mode_set_credentials(const char* ssid, const char* password);
// Any data arrived from the Mac over BLE: the Wi-Fi fetcher stays idle.
void wifi_mode_note_mac(void);
// Remember a quote table the daemon sent ('x' crypto, 'b' stocks, 'f' FIIs).
// cells: ticker/symbol, name (or BRL for crypto), price, P/VP.
void wifi_mode_capture_row(char table, int index, int total, const char* c0, const char* c1, const char* pvp);
// Call from loop(): persists catalog changes and hands fetched payloads to `handler`.
void wifi_mode_tick(wifi_payload_handler handler);
// True while payloads come from the Wi-Fi fetcher (so the parser doesn't treat them as the Mac).
bool wifi_mode_delivering(void);
