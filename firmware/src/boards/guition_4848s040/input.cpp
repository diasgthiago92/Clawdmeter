#include "../../hal/input_hal.h"

// No buttons: GPIO 0 is RGB data. Tap the screen to toggle splash/usage.

void input_hal_init(void) {}

bool input_hal_is_held(InputButton btn) {
    (void)btn;
    return false;
}
