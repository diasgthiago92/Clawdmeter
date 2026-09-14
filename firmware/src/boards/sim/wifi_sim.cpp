// Simulator: no Wi-Fi. The standalone mode is a no-op; the desktop sim is always
// fed by its scenario file instead.
#include "../../wifi_mode.h"
#include <stdio.h>

void wifi_mode_init(void) {}
void wifi_mode_set_credentials(const char* ssid, const char* password) {
    (void)password;
    printf("[sim] wifi credentials received for \"%s\" (ignored in the simulator)\n", ssid ? ssid : "");
}
void wifi_mode_note_mac(void) {}
void wifi_mode_capture_row(char, int, int, const char*, const char*, const char*) {}
void wifi_mode_tick(wifi_payload_handler) {}
bool wifi_mode_delivering(void) { return false; }
