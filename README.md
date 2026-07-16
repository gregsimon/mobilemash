# MobileMash - ESP32 Phone Button Presser

A 3D-printable, ESP32-controlled servo harness for programmatically pressing
phone buttons. Primary use case: booting phones into fastboot mode unattended.

## Supported Phones

| Phone | Status |
|-------|--------|
| Pixel 10 Pro | v1 |

## How It Works

Two SG90 micro servos are mounted in a 3D-printed cradle that clips around the
phone. Each servo has a small arm that presses a button (power, volume-down).
An ESP32 dev board drives the servos and accepts commands over USB serial.

### Fastboot Sequence

1. **Force shutdown** - Press and hold power for up to 30 seconds (or until
   told to release).
2. **Enter fastboot** - Hold volume-down, then briefly press power. Release
   when told or after a safety timeout.

## Hardware BOM

| Part | Qty | Notes |
|------|-----|-------|
| ESP32 DevKit v1 (or similar) | 1 | Any ESP32 board with USB |
| SG90 micro servo | 2 | 9g, 180-degree |
| M2x8 self-tapping screws | 4 | Mount servos to cradle |
| Rubber/silicone bumper pads | 2 | Glue to servo arms for grip |
| USB-A to micro-USB cable | 1 | Power + data to ESP32 |
| 470uF electrolytic capacitor | 1 | Optional, across servo 5V/GND |

## Wiring

Servo signal pins depend on the board (see `firmware/include/config.h`):

| Signal              | ESP32 DevKit | XIAO ESP32-C6 |
|---------------------|--------------|---------------|
| Power servo (orange)       | GPIO 13 | GPIO 18 (D10) |
| Volume-up servo (orange)   | —       | GPIO 20 (D9)  |
| Volume-down servo (orange) | GPIO 14 | GPIO 19 (D8)  |
| OLED SDA                   | —       | GPIO 2 (D2)   |
| OLED SCL                   | —       | GPIO 21 (D3)  |

The volume-up servo exists only on the XIAO ESP32-C6 build (commands
`PRESS_VOLUP` / `ANGLE_VOLUP`); the DevKit build has power + volume-down only.

The XIAO ESP32-C6 build also drives a 128x64 SSD1306 OLED over I2C
(address `0x3C`), which shows `Fuchsia` on boot. The DevKit build has no
display.

```
<board> 5V/5V-out --> Both servo VCC (red)
<board> GND       --> Both servo GND (brown)
```

## Firmware

Built with PlatformIO (Arduino framework). Two build environments are
defined in `firmware/platformio.ini`:

- `esp32` — ESP32 DevKit (default)
- `seeed` — Seeed XIAO ESP32-C6

```bash
cd firmware

# ESP32 DevKit (default env)
pio run -e esp32 -t upload

# Seeed XIAO ESP32-C6
pio run -e seeed -t upload

# Or via the helper script:
./flash.sh                     # DevKit
./flash.sh seeed  # XIAO ESP32-C6
```

### Serial Protocol (115200 baud)

Commands are newline-terminated ASCII:

| Command | Description |
|---------|-------------|
| `PRESS_POWER <ms>` | Press power button for `<ms>` milliseconds |
| `PRESS_VOLDN <ms>` | Press volume-down for `<ms>` milliseconds |
| `FASTBOOT [shutdown_ms] [combo_ms]` | Run full fastboot entry sequence |
| `RELEASE_ALL` | Immediately release all buttons |
| `STATUS` | Report current state |
| `PING` | Returns `PONG` |

Responses are newline-terminated:

| Response | Meaning |
|----------|---------|
| `OK <detail>` | Command accepted / completed |
| `ERR <detail>` | Error |
| `STATE <json>` | Status response |
| `PONG` | Ping reply |

## 3D Printing

OpenSCAD source in `cad/`. Export STL and print with:
- Material: PLA or PETG
- Layer height: 0.2mm
- Infill: 20%+
- Supports: not required

## Host Tool

```bash
pip install pyserial
python host/mobilemash_cli.py --port /dev/ttyUSB0 fastboot
```

## License

Apache 2.0
