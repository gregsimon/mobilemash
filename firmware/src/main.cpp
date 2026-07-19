/*
 * MobileMash – ESP32 phone-button presser firmware
 *
 * Drives two SG90 servos (power button, volume-down button) via simple
 * ASCII commands over USB serial.  Primary use case: programmatic fastboot
 * entry for Pixel phones.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include "config.h"
#ifdef HAS_OLED
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#endif

// ── Servos ──────────────────────────────────────────────────────────────
Servo servoPower;
Servo servoVolDn;
#ifdef PIN_SERVO_VOLUP
Servo servoVolUp;
#endif
static Preferences preferences;
#ifdef HAS_OLED
static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// Probe the I2C bus and report every device that ACKs. Handy for confirming
// the display is wired correctly and which address it answers on (0x3C vs
// 0x3D are both common for SSD1306-class modules).
static void i2cScan() {
    Serial.println("I2C scan:");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  device at 0x%02X\n", addr);
            found++;
        }
    }
    if (found == 0) Serial.println("  (none — check wiring/power)");
}

// Show a single line of text (large font) on the OLED.
static void oledShow(const char *msg) {
    oled.clearDisplay();
    oled.setTextSize(2);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println(msg);
    oled.display();
}
#endif

static bool powerPressed = false;
static bool volDnPressed = false;
#ifdef PIN_SERVO_VOLUP
static bool volUpPressed = false;
#endif

// ── Tunable servo angles ────────────────────────────────────────────────
// Seeded from the config.h defaults but adjustable at runtime via the
// on-device "Tuning" menu (see below). "Released"/start = arm away from the
// button, "Pressed"/stop = arm pushing it.
static int anglePowerReleased = ANGLE_POWER_RELEASED;
static int anglePowerPressed  = ANGLE_POWER_PRESSED;
static int angleVoldnReleased = ANGLE_VOLDN_RELEASED;
static int angleVoldnPressed  = ANGLE_VOLDN_PRESSED;
#ifdef PIN_SERVO_VOLUP
static int angleVolupReleased = ANGLE_VOLUP_RELEASED;
static int angleVolupPressed  = ANGLE_VOLUP_PRESSED;
#endif

// ── Helpers ─────────────────────────────────────────────────────────────

static void pressPower() {
    servoPower.write(anglePowerPressed);
    powerPressed = true;
}
static void releasePower() {
    servoPower.write(anglePowerReleased);
    powerPressed = false;
}
static void pressVolDn() {
    servoVolDn.write(angleVoldnPressed);
    volDnPressed = true;
}
static void releaseVolDn() {
    servoVolDn.write(angleVoldnReleased);
    volDnPressed = false;
}
#ifdef PIN_SERVO_VOLUP
static void pressVolUp() {
    servoVolUp.write(angleVolupPressed);
    volUpPressed = true;
}
static void releaseVolUp() {
    servoVolUp.write(angleVolupReleased);
    volUpPressed = false;
}
#endif
static void releaseAll() {
    releasePower();
    releaseVolDn();
#ifdef PIN_SERVO_VOLUP
    releaseVolUp();
#endif
}

// Block while holding, but keep reading serial for an early-release command.
// Returns true if we were interrupted by RELEASE_ALL, false if the duration
// expired normally.
static bool holdWithInterrupt(unsigned long durationMs) {
    unsigned long start = millis();
    while (millis() - start < durationMs) {
        if (Serial.available()) {
            String line = Serial.readStringUntil('\n');
            line.trim();
            if (line == "RELEASE_ALL") {
                releaseAll();
                Serial.println("OK RELEASE_ALL (interrupted hold)");
                return true;
            }
        }
        delay(10);
    }
    return false;
}

// ── Command handlers ────────────────────────────────────────────────────

static void cmdPressButton(void (*press)(), void (*release)(),
                           const char *name, unsigned long ms) {
    if (ms == 0 || ms > SAFETY_TIMEOUT_MS) {
        Serial.printf("ERR duration out of range (1-%lu)\n", SAFETY_TIMEOUT_MS);
        return;
    }
    Serial.printf("OK pressing %s for %lu ms\n", name, ms);
    press();
    bool interrupted = holdWithInterrupt(ms);
    if (!interrupted) {
        release();
        Serial.printf("OK released %s\n", name);
    }
}

static void cmdFastboot(unsigned long shutdownMs, unsigned long comboMs) {
    if (shutdownMs > SAFETY_TIMEOUT_MS) shutdownMs = SAFETY_TIMEOUT_MS;
    if (comboMs > SAFETY_TIMEOUT_MS)    comboMs    = SAFETY_TIMEOUT_MS;

    // Phase 1: force-shutdown by holding power
    Serial.printf("OK FASTBOOT phase1: holding power %lu ms\n", shutdownMs);
    pressPower();
    bool interrupted = holdWithInterrupt(shutdownMs);
    if (interrupted) return;
    releasePower();
    Serial.println("OK FASTBOOT phase1 done (power released)");

    // Brief pause to let the phone fully shut down
    delay(1000);

    // Phase 2: hold volume-down, then tap power
    // Stagger servo movements and detach vol-down PWM before moving power
    // to avoid brownout from simultaneous current draw.  The vol-down servo
    // holds its position via gear friction while unpowered.
    Serial.printf("OK FASTBOOT phase2: vol-down + power tap (%lu ms window)\n",
                  comboMs);
    pressVolDn();
    delay(500);            // hold vol-down for 500ms before moving power
    servoVolDn.detach();   // stop PWM — servo holds via friction
    delay(100);
    pressPower();

    unsigned long tapStart = millis();

    // Keep power pressed for POWER_TAP_MS or until interrupted
    while (millis() - tapStart < POWER_TAP_MS) {
        if (Serial.available()) {
            String line = Serial.readStringUntil('\n');
            line.trim();
            if (line == "RELEASE_ALL") {
                servoVolDn.attach(PIN_SERVO_VOLDN, 500, 2400);
                releaseAll();
                Serial.println("OK RELEASE_ALL (fastboot interrupted)");
                return;
            }
        }
        delay(10);
    }
    releasePower();  // release power after tap
    Serial.println("OK FASTBOOT power released, holding vol-down 5000 ms");

    // Re-attach vol-down and keep holding it for 5000ms after power release
    servoVolDn.attach(PIN_SERVO_VOLDN, 500, 2400);
    pressVolDn();  // re-assert vol-down now that power servo is idle

    bool postHoldInterrupted = holdWithInterrupt(5000);

    if (!postHoldInterrupted) {
        releaseAll();
        Serial.println("OK FASTBOOT complete");
    }
}

static void cmdStatus() {
#ifdef PIN_SERVO_VOLUP
    Serial.printf("STATE {\"power\":%s,\"volup\":%s,\"voldn\":%s}\n",
                  powerPressed ? "true" : "false",
                  volUpPressed ? "true" : "false",
                  volDnPressed ? "true" : "false");
#else
    Serial.printf("STATE {\"power\":%s,\"voldn\":%s}\n",
                  powerPressed ? "true" : "false",
                  volDnPressed ? "true" : "false");
#endif
}

// ── Rotary encoder + on-device menu ─────────────────────────────────────
#ifdef HAS_ENCODER
// Half-step quadrature decoder (Ben Buxton state table). Each ISR reads both
// channels and advances a state machine; contact bounce is absorbed as no-ops.
// This EC11 rests a detent at every HALF electrical cycle, so the half-step
// table (which emits at both the 00 and 11 rest points) yields one step per
// detent — 1:1 with the menu. A full-step table would need two detents per step.
#define R_START       0x0
#define R_CCW_BEGIN   0x1
#define R_CW_BEGIN    0x2
#define R_START_M     0x3
#define R_CW_BEGIN_M  0x4
#define R_CCW_BEGIN_M 0x5
#define DIR_CW        0x10
#define DIR_CCW       0x20

static const uint8_t ENC_TABLE[6][4] = {
    {R_START_M,           R_CW_BEGIN,    R_CCW_BEGIN,   R_START},
    {R_START_M | DIR_CCW, R_START,       R_CCW_BEGIN,   R_START},
    {R_START_M | DIR_CW,  R_CW_BEGIN,    R_START,       R_START},
    {R_START_M,           R_CCW_BEGIN_M, R_CW_BEGIN_M,  R_START},
    {R_START_M,           R_START_M,     R_CW_BEGIN_M,  R_START | DIR_CW},
    {R_START_M,           R_CCW_BEGIN_M, R_START_M,     R_START | DIR_CCW},
};

static volatile uint8_t  encState    = R_START;
static volatile int8_t   encoderDelta = 0;   // detent steps, consumed in loop()
static volatile uint32_t encIsrCount = 0;    // A/B edges seen (diagnostic)

static void IRAM_ATTR encoderISR() {
    encIsrCount++;
    uint8_t pins = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
    encState = ENC_TABLE[encState & 0x0f][pins];
    uint8_t dir = encState & 0x30;
    if (dir == DIR_CW)       encoderDelta++;
    else if (dir == DIR_CCW) encoderDelta--;
}

// Report raw encoder state — helps localize a fault: rotate slowly and watch
// whether the edge count climbs and A/B toggle, and whether SW drops to 0 when
// the knob is pressed.
static void cmdEncDebug() {
    Serial.printf("ENC A=%d B=%d SW=%d edges=%lu delta=%d\n",
                  digitalRead(PIN_ENC_A), digitalRead(PIN_ENC_B),
                  digitalRead(PIN_ENC_SW), (unsigned long)encIsrCount,
                  encoderDelta);
}

// Read every free XIAO header pin (servos use D8/D9/D10, I2C uses D2/D3) with
// internal pull-ups engaged. Turn the knob and watch which two toggle — those
// are the true A/B GPIOs; press it and watch which drops — that is SW.
struct ScanPin { const char *label; uint8_t gpio; };
static const ScanPin SCAN_PINS[] = {
    {"D0",  0}, {"D1", 1}, {"D6", 16}, {"D7", 17}, {"D4", 22}, {"D5", 23},
};
static const int SCAN_COUNT = sizeof(SCAN_PINS) / sizeof(SCAN_PINS[0]);

static void cmdScan() {
    Serial.print("SCAN");
    for (int i = 0; i < SCAN_COUNT; i++) {
        pinMode(SCAN_PINS[i].gpio, INPUT_PULLUP);
        Serial.printf(" %s(g%u)=%d", SCAN_PINS[i].label, SCAN_PINS[i].gpio,
                      digitalRead(SCAN_PINS[i].gpio));
    }
    Serial.println();
}

// Menu actions. Taps reuse the servo helpers; a tap keeps serial responsive so
// RELEASE_ALL still interrupts it.
static void actPowerTap()  { pressPower();  holdWithInterrupt(POWER_TAP_MS); releasePower();  }
#ifdef PIN_SERVO_VOLUP
static void actVolUp()     { pressVolUp();  holdWithInterrupt(300);          releaseVolUp();  }
#endif
static void actVolDn()     { pressVolDn();  holdWithInterrupt(300);          releaseVolDn();  }
static void actFastboot()  { cmdFastboot(DEFAULT_SHUTDOWN_HOLD_MS, DEFAULT_FASTBOOT_COMBO_MS); }

// A main-menu entry runs an action; the special "Tuning" entry (run == nullptr)
// opens the tuning submenu instead of running a blocking action.
struct MenuItem { const char *name; void (*run)(); };
static const MenuItem MENU[] = {
    {"Power tap", actPowerTap},
#ifdef PIN_SERVO_VOLUP
    {"Vol Up",    actVolUp},
#endif
    {"Vol Down",  actVolDn},
    {"Fastboot",  actFastboot},
    {"Tuning",    nullptr},
};
static const int MENU_COUNT = sizeof(MENU) / sizeof(MENU[0]);

// Each servo's tunable start/stop angles, indexed by the tuning submenu.
struct ServoTune {
    const char *name;
    Servo      *servo;
    int        *start;   // released angle (arm away)
    int        *stop;    // pressed angle  (arm on the button)
};
static ServoTune SERVOS[] = {
    {"Power",    &servoPower, &anglePowerReleased, &anglePowerPressed},
#ifdef PIN_SERVO_VOLUP
    {"Vol Up",   &servoVolUp, &angleVolupReleased, &angleVolupPressed},
#endif
    {"Vol Down", &servoVolDn, &angleVoldnReleased, &angleVoldnPressed},
};
static const int SERVO_COUNT = sizeof(SERVOS) / sizeof(SERVOS[0]);

// Menu navigation state. The encoder drives a small stack of screens:
//   MAIN → SERVOS (pick a servo) → PARAMS (Start/Stop/Back) → EDIT (live angle)
enum MenuMode { MODE_MAIN, MODE_SERVOS, MODE_PARAMS, MODE_EDIT };
static MenuMode menuMode  = MODE_MAIN;
static int      menuIndex = 0;   // selection within the current screen
static int      tuneServo = 0;   // servo chosen in MODE_SERVOS
static int      tuneParam = 0;   // 0 = start, 1 = stop (chosen in MODE_PARAMS)
static int      editAngle = 0;   // working angle while in MODE_EDIT

// Number of selectable rows on the current screen (SERVOS/PARAMS include Back).
static int currentCount() {
    switch (menuMode) {
        case MODE_MAIN:   return MENU_COUNT;
        case MODE_SERVOS: return SERVO_COUNT + 1;   // servos + Back
        case MODE_PARAMS: return 3;                 // Start, Stop, Back
        default:          return 1;
    }
}

// Draw whichever screen is active, with a ">" cursor on the current selection.
static void drawMenu() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);

    if (menuMode == MODE_MAIN) {
        oled.println("MobileMash");
        oled.drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);
        for (int i = 0; i < MENU_COUNT; i++) {
            oled.setCursor(0, 14 + i * 10);
            oled.printf("%c %s", (i == menuIndex) ? '>' : ' ', MENU[i].name);
        }
    } else if (menuMode == MODE_SERVOS) {
        oled.println("Tuning");
        oled.drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);
        for (int i = 0; i < SERVO_COUNT; i++) {
            oled.setCursor(0, 14 + i * 10);
            oled.printf("%c %s", (i == menuIndex) ? '>' : ' ', SERVOS[i].name);
        }
        oled.setCursor(0, 14 + SERVO_COUNT * 10);
        oled.printf("%c Back", (menuIndex == SERVO_COUNT) ? '>' : ' ');
    } else if (menuMode == MODE_PARAMS) {
        oled.println(SERVOS[tuneServo].name);
        oled.drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);
        oled.setCursor(0, 14);
        oled.printf("%c Start  %3d", (menuIndex == 0) ? '>' : ' ',
                    *SERVOS[tuneServo].start);
        oled.setCursor(0, 24);
        oled.printf("%c Stop   %3d", (menuIndex == 1) ? '>' : ' ',
                    *SERVOS[tuneServo].stop);
        oled.setCursor(0, 34);
        oled.printf("%c Back", (menuIndex == 2) ? '>' : ' ');
    } else {  // MODE_EDIT
        oled.printf("%s %s\n", SERVOS[tuneServo].name,
                    (tuneParam == 0) ? "Start" : "Stop");
        oled.drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);
        oled.setTextSize(3);
        oled.setCursor(24, 22);
        oled.printf("%d", editAngle);
        oled.setTextSize(1);
        oled.setCursor(0, 54);
        oled.println("turn:set  press:save");
    }
    oled.display();
}

// Run a main-menu action, showing a "Running" screen while it blocks, then
// return to the menu.
static void runMenuItem(int idx) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println("Running:");
    oled.setCursor(0, 20);
    oled.setTextSize(2);
    oled.println(MENU[idx].name);
    oled.display();
    Serial.printf("OK encoder run: %s\n", MENU[idx].name);
    MENU[idx].run();
    drawMenu();
}

// Handle a button press: activate the current selection for whichever screen
// is active, transitioning between screens as needed.
static void menuSelect() {
    switch (menuMode) {
    case MODE_MAIN:
        if (MENU[menuIndex].run == nullptr) {   // "Tuning" → open submenu
            menuMode  = MODE_SERVOS;
            menuIndex = 0;
            drawMenu();
        } else {
            runMenuItem(menuIndex);
        }
        break;

    case MODE_SERVOS:
        if (menuIndex == SERVO_COUNT) {         // Back → main menu
            menuMode  = MODE_MAIN;
            menuIndex = MENU_COUNT - 1;          // land back on "Tuning"
        } else {
            tuneServo = menuIndex;
            menuMode  = MODE_PARAMS;
            menuIndex = 0;
        }
        drawMenu();
        break;

    case MODE_PARAMS:
        if (menuIndex == 2) {                    // Back → servo list
            menuMode  = MODE_SERVOS;
            menuIndex = tuneServo;
        } else {                                 // edit Start or Stop live
            tuneParam = menuIndex;
            editAngle = (tuneParam == 0) ? *SERVOS[tuneServo].start
                                         : *SERVOS[tuneServo].stop;
            SERVOS[tuneServo].servo->write(editAngle);   // preview current
            menuMode  = MODE_EDIT;
        }
        drawMenu();
        break;

    case MODE_EDIT:                              // save and step back out
        if (tuneParam == 0) *SERVOS[tuneServo].start = editAngle;
        else                *SERVOS[tuneServo].stop  = editAngle;
        Serial.printf("OK tune %s %s=%d\n", SERVOS[tuneServo].name,
                      (tuneParam == 0) ? "start" : "stop", editAngle);

        // Save updated angle to non-volatile storage
        preferences.begin("servo_angles", false);
        if (tuneServo == 0) { // Power
            preferences.putInt(tuneParam == 0 ? "p_rel" : "p_prs", editAngle);
        }
#ifdef PIN_SERVO_VOLUP
        else if (tuneServo == 1) { // Vol Up
            preferences.putInt(tuneParam == 0 ? "vu_rel" : "vu_prs", editAngle);
        }
        else if (tuneServo == 2) { // Vol Down
            preferences.putInt(tuneParam == 0 ? "vd_rel" : "vd_prs", editAngle);
        }
#else
        else if (tuneServo == 1) { // Vol Down
            preferences.putInt(tuneParam == 0 ? "vd_rel" : "vd_prs", editAngle);
        }
#endif
        preferences.end();

        // Park the arm back at its start angle so it isn't left pressing.
        SERVOS[tuneServo].servo->write(*SERVOS[tuneServo].start);
        menuMode  = MODE_PARAMS;
        menuIndex = tuneParam;
        drawMenu();
        break;
    }
}

// Poll consumed encoder rotation and the debounced button; refresh the display
// on either event. Called every loop() iteration.
static void handleEncoder() {
    noInterrupts();
    int8_t delta = encoderDelta;
    encoderDelta = 0;
    interrupts();
    if (delta != 0) {
        if (menuMode == MODE_EDIT) {
            editAngle += delta;
            if (editAngle < 0)   editAngle = 0;
            if (editAngle > 180) editAngle = 180;
            SERVOS[tuneServo].servo->write(editAngle);   // live preview
        } else {
            int count = currentCount();
            menuIndex += delta;
            if (menuIndex < 0)       menuIndex = 0;
            if (menuIndex >= count)  menuIndex = count - 1;
        }
        drawMenu();
    }

    // Button: active-low, INPUT_PULLUP. Fire on the HIGH→LOW edge, debounced.
    static bool lastLevel = true;
    static unsigned long lastChange = 0;
    bool level = digitalRead(PIN_ENC_SW);
    if (level != lastLevel && millis() - lastChange > 30) {
        lastChange = millis();
        lastLevel  = level;
        if (level == LOW) menuSelect();
    }
}

#endif  // HAS_ENCODER

#ifdef PIN_PANIC
// Momentary panic button: release every servo to its home position from any
// state. Active-low with an internal pull-up, debounced on the HIGH→LOW edge.
static void handlePanicButton() {
    static bool lastLevel = true;
    static unsigned long lastChange = 0;
    bool level = digitalRead(PIN_PANIC);
    if (level != lastLevel && millis() - lastChange > 30) {
        lastChange = millis();
        lastLevel  = level;
        if (level == LOW) {
            releaseAll();
            Serial.println("OK PANIC (all servos home)");
#ifdef HAS_OLED
            // Flash "PANIC" large, then drop back to the home screen.
            oled.clearDisplay();
            oled.setTextSize(3);
            oled.setTextColor(SSD1306_WHITE);
            oled.setCursor(19, 20);   // roughly centered for 5 chars @ size 3
            oled.println("PANIC");
            oled.display();
            delay(1000);
  #ifdef HAS_ENCODER
            menuMode  = MODE_MAIN;
            menuIndex = 0;
            drawMenu();
  #endif
#endif
        }
    }
}
#endif  // PIN_PANIC

// ── Parse & dispatch ────────────────────────────────────────────────────

static unsigned long parseULong(const String &s, unsigned long defaultVal) {
    if (s.length() == 0) return defaultVal;
    return strtoul(s.c_str(), nullptr, 10);
}

static void processLine(String &line) {
    line.trim();
    if (line.length() == 0) return;

    if (line == "PING") {
        Serial.println("PONG");
        return;
    }
    if (line == "STATUS") {
        cmdStatus();
        return;
    }
#ifdef HAS_ENCODER
    if (line == "ENC") {
        cmdEncDebug();
        return;
    }
    if (line == "SCAN") {
        cmdScan();
        return;
    }
#endif
    if (line == "RELEASE_ALL") {
        releaseAll();
        Serial.println("OK RELEASE_ALL");
        return;
    }

    // Commands with arguments: split on first space
    int sp = line.indexOf(' ');
    String cmd = (sp > 0) ? line.substring(0, sp) : line;
    String arg = (sp > 0) ? line.substring(sp + 1) : "";
    arg.trim();

    if (cmd == "PRESS_POWER") {
        unsigned long ms = parseULong(arg, 0);
        if (ms == 0) { Serial.println("ERR usage: PRESS_POWER <ms>"); return; }
        cmdPressButton(pressPower, releasePower, "power", ms);
    } else if (cmd == "PRESS_VOLDN") {
        unsigned long ms = parseULong(arg, 0);
        if (ms == 0) { Serial.println("ERR usage: PRESS_VOLDN <ms>"); return; }
        cmdPressButton(pressVolDn, releaseVolDn, "voldn", ms);
#ifdef PIN_SERVO_VOLUP
    } else if (cmd == "PRESS_VOLUP") {
        unsigned long ms = parseULong(arg, 0);
        if (ms == 0) { Serial.println("ERR usage: PRESS_VOLUP <ms>"); return; }
        cmdPressButton(pressVolUp, releaseVolUp, "volup", ms);
#endif
    } else if (cmd == "FASTBOOT") {
        // FASTBOOT [shutdown_ms] [combo_ms]
        unsigned long shutMs  = DEFAULT_SHUTDOWN_HOLD_MS;
        unsigned long comboMs = DEFAULT_FASTBOOT_COMBO_MS;
        if (arg.length() > 0) {
            int sp2 = arg.indexOf(' ');
            if (sp2 > 0) {
                shutMs  = parseULong(arg.substring(0, sp2), shutMs);
                comboMs = parseULong(arg.substring(sp2 + 1), comboMs);
            } else {
                shutMs = parseULong(arg, shutMs);
            }
        }
        cmdFastboot(shutMs, comboMs);
    } else if (cmd == "ANGLE_POWER") {
        int a = arg.toInt();
        if (a < 0 || a > 180) { Serial.println("ERR angle 0-180"); return; }
        servoPower.write(a);
        Serial.printf("OK power angle %d\n", a);
    } else if (cmd == "ANGLE_VOLDN") {
        int a = arg.toInt();
        if (a < 0 || a > 180) { Serial.println("ERR angle 0-180"); return; }
        servoVolDn.write(a);
        Serial.printf("OK voldn angle %d\n", a);
#ifdef PIN_SERVO_VOLUP
    } else if (cmd == "ANGLE_VOLUP") {
        int a = arg.toInt();
        if (a < 0 || a > 180) { Serial.println("ERR angle 0-180"); return; }
        servoVolUp.write(a);
        Serial.printf("OK volup angle %d\n", a);
#endif
    } else {
        Serial.printf("ERR unknown command: %s\n", cmd.c_str());
    }
}

// ── Arduino entry points ────────────────────────────────────────────────

void setup() {
    Serial.begin(SERIAL_BAUD);
    // Wait for the USB host to enumerate the CDC port, but only briefly. On the
    // XIAO's native USB, Serial never becomes ready when powered from a plain
    // USB-C supply (no host to enumerate), so an unbounded wait would hang setup
    // and the board would appear dead. Time out and continue after 2s.
    unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < 2000) { delay(10); }

    // Load saved servo angles from preferences (NVS)
    preferences.begin("servo_angles", false);
    anglePowerReleased = preferences.getInt("p_rel", ANGLE_POWER_RELEASED);
    anglePowerPressed  = preferences.getInt("p_prs", ANGLE_POWER_PRESSED);
    angleVoldnReleased = preferences.getInt("vd_rel", ANGLE_VOLDN_RELEASED);
    angleVoldnPressed  = preferences.getInt("vd_prs", ANGLE_VOLDN_PRESSED);
#ifdef PIN_SERVO_VOLUP
    angleVolupReleased = preferences.getInt("vu_rel", ANGLE_VOLUP_RELEASED);
    angleVolupPressed  = preferences.getInt("vu_prs", ANGLE_VOLUP_PRESSED);
#endif
    preferences.end();

#ifdef HAS_OLED
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    i2cScan();
    // periphBegin=false: we already called Wire.begin() above with the custom
    // SDA/SCL pins; letting the library re-run Wire.begin() would revert I2C to
    // the board's default pins and the display would never respond.
    if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR, true, false)) {
        oled.setRotation(OLED_ROTATION);
        oledShow("Fuchsia");
        Serial.printf("OK OLED up at 0x%02X\n", OLED_I2C_ADDR);
    } else {
        Serial.printf("WARN OLED init failed at 0x%02X (see I2C scan above)\n",
                      OLED_I2C_ADDR);
    }
#endif

    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
#ifdef PIN_SERVO_VOLUP
    ESP32PWM::allocateTimer(2);
#endif

    servoPower.setPeriodHertz(50);
    servoVolDn.setPeriodHertz(50);

    servoPower.attach(PIN_SERVO_POWER, 500, 2400);
    servoVolDn.attach(PIN_SERVO_VOLDN, 500, 2400);
#ifdef PIN_SERVO_VOLUP
    servoVolUp.setPeriodHertz(50);
    servoVolUp.attach(PIN_SERVO_VOLUP, 500, 2400);
#endif

    releaseAll();

#ifdef PIN_PANIC
    pinMode(PIN_PANIC, INPUT_PULLUP);
#endif

#ifdef HAS_ENCODER
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderISR, CHANGE);
  #ifdef HAS_OLED
    drawMenu();   // replace the boot splash with the resting menu
  #endif
#endif

    Serial.println("OK MobileMash ready");
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        processLine(line);
    }
#ifdef HAS_ENCODER
    handleEncoder();
#endif
#ifdef PIN_PANIC
    handlePanicButton();
#endif
}
