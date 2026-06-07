#pragma once

#include <Arduino.h>

namespace BoardPins {
constexpr int I2C_SCL = 14;
constexpr int I2C_SDA = 13;
constexpr int I2C_PORT = 0;

constexpr int RLCD_SCK = 11;
constexpr int RLCD_MOSI = 12;
constexpr int RLCD_DC = 5;
constexpr int RLCD_CS = 40;
constexpr int RLCD_RST = 41;

constexpr int LCD_WIDTH = 400;
constexpr int LCD_HEIGHT = 300;

constexpr int BOOT_KEY = 0;
constexpr int GP18_KEY = 18;

constexpr uint8_t RTC_ADDR = 0x51;
}  // namespace BoardPins
