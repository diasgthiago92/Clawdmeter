#include "wifi_mode.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <time.h>

#define TAKEOVER_MS      (3UL * 60 * 1000)    // Mac quiet this long -> fetch by ourselves
#define QUOTES_EVERY_MS  (5UL * 60 * 1000)
#define GAMES_EVERY_MS   (30UL * 60 * 1000)
#define SAVE_DEBOUNCE_MS (30UL * 1000)
#define CAT_MAX          48
#define TZ_BRASILIA      "<-03>3"

struct CatRow {
    char c0[12];      // ticker / crypto symbol
    char c1[16];      // name (stocks, FIIs)
    char pvp[12];
};
struct Catalog {
    char key;
    CatRow rows[CAT_MAX];
    int count;
    bool dirty;
};
static Catalog catalogs[3] = {{'x'}, {'b'}, {'f'}};
static SemaphoreHandle_t cat_lock;
static QueueHandle_t out_queue;         // char* JSON payloads (PSRAM), freed by the consumer
static uint32_t last_mac_ms = 0, last_change_ms = 0;
static bool delivering = false;
static char wifi_ssid[33] = "", wifi_pass[64] = "";
static TaskHandle_t fetch_task = nullptr;

static Catalog* catalog_for(char key) {
    for (auto& c : catalogs) if (c.key == key) return &c;
    return nullptr;
}

static void catalog_load(void) {
    Preferences p;
    if (!p.begin("wificat", true)) return;
    for (auto& c : catalogs) {
        char k[2] = {c.key, 0};
        size_t n = p.getBytes(k, c.rows, sizeof(c.rows));
        c.count = (int)(n / sizeof(CatRow));
    }
    p.end();
}

static void catalog_save_if_dirty(void) {
    if (!last_change_ms || millis() - last_change_ms < SAVE_DEBOUNCE_MS) return;
    Preferences p;
    if (!p.begin("wificat", false)) return;
    xSemaphoreTake(cat_lock, portMAX_DELAY);
    for (auto& c : catalogs) {
        if (!c.dirty) continue;
        char k[2] = {c.key, 0};
        p.putBytes(k, c.rows, sizeof(CatRow) * c.count);
        c.dirty = false;
    }
    xSemaphoreGive(cat_lock);
    p.end();
    last_change_ms = 0;
}

void wifi_mode_capture_row(char table, int index, int total, const char* c0, const char* c1, const char* pvp) {
    if (delivering) return;                  // our own payloads carry nothing new
    Catalog* c = catalog_for(table);
    if (!c || index < 0 || index >= CAT_MAX) return;
    if (total > CAT_MAX) total = CAT_MAX;
    CatRow row = {};
    strlcpy(row.c0, c0 ? c0 : "", sizeof(row.c0));
    strlcpy(row.c1, c1 ? c1 : "", sizeof(row.c1));
    strlcpy(row.pvp, pvp ? pvp : "", sizeof(row.pvp));
    xSemaphoreTake(cat_lock, portMAX_DELAY);
    if (c->count != total || memcmp(&c->rows[index], &row, sizeof(row)) != 0) {
        c->rows[index] = row;
        c->count = total;
        c->dirty = true;
        last_change_ms = millis();
    }
    xSemaphoreGive(cat_lock);
}

void wifi_mode_note_mac(void) {
    if (!delivering) last_mac_ms = millis();
}

bool wifi_mode_delivering(void) { return delivering; }

// ---- helpers ----
static void push_payload(JsonDocument& doc) {
    const size_t len = measureJson(doc);
    char* buf = (char*)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return;
    serializeJson(doc, buf, len + 1);
    if (xQueueSend(out_queue, &buf, pdMS_TO_TICKS(1000)) != pdTRUE) heap_caps_free(buf);
}

static bool https_get(const char* url, bool browser_ua, String& body) {
    WiFiClientSecure client;
    client.setInsecure();                    // public market/fixture data only; nothing personal
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.setTimeout(12000);
    if (browser_ua) http.setUserAgent("Mozilla/5.0");
    const int code = http.GET();
    if (code == 200) body = http.getString();
    http.end();
    return code == 200 && body.length() > 0;
}

// pt-BR number: "395.187", "274,22", "0,4338".
static void fmt_number(double v, int decimals, char* out, size_t n) {
    if (decimals < 0) decimals = v >= 1000 ? 0 : v >= 1 ? 2 : 4;
    char raw[40];
    snprintf(raw, sizeof(raw), "%.*f", decimals, v);
    char* dot = strchr(raw, '.');
    const int int_len = dot ? (int)(dot - raw) : (int)strlen(raw);
    size_t o = 0;
    for (int i = 0; i < int_len && o + 2 < n; i++) {
        if (i > 0 && raw[0] != '-' && (int_len - i) % 3 == 0) out[o++] = '.';
        out[o++] = raw[i];
    }
    if (dot && o + 1 < n) {
        out[o++] = ',';
        for (char* p = dot + 1; *p && o + 1 < n; p++) out[o++] = *p;
    }
    out[o] = 0;
}

static float round_change(double pct) {
    float r = (float)(round(pct * 10.0) / 10.0);
    return r == 0.0f ? 0.0f : r;
}

// ---- fetchers ----
static const struct { const char* symbol; const char* id; } COIN_IDS[] = {
    {"BTC", "bitcoin"}, {"ETH", "ethereum"}, {"BNB", "binancecoin"}, {"SOL", "solana"},
    {"XRP", "ripple"}, {"ADA", "cardano"}, {"DOGE", "dogecoin"}, {"LTC", "litecoin"},
};

static void fetch_crypto(void) {
    Catalog snap;
    xSemaphoreTake(cat_lock, portMAX_DELAY);
    snap = *catalog_for('x');
    xSemaphoreGive(cat_lock);
    if (!snap.count) return;
    String ids;
    for (int i = 0; i < snap.count; i++)
        for (auto& c : COIN_IDS)
            if (strcmp(c.symbol, snap.rows[i].c0) == 0) { if (ids.length()) ids += ","; ids += c.id; }
    String body;
    const String url = "https://api.coingecko.com/api/v3/simple/price?vs_currencies=brl,usd&include_24hr_change=true&ids=" + ids;
    if (!https_get(url.c_str(), false, body)) return;
    JsonDocument data;
    if (deserializeJson(data, body)) return;
    JsonDocument doc;
    JsonArray rows = doc["x"].to<JsonArray>();
    for (int i = 0; i < snap.count; i++) {
        const char* id = nullptr;
        for (auto& c : COIN_IDS) if (strcmp(c.symbol, snap.rows[i].c0) == 0) id = c.id;
        if (!id || !data[id]["brl"].is<double>()) continue;
        char brl[20], usd[20];
        fmt_number(data[id]["brl"].as<double>(), -1, brl, sizeof(brl));
        fmt_number(data[id]["usd"] | 0.0, -1, usd, sizeof(usd));
        JsonArray r = rows.add<JsonArray>();
        r.add(snap.rows[i].c0); r.add(brl); r.add(usd); r.add(round_change(data[id]["brl_24h_change"] | 0.0));
    }
    doc["o"] = 0;
    doc["n"] = rows.size();
    if (rows.size()) push_payload(doc);
}

static bool yahoo_quote(const char* symbol, double* price, float* change) {
    String body;
    char url[160];
    snprintf(url, sizeof(url), "https://query1.finance.yahoo.com/v8/finance/chart/%s?range=1d&interval=1d", symbol);
    if (!https_get(url, true, body)) return false;
    JsonDocument filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
    JsonObject meta = doc["chart"]["result"][0]["meta"];
    const double p = meta["regularMarketPrice"] | 0.0, prev = meta["chartPreviousClose"] | 0.0;
    if (p <= 0 || prev <= 0) return false;
    *price = p;
    *change = round_change((p - prev) / prev * 100.0);
    return true;
}

static void fetch_b3(char key) {
    Catalog* live = catalog_for(key);
    Catalog* snap = (Catalog*)heap_caps_malloc(sizeof(Catalog), MALLOC_CAP_SPIRAM);
    if (!snap) return;
    xSemaphoreTake(cat_lock, portMAX_DELAY);
    *snap = *live;
    xSemaphoreGive(cat_lock);
    if (key == 'b') {                        // Ibovespa goes along with the first stocks chunk
        double idx; float idx_chg;
        if (yahoo_quote("%5EBVSP", &idx, &idx_chg)) {
            JsonDocument doc;
            char v[20];
            fmt_number(idx, 0, v, sizeof(v));
            doc["b"].to<JsonArray>();
            JsonArray i = doc["i"].to<JsonArray>();
            i.add(v); i.add(idx_chg);
            doc["o"] = 0;
            doc["n"] = snap->count;
            push_payload(doc);
        }
    }
    const char k[2] = {key, 0};
    for (int start = 0; start < snap->count; start += 8) {
        JsonDocument doc;
        JsonArray rows = doc[k].to<JsonArray>();
        int offset = -1;
        for (int i = start; i < start + 8 && i < snap->count; i++) {
            double price; float change;
            char sym[20];
            snprintf(sym, sizeof(sym), "%s.SA", snap->rows[i].c0);
            if (!yahoo_quote(sym, &price, &change)) continue;
            char p[20];
            fmt_number(price, 2, p, sizeof(p));
            if (offset < 0) offset = i;
            // Keep the rows contiguous from `offset`; a failed ticker ends this chunk early.
            if (offset + (int)rows.size() != i) break;
            JsonArray r = rows.add<JsonArray>();
            r.add(snap->rows[i].c0); r.add(snap->rows[i].c1); r.add(p); r.add(snap->rows[i].pvp); r.add(change);
        }
        if (!rows.size()) continue;
        doc["o"] = offset;
        doc["n"] = snap->count;
        push_payload(doc);
    }
    heap_caps_free(snap);
}

static time_t parse_iso_utc(const char* s) {   // "2026-09-19T23:30Z" / "...:00Z"
    int y, mo, d, h = 0, mi = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) < 3) return 0;
    y -= mo <= 2;                                // days from civil (Howard Hinnant)
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = era * 146097L + (long)doe - 719468L;
    return (time_t)(days * 86400L + h * 3600L + mi * 60L);
}

static const char* competition_name(const char* slug, const char* fallback) {
    static const struct { const char* slug; const char* name; } NAMES[] = {
        {"bra.1", "Brasileirão"}, {"bra.2", "Série B"}, {"bra.copa_do_brazil", "Copa do Brasil"},
        {"bra.camp.carioca", "Carioca"}, {"conmebol.libertadores", "Libertadores"},
        {"conmebol.sudamericana", "Sul-Americana"},
    };
    for (auto& n : NAMES) if (slug && strcmp(slug, n.slug) == 0) return n.name;
    return fallback ? fallback : "";
}

static void fetch_games(void) {
    const time_t now = time(nullptr);
    if (now < 1700000000) return;             // clock not synced yet
    String body;
    if (!https_get("https://site.api.espn.com/apis/site/v2/sports/soccer/all/teams/3454/schedule?fixture=true",
                   false, body)) return;
    JsonDocument filter;
    JsonObject ev = filter["events"][0].to<JsonObject>();
    ev["date"] = true;
    ev["timeValid"] = true;
    ev["league"]["slug"] = true;
    ev["league"]["shortName"] = true;
    ev["competitions"][0]["status"]["type"]["state"] = true;
    ev["competitions"][0]["competitors"][0]["homeAway"] = true;
    ev["competitions"][0]["competitors"][0]["team"]["id"] = true;
    ev["competitions"][0]["competitors"][0]["team"]["shortDisplayName"] = true;
    JsonDocument data;
    if (deserializeJson(data, body, DeserializationOption::Filter(filter))) return;
    body = String();
    static const char* WEEKDAYS[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb"};
    JsonDocument doc;
    JsonArray rows = doc["v"].to<JsonArray>();
    for (JsonObject e : data["events"].as<JsonArray>()) {
        if (rows.size() >= 10) break;
        JsonObject comp = e["competitions"][0];
        if (strcmp(comp["status"]["type"]["state"] | "", "pre") != 0) continue;
        const time_t start = parse_iso_utc(e["date"] | "");
        if (!start || start < now) continue;
        const char* opponent = "?";
        bool home = true;
        for (JsonObject c : comp["competitors"].as<JsonArray>()) {
            if (strcmp(c["team"]["id"] | "", "3454") == 0) home = strcmp(c["homeAway"] | "", "home") == 0;
            else opponent = c["team"]["shortDisplayName"] | "?";
        }
        struct tm lt;
        localtime_r(&start, &lt);
        char when[24];
        if (e["timeValid"] | true)
            snprintf(when, sizeof(when), "%s %02d/%02d %02d:%02d", WEEKDAYS[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1, lt.tm_hour, lt.tm_min);
        else
            snprintf(when, sizeof(when), "%s %02d/%02d", WEEKDAYS[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1);
        JsonArray r = rows.add<JsonArray>();
        r.add(opponent); r.add(home ? "C" : "F"); r.add(when);
        r.add(competition_name(e["league"]["slug"] | "", e["league"]["shortName"] | ""));
    }
    doc["o"] = 0;
    doc["n"] = rows.size();
    push_payload(doc);
}

static void fetch_loop(void*) {
    uint32_t last_quotes = 0, last_games = 0;
    bool time_started = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (WiFi.status() != WL_CONNECTED) continue;
        if (!time_started) {
            configTzTime(TZ_BRASILIA, "pool.ntp.org", "time.google.com");
            time_started = true;
        }
        const uint32_t now = millis();
        const bool mac_quiet = now > TAKEOVER_MS && (last_mac_ms == 0 || now - last_mac_ms > TAKEOVER_MS);
        if (!mac_quiet) continue;
        if (!last_quotes || now - last_quotes > QUOTES_EVERY_MS) {
            Serial.println("wifi: Mac quiet, fetching quotes");
            fetch_crypto();
            fetch_b3('b');
            fetch_b3('f');
            last_quotes = millis();
        }
        if (!last_games || now - last_games > GAMES_EVERY_MS) {
            fetch_games();
            last_games = millis();
        }
    }
}

static void wifi_connect(void) {
    if (!wifi_ssid[0]) return;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifi_ssid, wifi_pass);
    Serial.printf("wifi: connecting to %s\n", wifi_ssid);
    if (!fetch_task) xTaskCreatePinnedToCore(fetch_loop, "wifi_fetch", 12288, nullptr, 1, &fetch_task, 0);
}

void wifi_mode_init(void) {
    cat_lock = xSemaphoreCreateMutex();
    out_queue = xQueueCreate(24, sizeof(char*));
    catalog_load();
    Preferences p;
    if (p.begin("wifi", true)) {
        p.getString("ssid", wifi_ssid, sizeof(wifi_ssid));
        p.getString("pass", wifi_pass, sizeof(wifi_pass));
        p.end();
    }
    wifi_connect();
}

void wifi_mode_set_credentials(const char* ssid, const char* password) {
    if (!ssid || !ssid[0]) return;
    if (strcmp(ssid, wifi_ssid) == 0 && strcmp(password ? password : "", wifi_pass) == 0) return;
    strlcpy(wifi_ssid, ssid, sizeof(wifi_ssid));
    strlcpy(wifi_pass, password ? password : "", sizeof(wifi_pass));
    Preferences p;
    if (p.begin("wifi", false)) {
        p.putString("ssid", wifi_ssid);
        p.putString("pass", wifi_pass);
        p.end();
    }
    WiFi.disconnect();
    wifi_connect();
}

void wifi_mode_tick(wifi_payload_handler handler) {
    catalog_save_if_dirty();
    char* json;
    while (out_queue && xQueueReceive(out_queue, &json, 0) == pdTRUE) {
        delivering = true;
        if (handler) handler(json);
        delivering = false;
        heap_caps_free(json);
    }
}
