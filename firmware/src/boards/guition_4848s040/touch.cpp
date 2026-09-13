#include "../../hal/touch_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <TouchDrvGT911.hpp>

// GT911 INT/RST are not wired to GPIOs on this board, so the address depends
// on power-up state — probe 0x5D then fall back to 0x14.

static TouchDrvGT911 touch;

static bool     touch_ok      = false;
static bool     touch_pressed = false;
static uint16_t touch_x       = 0;
static uint16_t touch_y       = 0;

void touch_hal_init(void) {
    uint8_t gt_addr = 0x14;
    Wire.beginTransmission(0x5D);
    if (Wire.endTransmission() == 0) gt_addr = 0x5D;

    touch.setPins(-1, -1);
    if (!touch.begin(Wire, gt_addr, IIC_SDA, IIC_SCL)) {
        Serial.printf("Touch GT911 init failed (tried 0x%02X)\n", gt_addr);
        return;
    }
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
    touch.setSwapXY(false);
    touch.setMirrorXY(false, false);
    touch_ok = true;
    Serial.printf("Touch GT911 init OK (addr 0x%02X)\n", gt_addr);
}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    if (touch_ok) {
        int16_t tx[5], ty[5];
        uint8_t n = touch.getPoint(tx, ty, touch.getSupportTouchPoint());
        touch_pressed = n > 0;
        if (touch_pressed) {
            touch_x = (uint16_t)tx[0];
            touch_y = (uint16_t)ty[0];
        }
    }
    *x = touch_x;
    *y = touch_y;
    *pressed = touch_pressed;
}
