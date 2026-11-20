# Dodawanie czujnika Matter do SUPLA — Matter2Supla S7.16

## Przed rozpoczęciem

1. Uruchom bramkę ESP32-C5 i otwórz jej panel WWW pod aktualnym adresem IP.
2. Sprawdź Wi-Fi, aktywną rolę Thread oraz gotowość kontrolera Matter.
3. Włącz tryb parowania czujnika zgodnie z instrukcją producenta. Potrzebny jest kod Matter z etykiety urządzenia. Jeśli urządzenie jest już sparowane z innym ekosystemem, otwórz dodatkowe okno parowania lub postępuj zgodnie z instrukcją producenta.

## Metoda A — Skaner Matter BLE + kod 11-cyfrowy

1. Na pulpicie znajdź widżet **Skaner Matter BLE** obok **Parowanie Matter**. Możesz również otworzyć zakładkę **Parowanie**.
2. Uruchom skanowanie BLE. Z listy urządzeń wybierz właściwy `discriminator` przyciskiem **Użyj discriminator**.
3. W **Parowanie Matter** wpisz nazwę czujnika.
4. Wprowadź 11-cyfrowy kod z etykiety. W S7.16 pole pokazuje cyfry w trakcie wpisywania — zadbaj o prywatność.
5. Kliknij **Dodaj czujnik**. Po sukcesie sprawdź nową pozycję w **Czujniki**.

## Metoda B — kod QR Matter

1. Wprowadź czujnik w tryb parowania.
2. Odczytaj pełną tekstową treść QR Matter (`MT:...`) w aplikacji lub czytniku QR pokazującym surową zawartość. Panel w obecnej wersji nie uruchamia aparatu telefonu.
3. W **Parowanie Matter** wpisz nazwę, wklej ciąg `MT:...` w pole QR i kliknij **Dodaj czujnik**. Nie wpisuj 11-cyfrowego kodu w to pole.
4. Sprawdź czujnik w **Czujniki**, poczekaj na nawiązanie subskrypcji i pojawienie się pomiarów.

## Przypisanie do SUPLA

1. Otwórz zakładkę **SUPLA**. Jeśli jeszcze nie skonfigurowano połączenia, w SUPLA CLOUD włącz rejestrację nowych urządzeń, uzupełnij serwer i adres e-mail konta SUPLA, następnie zapisz ustawienia.
2. W sekcji **Matter → kanały SUPLA** wybierz sparowany czujnik oraz typ kanału obsługiwany przez firmware. Dla czujnika temperatury/wilgotności dobierz właściwe endpointy na podstawie jego odczytów; nie zakładaj, że każdy producent używa identycznych endpointów.
3. Wybierz **Dodaj stały kanał i uruchom ponownie**. Nie zmieniaj istniejącego przypisania kanału 0 do innego fizycznego czujnika.
4. Po restarcie sprawdź status połączenia w panelu i nowy kanał w SUPLA CLOUD.
5. Jeśli kanał nie pokazuje danych, najpierw sprawdź w panelu stan `subscription.reporting` i liczniki raportów czujnika.

> Dane QR i kody parowania traktuj jako poufne. Nie publikuj ich w zgłoszeniach GitHub ani zrzutach ekranu.

> S7.16 jest wersją beta. Obsługa danego modelu czujnika wymaga sprawdzenia jego klastrów, endpointów i zgodności uwierzytelnienia urządzenia.
