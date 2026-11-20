# Matter2Supla Beta — Stage11E S7.16 FIX1

**ESP32-C5 · Matter over Thread → SUPLA**

## Nowości

- **Skaner Matter BLE na pulpicie** obok sekcji **Parowanie Matter**. Oba widżety używają istniejącego skanera i mechanizmu parowania; nie uruchamiają drugiego skanera BLE.
- **Widoczny 11-cyfrowy kod parowania** podczas wpisywania (pole tekstowe zamiast hasła). Kod jest czyszczony po rozpoczęciu parowania; nie należy wpisywać go przy osobach postronnych.
- **Zakładka Pomoc**: instrukcja BLE + 11 cyfr i instrukcja QR `MT:...`, następnie konfiguracja czujnika w SUPLA. Ilustracje SVG i HTML są dostarczane jako zasób gzip we Flash.
- **Odświeżony panel S7.13** — biały układ, zielone akcenty SUPLA, responsywne sekcje i statusy.

## Poprawki z S7.14 zachowane w kompilacji

- Odzyskiwanie Wi-Fi, gdy zapamiętana sieć nie jest dostępna. Po trzech błędach `NO_AP_FOUND` bramka pozostawia SoftAP konfiguracyjny, umożliwiając wybór nowego SSID.
- Po ponownym połączeniu Wi-Fi przywracane są Thread i subskrypcje czujników Matter. W jednym scenariuszu testowym wszystkie pięć czujników powróciło do `reporting=True`.
- Trwała rejestracja czujników i kanałów SUPLA pozostaje zachowana przy aktualizacji samej aplikacji, bez kasowania NVS.

## Zweryfikowano

- Kompilacja ESP-IDF 5.5.6 dla ESP32-C5, 32 MB Flash / 8 MB PSRAM: sukces.
- Obraz aplikacji: **3,491,440 B**, SHA256 `952D96FF61FCBD130D717F24A1A4F90F827A5262F575915750B325AA0A076CBE`.
- Rozmiar statycznej HP SRAM: przyrost **121 B** względem bazy raportu porównawczego.
- Przywrócenie oryginalnych źródeł i konfiguracji po kompilacji.

## Ograniczenia i ostrzeżenia

- To wydanie **beta**. Po wgraniu S7.16 na urządzenie wymagane jest jeszcze potwierdzenie działania interfejsu i regresji Matter/Thread/SUPLA.
- Surowy tekst QR trzeba skopiować do formularza; nie ma jeszcze skanowania aparatem bezpośrednio z panelu.
- Nie każdy czujnik Matter musi być kompatybilny; zależy to od implementowanych klastrów i certyfikatów.
- **Nie używać `erase-flash`** na urządzeniu z danymi Matter. Wgrywać tylko zgodny obraz aplikacji pod `0x20000` po sprawdzeniu mapy partycji.
- Plik BIN nie jest częścią tego pakietu źródłowego. Do GitHub Releases należy dodać lokalnie skompilowany, sprawdzony obraz o powyższej sumie SHA256.

**Proponowany tag:** `v0.9.3-s7.16-beta` (prerelease). Nie należy oznaczać jako stabilne bez testu sprzętowego.
