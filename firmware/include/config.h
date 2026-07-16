#pragma once

// ---------- Servo GPIO pins ----------
// Pins differ per board; the active board is selected by a -D flag in
// platformio.ini (BOARD_ESP32_DEVKIT or BOARD_XIAO_ESP32C6).
// A board that defines PIN_SERVO_VOLUP gets a third (volume-up) servo;
// boards that leave it undefined build without one.
#if defined(BOARD_XIAO_ESP32C6)
  // Seeed XIAO ESP32-C6
  #define PIN_SERVO_POWER   18   // D10
  #define PIN_SERVO_VOLUP   20   // D9
  #define PIN_SERVO_VOLDN   19   // D8
  // SSD1306 128x64 OLED on I2C
  #define HAS_OLED
  #define PIN_I2C_SDA       2    // D2
  #define PIN_I2C_SCL       21   // D3
  #define OLED_WIDTH        128
  #define OLED_HEIGHT       64
  #define OLED_I2C_ADDR     0x3C
  #define OLED_ROTATION     2    // 0=none, 1=90°, 2=180°, 3=270°
  // Rotary encoder with push-button switch. Drives an on-device menu shown on
  // the OLED: turning scrolls the selection, pressing runs it.
  #define HAS_ENCODER
  #define PIN_ENC_A         17   // D7  (header pin 14) quadrature channel A
  #define PIN_ENC_B         22   // D4  (header pin 5)  quadrature channel B
  #define PIN_ENC_SW        23   // D5  (header pin 6)  push-button (active low)
  // Momentary panic button — releases every servo back to its home position
  // from anywhere (active low, internal pull-up).
  #define PIN_PANIC         16   // D6  (header pin 7)
#elif defined(BOARD_ESP32_DEVKIT)
  // ESP32 DevKit (power + volume-down only)
  #define PIN_SERVO_POWER   13
  #define PIN_SERVO_VOLDN   14
#else
  #error "No board selected — define BOARD_ESP32_DEVKIT or BOARD_XIAO_ESP32C6 (see platformio.ini)"
#endif

// ---------- Servo angles (degrees) ----------
// "Released" = arm is away from the button.
// "Pressed"  = arm pushes the button.
// Tune these per-build until the arm cleanly presses the button.
#define ANGLE_POWER_RELEASED  0
#define ANGLE_POWER_PRESSED   75

#define ANGLE_VOLDN_RELEASED  0
#define ANGLE_VOLDN_PRESSED   55

#ifdef PIN_SERVO_VOLUP
#define ANGLE_VOLUP_RELEASED  0
#define ANGLE_VOLUP_PRESSED   55
#endif

// ---------- Timing defaults (ms) ----------
#define DEFAULT_SHUTDOWN_HOLD_MS   45000   // 45 s power hold for force-off
#define DEFAULT_FASTBOOT_COMBO_MS   5000   // 5 s vol-down + power tap
#define POWER_TAP_MS                1000   // brief power press in combo
#define SAFETY_TIMEOUT_MS          60000   // hard cap on any single hold

// ---------- Serial ----------
#define SERIAL_BAUD  115200
