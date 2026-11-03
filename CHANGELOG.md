# Matter2Supla - Changelog

## v0.9.3-stage11c - 2026-11-03

### Stage 11C - Compact UI

- Zachowano cztery kafelki statusu:
  Wi-Fi, Thread, Matter i SUPLA.
- Usunieto gorna karte pojedynczego czujnika.
- Usunieto wykres temperatury.
- Przeniesiono sekcje urzadzen SUPLA
  bezposrednio pod skaner Matter BLE.
- Zachowano pozostale elementy panelu WWW.

### SUPLA - zmiany od Stage 10F

- Rozszerzono integracje Matter z SUPLA.
- Dodano obsluge wyboru urzadzenia Matter
  dla kanalow temperatury i wilgotnosci.
- Zachowano konfiguracje SUPLA w NVS.

### Zweryfikowane na urzadzeniu

- ESP32-C5 revision v1.2.
- Firmware: 0.9.3-stage11c-compact-ui.
- Thread role: leader.
- Matter ready: true.
- SUPLA online: true.
- Zachowane dwa wpisy urzadzen Matter:
  NodeId 2 i NodeId 3.

### Firmware

Plik: Matter2Supla_0.9.3_Stage11C_ESP32C5.bin

SHA256:
7a01394496a1a4ac9221583a6d8a47c381aa54d093d59f92bce5e75eccf3f5aa

### Flashowanie bez kasowania NVS

Firmware nalezy wgrac pod adres 0x20000.

Nie wykonywac erase-flash.

### Ograniczenia weryfikacji

- Nie potwierdzono jeszcze odczytow pomiarowych
  obu czujnikow po aktualizacji.
- Nie potwierdzono pelnego dzialania wszystkich
  mapowan kanalow SUPLA.
