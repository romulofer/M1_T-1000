# Changelog

All notable changes to the M1 T-1000 firmware will be documented in this file.

## [0.1.4] - 2026-06-10

### Added
- **WiFi → Offensive Tools → Deauth All** — scans for nearby APs, then broadcast-
  deauthenticates every one of them with channel hopping; start/stop, no target entry
- **WiFi → Offensive Tools → Evil Twin** — open rogue AP with a DNS-hijack captive
  portal; editable SSID and channel, start/stop
- **Bluetooth → BLE Spam** — floods Apple / Google / Microsoft "device nearby"
  advertisements (vendor selectable: All / Apple / Google / Microsoft)
- **Animated main-menu logo** — the M1 owl idly bounces DVD-screensaver style
  inside its panel, pauses for a beat, then slides off the left edge and returns
  on a loop. Purely cosmetic: it animates only while the menu sits idle, and any
  keypress instantly restores the static menu

### Fixed
- **Bluetooth reliability** — Bad-BT, Bluetooth Advertise, and BT Info no longer
  freeze or fail when first opened. The ESP32-C6 bring-up now waits for the
  coprocessor to actually answer `AT` after a reset instead of a blind 200 ms delay,
  so commands no longer race the C6's boot (added a readiness handshake in
  `esp32_main_init()` and after `AT+RST`)
- **BT Info** firmware-version readout is more robust to the multi-line `AT+GMR`
  reply splitting across SPI transfers

### Notes
- The new WiFi/BLE attacks require matching ESP32-C6 AT firmware
  (`AT+M1DEAUTHALL`, `AT+M1EVILTWIN`, `AT+M1BLESPAM`). Reflash the C6 factory image
  if these report "unknown command".

## [0.1.3] - 2026-06-09

### Added
- **RGB Backlight mod support** (SK6805) with a full control menu:
  - Color modes, animations (Static, Breathe, Color Cycle, Strobe, Fade) and brightness
  - **Custom RGB color editor** — set an exact R/G/B color, saved to SD
  - **Reactive lighting** — drives the RGB mod from live system state: battery-level
    color (green → amber → red), a pulse while charging, and a flash on notifications
  - Settings persist and restore on boot from either entry point
- **IR Universal Power-Off (TV-B-Gone)** — blast every TV power code from
  `IR/TV/Universal_Power.ir` to switch off nearby televisions, with progress and abort
- **IR Power Off A/V** — same blaster extended to soundbars, receivers, and projectors
- **NFC NDEF Writer** — write a URL or Text record to an NTAG/Type-2 tag (NFC Tools menu)
- **NFC recovered-key report** — after a MIFARE Classic dictionary read, view the key
  (A/B) recovered for each sector on screen and save it to `NFC/<UID>_keys.txt`

### Fixed
- **Power Off / Reboot screen** device icon no longer overflows the frame or the caption
- **Main menu icons** resized so they sit inside their rows (no border bleed / clipping)
- **"M1" panel label** no longer sits on the box border
- **RGB Backlight menu** label and brightness-readout inconsistencies; "Color Cycle"
  now animates on solid colors

## [0.1.2]

### Fixed
- Infrared transmit fix
- Restored missing firmware functions

## [0.1.1] - 2026-04-17

### Added
- **WiFi Offensive Tools** menu with 6 functions:
  - Deauth Flood
  - PMKID Capture  
  - Handshake Capture
  - Beacon Spam
  - Karma Attack
  - Probe Sniff
- **Attack List** to save scan targets for reuse
- **Virtual keyboard** with MAC address formatting (`AABBCCDDEEFF` → `AA:BB:CC:DD:EE:FF`)
- **Maximum power NFC carrier** (40% modulation - ST25R3916 hardware maximum)
- **Long duration NFC tests** (up to 60 seconds)

### Fixed
- **WiFi Offensive Tools menu crash** when opening
- **Attack List deauth failure** 
- **Virtual keyboard cursor** not moving when typing
- **MAC address input** without colon key on keyboard
- **NFC false positives** in scan results

### Improved
- **WiFi reliability** with ESP32 readiness checks
- **SPI communication** with automatic retry logic
- **NFC signal strength** at maximum hardware capability
- **Menu stability** across all functions

## [0.1.0] - Initial Release

Base firmware with:
- Sub-GHz radio with 30+ protocol decoders
- NFC/RFID reader/writer
- Infrared remote control
- BadUSB/Bad-BT
- WiFi scanning and connection
- External app loader
- Games and utilities
- Dual boot system