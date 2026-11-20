## [Stage11E S7.16 FIX1] - 2026-11-20

### Added
- Matter BLE scanner widget on Home Dashboard alongside Matter pairing.
- Illustrated in-device help for 11-digit BLE and QR pairing, followed by SUPLA registration.

### Changed
- Matter manual setup code field displays 11 digits during typing.
- Existing scanner reused across tabs; no second scanner/background task.
- Compressed HTML/SVG remains embedded in Flash.

### Carried over
- Stage11E S7.14 FIX1 Wi-Fi missing-SSID recovery and setup AP hold.
- S7.12 SUPLA sensor naming and S7.13 dashboard styling.

### Validation
- ESP32-C5 / ESP-IDF 5.5.6 build success.
- App size 3,491,440 bytes; SHA256 `952D96FF61FCBD130D717F24A1A4F90F827A5262F575915750B325AA0A076CBE`.
- Static HP SRAM delta 121 bytes per build report.
- On-device S7.16 regression testing pending.
