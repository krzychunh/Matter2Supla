# Matter2Supla Beta

**Bramka Matter over Thread → SUPLA oparta na ESP32-C5**  
**Wydanie:** Stage 10F — Smart Dashboard + QR Preview  
**Repozytorium:** [krzychunh/Matter2Supla](https://github.com/krzychunh/Matter2Supla)

Matter2Supla pozwala połączyć zgodny czujnik **Matter over Thread** z systemem **SUPLA**. Bramka odbiera pomiary przez Thread i przekazuje temperaturę oraz wilgotność do SUPLA przez Wi-Fi. Pierwszym urządzeniem, na którym sprawdzono komunikację, jest **IKEA TIMMERFLOTTE**.

> **Status: Beta.** To niezależny, rozwijany projekt hobbystyczny, a nie oficjalny produkt IKEA ani SUPLA. Najlepiej przetestowaną konfiguracją jest ESP32-C5 + IKEA TIMMERFLOTTE. Obsługa innych czujników wymaga weryfikacji.

## Co potrafi bramka?

- Łączy się z domowym Wi-Fi i udostępnia lokalny panel WWW.
- Działa jako kontroler Matter i obsługuje komunikację Thread z czujnikiem.
- Paruje czujnik przy użyciu tekstowej zawartości kodu QR `MT:…` albo poprawnego PIN-u Matter i pełnego discriminator.
- Odbiera **automatyczne raporty temperatury i wilgotności** przez subskrypcję Matter; ma też konfigurowalny odczyt zapasowy.
- Przekazuje temperaturę i wilgotność do SUPLA Cloud.
- Pokazuje baterię czujnika (jeśli udało się odebrać pomiar).
- Udostępnia **Smart Dashboard** z ikonami Wi-Fi, Thread, Matter i SUPLA: zielone oznaczają stan aktywny, szare — nieaktywny.
- Pokazuje RSSI i jakość łącza Thread (0–3), gdy czujnik jest bezpośrednim dzieckiem bramki i można przypisać pomiar.
- Wyświetla prosty wykres temperatury zbierany podczas otwartej strony WWW.
- Oferuje skaner ogłoszeń **Matter BLE** oraz podgląd i kopiowanie tekstu QR w formularzu parowania.

**Uwaga:** historia wykresu istnieje tylko w pamięci otwartej przeglądarki. Kod `MT:…` nie jest przechowywany jako historia QR i nie można go odzyskać po wcześniejszym parowaniu.

## Jak to działa?

```text
IKEA TIMMERFLOTTE
     │ Matter over Thread (IEEE 802.15.4)
     ▼
ESP32-C5 — Matter2Supla
     │ Wi-Fi / Internet
     ▼
SUPLA Cloud
     │
     ▼
Aplikacja SUPLA / przeglądarka
```

## Wymagania

- Płytka **ESP32-C5** z wgranym Matter2Supla. Konfiguracja używana podczas prac: **32 MB flash, 8 MB PSRAM**.
- Stabilne zasilanie bramki.
- Domowa sieć Wi-Fi 2,4 GHz z dostępem do Internetu.
- Czujnik **IKEA TIMMERFLOTTE** z bateriami oraz jego kod QR Matter.
- Konto w [SUPLA Cloud](https://cloud.supla.org).
- Telefon albo komputer z przeglądarką WWW.

## Pierwsze uruchomienie — Wi-Fi

1. Włącz zasilanie bramki ESP32-C5.
2. Jeśli Wi-Fi nie jest jeszcze skonfigurowane, połącz telefon lub komputer z siecią **`Matter2Supla-XXXX`**.
3. Domyślne hasło tej sieci konfiguracyjnej to **`12345678`**.
4. Otwórz adres **http://192.168.4.1**.
5. W sekcji **„Sieć Wi-Fi”** kliknij **„Wyszukaj sieci”** albo ręcznie wpisz SSID.
6. Wpisz hasło domowej sieci i wybierz **„Sprawdź i zapisz Wi-Fi”**.
7. Po połączeniu bramka otrzyma adres IP z routera. Połącz się ponownie z domowym Wi-Fi i otwórz ten adres w przeglądarce.

> Adres `192.168.4.1` dotyczy trybu konfiguracyjnego, nie docelowego adresu w domowej sieci. Docelowe IP można sprawdzić w routerze. Nie wystawiaj panelu konfiguracyjnego bezpośrednio do Internetu.

## Dodawanie IKEA TIMMERFLOTTE

1. Włóż baterie do TIMMERFLOTTE i uruchom **tryb parowania Matter** zgodnie z instrukcją czujnika. W razie potrzeby ponownie otwórz okno parowania przyciskiem systemowym.
2. W panelu Matter2Supla sprawdź stan kontrolera Matter. Jeśli jest wyłączony, użyj **„Uruchom kontroler”** i poczekaj na gotowość Thread/Matter.
3. W sekcji **„Dodaj czujnik IKEA Matter”** wpisz nazwę, np. `TIMMERFLOTTE Salon`.
4. Odczytaj **tekstową zawartość** kodu QR Matter (zaczyna się od **`MT:`**) i wklej ją w pole **„Treść kodu QR (MT:…)”**.
5. Możesz użyć **„Pokaż treść QR”** do sprawdzenia wpisu lub **„Kopiuj MT:…”**. Kod traktuj jako poufne dane parowania.
6. Kliknij **„Dodaj czujnik”**. Nie restartuj bramki podczas parowania.
7. Po udanym parowaniu sprawdź listę czujników i poczekaj na pierwsze pomiary.

Alternatywnie formularz pozwala wpisać **PIN Matter i pełny discriminator (0–4095)**. Sam discriminator wykryty przez skaner BLE nie wystarcza do parowania. Najprościej użyć pełnego `MT:…`.

**Skaner Matter BLE** pokazuje urządzenia, które aktualnie ogłaszają możliwość parowania. Samo wykrycie w skanerze nie oznacza jeszcze dodania czujnika.

## Połączenie z SUPLA

1. Zaloguj się do [SUPLA Cloud](https://cloud.supla.org) i sprawdź adres serwera swojego konta (np. `svr51.supla.org`).
2. W SUPLA Cloud **włącz rejestrację nowych urządzeń** na czas pierwszego podłączania bramki.
3. W lokalnym panelu Matter2Supla otwórz sekcję **„SUPLA · pierwszy czujnik”**.
4. Zaznacz **„Włącz integrację SUPLA”**.
5. Uzupełnij pola:

   | Pole | Wartość |
   | --- | --- |
   | Serwer SUPLA | Host przypisany do konta, np. `svr51.supla.org` — bez `https://` i portu |
   | E-mail konta SUPLA | Adres konta SUPLA |
   | Numer czujnika Matter | Numer widoczny przy sparowanym TIMMERFLOTTE; formularz może wybrać go automatycznie |
   | Endpoint temperatury | **`1`** dla testowanego TIMMERFLOTTE |
   | Endpoint wilgotności | **`2`** dla testowanego TIMMERFLOTTE |

6. Kliknij **„Zapisz i uruchom ponownie”**.
7. Po restarcie sprawdź status SUPLA na pulpicie i pojawienie się pomiarów w aplikacji SUPLA.

**Do połączenia z SUPLA nie wpisuje się hasła Wi-Fi ponownie ani hasła do konta SUPLA.** Wystarczają serwer, e-mail i prawidłowe przypisanie czujnika. Przy pierwszej konfiguracji zweryfikuj numer Matter — po ponownym parowaniu nie musi to być `1`.

> Uwaga na trwałość kanałów: przypisanie pierwszego czujnika i kanałów SUPLA może zostać zablokowane po zapisie, aby zachować spójność historii pomiarów. Nie zmieniaj go przypadkowo.

## Smart Dashboard — Stage 10F

Na górze panelu znajdują się cztery ikony: **Wi-Fi**, **Thread**, **Matter**, **SUPLA**. Kolor **zielony** oznacza aktywny stan danej usługi, a **szary** — brak aktywności lub połączenia.

Karta czujnika pokazuje **temperaturę, wilgotność, baterię, ostatni raport Matter i subskrypcję**. Poniżej jest wskaźnik **siły sygnału Thread** z RSSI w dBm oraz jakością 0–3.

**Ograniczenie RSSI:** OpenThread udostępnia dane bezpośrednio podłączonych dzieci Thread. Jeżeli czujnik komunikuje się przez inny router lub nie można go jednoznacznie zidentyfikować, panel nie powinien przypisywać obcego RSSI do TIMMERFLOTTE. Czujnik bateryjny może spać, więc ostatni odczyt RSSI nie musi być bieżący.

Na dole dostępne są ustawienia, lista urządzeń, skaner BLE oraz diagnostyka. Podgląd `/api/status` służy do rozwiązywania problemów; przed udostępnieniem jego zawartości usuń prywatne informacje.

## Aktualizacja firmware — ważne

Istnieją dwa różne rodzaje plików BIN:

| Obraz | Adres zapisu | Zastosowanie |
| --- | --- | --- |
| `esp_ot_br.bin` — **sama aplikacja** | **`0x20000`** | Aktualizacja istniejącej, skonfigurowanej bramki |
| `FULL-0x0.bin` — **scalony obraz** | **`0x0`** | Czysta instalacja na nowym lub świadomie resetowanym urządzeniu |

**Nie zapisuj pliku samej aplikacji pod adres `0x0`.** Pełny obraz musi zawierać odpowiednio rozmieszczony bootloader (`0x2000`), tablicę partycji (`0x8000`) i aplikację (`0x20000`). Pełny obraz może nadpisać NVS, a więc usunąć Wi-Fi, konfigurację SUPLA, dane Thread i parowanie Matter.

Przykład **aktualizacji tylko aplikacji** w Windows PowerShell (dostosuj port i ścieżkę):

```powershell
py -m esptool --chip esp32c5 --port COM13 --baud 115200 --no-stub --before default-reset --after hard-reset write-flash 0x20000 "C:\\sciezka\\do\\esp_ot_br.bin"
```

**Nie używaj `erase-flash` ani pełnego `idf.py flash` do zwykłej aktualizacji sparowanej bramki.**

## Kompilacja ze źródeł

Projekt rozwijano w środowisku:

- **ESP-IDF 5.5.6**
- **ESP-Matter v15**
- Układ docelowy **ESP32-C5**
- Python 3.12 i narzędzia ESP-IDF

W repozytorium znajdują się katalogi `main/`, `components/m2s_supla/`, `vendor/supla-device/`, pliki CMake i domyślna konfiguracja. Komponenty zarządzane przez ESP-IDF są pobierane podczas przygotowania środowiska.

Przykładowa sekwencja w Ubuntu/WSL po skonfigurowaniu obu SDK:

```bash
git clone https://github.com/krzychunh/Matter2Supla.git
cd Matter2Supla
source /sciezka/do/esp-idf-v5.5.6/export.sh
export ESP_MATTER_PATH=/sciezka/do/esp-matter-v15
source "$ESP_MATTER_PATH/export.sh"
idf.py set-target esp32c5
idf.py build
```

**Istotne:** lokalny, testowany firmware budowano z określonym `sdkconfig`, który jest wyłączony z Git ze względu na ustawienia zależne od sprzętu. Samo `sdkconfig.defaults` może nie wystarczyć do odtworzenia identycznego obrazu. Przed flashowaniem zweryfikuj konfigurację 32 MB flash, 8 MB PSRAM i tablicę partycji: NVS `0x9000`, bootloader `0x2000`, partycje `0x8000`, aplikacja `0x20000` o rozmiarze `0x800000`.

## Rozwiązywanie problemów

| Problem | Co sprawdzić |
| --- | --- |
| Nie otwiera się panel | Czy urządzenie ma adres IP w routerze; przy pierwszym uruchomieniu użyj `192.168.4.1` po połączeniu z AP bramki |
| Czujnik nie jest wykrywany | Tryb parowania, baterie, odległość od bramki, skaner BLE Matter |
| Nie udaje się parowanie | Poprawność `MT:…`, gotowość Thread/Matter, czy czujnik nie jest już powiązany z innym kontrolerem |
| Brak baterii | Poczekaj na udany odczyt; przy braku odpowiedzi bramka ponawia próbę |
| Brak sygnału Thread | Czujnik może być za innym routerem Thread lub przypisanie RSSI może być niejednoznaczne |
| SUPLA offline | Poprawność hosta, e-maila, aktywna rejestracja urządzeń i dostęp do Internetu |
| Jest pomiar Matter, ale nie w SUPLA | Numer węzła i endpointy, stan połączenia SUPLA, rejestracja urządzenia |

## Bezpieczeństwo i prywatność

- **Nie publikuj** swojego kodu Matter `MT:…`, haseł Wi-Fi, tokenów ani zrzutów pamięci NVS.
- Domyślna sieć konfiguracyjna ma znane hasło `12345678`; uruchamiaj konfigurację w zaufanym otoczeniu.
- Nie udostępniaj lokalnego panelu WWW bezpośrednio przez przekierowanie portów z Internetu.
- Przed publicznym `git push` sprawdzaj zmiany i pliki konfiguracyjne, również te wyłączone z kompilacji.
- Projekt jest w fazie Beta; nie stosuj go jako jedynego źródła danych w systemach wymagających niezawodności lub bezpieczeństwa.

## Stan projektu i plany

**Stage 10F** został skompilowany dla ESP32-C5. Wcześniejsze testy potwierdziły automatyczne raportowanie temperatury i wilgotności z TIMMERFLOTTE oraz ich aktualizację w SUPLA. Nowe elementy pulpitu Stage 10F wymagają dalszego sprawdzenia na urządzeniu.

Planowane kierunki rozwoju to długoterminowa diagnostyka, historia pomiarów i szersza obsługa czujników Matter — z uwzględnieniem ograniczonej pamięci wewnętrznej ESP32-C5.

## Licencje i autorstwo

Projekt: **[krzychunh](https://github.com/krzychunh)**.  
W katalogu `vendor/supla-device/` znajduje się kod biblioteki SUPLA wraz z jej własną licencją. Licencję kodu własnego Matter2Supla należy określić oddzielnie w pliku `LICENSE`, zanim projekt zostanie ogłoszony jako oprogramowanie open source.

---

**Matter2Supla Beta — Matter i Thread w świecie SUPLA.**
