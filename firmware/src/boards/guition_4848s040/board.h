#pragma once

// Guition / Sunton ESP32-4848S040 — 4" 480x480 ST7701 RGB TFT + GT911 touch.
// ESP32-S3R8, CH340 USB-UART (no native USB), no buttons, no PMU, no IMU.
// Pin map matches Arduino_GFX's ESP32_4848S040_86BOX_GUITION declaration.

#define BOARD_NAME           "Guition 4848S040"

#define LCD_WIDTH            480
#define LCD_HEIGHT           480

#define LCD_DE               18
#define LCD_VSYNC            17
#define LCD_HSYNC            16
#define LCD_PCLK             21
#define LCD_R0               11
#define LCD_R1               12
#define LCD_R2               13
#define LCD_R3               14
#define LCD_R4                0
#define LCD_G0                8
#define LCD_G1               20
#define LCD_G2                3
#define LCD_G3               46
#define LCD_G4                9
#define LCD_G5               10
#define LCD_B0                4
#define LCD_B1                5
#define LCD_B2                6
#define LCD_B3                7
#define LCD_B4               15

#define LCD_SPI_CS           39
#define LCD_SPI_SCK          48
#define LCD_SPI_MOSI         47

#define LCD_BL               38

#define IIC_SDA              19
#define IIC_SCL              45

// GPIO 0 (BOOT) is RGB R4, so there is no usable button.
#define BOARD_HAS_SECONDARY_BUTTON 0
#define BOARD_HAS_ROTATION         0
#define BOARD_HAS_IMU              0
#define BOARD_HAS_BATTERY          0
#define BOARD_HAS_IO_EXPANDER      0
#define BOARD_HAS_SOUND            0
