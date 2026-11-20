// Local configuration and Matter commissioning panel.
#include "m2s_web.h"
#include "m2s_network.h"
#include "m2s_devices.h"
#include "m2s_matter_api.h"
#include "m2s_readings.h"
#include "m2s_ikea_catalog.h"
#include "m2s_supla.h"
#include "m2s_ble_scan.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
extern "C" bool m2s_readings_is_paused(void);
extern "C" void m2s_readings_set_paused(bool paused);
extern "C" esp_err_t m2s_readings_run_all(void);
static const char *TAG="M2S_WEB";
static httpd_handle_t server=nullptr;
static char token[33];
static const char html[]=R"M2SHTML(
<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Matter2Supla · konfiguracja</title>
<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#0c1422;color:#edf4ff}*{box-sizing:border-box}body{margin:0}main{max-width:1050px;margin:auto;padding:30px 20px 60px}h1{font-size:36px;letter-spacing:-.04em;margin:8px 0}h2{font-size:22px;margin:0 0 18px}p{line-height:1.6;color:#acbdd3}.eyebrow{font-size:12px;letter-spacing:.14em;color:#76e1c5;font-weight:700}.top{display:flex;align-items:center;justify-content:space-between;gap:16px}.badge{background:#173e36;border-radius:30px;padding:9px 15px;color:#93e9ca}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:18px;margin:24px 0}.card{background:#152237;border:1px solid #30445e;border-radius:18px;padding:24px}.wide{grid-column:1/-1}label{display:block;margin:15px 0 7px;color:#c1cfe1;font-size:14px}input,textarea,select{font:inherit;width:100%;padding:12px;border-radius:9px;border:1px solid #4d617b;background:#0b1729;color:#fff}button{font:inherit;font-weight:650;padding:11px 16px;border:0;border-radius:9px;background:#77e1c7;color:#09251f;cursor:pointer;margin:12px 8px 0 0}button.secondary{background:#2b405c;color:#e6f0ff}button.danger{background:#733b42;color:#ffe2e2}button:disabled{opacity:.45;cursor:wait}.hint{font-size:13px;color:#adbed2}.notice{padding:12px 14px;border-radius:10px;background:#0d1b2f;border:1px solid #344b67;white-space:pre-wrap}.error{color:#ffb7b7}.ok{color:#8ce8bf}.row{display:flex;gap:12px;flex-wrap:wrap}.row>*{flex:1;min-width:160px}table{border-collapse:collapse;width:100%}td,th{text-align:left;border-bottom:1px solid #31435b;padding:12px 8px;font-size:14px}details{margin-top:20px}summary{cursor:pointer;color:#bdcfe5}code{color:#94e8d0}a{color:#91dce8}footer{font-size:12px;color:#8aa0bc}#connection{margin-top:14px} .facts{display:flex;gap:15px;flex-wrap:wrap;font-size:14px;color:#b8c9df}.facts span{background:#152237;padding:9px 12px;border-radius:8px}

/* Stage10C: diagonal watermark */
body{min-height:100vh;isolation:isolate}
body::before{
content:"";position:fixed;inset:-65vmax;
z-index:0;pointer-events:none;
transform:rotate(-28deg);opacity:.17;
background-image:url("data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20width%3D%22450%22%20height%3D%22150%22%3E%3Cg%20font-family%3D%22Arial%2Csans-serif%22%20font-size%3D%2219%22%20font-weight%3D%22700%22%20letter-spacing%3D%222%22%20fill%3D%22%239beedb%22%3E%3Ctext%20x%3D%2216%22%20y%3D%2242%22%3EMatter2Supla%20Beta%3C%2Ftext%3E%3Ctext%20x%3D%22145%22%20y%3D%22118%22%3ETIMMERFLOTTE%20-%20OK%3C%2Ftext%3E%3C%2Fg%3E%3C%2Fsvg%3E");
background-repeat:repeat;background-size:450px 150px
}
main{position:relative;z-index:1}
@media(max-width:600px){
body::before{opacity:.12;background-size:390px 130px}
}

/* Stage 11B: clickable, keyboard-accessible SUPLA channel selectors. */
.supla-assigned{display:block;width:100%;margin:0;padding:12px;text-align:left;color:inherit;font:inherit;font-weight:400;border:1px solid transparent;border-bottom:1px solid #30445e;border-radius:8px;background:transparent;cursor:pointer}
.supla-assigned:hover{background:#183249}
.supla-assigned.selected{border-color:#77e1c7;background:#153d3a}
.supla-assigned:focus-visible{outline:3px solid #77e1c7;outline-offset:2px}
/* Stage 11A: assigned devices. */
.supla-assigned{padding:12px;border-bottom:1px solid #30445e}
.supla-assigned:last-of-type{border-bottom:0}
.supla-assigned strong{color:#85ebbb}
.supla-assigned p{margin:5px 0;line-height:1.45}
/* Stage 10F: no network resources or external fonts; minimal flash-only UI. */
.badge.is-muted{color:#9aa9bc;background:#293544}.sd-wrap{margin:24px 0 4px}.sd-wrap h2{margin:0}.sd-topline{display:flex;gap:10px;justify-content:space-between;align-items:center;flex-wrap:wrap;margin-bottom:15px}
.sd-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:11px}
.sd-state{display:flex;align-items:center;gap:9px;padding:14px 12px;border:1px solid #38485a;background:#192333;border-radius:13px;color:#8796a9;min-width:0}
.sd-state svg{width:24px;height:24px;flex:none;stroke:currentColor;stroke-width:2;fill:none;stroke-linecap:round;stroke-linejoin:round}
.sd-state.is-up{color:#77e1b2;background:#14362e;border-color:#2d8062}
.sd-state strong{display:block;font-size:13px;color:inherit}.sd-state small{font-size:12px;color:inherit;opacity:.85}
.sd-tools{display:flex;gap:8px;flex-wrap:wrap}.sd-tools button{margin:6px 0 0;padding:8px 11px}
.sd-statusline{min-height:1.2em}
@media(max-width:650px){.sd-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}
@media(max-width:380px){.sd-state{padding:9px 7px;gap:5px}.sd-state svg{width:18px;height:18px}.sd-state strong{font-size:11px}}
\n/* S7.13 SUPLA-inspired light visual layer; no external assets. */
:root{color-scheme:light;background:#fff;color:#192324;font-family:system-ui,-apple-system,"Segoe UI",Arial,sans-serif}
html{scroll-behavior:smooth}body{background:#fff;color:#192324;min-height:100vh}
body::before{content:"";position:fixed;inset:0;pointer-events:none;z-index:0;transform:none;opacity:1;background-repeat:repeat;background-size:360px 112px;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='360' height='112'%3E%3Ctext x='12' y='57' fill='%23E6ECE8' font-family='Arial,sans-serif' font-size='16' font-weight='600' letter-spacing='1' transform='rotate(-25 180 56)'%3EMatter2Supla - Beta%3C/text%3E%3C/svg%3E")}
main{max-width:1370px;padding:23px 25px 65px}h1{font-size:33px;color:#142123;margin:0;letter-spacing:-.05em}h2{font-size:22px;color:#182122;letter-spacing:-.02em;margin-bottom:10px}h3{color:#172522}
p{color:#4e5b59;line-height:1.5}.eyebrow{letter-spacing:.07em;color:#15ad50;font-size:11px}.top{background:rgba(255,255,255,.95);border-radius:14px;padding:12px 16px;border:1px solid #e2ece6;box-shadow:0 3px 18px #152f2210}.top h1::before{content:'◉';font-size:28px;color:#00bd4c;margin-right:14px}.badge{background:#e0f9e9;color:#056a2a;font-weight:700}.badge.is-muted{background:#f1f3f1;color:#6a7370}
.facts{gap:8px;font-size:12px;color:#53645c}.facts span{background:#f1f8f3;border:1px solid #d7eede;color:#335543;padding:8px 11px}
.sd-wrap{margin:17px 0}.sd-topline{display:none}.sd-grid{grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}.sd-state,.sd-state.is-up{background:#effbf2;border:1px solid #d7f4e1;border-radius:12px;color:#15863a;padding:12px 14px;box-shadow:0 2px 10px #1b56200a}.sd-state strong{color:#192c20}.sd-state small{color:#167638}
.grid{grid-template-columns:repeat(2,minmax(0,1fr));align-items:start;gap:20px;margin:20px 0}.card{background:rgba(255,255,255,.96);border:1px solid #dfe7e3;border-radius:17px;padding:25px;box-shadow:0 5px 22px #172d2013;min-width:0}.card.wide{grid-column:auto}.card[data-panel="system"]{grid-column:1/-1}.card[hidden]{display:none!important}.card h2{display:flex;align-items:center;gap:12px}.card h2::before{content:'✦';color:#00c851;font-size:24px}.card[data-panel='wifi'] h2::before{content:'◉'}.card[data-panel='supla'] h2::before{content:'☁'}.card[data-panel='sensors'] h2::before{content:'▥'}.card[data-panel='system'] h2::before{content:'⚙'}
label{color:#26342c;font-weight:650;font-size:13px}input,textarea,select{color:#1b2822;background:#fff;border:1px solid #cbd6d1;box-shadow:inset 0 1px 1px #344a3510;padding:11px 12px}input:focus,select:focus,textarea:focus{outline:2px solid #80e8a8;outline-offset:1px;border-color:#00c851}input[type='checkbox']{accent-color:#00c851}.hint{color:#697873;font-size:12px}.notice{color:#344b39;background:#eefaf0;border:1px solid #d3eede;border-radius:11px}.error,#suplaRegistrationWarning{color:#da1f27!important}#suplaRegistrationWarning{border:0!important;background:transparent!important;border-radius:0!important;padding:3px 0 3px 22px!important;position:relative;margin:4px 0 13px!important;font-size:14px}#suplaRegistrationWarning::before{content:'⚠';position:absolute;left:0}.ok{color:#078c36}code{color:#168f45;background:#eaf8ee;padding:1px 4px;border-radius:4px}a{color:#008d38}td,th{border-bottom:1px solid #e0e8e2;color:#34413b}summary{color:#167c39;font-weight:650}footer{color:#68716c}
button{background:#00d151;color:#092614;border:1px solid #00bc48;border-radius:10px;box-shadow:0 2px 5px #0b6e2e12;margin:10px 7px 0 0}button:hover:not(:disabled){filter:brightness(.96)}button.secondary{color:#146c33;background:#f4fbf6;border:1px solid #9de8b6}button.danger{color:#d92832;background:#fff7f8;border:1px solid #f0aeb3}button:disabled{opacity:.48}.supla-assigned{color:#2c4735;border-bottom-color:#e2e9e3}.supla-assigned:hover{background:#f0faf2}.supla-assigned.selected{background:#e5f9ec;border-color:#00d151}.supla-assigned strong{color:#138443}.supla-assigned:focus-visible{outline-color:#00d151}
#newWifiIpBox{background:#e2fae9;border-color:#c0edd0;color:#0b7932}#newWifiIp{font-size:19px;font-weight:800;color:#009b3d}#saveWifi{width:100%;margin-top:16px}#pauseReads{background:#f8faf8;border-color:#c4cbc8;color:#33403b}#runAllReads{background:#fff;color:#009a38;border-color:#00bd4d}
#suplaConnectionConfig{margin-top:14px}#suplaConnectionConfig>summary{padding:10px 0}#suplaDeviceList{background:#f6fbf7}#sensorWidgets{grid-template-columns:repeat(auto-fit,minmax(230px,1fr))}.card .grid .card{box-shadow:none}.card .grid{gap:10px}
.m2s-nav{display:flex;gap:4px;flex-wrap:wrap;align-items:center;background:rgba(255,255,255,.97);border-bottom:2px solid #00ce51;margin-top:19px;padding:5px 0 0}.m2s-tab{font-weight:700;border:0;box-shadow:none;background:transparent;color:#34413b;margin:0 2px;padding:12px 17px;border-radius:11px 11px 0 0}.m2s-tab:hover:not(:disabled){background:#e7faed}.m2s-tab[aria-selected='true']{background:#00d151;color:#092714}.m2s-intro{margin:15px 0 5px}.m2s-intro h2{margin:0}.m2s-intro p{margin:5px 0}.m2s-beta{font-weight:700;font-size:11px;color:#0e8b3b;letter-spacing:.05em}.sd-wrap{margin:12px 0 0}.m2s-layout-note{font-size:12px;color:#64756a}.m2s-hidden-nav[hidden]{display:none!important}
@media(max-width:930px){main{padding:15px 12px 45px}.grid{grid-template-columns:1fr}.sd-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.card.wide{grid-column:auto}.m2s-nav{gap:2px}.m2s-tab{padding:11px 12px;font-size:13px}}
@media(max-width:540px){.top{padding:12px}.top h1{font-size:27px}.card{padding:17px}.m2s-tab{font-size:12px;padding:9px}.sd-grid{gap:7px}.sd-state{padding:9px 8px}.sd-state svg{width:18px;height:18px}.sd-state strong{font-size:11px}.sd-state small{font-size:11px}.row>*{min-width:125px}}

/* S7.15 FLASH-only help, no remote assets or extra API polling */
.help-panel{grid-column:1/-1}.help-lead{font-size:15px;line-height:1.6}.help-option{display:flex;gap:10px;flex-wrap:wrap;margin:12px 0 22px}.help-option button{margin:0}.help-section{padding:15px 0 18px;border-top:1px solid #e3ece6}.help-section h3{margin:4px 0 12px;font-size:19px;color:#163d27}.help-step{display:grid;grid-template-columns:minmax(0,1fr) 245px;gap:18px;align-items:center;background:#fff;border:1px solid #e4eee6;border-radius:14px;padding:16px;margin:10px 0}.help-step p{margin:6px 0}.help-no{display:inline-block;color:#008b36;font-weight:800;margin-bottom:3px}.help-visual{width:100%;height:auto;max-height:160px;border-radius:10px;background:#f4faf6;border:1px solid #d3ecd9}.help-call{background:#eefbf1;border-left:4px solid #00d151;padding:12px 16px;border-radius:8px}.help-warn{background:#fff8e7;border-left:4px solid #d09b20;padding:12px 16px;border-radius:8px}.help-method[hidden]{display:none!important}.help-caption{font-size:12px;color:#697973}.help-jump{display:inline-flex;align-items:center;gap:6px}.help-panel code{white-space:normal;overflow-wrap:anywhere}.help-panel ol{padding-left:20px}.help-panel li{padding:4px 0}
@media(max-width:700px){.help-step{grid-template-columns:1fr}.help-visual{max-height:140px}}
/* S7.16: pulpit: Parowanie Matter i skaner BLE w jednym wierszu. */
#settingsPanel[data-active-tab="home"] > [data-panel="pair"]{order:-2}
#settingsPanel[data-active-tab="home"] > [data-panel="ble"]{order:-1}
#settingsPanel [data-panel="ble"]{min-width:0}
#settingsPanel [data-panel="ble"] table{min-width:450px}
#manualCode{letter-spacing:.075em;font-variant-numeric:tabular-nums}
</style></head><body><main>
<div class="top"><div><div class="eyebrow">SMART HOME GATEWAY · ESP32-C5</div><h1>Matter2Supla</h1></div><span id="online" class="badge">Łączenie…</span></div>
<p>Konfiguracja sieci i dodawanie czujników Matter przez Thread.</p>
<div class="facts"><span id="ip">IP: —</span><span id="thread">Thread: —</span><span id="ram">RAM: —</span><span id="controller">Kontroler: —</span></div>
<div id="connection" class="notice" hidden></div>

<nav class="m2s-nav" aria-label="Nawigacja Matter2Supla">
<button class="m2s-tab" type="button" data-tab="home" aria-selected="true">⌂ Pulpit</button>
<button class="m2s-tab" type="button" data-tab="sensors" aria-selected="false">◉ Czujniki</button>
<button class="m2s-tab" type="button" data-tab="pair" aria-selected="false">⛓ Parowanie</button>
<button class="m2s-tab" type="button" data-tab="supla" aria-selected="false">☁ SUPLA</button>
<button class="m2s-tab" type="button" data-tab="wifi" aria-selected="false">◉ Sieć</button>
<button class="m2s-tab" type="button" data-tab="system" aria-selected="false">⚙ System</button>
<button class="m2s-tab" type="button" data-tab="help" aria-selected="false">? Pomoc</button>
</nav><div class="m2s-intro"><div class="m2s-beta">SMART HOME GATEWAY · S7.15 BETA</div><h2 id="m2sPageTitle">Przegląd systemu</h2><p id="m2sPageDesc">Połączenia, czujniki Matter i integracja SUPLA w jednym miejscu.</p></div>
<section class="sd-wrap" aria-label="Smart Dashboard">
 <div class="sd-topline"><div><div class="eyebrow">SMART DASHBOARD · STAGE 11C</div><h2>Stan bramki</h2></div><a href="#settingsPanel">Przejdź do konfiguracji ↓</a></div>
 <div class="sd-grid"><div class="sd-state" id="sd-wifi"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M2 8a15 15 0 0 1 20 0M5 11a11 11 0 0 1 14 0M8.5 14.5a6 6 0 0 1 7 0"/><circle cx="12" cy="18" r="1" fill="currentColor" stroke="none"/></svg><span><strong>Wi-Fi</strong><small id="sd-wifi-state">Offline</small></span></div><div class="sd-state" id="sd-thread"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v18M9 21h6M8 8a6 6 0 0 0 0 8M16 8a6 6 0 0 1 0 8M5 5a11 11 0 0 0 0 14M19 5a11 11 0 0 1 0 14"/></svg><span><strong>Thread</strong><small id="sd-thread-state">Offline</small></span></div><div class="sd-state" id="sd-matter"><svg viewBox="0 0 24 24" aria-hidden="true"><rect x="5" y="5" width="14" height="14" rx="2"/><path d="M9 9h6v6H9zM9 1v4M15 1v4M9 19v4M15 19v4M1 9h4M1 15h4M19 9h4M19 15h4"/></svg><span><strong>Matter</strong><small id="sd-matter-state">Offline</small></span></div><div class="sd-state" id="sd-supla"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 18H5a4 4 0 0 1-.3-8A7 7 0 0 1 18 9a4.5 4.5 0 0 1 1 9H7z"/></svg><span><strong>SUPLA</strong><small id="sd-supla-state">Offline</small></span></div></div>
</section>
<div class="grid" id="settingsPanel"><section class="card"><h2>Sieć Wi-Fi</h2><p class="hint">Wybierz sieć lub wpisz nazwę ręcznie. Błędne ustawienia nie zastąpią poprzedniego połączenia.</p>
<button id="scan" class="secondary" type="button">Szybkie wyszukiwanie (3–5 s)</button><button id="scanFull" class="secondary" type="button">Pełne wyszukiwanie</button><select id="networks" aria-label="Znalezione sieci" hidden><option value="">Wybierz sieć…</option></select>
<form id="wifiForm"><label for="ssid">Nazwa sieci (SSID)</label><input id="ssid" maxlength="32" required autocomplete="off"><label for="password">Hasło Wi-Fi</label><input id="password" type="password" maxlength="63" autocomplete="new-password"><p class="hint">Puste hasło oznacza sieć otwartą. Dla WPA2/WPA3 wpisz 8–63 znaki.</p><button id="saveWifi">Sprawdź i zapisz Wi-Fi</button></form><p id="wifiResult" class="notice" role="status">Oczekiwanie na status…</p><div id="newWifiIpBox" class="notice" role="status" aria-live="polite"><strong>Adres IP po połączeniu:</strong> <span id="newWifiIp">Oczekiwanie na połączenie z Wi-Fi</span> <a id="newWifiIpLink" href="#" hidden target="_blank" rel="noopener">Otwórz panel pod nowym IP</a></div><p class="hint">Sieć konfiguracji: <strong>Matter2Supla-XXXX</strong><br>Hasło: <code>12345678</code> · adres: <a href="http://192.168.4.1">192.168.4.1</a></p></section>
<section class="card"><h2>Parowanie Matter</h2><p class="hint">Włącz tryb parowania czujnika. Wyszukaj BLE i wybierz discriminator, następnie wpisz 11-cyfrowy kod Matter z etykiety albo wklej kod QR MT:…. Na tym etapie obsługujemy certyfikat PAA IKEA G1.</p>
<button id="start" type="button" class="secondary">Uruchom kontroler</button>
<form id="pairForm"><label for="name">Nazwa czujnika</label><input id="name" maxlength="48" required placeholder="np. Salon"><label for="qr">Treść kodu QR (MT:…)</label><input id="qr" type="password" autocomplete="off" spellcheck="false" placeholder="MT:…"><div class="sd-tools"><button id="sd-qr-toggle" type="button" class="secondary" aria-pressed="false">Pokaż treść QR</button><button id="sd-qr-copy" type="button" class="secondary">Kopiuj MT:…</button></div><p class="hint" id="sd-qr-info" role="status">Kod widoczny tylko podczas wpisywania. Po rozpoczęciu parowania zostanie wyczyszczony; nie zapisujemy go w bramce.</p><label for="manualCode">11-cyfrowy kod parowania Matter z etykiety</label><input id="manualCode" type="text" inputmode="numeric" autocomplete="off" spellcheck="false" maxlength="16" placeholder="XXXX-XXX-XXXX"><p class="hint">Kod podczas wpisywania pozostaje widoczny na ekranie — zachowaj prywatność. Wpisz kod 11-cyfrowy, a bramka sprawdzi cyfrę kontrolną i dobierze pełny discriminator z reklam BLE. Czujnik musi być w trybie parowania. Przy kilku pasujących urządzeniach użyj kodu QR.</p><input id="disc" type="hidden"><p class="hint">Wybierz urządzenie z listy BLE przez „Użyj discriminator”, a następnie wpisz jego 11-cyfrowy kod Matter. Nie jest wymagane osobne pole PIN.</p><button id="pair">Dodaj czujnik</button><button id="pairCancel" type="button" class="danger" disabled>Zatrzymaj parowanie</button></form><p id="pairResult" class="notice" role="status">Oczekiwanie na kontroler…</p></section>
<section class="card"><h2>Skaner Matter BLE</h2>
<p class="hint">Uruchom tryb parowania w czujniku. Skaner przez 8 sekund pasywnie nasłuchuje reklam Matter (UUID 0xFFF6). Nie łączy się z urządzeniami, nie zapisuje niczego w NVS i nie zmienia kanałów SUPLA. Lista jest ulotna, maksymalnie 12 urządzeń.</p>
<button id="bleStart" type="button">Wyszukaj urządzenia Matter</button><button id="bleStop" type="button" class="secondary" disabled>Zatrzymaj</button>
<p id="bleScanStatus" class="notice" role="status">Skanowanie nieuruchomione.</p>
<div style="overflow-x:auto"><table><thead><tr><th>Urządzenie</th><th>Vendor / Product</th><th>Discriminator</th><th>RSSI</th><th></th></tr></thead><tbody id="bleResults"><tr><td colspan="5">Brak wyników.</td></tr></tbody></table></div>
<p class="hint">Discriminator wskazuje urządzenie. Następnym krokiem jest wpisanie 11-cyfrowego kodu Matter lub zeskanowanie QR. Skaner wykrywa tylko urządzenia nadające ogłoszenie BLE Matter podczas otwartego okna parowania; nie znajduje Zigbee ani już sparowanych urządzeń bez otwartego okna.</p></section>

<section class="card wide"><h2>Integracja SUPLA</h2><p class="hint">Przypisane czujniki i ich stałe kanały SUPLA. Numer węzła Matter nie jest numerem kanału SUPLA.</p><div id="suplaDeviceList" class="notice" aria-live="polite">Oczekiwanie na konfigurację SUPLA…</div><p id="suplaSelectedStatus" class="hint" role="status">Kliknij czujnik z listy, aby zobaczyć numer Matter i endpointy poniżej.</p><p id="suplaStatus" class="notice" role="status"></p><details id="suplaConnectionConfig"><summary id="suplaConfigSummary">Ustawienia połączenia SUPLA i kanału 0</summary><form id="suplaForm"><label><input id="suplaEnabled" type="checkbox" style="width:auto"> Włącz integrację SUPLA</label><p class="error" id="suplaRegistrationWarning" style="font-weight:800;color:#ff6565;border:1px solid #d84343;padding:10px;border-radius:8px">Włącz rejestrację urządzeń w SUPLA CLOUD</p><label for="suplaServer">Serwer z ustawień Twojego konta SUPLA (sama nazwa hosta)</label><input id="suplaServer" maxlength="127" placeholder="np. svr…supla.org" required><label for="suplaEmail">E-mail konta SUPLA</label><input id="suplaEmail" type="email" maxlength="127" required><label for="suplaNode">Czujnik Matter dla kanału SUPLA 0 (wybór automatyczny)</label><select id="suplaNode" required><option value="">Wybierz sparowany czujnik Matter…</option></select><div class="row"><div><label for="suplaTempEp">Endpoint temperatury</label><input id="suplaTempEp" type="number" min="0" max="65534" value="1" required></div><div><label for="suplaHumEp">Endpoint wilgotności</label><input id="suplaHumEp" type="number" min="0" max="65534" value="2" required></div></div><p class="hint">Dla TIMMERFLOTTE: temperatura EP 1, wilgotność EP 2. Numer węzła Matter wybierz według listy sparowanych czujników. Kanał SUPLA ma stały numer 0. Po pierwszym zapisie przypisanie jest zablokowane, aby nie mieszać historii pomiarów. Przed pierwszym połączeniem włącz rejestrację urządzeń w SUPLA Cloud.</p><button id="saveSupla">Zapisz i uruchom ponownie</button></form><p id="suplaResult" role="status"></p></details></section>
<section class="card wide">
<h2>Siła sygnału Thread</h2>
<p class="hint">
Pomiary bezpośrednich dzieci ESP32-C5.
RSSI pochodzi z odebranych ramek, a nie z ciągłego skanowania.
Jakość łącza: 0–3.
</p>
<div id="threadSignal" class="notice" aria-live="polite">
Oczekiwanie na dane Thread…
</div>
</section>
<section class="card wide"><h2>Odczyt czujników</h2><p class="hint">MYGGBETT, MYGGSPRAY i KLIPPBOK: szybkie stany z subskrypcji Matter; odczyt okresowy pozostaje zabezpieczeniem. ALPSTUGA: temperatura i wilgotność oraz dodatkowe pomiary powietrza. Pierwsza identyfikacja czujnika po restarcie może potrwać kilka minut.</p>
<form id="pollForm"><div class="row"><div><label for="pollNode">Czujnik Matter</label><select id="pollNode" required><option value="">Brak czujników</option></select></div><div><label for="pollMinutes">Interwał automatycznych odczytów (minuty)</label><input id="pollMinutes" type="number" min="1" max="1440" step="1" value="1" required></div></div><button id="pollSave" type="submit">Zapisz interwał bez restartu</button></form>
<p id="pollResult" class="notice" role="status">Domyślnie 1 minuta dla tego testu. Sugerowane 15–30 minut dla czujników bateryjnych. Krótszy czas może zwiększyć zużycie baterii i obciążenie sieci Thread; nie gwarantuje szybszego pomiaru wewnątrz czujnika. Odczyt baterii: co godzinę po sukcesie; przy braku wartości ponowienie co 5 minut podczas następnego odczytu.</p>
<div class="row"><button id="pauseReads" type="button" class="secondary">Zatrzymaj odczyt czujników</button><button id="runAllReads" type="button">Uruchom odczyt wszystkich czujników</button></div><p class="hint">Zatrzymanie blokuje nowe odczyty i automatyczne ponawianie. Trwający odczyt może się zakończyć; aktywne subskrypcje nadal mogą wysyłać raporty. Uruchomienie odczytu obejmuje wszystkie zapisane czujniki, kolejno.</p><p id="readControlStatus" role="status"></p><p id="autoStatus" role="status"></p><div id="sensorWidgets" class="grid"></div><div style="overflow-x:auto"><table><thead><tr><th>Nazwa</th><th>Numer</th><th>Stan dodawania</th></tr></thead><tbody id="devices"></tbody></table></div><p class="hint">„Sparowany” oznacza zakończone dodawanie, nie bieżącą dostępność. Kliknij nazwę czujnika, aby odczytać temperaturę i wilgotność. Inne rodzaje pomiarów nie są jeszcze obsługiwane.</p><div id="sensorDetail" hidden><h3 id="sensorTitle"></h3><button id="readSensor" type="button">Odśwież</button><p id="readStatus" class="notice" role="status" aria-live="polite"></p><div id="readValues"></div><p class="hint">Czujnik bateryjny może odpowiedzieć z opóźnieniem. Odczyt nie zmienia parowania.</p></div><details><summary>Pokaż czujnik sparowany wcześniej przez konsolę</summary><form id="adoptForm"><div class="row"><div><label for="existingName">Nazwa</label><input id="existingName" value="TIMMERFLOTTE" maxlength="48" required></div><div><label for="existingNode">Numer węzła</label><input id="existingNode" type="number" min="1" max="65535" value="1" required></div></div><button class="secondary">Dodaj do listy bez parowania</button></form><p id="adoptResult" role="status"></p></details></section>
<section class="card wide"><h2>Matter → kanały SUPLA</h2>
<p class="hint">Każdy sparowany czujnik może mieć oddzielny kanał SUPLA. Wybierz model, poczekaj na poprawne pomiary i dodaj kanał. Numer kanału jest trwały — nie zamieniaj urządzeń miejscami. Kanał 0 i istniejące przypisania zostają zachowane. Bramka odmówi dodania, gdy wyczerpie się RAM, miejsce na dane lub limit SUPLA.</p>
<p id="extraSuplaStatus" class="notice" role="status"></p>
<form id="extraSuplaForm"><div class="row"><div><label for="extraNode">Sparowany czujnik Matter</label><select id="extraNode" required><option value="">Wybierz czujnik z listy</option></select><p id="extraPickerInfo" class="hint" role="status"></p></div>
<div><label for="extraType">Rodzaj kanału</label><select id="extraType"><option value="2">MYGGSPRAY - ruch</option><option value="3">MYGGBETT - kontakt</option><option value="4">KLIPPBOK - zalanie</option><option value="1">TIMMERFLOTTE / ALPSTUGA - temperatura i wilgotność</option><option value="5">ALPSTUGA - CO₂</option><option value="6">ALPSTUGA - PM2.5</option></select></div></div>
<div class="row"><div><label for="extraEp">Endpoint pomiaru</label><input id="extraEp" type="number" min="0" max="65534" value="1" required></div>
<div><label for="extraAuxEp">Endpoint wilgotności (tylko temp/wilg.)</label><input id="extraAuxEp" type="number" min="0" max="65534" value="1" required></div></div>
<label><input id="extraInvert" type="checkbox" style="width:auto"> Odwróć stan logiczny (ruch, kontakt, woda)</label>
<p class="hint">MYGGBETT, MYGGSPRAY i KLIPPBOK korzystają z raportów subskrypcji Matter dla szybkich zmian (gdy czujnik jest aktywny). Odczyt okresowy pozostaje awaryjny. Bramka nie zastępuje certyfikowanego systemu alarmowego.</p>
<button id="extraAdd" type="submit">Dodaj stały kanał i uruchom ponownie</button></form><p id="extraSuplaResult" role="status"></p></section>
<section class="card wide"><h2>System</h2><p class="hint">Operacje systemowe i konserwacja konfiguracji Matter2Supla.</p><details><summary>Przywrócenie ustawień fabrycznych</summary><p class="error">Usuwa wszystkie dane NVS: Wi-Fi, konfigurację Thread, dane kontrolera Matter i listę czujników. Po restarcie urządzenie rozgłosi Matter2Supla-XXXX z hasłem 12345678 pod adresem 192.168.4.1. Czujniki mogą wymagać osobnego resetu przed ponownym dodaniem.</p><label for="resetText">Wpisz USUN WSZYSTKO, aby potwierdzić</label><input id="resetText" autocomplete="off"><button id="reset" class="danger">Przywróć ustawienia fabryczne</button><p id="resetResult" role="status"></p></details></section>
<section class="card wide help-panel" data-panel="help" hidden><h2>Pomoc · Dodawanie czujników Matter do SUPLA</h2><p class="help-lead">Wybierz sposób parowania. Po sparowaniu przejdź przez wspólne etapy dodania czujnika do SUPLA CLOUD. Ilustracje są poglądowe, a numery, kody i nazwy w przykładach nie są danymi Twojego urządzenia.</p><div class="help-option"><button type="button" id="helpBle" class="secondary" aria-pressed="true">① Skaner BLE + kod 11 cyfr</button><button type="button" id="helpQr" class="secondary" aria-pressed="false">② Kod QR Matter</button><button type="button" id="helpSupla" class="secondary">③ Przejdź do SUPLA</button></div>
<div class="help-method" id="helpBleSteps"><div class="help-section"><h3>Metoda A · Skaner Matter BLE + kod 11-cyfrowy</h3>
<div class="help-step"><div><div class="help-no">KROK 1</div><h3>Włącz tryb parowania w czujniku</h3><p>Zasil czujnik, ustaw go blisko bramki ESP32-C5 i uruchom tryb parowania według instrukcji producenta. Urządzenie musi reklamować usługę Matter BLE.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Urządzenie Matter i przycisk parowania"><rect x="14" y="15" width="100" height="121" rx="18" fill="#fff" stroke="#b5d9bd" stroke-width="3"/><circle cx="64" cy="60" r="28" fill="#e0f9e8" stroke="#00bd4a" stroke-width="3"/><text x="64" y="66" font-size="14" font-weight="bold" text-anchor="middle" fill="#008d3a">22°C</text><rect x="43" y="102" width="42" height="12" rx="6" fill="#d3e3d7"/><path d="M137 75h58" stroke="#00b94a" stroke-width="5"/><path d="M187 65l12 10-12 10" fill="none" stroke="#00b94a" stroke-width="5"/><rect x="215" y="27" width="65" height="95" rx="12" fill="#eaf8ed" stroke="#7bce96"/><circle cx="247" cy="75" r="15" fill="#00d151"/><text x="247" y="79" font-size="12" fill="#083a16" text-anchor="middle">BLE</text></svg></div><div class="help-step"><div><div class="help-no">KROK 2</div><h3>Na pulpicie użyj widżetu Skaner Matter BLE</h3><p>Kliknij „Wyszukaj urządzenia Matter”. Po zakończeniu skanowania wybierz właściwy discriminator przyciskiem „Użyj discriminator”.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Skaner Matter i wybór discriminatora"><rect x="12" y="16" width="276" height="119" rx="12" fill="#fff" stroke="#b7dbc2"/><rect x="22" y="25" width="256" height="25" rx="5" fill="#e7f9ec"/><text x="31" y="42" font-size="12" font-weight="bold" fill="#174c2b">Skaner Matter BLE</text><rect x="25" y="60" width="250" height="32" rx="6" fill="#f5faf6" stroke="#cde8d3"/><circle cx="42" cy="76" r="7" fill="#00d151"/><text x="57" y="81" font-size="11" fill="#233b2b">Urządzenie · disc 3697</text><rect x="180" y="99" width="91" height="27" rx="5" fill="#00d151"/><text x="225" y="116" text-anchor="middle" font-size="10" font-weight="bold" fill="#0f361d">Użyj disc.</text></svg></div><div class="help-step"><div><div class="help-no">KROK 3</div><h3>Wpisz kod 11-cyfrowy</h3><p>W formularzu „Parowanie Matter” podaj nazwę i kod z etykiety urządzenia (format XXXX-XXX-XXXX). Nie wpisuj go w pole QR. Naciśnij „Dodaj czujnik”.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Pole 11-cyfrowego kodu Matter"><rect x="13" y="14" width="274" height="122" rx="12" fill="#fff" stroke="#bddfc5"/><text x="27" y="41" font-size="13" fill="#274331">Kod Matter (11 cyfr)</text><rect x="27" y="49" width="246" height="39" rx="7" fill="#f7fbf8" stroke="#a8cdb2"/><text x="41" y="74" font-family="monospace" font-size="18" fill="#2e5038">XXXX-XXX-XXXX</text><rect x="138" y="96" width="134" height="28" rx="6" fill="#00d151"/><text x="205" y="114" text-anchor="middle" font-size="12" font-weight="bold" fill="#124020">Dodaj czujnik</text></svg></div><p class="help-call">Po poprawnym parowaniu sprawdź czujnik w zakładce <a href="#" data-help-open="sensors">Czujniki</a>. Pomyślne parowanie nie jest jeszcze równoznaczne z rejestracją kanału SUPLA.</p></div></div>
<div class="help-method" id="helpQrSteps" hidden><div class="help-section"><h3>Metoda B · kod QR Matter</h3>
<div class="help-step"><div><div class="help-no">KROK 1</div><h3>Włącz tryb parowania</h3><p>Włącz okno parowania na czujniku. Znajdź kod QR Matter na urządzeniu lub instrukcji.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Urządzenie Matter i przycisk parowania"><rect x="14" y="15" width="100" height="121" rx="18" fill="#fff" stroke="#b5d9bd" stroke-width="3"/><circle cx="64" cy="60" r="28" fill="#e0f9e8" stroke="#00bd4a" stroke-width="3"/><text x="64" y="66" font-size="14" font-weight="bold" text-anchor="middle" fill="#008d3a">22°C</text><rect x="43" y="102" width="42" height="12" rx="6" fill="#d3e3d7"/><path d="M137 75h58" stroke="#00b94a" stroke-width="5"/><path d="M187 65l12 10-12 10" fill="none" stroke="#00b94a" stroke-width="5"/><rect x="215" y="27" width="65" height="95" rx="12" fill="#eaf8ed" stroke="#7bce96"/><circle cx="247" cy="75" r="15" fill="#00d151"/><text x="247" y="79" font-size="12" fill="#083a16" text-anchor="middle">BLE</text></svg></div><div class="help-step"><div><div class="help-no">KROK 2</div><h3>Odczytaj pełną zawartość QR</h3><p>Odczytaj QR telefonem lub czytnikiem pokazującym jego surową treść. Skopiuj tekst rozpoczynający się od „MT:”. Obecny panel nie uruchamia aparatu telefonu.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Etykieta kodu QR Matter"><rect x="11" y="11" width="278" height="128" rx="12" fill="#fff" stroke="#b7d8bf"/><rect x="30" y="27" width="89" height="89" fill="#fff" stroke="#2b3e30"/><g fill="#1c3826"><path d="M37 34h24v24H37zM87 34h24v24H87zM37 84h24v24H37z"/><rect x="68" y="34" width="10" height="10"/><rect x="75" y="54" width="10" height="10"/><rect x="67" y="70" width="12" height="12"/><rect x="88" y="71" width="22" height="8"/><rect x="84" y="93" width="8" height="16"/><rect x="102" y="88" width="9" height="21"/></g><text x="140" y="53" font-size="15" font-weight="bold" fill="#1b3f27">Kod QR Matter</text><text x="140" y="76" font-size="13" fill="#067d34">MT:…</text><text x="140" y="100" font-size="10" fill="#54735e">Skopiuj pełną treść</text></svg></div><div class="help-step"><div><div class="help-no">KROK 3</div><h3>Wklej kod i sparuj</h3><p>W zakładce „Parowanie” wpisz nazwę czujnika, wklej pełną treść MT:… do pola „Treść kodu QR”, pozostaw pole 11-cyfrowe puste i kliknij „Dodaj czujnik”.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Pole 11-cyfrowego kodu Matter"><rect x="13" y="14" width="274" height="122" rx="12" fill="#fff" stroke="#bddfc5"/><text x="27" y="41" font-size="13" fill="#274331">Kod Matter (11 cyfr)</text><rect x="27" y="49" width="246" height="39" rx="7" fill="#f7fbf8" stroke="#a8cdb2"/><text x="41" y="74" font-family="monospace" font-size="18" fill="#2e5038">XXXX-XXX-XXXX</text><rect x="138" y="96" width="134" height="28" rx="6" fill="#00d151"/><text x="205" y="114" text-anchor="middle" font-size="12" font-weight="bold" fill="#124020">Dodaj czujnik</text></svg></div><p class="help-call">Treść QR jest poufna. Nie udostępniaj jej publicznie. Po zakończeniu dodawania sprawdź czujnik w zakładce <a href="#" data-help-open="sensors">Czujniki</a>.</p></div></div>
<div class="help-section" id="helpSuplaSteps"><h3>Wspólne kroki · Podłączenie czujnika do SUPLA CLOUD</h3>
<div class="help-step"><div><div class="help-no">KROK 4</div><h3>Sprawdź działanie Matter i Thread</h3><p>Otwórz „Czujniki”. Znajdź nowy Node ID i nazwę urządzenia. Wykonaj odczyt i zweryfikuj wartości. Subskrypcja może potrzebować czasu na nawiązanie po restarcie; nie usuwaj od razu parowania.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Urządzenie Matter i przycisk parowania"><rect x="14" y="15" width="100" height="121" rx="18" fill="#fff" stroke="#b5d9bd" stroke-width="3"/><circle cx="64" cy="60" r="28" fill="#e0f9e8" stroke="#00bd4a" stroke-width="3"/><text x="64" y="66" font-size="14" font-weight="bold" text-anchor="middle" fill="#008d3a">22°C</text><rect x="43" y="102" width="42" height="12" rx="6" fill="#d3e3d7"/><path d="M137 75h58" stroke="#00b94a" stroke-width="5"/><path d="M187 65l12 10-12 10" fill="none" stroke="#00b94a" stroke-width="5"/><rect x="215" y="27" width="65" height="95" rx="12" fill="#eaf8ed" stroke="#7bce96"/><circle cx="247" cy="75" r="15" fill="#00d151"/><text x="247" y="79" font-size="12" fill="#083a16" text-anchor="middle">BLE</text></svg></div><div class="help-step"><div><div class="help-no">KROK 5</div><h3>Włącz SUPLA i rejestrację urządzeń</h3><p>Zaloguj się na cloud.supla.org i włącz możliwość rejestrowania nowych urządzeń. W zakładce SUPLA w bramce włącz integrację, wpisz serwer i e-mail SUPLA, a następnie zapisz. Jeśli konto jest już połączone, nie musisz konfigurować go drugi raz.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Kanały SUPLA i nowe urządzenie"><rect x="12" y="13" width="276" height="126" rx="12" fill="#fff" stroke="#badbc3"/><text x="24" y="38" font-size="14" font-weight="bold" fill="#205032">Matter → SUPLA</text><rect x="26" y="47" width="248" height="28" rx="5" fill="#f0fbf4" stroke="#b5dfc2"/><text x="34" y="66" font-size="12" fill="#295638">TIMMERFLOTTE 2 · Node 3</text><rect x="26" y="83" width="248" height="28" rx="5" fill="#f5faf6" stroke="#b5dfc2"/><text x="34" y="102" font-size="12" fill="#295638">Temperatura i wilgotność</text><circle cx="259" cy="124" r="9" fill="#00d151"/><path d="M254 124l4 4 7-9" fill="none" stroke="#fff" stroke-width="2.5"/></svg></div><div class="help-step"><div><div class="help-no">KROK 6</div><h3>Wybierz sparowany czujnik i rodzaj kanału</h3><p>W zakładce „SUPLA” przejdź do „Matter → kanały SUPLA”, wybierz urządzenie z listy, typ czujnika i prawidłowe endpointy. Dla TIMMERFLOTTE zwykle temperatura to endpoint 1, a wilgotność endpoint 2; odczytaj rzeczywiste endpointy z danych urządzenia.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Kanały SUPLA i nowe urządzenie"><rect x="12" y="13" width="276" height="126" rx="12" fill="#fff" stroke="#badbc3"/><text x="24" y="38" font-size="14" font-weight="bold" fill="#205032">Matter → SUPLA</text><rect x="26" y="47" width="248" height="28" rx="5" fill="#f0fbf4" stroke="#b5dfc2"/><text x="34" y="66" font-size="12" fill="#295638">TIMMERFLOTTE 2 · Node 3</text><rect x="26" y="83" width="248" height="28" rx="5" fill="#f5faf6" stroke="#b5dfc2"/><text x="34" y="102" font-size="12" fill="#295638">Temperatura i wilgotność</text><circle cx="259" cy="124" r="9" fill="#00d151"/><path d="M254 124l4 4 7-9" fill="none" stroke="#fff" stroke-width="2.5"/></svg></div><div class="help-step"><div><div class="help-no">KROK 7</div><h3>Dodaj stały kanał i uruchom ponownie</h3><p>Kliknij „Dodaj stały kanał i uruchom ponownie”. Po restarcie sprawdź połączenie Wi-Fi, Thread, Matter i status SUPLA Cloud. Uwaga: istniejących przypisań kanałów nie nadpisuj innym fizycznym czujnikiem.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Kanały SUPLA i nowe urządzenie"><rect x="12" y="13" width="276" height="126" rx="12" fill="#fff" stroke="#badbc3"/><text x="24" y="38" font-size="14" font-weight="bold" fill="#205032">Matter → SUPLA</text><rect x="26" y="47" width="248" height="28" rx="5" fill="#f0fbf4" stroke="#b5dfc2"/><text x="34" y="66" font-size="12" fill="#295638">TIMMERFLOTTE 2 · Node 3</text><rect x="26" y="83" width="248" height="28" rx="5" fill="#f5faf6" stroke="#b5dfc2"/><text x="34" y="102" font-size="12" fill="#295638">Temperatura i wilgotność</text><circle cx="259" cy="124" r="9" fill="#00d151"/><path d="M254 124l4 4 7-9" fill="none" stroke="#fff" stroke-width="2.5"/></svg></div><div class="help-step"><div><div class="help-no">KROK 8</div><h3>Sprawdź kanał w SUPLA CLOUD</h3><p>W cloud.supla.org odszukaj nowe urządzenie/kanał. Zweryfikuj aktualne wartości temperatury, wilgotności, ruchu, zalania lub kontaktu. Nazwy istniejących kanałów mogą wymagać ręcznej zmiany w SUPLA CLOUD.</p></div><svg class="help-visual" viewBox="0 0 300 150" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Kanały SUPLA i nowe urządzenie"><rect x="12" y="13" width="276" height="126" rx="12" fill="#fff" stroke="#badbc3"/><text x="24" y="38" font-size="14" font-weight="bold" fill="#205032">Matter → SUPLA</text><rect x="26" y="47" width="248" height="28" rx="5" fill="#f0fbf4" stroke="#b5dfc2"/><text x="34" y="66" font-size="12" fill="#295638">TIMMERFLOTTE 2 · Node 3</text><rect x="26" y="83" width="248" height="28" rx="5" fill="#f5faf6" stroke="#b5dfc2"/><text x="34" y="102" font-size="12" fill="#295638">Temperatura i wilgotność</text><circle cx="259" cy="124" r="9" fill="#00d151"/><path d="M254 124l4 4 7-9" fill="none" stroke="#fff" stroke-width="2.5"/></svg></div><div class="help-warn"><strong>Bezpieczne postępowanie:</strong> nie używaj „Przywróć ustawienia fabryczne” w celu rozwiązania problemu z pojedynczym kanałem. Reset fabryczny usuwa również konfigurację Matter i Thread. W razie problemów sprawdź najpierw <code>/api/status</code>, odczyty i rejestrację SUPLA.</div><p><button type="button" class="secondary help-jump" data-help-go="pair">Otwórz Parowanie</button><button type="button" class="secondary help-jump" data-help-go="supla">Otwórz SUPLA</button><button type="button" class="secondary help-jump" data-help-go="sensors">Otwórz Czujniki</button></p></div></section>
</div><footer>Panel lokalny · konfiguracja jest zapisywana dopiero po uzyskaniu adresu IP · nie udostępniaj panelu w Internecie<br>
Stworzona za pomocą AI przez krzychunh · mail: <a href="mailto:k.goraczko@gmail.com">k.goraczko@gmail.com</a></footer>
</main><script>
'use strict';
const $=id=>document.getElementById(id);let token='',latest=null,refreshBusy=false,scanBusy=false,scanNoticeUntil=0;const utf8=s=>new TextEncoder().encode(s).length;
let suplaLoaded=false;
let suplaNodeUserEdited=false;
function updateSuplaNodeOptions(s){const select=$('suplaNode'), current=select.value, u=s.supla_status||{};const devices=(s.devices||[]).filter(d=>(d.state===0||d.state===2)&&Number(d.node)>0&&Number(d.node)<=65535);const target=u.configured?String(u.node):(suplaNodeUserEdited?current:current);const signature=devices.map(d=>d.node+':'+d.name).join('|')+'|'+(u.configured?u.node:'');if(select.dataset.signature!==signature){select.replaceChildren();const first=document.createElement('option');first.value='';first.textContent='Wybierz sparowany czujnik Matter…';select.append(first);for(const d of devices){const op=document.createElement('option');op.value=String(d.node);op.textContent=d.name+' (Matter Node '+d.node+')';select.append(op)}if(u.configured&&!devices.some(d=>String(d.node)===String(u.node))){const op=document.createElement('option');op.value=String(u.node);op.textContent='Zapisany węzeł '+u.node+' (nieobecny)';select.append(op)}select.dataset.signature=signature;}select.value=target;if(!select.value&&!u.configured&&devices.length===1&&!suplaNodeUserEdited)select.value=String(devices[0].node);}

$('suplaNode').addEventListener('change',()=>{suplaNodeUserEdited=true;$('suplaNode').dataset.edited='1'});
let pollNodeSignature='';
let blePollTimer=null,bleReading=false;
function bleSchedule(){if(blePollTimer)clearTimeout(blePollTimer);blePollTimer=setTimeout(bleRead,900)}
function renderBle(j){
 $('bleStart').disabled=!!j.active||!j.available||!latest?.matter?.ready||!!latest?.matter?.pair_busy||!!latest?.readings?.busy||!!latest?.network?.testing;
 $('bleStop').disabled=!j.active;
 const count=(j.devices||[]).length;
 const msg=!j.available?'Skaner niedostępny: aktualny firmware nie używa NimBLE.':j.active?'Skanowanie BLE trwa… znaleziono '+count+' urządzeń Matter.':j.host_error?'Skanowanie przerwane, błąd stosu BLE: '+j.host_error+'.':count?'Skanowanie zakończone. Znaleziono '+count+' urządzeń Matter.':'Skanowanie zakończone. Brak reklam Matter w pobliżu.';
 message('bleScanStatus',msg,!!j.host_error);
 const body=$('bleResults');body.replaceChildren();
 for(const d of j.devices||[]){
  const tr=document.createElement('tr');
  for(const value of [d.model||'Nieznany producent',d.vendor_id+' / '+d.product_id,d.discriminator,d.rssi+' dBm']){const td=document.createElement('td');td.textContent=value;tr.append(td)}
  const td=document.createElement('td');const button=document.createElement('button');button.type='button';button.className='secondary';button.textContent='Użyj discriminator';button.disabled=!!j.active;button.onclick=()=>{
 const discriminator=Number(d.discriminator);
 if(!Number.isInteger(discriminator)||discriminator<0||discriminator>4095){
   message('bleScanStatus','Nieprawidłowy discriminator urządzenia BLE.',true);
   return;
 }
 let sensorName=String(d.model||'IKEA Matter').trim();
 if(!sensorName||utf8(sensorName)>48)sensorName='IKEA Matter';
 $('name').value=sensorName;
 $('disc').value=String(discriminator);
 $('pairForm').scrollIntoView({behavior:'smooth',block:'start'});
 $('manualCode').focus({preventScroll:true});
 message('bleScanStatus','Wybrano '+sensorName+' (discriminator '+discriminator+'). Wpisz teraz kod Matter 11-cyfrowy.');
};td.append(button);tr.append(td);body.append(tr);
 }
 if(!count){const tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=5;td.textContent=j.active?'Szukam urządzeń…':'Brak wyników.';tr.append(td);body.append(tr)}
 if(j.active)bleSchedule();
}
async function bleRead(){if(bleReading||!token)return;bleReading=true;try{const r=await fetch('/api/matter/ble-scan',{headers:{'Content-Type':'application/json','X-M2S-Token':token},cache:'no-store'});const j=await r.json();if(!r.ok)throw Error(j.error||('HTTP '+r.status));renderBle(j)}catch(e){message('bleScanStatus',e.message,true)}finally{bleReading=false}}
$('bleStart').onclick=async()=>{try{await post('/api/matter/ble-scan',{action:'start'});await bleRead()}catch(e){message('bleScanStatus',e.message,true)}};
$('bleStop').onclick=async()=>{try{await post('/api/matter/ble-scan',{action:'stop'});if(blePollTimer)clearTimeout(blePollTimer);await bleRead()}catch(e){message('bleScanStatus',e.message,true)}};

// STAGE11B_SELECTABLE_SUPLA: display assigned channels without ever rewriting NVS.
let suplaSelectedChannel=0;
let suplaPreviewWasExtra=false;
function suplaMappings(s){
 const u=s.supla_status||{},slots=s.supla_channels||[],out=[];
 if(u.configured&&Number(u.node)>0)
   out.push({channel:0,node:Number(u.node),type:1,ep:Number(u.temperature_endpoint),aux:Number(u.humidity_endpoint)});
 for(let i=0;i<slots.length;i++){
   const v=slots[i];if(!v||!(Number(v.node)>0))continue;
   out.push({channel:i+1,node:Number(v.node),type:Number(v.type),ep:Number(v.endpoint),aux:Number(v.aux_endpoint),invert:!!v.invert});
 }
 return out;
}
function showSuplaSelection(s){
 const u=s.supla_status||{},assignments=suplaMappings(s);
 if(assignments.length&&!assignments.some(a=>a.channel===suplaSelectedChannel))
   suplaSelectedChannel=assignments[0].channel;
 const a=assignments.find(v=>v.channel===suplaSelectedChannel);
 const extra=!!a&&a.channel!==0;
 const node=$('suplaNode'),tep=$('suplaTempEp'),hep=$('suplaHumEp');
 const summary=$('suplaConfigSummary'),button=$('saveSupla');
 const tempLabel=document.querySelector('label[for="suplaTempEp"]');
 const humLabel=document.querySelector('label[for="suplaHumEp"]');
 if(extra){
   node.value=String(a.node);tep.value=String(a.ep);hep.value=String(a.aux);
   summary.textContent='Podgląd przypisania — kanał SUPLA '+a.channel+' (bez edycji)';
   $('suplaSelectedStatus').textContent='Kanał SUPLA '+a.channel+' → numer czujnika Matter '+a.node+'. Przypisanie jest trwałe. Zmiana nie jest dostępna.';
 }else{
   // Restore the REAL channel 0 values on return from another channel.
   // Never overwrite an unsaved first-time setup form during background refresh.
   if(suplaPreviewWasExtra&&u.configured){
     node.value=String(u.node);tep.value=String(u.temperature_endpoint);
     hep.value=String(u.humidity_endpoint);
   }
   summary.textContent='Ustawienia połączenia SUPLA i kanału 0';
   $('suplaSelectedStatus').textContent=a
     ?'Kanał SUPLA 0 → numer czujnika Matter '+a.node+'. '+
  ((s.devices||[]).some(d=>Number(d.node)===a.node&&(d.state===0||d.state===2))?
   'Przypisanie aktywnego czujnika.':
   'Przypisany czujnik został usunięty. Kanał 0 pozostanie offline. Nowe czujniki otrzymają kolejne kanały SUPLA.')
     :'Nie przypisano jeszcze kanału 0. Możesz skonfigurować pierwszy czujnik.';
 }
 node.disabled=extra||!!u.configured;
 tep.readOnly=extra||!!u.configured;
 hep.readOnly=extra||!!u.configured;
 if(tempLabel)tempLabel.textContent=extra&&a.type!==1?'Endpoint pomiaru':'Endpoint temperatury';
 if(humLabel)humLabel.textContent=extra&&a.type!==1?'Endpoint pomocniczy':'Endpoint wilgotności';
 button.disabled=extra||!!s.readings?.busy||!!s.matter?.pair_busy||!!s.network?.testing||!!u.restart_required;
 button.hidden=extra;
 suplaPreviewWasExtra=extra;
}
function renderSuplaDeviceList(s){
 const root=$('suplaDeviceList'),u=s.supla_status||{},devices=s.devices||[];
 const assigned=suplaMappings(s);
  const isListed=a=>devices.some(d=>
    Number(d.node)===a.node&&(d.state===0||d.state===2));
 const signature=JSON.stringify(assigned)+JSON.stringify(devices.map(d=>[d.node,d.name,d.matter_model,d.state]));
 if(root.dataset.signature!==signature){
   root.replaceChildren();
   if(!assigned.length){root.textContent='Nie przypisano jeszcze żadnego czujnika. Skonfiguruj kanał 0, następnie dodaj kolejne.';}
   let previousGroup='';
    const ordered=[...assigned].sort((a,b)=>
      Number(isListed(b))-Number(isListed(a))||a.channel-b.channel);
    for(const a of ordered){
      const listed=isListed(a);
      const group=listed?
        'Przypisania czujników z rejestru Matter':
        'Archiwalne przypisania SUPLA';
      if(group!==previousGroup){
        const title=document.createElement('p');
        title.className='hint';
        const strong=document.createElement('strong');
        strong.textContent=group;
        title.append(strong);
        root.append(title);
        previousGroup=group;
      }
     const d=devices.find(v=>
        Number(v.node)===a.node&&(v.state===0||v.state===2));
     const card=document.createElement('button');card.type='button';card.className='supla-assigned';
     card.dataset.channel=String(a.channel);
     const heading=document.createElement('strong');
     heading.textContent=(d?.name||'Usunięty lub nieaktywny czujnik')+' · kanał SUPLA '+a.channel;
     card.append(heading);
     const line=document.createElement('p');line.textContent='Numer czujnika Matter: '+a.node+
        ' · '+(d?.matter_model||'model nieustalony')+
        (listed?' · W rejestrze Matter':
        ' · Nieaktywny — przypisanie zachowane');card.append(line);
     const ep=document.createElement('p');ep.className='hint';
     const typeNames={1:'Temperatura i wilgotność',2:'Ruch',3:'Kontakt',4:'Zalanie',5:'CO₂',6:'PM2.5'};
     ep.textContent=(typeNames[a.type]||'Inny')+' · EP '+a.ep+(a.type===1?'/'+a.aux:'');card.append(ep);
     card.onclick=()=>{
       suplaSelectedChannel=a.channel;
       renderSuplaDeviceList(latest||s);
       showSuplaSelection(latest||s);
       $('suplaConnectionConfig').open=true;
     };
     root.append(card);
   }
   const foot=document.createElement('p');foot.id='suplaChannelSummary';foot.className='hint';root.append(foot);
   root.dataset.signature=signature;
 }
 for(const button of root.querySelectorAll('.supla-assigned')){
   const active=Number(button.dataset.channel)===suplaSelectedChannel;
   button.classList.toggle('selected',active);
   button.setAttribute('aria-pressed',active?'true':'false');
 }
 const foot=$('suplaChannelSummary');
 if(foot)foot.textContent=
    'Czujniki w rejestrze Matter: '+
      devices.filter(d=>d.state===0||d.state===2).length+
    ' · Aktywne przypisania SUPLA: '+assigned.filter(isListed).length+
    ' · Zapisane kanały SUPLA: '+assigned.length+
    ' · Połączenie: '+(u.online?'Online':'Offline');
}

function showSupla(s){const u=s.supla_status;if(!u)return;message('suplaStatus',(u.restart_required?'Trwa zapisywanie kanałów SUPLA. Planowany automatyczny restart bramki: panel może być chwilowo niedostępny. ':'')+(u.online?'Połączono z SUPLA. ':'')+u.message+' (status '+u.status+')');
 if(!suplaLoaded){$('suplaEnabled').checked=u.enabled;$('suplaServer').value=u.server;$('suplaEmail').value=u.email;$('suplaNode').value=u.configured?String(u.node):'';$('suplaTempEp').value=u.temperature_endpoint;$('suplaHumEp').value=u.humidity_endpoint;suplaLoaded=true;}
 updateSuplaNodeOptions(s);
 $('suplaNode').disabled=!!u.configured;
 for(const id of ['suplaTempEp','suplaHumEp'])$(id).readOnly=u.configured;
 $('saveSupla').disabled=!!s.readings?.busy||s.matter.pair_busy||s.network.testing||u.restart_required;
 const names={1:'Temperatura/wilgotność',2:'Ruch',3:'Kontakt',4:'Woda',5:'CO₂',6:'PM2.5'};
 const slots=s.supla_channels||[];
 renderSuplaDeviceList(s);
 showSuplaSelection(s);
 message('extraSuplaStatus',slots.length?slots.map((v,i)=>'Kanał '+(i+1)+': '+(names[v.type]||'?')+', węzeł '+v.node+', EP '+v.endpoint+(v.type===1?'/'+v.aux_endpoint:'')+(v.invert?' (odwrócony)':'')).join('\n'):'Brak dodatkowych kanałów. Kanał 0 pozostaje bez zmian.');
 const picker=$('extraNode'),previous=picker.value;
 // All paired Matter devices remain VISIBLE. Already assigned nodes are
 // selectable for inspection, but cannot be duplicated in SUPLA.
 const mappings=suplaMappings(s);
 const used=new Map();
 for(const a of mappings){
   if(!used.has(a.node))used.set(a.node,[]);
   used.get(a.node).push(a);
 }
 const selectedType=Number($('extraType').value);
 const alreadyMapped=(node,type)=>
   (used.get(Number(node))||[]).some(a=>a.type===Number(type));
 const candidates=(s.devices||[]).filter(d=>(d.state===0||d.state===2)&&Number(d.node)>0);
 const signature=candidates.map(d=>[d.node,d.name,d.state,(used.get(Number(d.node))||[]).map(a=>a.channel).join('/')||'-'].join('|')).join(',');
 if(picker.dataset.ids!==signature){
   const first=document.createElement('option');first.value='';first.textContent='Wybierz sparowany czujnik';
   picker.replaceChildren(first);
   for(const d of candidates){
     const mapped=used.get(Number(d.node))||[];
     const option=document.createElement('option');option.value=String(d.node);
     option.textContent=d.name+' (Matter '+d.node+')'+(mapped.length?' · SUPLA: '+mapped.map(a=>'kanał '+a.channel).join(', '):'');
     picker.append(option);
   }
   const available=candidates.filter(d=>!alreadyMapped(d.node,selectedType));
   picker.value=candidates.some(d=>String(d.node)===previous)?previous:
      (available.length===1?String(available[0].node):'');
   picker.dataset.ids=signature;
   picker.dataset.defaultsFor='';
 }
 const selected=candidates.find(d=>String(d.node)===picker.value);
 const mapped=selected?(used.get(Number(selected.node))||[]):[];
 if(selected&&picker.dataset.defaultsFor!==String(selected.node)){
   if(selected.matter_model==='ALPSTUGA'){
     $('extraType').value=String([5,6].find(t=>!alreadyMapped(selected.node,t))||5);
     $('extraEp').value='1';
     $('extraAuxEp').value='1';
   }else if(selected.matter_model==='TIMMERFLOTTE'){
     $('extraType').value='1';
     $('extraEp').value='1';
     $('extraAuxEp').value='2';
   }
   picker.dataset.defaultsFor=String(selected.node);
 }
 const duplicate=!!selected&&alreadyMapped(selected.node,$('extraType').value);
 $('extraPickerInfo').textContent=!selected
   ?(candidates.length?'Wybierz czujnik Matter.':'Brak sparowanych czujników Matter.')
   :duplicate?'Ten typ pomiaru jest już przypisany do SUPLA. Wybierz inny typ.'
   :mapped.length?'Czujnik ma już inne kanały SUPLA. Możesz dodać nowy typ pomiaru.'
   :'Możesz dodać kanał SUPLA dla tego czujnika.';
 $('extraAdd').disabled=!u.online||!s.matter.ready||!!s.readings?.busy||
   s.matter.pair_busy||s.network.testing||u.restart_required||
   slots.length>=Number(s.supla_channels_limit||127)||!selected||duplicate;
}
$('suplaForm').onsubmit=async e=>{e.preventDefault();try{if(suplaSelectedChannel!==0)throw Error('Kanał dodatkowy jest tylko do podglądu. Wróć do kanału 0, aby zapisać ustawienia SUPLA.');
 const enabled=$('suplaEnabled').checked;
 const server=$('suplaServer').value.trim(),email=$('suplaEmail').value.trim();
 const node=Number($('suplaNode').value),tempEp=Number($('suplaTempEp').value),humEp=Number($('suplaHumEp').value);
 if(!/^[a-zA-Z0-9.-]{1,127}$/.test(server))throw Error('Serwer SUPLA: wpisz sam host, np. svr51.supla.org, bez http://, portu i ukośnika.');
 if(!email.includes('@')||email.length>127||/[^\x21-\x7E]/.test(email))throw Error('SUPLA: sprawdź adres e-mail (bez spacji).');
 if(!Number.isInteger(node)||node<1||node>65535)throw Error('SUPLA: wybierz prawidłowy numer czujnika Matter.');
 const nodePresent=(latest?.devices||[]).some(d=>
  Number(d.node)===node&&(d.state===0||d.state===2));
  const oldMapping=!!latest?.supla_status?.configured &&
    node===Number(latest.supla_status.node);
  if(enabled&&!nodePresent&&!oldMapping)
    throw Error('SUPLA: wybierz sparowany czujnik Matter.');
  const orphaned=enabled&&!nodePresent&&oldMapping;
 if(latest?.supla_status?.configured && node!==Number(latest.supla_status.node))throw Error('SUPLA: nie można zmienić numeru czujnika po pierwszej konfiguracji.');
 if(!confirm(orphaned?
  'Poprzedni czujnik został usunięty. Kanał SUPLA 0 pozostanie offline. Nowe czujniki otrzymają kolejne kanały. Zapisać i uruchomić ponownie?':
  'Zapisać ustawienia SUPLA i uruchomić urządzenie ponownie?'))return;
 await post('/api/supla/config',{enabled:enabled?1:0,server,email,node,temp_ep:tempEp,hum_ep:humEp});
 message('suplaResult','Zapisano konfigurację SUPLA. Trwa restart.');
 }catch(e){message('suplaResult',e.message,true)}};
$('extraNode').addEventListener('change',()=>{$('extraNode').dataset.defaultsFor='';if(latest)showSupla(latest)});
$('extraType').addEventListener('change',()=>{if(latest)showSupla(latest)});
$('extraSuplaForm').onsubmit=async e=>{e.preventDefault();try{
 if(!confirm('Dodać trwale kolejny kanał SUPLA? Numer i typ kanału nie będą mogły być zmienione. Urządzenie uruchomi się ponownie.'))return;
 await post('/api/supla/add-sensor',{node:Number($('extraNode').value),type:Number($('extraType').value),endpoint:Number($('extraEp').value),aux_endpoint:Number($('extraAuxEp').value),invert:$('extraInvert').checked?1:0});
 message('extraSuplaResult','Kanał zapisany. Trwa restart ESP32-C5.');
 }catch(error){message('extraSuplaResult',error.message,true)} };
function message(id,text,error=false){$(id).textContent=text;$(id).classList.toggle('error',error)}
async function post(path,data){if(!token)throw Error('Poczekaj na połączenie z urządzeniem.');const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json','X-M2S-Token':token},body:JSON.stringify(data),cache:'no-store'});const j=await r.json();if(!r.ok)throw Error(j.error||('HTTP '+r.status));return j;}
function describeValue(v){
 const names={1026:'Temperatura',1029:'Wilgotność',1030:'Ruch',69:'BooleanState',1037:'CO₂',1066:'PM2.5'};
 const unit=v.cluster===1026?' °C':v.cluster===1029?' %':v.cluster===1037?' ppm':v.cluster===1066?' μg/m³':'';
 const number=(v.cluster===1030||v.cluster===69)?v.value:v.value/100;
 if(v.cluster===69)return 'Stan kontaktu: '+(v.valid?(v.value===0?'OTWARCIE':'ZAMKNIĘCIE'):'BRAK DANYCH');
 return (names[v.cluster]||('Klaster '+v.cluster))+': '+(v.valid?number.toLocaleString('pl-PL',{maximumFractionDigits:2})+unit:'brak wartości');
}
let selectedNode=0,selectedName='',readPosting=false;

function syncPollSettings(s){
 const select=$('pollNode'), input=$('pollMinutes');
 const nodes=s.devices.filter(d=>d.state===0||d.state===2);
 const signature=nodes.map(d=>d.node+':'+d.name).join('|');
 if(pollNodeSignature!==signature){
   const previous=select.value;select.replaceChildren();
   for(const d of nodes)select.add(new Option(d.name+' (węzeł '+d.node+')',String(d.node)));
   const desired=nodes.some(d=>String(d.node)===previous)?previous:(nodes[0]?String(nodes[0].node):'');
   select.value=desired;pollNodeSignature=signature;
   input.value=nodes.find(d=>String(d.node)===desired)?.poll_interval_min||1;
 }
 if(document.activeElement!==input){
   const sensor=nodes.find(d=>String(d.node)===select.value);
   if(sensor)input.value=sensor.poll_interval_min||1;
 }
 $('pollSave').disabled=!nodes.length||!!s.readings?.busy||!!s.matter?.pair_busy||!!s.network?.testing;
}
$('pollNode').onchange=()=>{const d=latest?.devices.find(d=>String(d.node)===$('pollNode').value);if(d)$('pollMinutes').value=d.poll_interval_min||1;};
$('pollForm').onsubmit=async e=>{
 e.preventDefault();const minutes=Number($('pollMinutes').value),node=Number($('pollNode').value);
 try{
   if(!Number.isInteger(node)||!Number.isInteger(minutes)||minutes<1||minutes>1440)throw Error('Podaj interwał od 1 do 1440 pełnych minut.');
   await post('/api/matter/poll-interval',{node,minutes});
   message('pollResult','Zapisano: węzeł '+node+' · co '+minutes+' minut. Bez restartu; harmonogram został przesunięty. Bateria: nie częściej niż co godzinę.');
   await refresh();
 }catch(error){message('pollResult',error.message,true)}
};
const widgetCache=new Map(), nextRead=new Map();
let autoPosting=false,removePosting=false,nextGlobalRead=0,lastUptime=0;
function updateWidgets(s){
 if(s.uptime_s<lastUptime){widgetCache.clear();nextRead.clear();}
 lastUptime=s.uptime_s;
 for(const d of s.devices){const r=d.readings;if(r)widgetCache.set(d.node,{state:r.state,busy:r.busy,values:r.values.map(v=>({...v,at:s.uptime_s-v.age_s})),battery:r.battery_valid?{percent:r.battery_percent,at:s.uptime_s-r.battery_age_s}:null,previous:r.state>=3});}
 const active=new Set(s.devices.map(d=>d.node));
 for(const id of widgetCache.keys())if(!active.has(id))widgetCache.delete(id);
 const box=$('sensorWidgets');box.replaceChildren();
 for(const d of s.devices){
   const card=document.createElement('article');card.className='card';
   const title=document.createElement('button');title.type='button';title.className='secondary';title.textContent=d.name;title.onclick=()=>selectSensor(d);card.append(title);
   const c=widgetCache.get(d.node);
   const model=String(d.matter_model||'')+' '+String(d.name||'');
   const assigned=(s.supla_channels||[]).find(ch=>Number(ch.node)===Number(d.node));
   const motion=/MYGGSPRAY/i.test(model)||Number(assigned?.type)===2;
   const water=/KLIPPBOK/i.test(model)||Number(assigned?.type)===4;
   const contact=/MYGGBETT/i.test(model)||Number(assigned?.type)===3;
   if(motion||water||contact){
     const cluster=motion?1030:69;
     const v=(c?.values||[]).find(v=>Number(v.cluster)===cluster && v.valid);
     const age=v?Math.max(0,Math.floor(s.uptime_s-v.at)):Infinity;
     const fresh=!!v && age<=900 && d.state===2 && !!s.matter?.ready;
     const alarm=fresh&&(contact?Number(v.value)===0:Number(v.value)!==0);
     const label=motion?'MYGGSPRAY':water?'KLIPPBOK':'MYGGBETT';
     const words=motion?(alarm?'WYKRYTO RUCH':'BRAK RUCHU'):
       water?(alarm?'WYKRYTO ZALANIE':'BRAK ZALANIA'):
       (alarm?'OTWARTE':'ZAMKNIĘTE');
     const state=document.createElement('p');
     state.className='notice '+(fresh?'ok':'hint');
     state.setAttribute('role','status');
     state.textContent=label+' — '+(fresh?words:'BRAK AKTUALNYCH DANYCH');
     card.append(state);
     const detail=document.createElement('p');detail.className='hint';
     const sub=d.subscription||{};
     detail.textContent=(sub.reporting?'Aktywna subskrypcja Matter':'Subskrypcja nieaktywna — dostępny odczyt okresowy')+
       (fresh?' · raport/odczyt '+age+' s temu':'');
     card.append(detail);
   }
   if(/ALPSTUGA/i.test(model)){
     const temp=(c?.values||[]).find(v=>Number(v.cluster)===1026&&v.valid);
     const hum=(c?.values||[]).find(v=>Number(v.cluster)===1029&&v.valid);
     const th=document.createElement('p');th.className='notice';
     th.textContent='ALPSTUGA — Temperatura: '+(temp?(Number(temp.value)/100).toLocaleString('pl-PL')+' °C':'brak danych')+
       ' · Wilgotność: '+(hum?(Number(hum.value)/100).toLocaleString('pl-PL')+' %':'brak danych');
     card.append(th);
   }
   const info=document.createElement('p');info.className='hint';info.textContent='Węzeł '+d.node+' · '+(d.matter_model||'oczekiwanie na VID/PID')+' · interwał '+(d.poll_interval_min||1)+' min · '+(!c||c.state===0?'Oczekiwanie na pierwszy odczyt':c.busy?'Odczytywanie…':c.state>=3?'Brak pełnej odpowiedzi — zaplanowano ponowienie':c.values.length?'Ostatnia odpowiedź Matter (niekoniecznie nowy pomiar)':'Brak obsługiwanych pomiarów');card.append(info);
   for(const v of c?.values||[]){const line=document.createElement('p');const age=Math.max(0,Math.floor(s.uptime_s-v.at));const stale=age>Math.max(900,2*(d.poll_interval_min||1)*60);line.textContent=describeValue(v)+' · EP '+v.endpoint+' · '+age+' s od odpowiedzi Matter · wartość nie zmieniła się od '+(v.unchanged_s??age)+' s'+(stale?' (NIEAKTUALNY ODCZYT)':c.previous||c.state>=3?' (poprzedni lub częściowy odczyt)':'');if(stale)line.classList.add('error');card.append(line);}
   {const line=document.createElement('p');line.className='hint';if(c?.battery){const age=Math.max(0,Math.floor(s.uptime_s-c.battery.at));line.textContent='Bateria: '+c.battery.percent.toLocaleString('pl-PL',{maximumFractionDigits:1})+'% · '+age+' s temu'+(age>86400?' (stary odczyt)':'');}else{line.textContent='Bateria: oczekiwanie na pierwszy poprawny odczyt (ponowienie co 5 min).';}card.append(line);}
   const del=document.createElement('button');del.type='button';del.className='danger';del.textContent='Usuń z panelu';del.disabled=!!s.readings?.busy||!!s.matter.pair_busy||s.network.testing||removePosting||autoPosting||readPosting||d.state===1;del.onclick=()=>removeSensor(d);card.append(del);box.append(card);
 }
 $('autoStatus').textContent=s.readings?.busy?'Trwa odczyt węzła '+s.readings.node+'.':'MYGGBETT / MYGGSPRAY / KLIPPBOK: stany po raporcie Matter (odświeżenie WWW do około 2,5 s). Odczyt okresowy jest awaryjny; bateria sprawdzana osobno.';
}
async function removeSensor(d){
 if(removePosting||autoPosting||readPosting)return;
 removePosting=true;
 try{
   if(!confirm('Usunąć „'+d.name+'” (węzeł '+d.node+') z panelu? Zatrzyma to jego automatyczne odczyty. Parowanie Matter pozostanie. Wpis można przywrócić przez „Pokaż czujnik sparowany wcześniej przez konsolę”.'))return;
   await post('/api/devices/remove',{node:d.node});widgetCache.delete(d.node);nextRead.delete(d.node);
   if(selectedNode===d.node){selectedNode=0;$('sensorDetail').hidden=true;}
   message('adoptResult','Usunięto wpis węzła '+d.node+'. Parowanie Matter zachowano.');
 }catch(e){message('adoptResult',e.message,true)}finally{removePosting=false;nextGlobalRead=Date.now()+10000;await refresh();}
}
function showReadings(s){
 if(!selectedNode)return;
 $('sensorDetail').hidden=false;$('sensorTitle').textContent=selectedName+' · węzeł '+selectedNode;
 const r=s?.devices.find(d=>d.node===selectedNode)?.readings;
 $('readSensor').disabled=readPosting||!!s?.readings?.busy||!s?.matter.ready||!!s?.matter.pair_busy||!!s?.network.testing;
 $('readValues').replaceChildren();
 if(!r||r.node!==selectedNode){message('readStatus',r?.busy?'Trwa odczyt innego czujnika. Poczekaj.':'Naciśnij Odśwież, aby pobrać pomiary.');return;}
 const states={0:'Brak odczytu.',1:'Oczekiwanie na czujnik… '+Math.floor(r.elapsed_s)+' s (limit panelu: 90 s).',2:r.values.length?'Odczyt zakończony.':'Brak obsługiwanych pomiarów w tej odpowiedzi.',3:'Odczyt nie powiódł się lub odpowiedź była niepełna. Sprawdź dostępność czujnika i ponów.',4:r.busy?'Brak odpowiedzi w ciągu 90 s. Oczekiwanie na zakończenie operacji Matter; przycisk odblokuje się po jej zakończeniu.':'Przekroczono czas oczekiwania. Możesz ponowić odczyt.'};
 message('readStatus',states[r.state]||'Nieznany stan.',r.state>=3);
 for(const v of r.values){const line=document.createElement('p');line.textContent=describeValue(v)+' · endpoint '+v.endpoint+' · odebrano '+Math.floor(v.age_s)+' s temu'+(r.state===1?' (odczyt trwa)':r.state>=3?' (wynik częściowy)':'');$('readValues').append(line);}
 {const line=document.createElement('p');if(r.battery_valid){line.textContent='Bateria · endpoint 0: '+r.battery_percent.toLocaleString('pl-PL',{maximumFractionDigits:1})+'% · odebrano '+Math.floor(r.battery_age_s)+' s temu'+(r.battery_age_s>86400?' (stary odczyt)':'');}else{line.className='hint';line.textContent='Bateria: jeszcze nie odebrano wartości; próba zostanie ponowiona automatycznie.';}$('readValues').append(line);}
}
async function readSelected(){if(!selectedNode||readPosting||autoPosting||removePosting)return;readPosting=true;$('readSensor').disabled=true;try{await post('/api/matter/read',{node:selectedNode});message('readStatus','Odczyt zlecony. Oczekiwanie na czujnik…');}catch(e){message('readStatus',e.message,true)}finally{readPosting=false;await refresh();}}
function selectSensor(d){selectedNode=d.node;selectedName=d.name;showReadings(latest);if(latest?.matter.ready&&!latest?.readings?.busy&&!latest?.matter.pair_busy&&!latest?.network.testing)readSelected();}
$('readSensor').onclick=readSelected;
const pairStates=['Brak próby w tej sesji','Parowanie trwa…','Czujnik został sparowany.','Parowanie nie powiodło się.','Parowanie anulowane.'];

function renderThreadSignal(s) {
 const box = $('threadSignal');
 box.replaceChildren();

 const state = s.thread_signal_status;

 if (state !== 'ok') {
   const p = document.createElement('p');
   p.textContent =
     state === 'lock_timeout'
       ? 'Odczyt Thread chwilowo niedostępny.'
       : state === 'not_router'
       ? 'ESP32-C5 nie jest teraz liderem ani routerem Thread.'
       : 'OpenThread jeszcze nie działa.';
   box.append(p);
   return;
 }

 const children = s.thread_children || [];

 if (!children.length) {
   box.textContent =
     'Brak bezpośrednich dzieci Thread. Czujnik może być połączony przez inny router.';
   return;
 }

 const unique =
   s.devices?.length === 1 && children.length === 1;

 for (const c of children) {
   const row = document.createElement('div');
   row.style.marginBottom = '10px';

   const title = document.createElement('strong');
   title.textContent = unique
     ? s.devices[0].name +
       ' · jedyne dziecko Thread (przypisanie prawdopodobne)'
     : 'Dziecko Thread · RLOC16 0x' +
       Number(c.rloc16).toString(16).padStart(4,'0').toUpperCase();
   row.append(title);

   const line = document.createElement('p');
   const fmt = x =>
     Number.isFinite(x) ? x + ' dBm' : 'brak danych';

   line.textContent =
     'RSSI średnie: ' + fmt(c.average_rssi_dbm) +
     ' · ostatnie: ' + fmt(c.last_rssi_dbm) +
     ' · jakość: ' + c.link_quality_in + '/3' +
     ' · kontakt: ' + c.last_heard_s + ' s temu' +
     (c.sleepy ? ' · czujnik usypiany' : '');
   row.append(line);

   const meter = document.createElement('meter');
   meter.min = 0;
   meter.max = 3;
   meter.value = Math.max(0, Math.min(3, c.link_quality_in));
   meter.style.width = 'min(250px,100%)';
   meter.setAttribute('aria-label','Jakość łącza Thread');
   row.append(meter);

   const extra = document.createElement('p');
   extra.className = 'hint';
   extra.textContent =
     'RLOC16: 0x' +
     Number(c.rloc16).toString(16).padStart(4,'0').toUpperCase() +
     ' · EUI-64: ' + c.ext_addr +
     ' · limit nieaktywności: ' + c.timeout_s + ' s';
   row.append(extra);

   box.append(row);
 }

 if (s.thread_children_truncated) {
   const p = document.createElement('p');
   p.textContent = 'Lista ograniczona do 8 dzieci Thread.';
   box.append(p);
 }

 if (!unique) {
   const p = document.createElement('p');
   p.className = 'hint';
   p.textContent =
     'Przy wielu urządzeniach nie przypisujemy pomiaru do konkretnego numeru Matter bez identyfikacji.';
   box.append(p);
 }
}


/* STAGE11C_LAYOUT: only four gateway status icons; no sensor preview/chart. */
function sdStatus(name,on){
 const tile=$('sd-'+name);
 tile.classList.toggle('is-up',!!on);
 $('sd-'+name+'-state').textContent=on?'Online':'Offline';
}
function sdRender(s){
 const thread=['leader','router','child'].includes(s.thread_role);
 sdStatus('wifi',!!s.network?.connected);
 sdStatus('thread',thread);
 sdStatus('matter',!!s.matter?.ready);
 sdStatus('supla',!!s.supla_status?.online);
 const allUp=!!s.network?.connected&&thread&&!!s.matter?.ready&&!!s.supla_status?.online;
 $('online').textContent=allUp?'SYSTEM OK':'CZĘŚĆ USŁUG OFFLINE';
 $('online').classList.toggle('is-muted',!allUp);
}
$('sd-qr-toggle').onclick=()=>{
 const input=$('qr');const reveal=input.type==='password';input.type=reveal?'text':'password';
 $('sd-qr-toggle').textContent=reveal?'Ukryj treść QR':'Pokaż treść QR';
 $('sd-qr-toggle').setAttribute('aria-pressed',reveal?'true':'false');
};
$('sd-qr-copy').onclick=async()=>{
 const input=$('qr'),value=input.value.trim();
 if(!value){$('sd-qr-info').textContent='Najpierw wklej treść MT:…';return;}
 try{
  if(navigator.clipboard&&window.isSecureContext)await navigator.clipboard.writeText(value);
  else {const type=input.type;input.type='text';input.focus();input.select();const ok=document.execCommand('copy');input.type=type;input.setSelectionRange(0,0);if(!ok)throw Error('copy blocked');}
  $('sd-qr-info').textContent='Skopiowano kod QR do schowka. Traktuj go jak poufny kod parowania.';
 }catch(e){$('sd-qr-info').textContent='Przeglądarka zablokowała schowek. Użyj Pokaż treść QR i skopiuj ręcznie.';}
};

async function refresh(){if(refreshBusy||scanBusy)return;refreshBusy=true;try{const ctl=new AbortController();const timeout=setTimeout(()=>ctl.abort(),5000);let r,s;try{r=await fetch('/api/status',{cache:'no-store',signal:ctl.signal});if(!r.ok)throw Error('HTTP '+r.status);s=await r.json();}finally{clearTimeout(timeout)}latest=s;token=s.token;$('online').textContent='● Połączono';$('ip').textContent='IP: '+s.network.ipv4;$('thread').textContent='Thread: '+s.thread_role;$('ram').textContent='RAM: '+s.internal_kib+' KiB';$('controller').textContent='Kontroler: '+(s.matter.ready?'gotowy':s.matter.start_error?'błąd startu':s.matter.start_requested?'uruchamianie':'wyłączony');$('start').disabled=s.matter.start_requested||!s.network.connected||s.network.ap_active;$('pair').disabled=!s.matter.ready||s.matter.pair_busy||s.network.testing||s.ble_scan?.active||!['leader','router','child'].includes(s.thread_role);$('pairCancel').disabled=!s.matter.pair_busy;$('saveWifi').disabled=s.network.testing||s.matter.pair_busy;$('scan').disabled=s.network.testing||s.matter.pair_busy||scanBusy;$('scanFull').disabled=$('scan').disabled;
$('bleStart').disabled=!s.ble_scan?.available||s.ble_scan?.active||!s.matter.ready||s.matter.pair_busy||s.readings?.busy||s.network.testing||s.internal_kib<14;
$('bleStop').disabled=!s.ble_scan?.active;
$('pauseReads').disabled=!!s.readings_paused||!!s.network?.testing;
$('runAllReads').disabled=!!s.network?.testing||!s.matter.ready;
message('readControlStatus',s.readings_paused?'Automatyczne odczyty: WSTRZYMANE.':'Automatyczne odczyty: AKTYWNE.');
const ns=s.network;
const validStaIp=!!ns.connected && /^\d{1,3}(?:\.\d{1,3}){3}$/.test(ns.ipv4||'') && ns.ipv4!=='0.0.0.0';
$('newWifiIp').textContent=validStaIp?ns.ipv4:(ns.testing?'Trwa przydzielanie adresu IP...':'Brak adresu IP z routera');
$('newWifiIpLink').hidden=!validStaIp;
if(validStaIp)$('newWifiIpLink').href='http://'+ns.ipv4+'/';
const states={setup_ap:'Połącz się z siecią konfiguracyjną i zapisz Wi-Fi.',connecting:'Łączenie z zapisaną siecią…',queued:'Przygotowanie testu nowej sieci…',testing:'Szybkie łączenie z nową siecią (do 25 sekund)…',test_failed:'Test nieudany. Przywrócono poprzednie ustawienia; sieć konfiguracji pozostaje dostępna.',save_failed:'Nie udało się zapisać ustawień.',recovery_ap:'Brak połączenia z routerem. Sieć konfiguracji jest dostępna.',connected:'Połączenie sprawdzone, ustawienia zapisane.',ap_failed:'Nie udało się uruchomić sieci konfiguracji.'};if(!scanBusy && Date.now()>scanNoticeUntil)message('wifiResult',(ns.connected?'Połączono z '+ns.ssid+'. ':'')+(states[ns.state]||ns.state),ns.state.includes('failed'));$('connection').hidden=!ns.ap_active;if(ns.ap_active)$('connection').textContent='Sieć konfiguracji: '+ns.ap_ssid+' · hasło 12345678 · http://192.168.4.1'+(ns.connected?'\nPo udanym zapisie sieć konfiguracji zostanie wyłączona po około 15 s. Otwórz http://'+ns.ipv4+' w domowej sieci.':'');
message('pairResult',s.matter.pair_busy?'Parowanie trwa. Poczekaj; nie uruchamiaj ponownie.':(s.matter.pair_result?pairStates[s.matter.pair_result]:(s.matter.start_error?'Błąd uruchomienia kontrolera. Zapisz log i uruchom urządzenie ponownie.':s.matter.ready?'Gotowy do dodawania czujnika.':'Kontroler uruchomi się po połączeniu Wi-Fi i zamknięciu sieci konfiguracji.')),s.matter.pair_result===3);
const body=$('devices');body.replaceChildren();for(const d of s.devices){const tr=document.createElement('tr');for(const v of [d.name,d.node,['Zapisany — sprawdź odczytem','Parowanie','Sparowany','Próba nieudana'][d.state]||'Nieznany']){const td=document.createElement('td');if(tr.children.length===0){const b=document.createElement('button');b.type='button';b.className='secondary';b.textContent=v;b.onclick=()=>selectSensor(d);td.append(b)}else td.textContent=v;tr.append(td)}body.append(tr)}if(!s.devices.length){const tr=document.createElement('tr');const td=document.createElement('td');td.colSpan=3;td.textContent='Brak wpisów. Dodaj nowy czujnik lub pokaż wcześniej sparowany.';tr.append(td);body.append(tr)}
syncPollSettings(s);showReadings(s);updateWidgets(s);renderThreadSignal(s);showSupla(s);sdRender(s);
}catch(e){$('online').textContent='● Brak łączności';$('online').classList.add('is-muted');for(const key of ['wifi','thread','matter','supla'])sdStatus(key,false);if(selectedNode){$('readSensor').disabled=true;message('readStatus','Brak łączności z panelem. Wyświetlone wartości mogą być nieaktualne.',true);}}finally{refreshBusy=false}}
function decodeQR(input){const q=input.replace(/\s/g,'').toUpperCase();if(!q.startsWith('MT:'))throw Error('Kod QR musi zaczynać się od MT:.');const alphabet='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.';const encoded=q.slice(3),bytes=[];if(encoded.length>512)throw Error('Zbyt długi kod QR.');for(let i=0;i<encoded.length;i+=5){const chunk=encoded.slice(i,i+5),size={2:1,4:2,5:3}[chunk.length];if(!size)throw Error('Nieprawidłowa długość QR.');let v=0,m=1;for(const c of chunk){const d=alphabet.indexOf(c);if(d<0)throw Error('Nieprawidłowy znak w QR.');v+=d*m;m*=38}if(v>=2**(8*size))throw Error('Nieprawidłowy QR.');for(let j=0;j<size;j++){bytes.push(v&255);v=Math.floor(v/256)}}if(bytes.length<11)throw Error('Niepełny QR.');let bits=0n;for(let i=10;i>=0;i--)bits=(bits<<8n)|BigInt(bytes[i]);if((bits&7n)!==0n || ((bits>>84n)&15n)!==0n)throw Error('Nieobsługiwana wersja QR.');return {pin:Number((bits>>57n)&((1n<<27n)-1n)),discriminator:Number((bits>>45n)&4095n)}}
$('pauseReads').onclick=async()=>{try{await post('/api/matter/read-control',{action:'pause'});await refresh();message('readControlStatus','Wstrzymano nowe odczyty. Trwający odczyt może się jeszcze zakończyć.')}catch(e){message('readControlStatus',e.message,true)}};
$('runAllReads').onclick=async()=>{try{await post('/api/matter/read-control',{action:'run_all'});await refresh();message('readControlStatus','Zlecono odczyt wszystkich zapisanych czujników. Odczyty wykonywane są kolejno.')}catch(e){message('readControlStatus',e.message,true)}};
$('wifiForm').onsubmit=async e=>{e.preventDefault();try{const ssid=$('ssid').value,password=$('password').value;if(utf8(ssid)<1||utf8(ssid)>32)throw Error('SSID musi mieć 1–32 bajty.');if(utf8(password)>63||(password.length&&utf8(password)<8))throw Error('Hasło musi mieć 8–63 bajty lub być puste.');if(latest?.network.connected&&!confirm('Zmienić sieć Wi-Fi? Panel może chwilowo stracić połączenie.'))return;await post('/api/wifi',{ssid,password});$('password').value='';message('wifiResult','Trwa łączenie z wybraną siecią. Nowy adres IP pojawi się poniżej przycisku.');$('newWifiIp').textContent='Trwa przydzielanie adresu IP...';$('newWifiIpLink').hidden=true;}catch(e){message('wifiResult',e.message,true)}};
$('start').onclick=async()=>{try{await post('/api/matter/start',{});refresh()}catch(e){message('pairResult',e.message,true)}};
$('pairCancel').onclick=async()=>{try{$('pairCancel').disabled=true;await post('/api/matter/pair/cancel',{});message('pairResult','Zatrzymywanie parowania…');refresh()}catch(e){message('pairResult',e.message,true);refresh()}};
$('pairForm').onsubmit=async e=>{e.preventDefault();try{const name=$('name').value.trim();if(!name||utf8(name)>48)throw Error('Nazwa musi mieć 1–48 bajtów.');let params;if($('qr').value.trim())params=decodeQR($('qr').value);else if($('manualCode').value.trim()){params={manual_code:$('manualCode').value.trim()};if($('disc').value!=='')params.selected_discriminator=Number($('disc').value);message('pairResult','Sprawdzam kod i listę ogłoszeń BLE Matter…');}else throw Error('Wpisz 11-cyfrowy kod Matter lub kod QR MT:.');const r=await post('/api/matter/pair',{name,...params});$('qr').value='';$('qr').type='password';$('sd-qr-toggle').textContent='Pokaż treść QR';$('sd-qr-toggle').setAttribute('aria-pressed','false');$('manualCode').value='';$('disc').value='';message('pairResult','Rozpoczęto dodawanie czujnika '+r.node+'.');refresh()}catch(e){message('pairResult',e.message,true)}};
$('adoptForm').onsubmit=async e=>{e.preventDefault();try{await post('/api/devices/adopt',{name:$('existingName').value.trim(),node:Number($('existingNode').value)});message('adoptResult','Dodano wpis do listy. Parowania nie zmieniono.');refresh()}catch(e){message('adoptResult',e.message,true)}};
$('reset').onclick=async()=>{try{if($('resetText').value!=='USUN WSZYSTKO')throw Error('Wpisz dokładnie USUN WSZYSTKO.');if(!confirm('Usunąć Wi-Fi, Thread i wszystkie dane Matter?'))return;await post('/api/factory-reset',{confirm:$('resetText').value});message('resetResult','Reset zaplanowany; trwa bezpieczne zakończenie bieżącego odczytu (do około 100 s). Połącz się z Matter2Supla-XXXX, hasło 12345678, i otwórz 192.168.4.1.')}catch(e){message('resetResult',e.message,true)}};
async function doWifiScan(full){
  if(scanBusy)return;
  scanBusy=true;$('scan').disabled=true;$('scanFull').disabled=true;
  message('wifiResult',full?'Pełne wyszukiwanie Wi-Fi...':'Szybkie wyszukiwanie Wi-Fi (cel 3–5 s)...');
  try{
    await post('/api/wifi/scan',{mode:full?'full':'fast'});
    let consecutiveFailures=0, lastNetworkError='';
    for(let attempt=0;attempt<(full?40:16);attempt++){
      await new Promise(resolve=>setTimeout(resolve,1000));
      try{
        const ctl=new AbortController();const t=setTimeout(()=>ctl.abort(),6000);
        let r;
        try{r=await fetch('/api/wifi/scan',{cache:'no-store',signal:ctl.signal});}
        finally{clearTimeout(t)}
        if(!r.ok)throw Error('HTTP '+r.status);
        const j=await r.json();
        if(!Array.isArray(j.networks))throw Error('Nieprawidłowa odpowiedź API');
        if(j.busy){consecutiveFailures=0;continue;}
        if(j.error_code && j.error_code!==0)throw Error('Błąd skanera: '+(j.error||j.error_code));
        const select=$('networks');select.replaceChildren(new Option('Wybierz sieć…',''));
        for(const n of j.networks){if(!n.ssid)continue;const band=n.channel>14?'5 GHz':n.channel>0?'2,4 GHz':'Wi-Fi';select.add(new Option(n.ssid+' · '+band+' · '+n.rssi+' dBm'+(n.secure?' · zabezpieczona':''),n.ssid));}
        select.hidden=false;
        scanNoticeUntil=Date.now()+20000;
        message('wifiResult','Skanowanie zakończone. Wykryto '+j.networks.length+' punktów dostępowych.');
        return;
      }catch(e){
        if(String(e.message).startsWith('Błąd skanera:'))throw e;
        lastNetworkError=e.message||String(e);consecutiveFailures++;
        if(consecutiveFailures>=6)throw Error('Utracono dostęp do AP podczas skanowania: '+lastNetworkError+'. Połącz się ponownie z Matter2Supla i odśwież panel.');
      }
    }
    throw Error('Przekroczono czas oczekiwania na skanowanie Wi-Fi.');
  }catch(e){scanNoticeUntil=Date.now()+25000;message('wifiResult',e.message||String(e),true);}
  finally{scanBusy=false;$('scan').disabled=false;$('scanFull').disabled=false;refresh();}
}
$('scan').onclick=()=>doWifiScan(false);
$('scanFull').onclick=()=>doWifiScan(true);
$('networks').onchange=()=>{$('ssid').value=$('networks').value};refresh();setInterval(refresh,2500);

/* S7.13 visual navigation: existing forms, IDs, APIs and NVS logic remain unchanged. */
(function(){
 const names={home:['Przegląd systemu','Stan bramki i najważniejsze ustawienia.'],wifi:['Sieć Wi-Fi','Połącz Matter2Supla z siecią domową.'],pair:['Parowanie Matter','Wybierz discriminator BLE i wpisz 11-cyfrowy kod.'],supla:['Integracja SUPLA','Powiąż urządzenia Matter z kanałami SUPLA Cloud.'],sensors:['Czujniki Matter','Odczyty, bateria, interwały i diagnostyka Thread.'],system:['System','Ustawienia administracyjne i operacje serwisowe.'],help:['Pomoc','Przewodnik parowania Matter i dodawania czujnika do SUPLA CLOUD.']};
 const sections=Array.from(document.querySelectorAll('#settingsPanel > section'));
 let groups={};
 for(const section of sections){
  const heading=(section.querySelector('h2')?.textContent||'').trim();
  let panel='system';
  if(heading==='Pomoc · Dodawanie czujników Matter do SUPLA')panel='help';
  else if(heading==='Sieć Wi-Fi')panel='wifi';
  else if(heading==='Parowanie Matter')panel='pair';
  else if(heading.includes('Wykrywanie')||heading.includes('BLE')||heading.includes('discriminator'))panel='ble';
  else if(heading==='Integracja SUPLA')panel='supla';
  else if(heading==='Odczyt czujników')panel='sensors';
  else if(heading.includes('Siła sygnału Thread'))panel='thread';
  else if(heading.includes('kanały SUPLA'))panel='extra';
  section.dataset.panel=panel;
  groups[panel]=(groups[panel]||[]).concat(section);
 }
 function openTab(tab){
  if(!names[tab])tab='home';
  const show=tab==='help'?['help']:tab==='home'?['wifi','pair','ble','sensors','supla','system']:tab==='pair'?['pair','ble']:tab==='supla'?['supla','extra']:tab==='sensors'?['sensors','thread']:[tab];
  for(const section of sections)section.hidden=!show.includes(section.dataset.panel);
  for(const btn of document.querySelectorAll('.m2s-tab'))btn.setAttribute('aria-selected',btn.dataset.tab===tab?'true':'false');
  document.getElementById('m2sPageTitle').textContent=names[tab][0];
  document.getElementById('m2sPageDesc').textContent=names[tab][1];
  document.querySelector('.sd-wrap').hidden=tab!=='home';
  if(tab==='supla' && document.getElementById('suplaConnectionConfig'))document.getElementById('suplaConnectionConfig').open=true;
  document.getElementById('settingsPanel').dataset.activeTab=tab;
  document.getElementById('settingsPanel').classList.toggle('m2s-section-view',tab!=='home');
 }
 for(const btn of document.querySelectorAll('.m2s-tab'))btn.addEventListener('click',()=>{openTab(btn.dataset.tab);window.scrollTo({top:0,behavior:'smooth'});});
 document.querySelector('a[href="#settingsPanel"]')?.addEventListener('click',(e)=>{e.preventDefault();openTab('wifi');});
 document.getElementById('helpBle').onclick=()=>{document.getElementById('helpBleSteps').hidden=false;document.getElementById('helpQrSteps').hidden=true;document.getElementById('helpBle').setAttribute('aria-pressed','true');document.getElementById('helpQr').setAttribute('aria-pressed','false')};
 document.getElementById('helpQr').onclick=()=>{document.getElementById('helpBleSteps').hidden=true;document.getElementById('helpQrSteps').hidden=false;document.getElementById('helpBle').setAttribute('aria-pressed','false');document.getElementById('helpQr').setAttribute('aria-pressed','true')};
 document.getElementById('helpSupla').onclick=()=>document.getElementById('helpSuplaSteps').scrollIntoView({behavior:'smooth',block:'start'});
 for(const link of document.querySelectorAll('[data-help-open],[data-help-go]'))link.addEventListener('click',e=>{e.preventDefault();openTab(link.dataset.helpOpen||link.dataset.helpGo);window.scrollTo({top:0,behavior:'smooth'})});
 openTab('home');
 window.m2sOpenTab=openTab;
})();
</script></body></html>
)M2SHTML";
static esp_err_t json_reply(httpd_req_t *req,cJSON *object,const char *status="200 OK"){
    if(!object)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON allocation failed");
    char *encoded=cJSON_PrintUnformatted(object);cJSON_Delete(object);
    if(!encoded)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON allocation failed");
    httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    esp_err_t e=httpd_resp_send(req,encoded,HTTPD_RESP_USE_STRLEN);cJSON_free(encoded);return e;
}
static esp_err_t error_reply(httpd_req_t *req,const char *message,const char *status="400 Bad Request"){
    cJSON *j=cJSON_CreateObject();if(j)cJSON_AddStringToObject(j,"error",message);return json_reply(req,j,status);
}
static bool allowed_host(httpd_req_t *req){
    char host[96]{};if(httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK)return false;
    char *port=strchr(host,':');if(port){if(strcmp(port,":80")!=0)return false;*port=0;}
    m2s_network_status_t net{};m2s_network_status(&net);
    return strcmp(host,"192.168.4.1")==0 || (net.connected && strcmp(host,net.ipv4)==0) || strcmp(host,"esp-ot-br.local")==0;
}
static bool authorized(httpd_req_t *req){
    char supplied[40]{};if(!allowed_host(req) || httpd_req_get_hdr_value_str(req,"X-M2S-Token",supplied,sizeof(supplied))!=ESP_OK || strlen(supplied)!=32)return false;
    unsigned difference=0;for(unsigned i=0;i<32;i++)difference|=supplied[i]^token[i];if(difference)return false;
    char site[32]{};if(httpd_req_get_hdr_value_str(req,"Sec-Fetch-Site",site,sizeof(site))==ESP_OK && strcmp(site,"cross-site")==0)return false;
    char type[80]{};if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK || strncmp(type,"application/json",16)!=0)return false;
    return true;
}
static cJSON *receive_json(httpd_req_t *req){
    if(req->content_len<2 || req->content_len>1024)return nullptr;
    auto *body=static_cast<char*>(heap_caps_calloc(req->content_len+1,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!body)return nullptr;
    size_t count=0;int timeouts=0;
    while(count<req->content_len){int n=httpd_req_recv(req,body+count,req->content_len-count);if(n==HTTPD_SOCK_ERR_TIMEOUT && timeouts++<2)continue;if(n<=0){heap_caps_free(body);return nullptr;}count+=n;}
    // All form requests are flat objects. Bound parser recursion before cJSON.
    bool quoted=false, escaped=false, valid=true;int depth=0;
    for(size_t i=0;i<count;i++){
        const char c=body[i];
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}
        else if(c=='"')quoted=true;
        else if(c=='{' || c=='['){if(c=='[' || ++depth>1){valid=false;break;}}
        else if(c=='}')depth--;
    }
    if(quoted || depth!=0 || strstr(body,"\\u0000"))valid=false;
    cJSON *j=valid?cJSON_ParseWithOpts(body,nullptr,true):nullptr;
    volatile char *v=body;for(size_t i=0;i<=req->content_len;i++)v[i]=0;heap_caps_free(body);
    if(j && !cJSON_IsObject(j)){cJSON_Delete(j);return nullptr;}return j;
}
static const char *string_field(cJSON *j,const char *name){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,name);return cJSON_IsString(v)?v->valuestring:nullptr;}
static bool integer_field(cJSON *j,const char *name,uint32_t min,uint32_t max,uint32_t *out){
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,name);if(!cJSON_IsNumber(v)||!std::isfinite(v->valuedouble)||v->valuedouble<min||v->valuedouble>max||std::floor(v->valuedouble)!=v->valuedouble)return false;*out=static_cast<uint32_t>(v->valuedouble);return true;
}
static const char *role_name(otDeviceRole r){switch(r){case OT_DEVICE_ROLE_LEADER:return "leader";case OT_DEVICE_ROLE_ROUTER:return "router";case OT_DEVICE_ROLE_CHILD:return "child";case OT_DEVICE_ROLE_DETACHED:return "detached";default:return "disabled";}}

// Passive snapshot of directly attached Thread children.
// Matter NodeId is not the same identifier as Thread RLOC16.
static void add_thread_signal_json(cJSON *root) {
    otChildInfo kids[8]{};
    unsigned count = 0;
    bool truncated = false;
    const char *state = "lock_timeout";

    if (esp_openthread_lock_acquire(pdMS_TO_TICKS(50))) {
        otInstance *ot = esp_openthread_get_instance();
        if (!ot) {
            state = "not_started";
        } else {
            otDeviceRole role = otThreadGetDeviceRole(ot);
            if (role != OT_DEVICE_ROLE_LEADER &&
                role != OT_DEVICE_ROLE_ROUTER) {
                state = "not_router";
            } else {
                state = "ok";
                uint16_t capacity = otThreadGetMaxAllowedChildren(ot);

                for (uint16_t i = 0; i < capacity; i++) {
                    otChildInfo item{};
                    if (otThreadGetChildInfoByIndex(ot, i, &item)
                        != OT_ERROR_NONE) continue;

                    if (count < 8) kids[count++] = item;
                    else {
                        truncated = true;
                        break;
                    }
                }
            }
        }
        esp_openthread_lock_release();
    }

    cJSON_AddStringToObject(root, "thread_signal_status", state);
    cJSON_AddBoolToObject(root, "thread_children_truncated",
                          truncated);

    cJSON *array = cJSON_AddArrayToObject(root, "thread_children");
    if (!array) return;

    for (unsigned i = 0; i < count; i++) {
        const otChildInfo &child = kids[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;

        char extaddr[17]{};
        for (unsigned x = 0; x < 8; x++) {
            snprintf(extaddr + 2*x, sizeof(extaddr) - 2*x,
                     "%02X", (unsigned)child.mExtAddress.m8[x]);
        }

        cJSON_AddStringToObject(item, "ext_addr", extaddr);
        cJSON_AddNumberToObject(item, "rloc16", child.mRloc16);
        cJSON_AddNumberToObject(item, "last_heard_s", child.mAge);
        cJSON_AddNumberToObject(item, "timeout_s", child.mTimeout);
        cJSON_AddNumberToObject(item, "link_quality_in",
                                child.mLinkQualityIn);

        if (child.mAverageRssi < 0 && child.mAverageRssi > -128)
            cJSON_AddNumberToObject(item, "average_rssi_dbm",
                                    child.mAverageRssi);
        else
            cJSON_AddNullToObject(item, "average_rssi_dbm");

        if (child.mLastRssi < 0 && child.mLastRssi > -128)
            cJSON_AddNumberToObject(item, "last_rssi_dbm",
                                    child.mLastRssi);
        else
            cJSON_AddNullToObject(item, "last_rssi_dbm");

        cJSON_AddBoolToObject(item, "sleepy",
                              !child.mRxOnWhenIdle);
        cJSON_AddItemToArray(array, item);
    }
}

#include "m2s_web_panel_gzip.inc"

static esp_err_t index_get(httpd_req_t *req){
    if(!allowed_host(req))return error_reply(req,"Nieprawidlowy adres panelu.","403 Forbidden");
    httpd_resp_set_type(req,"text/html; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Frame-Options","DENY");httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; img-src 'self' data:; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    // Gzip greatly reduces radio/TCP time for the 56-KB built-in page.
    // Respect Accept-Encoding: PowerShell and legacy clients can still receive raw HTML.
    char enc[128]{};
    if(httpd_req_get_hdr_value_str(req,"Accept-Encoding",enc,sizeof(enc))==ESP_OK && strstr(enc,"gzip")){
        httpd_resp_set_hdr(req,"Content-Encoding","gzip");
        httpd_resp_set_hdr(req,"Vary","Accept-Encoding");
        return httpd_resp_send(req,reinterpret_cast<const char *>(m2s_panel_gzip),sizeof(m2s_panel_gzip));
    }
    return httpd_resp_send(req,html,HTTPD_RESP_USE_STRLEN);
}
static cJSON *readings_json(const m2s_readings_t &r){
 cJSON *j=cJSON_CreateObject();if(!j)return nullptr;const int64_t now=esp_timer_get_time();
 cJSON_AddNumberToObject(j,"node",r.node);cJSON_AddBoolToObject(j,"busy",r.busy);cJSON_AddNumberToObject(j,"state",r.state);cJSON_AddNumberToObject(j,"elapsed_s",r.started_us?(now-r.started_us)/1000000:0);
 cJSON_AddBoolToObject(j,"identity_valid",r.vendor_valid&&r.product_valid);
 if(r.vendor_valid)cJSON_AddNumberToObject(j,"vendor_id",r.vendor_id);
 if(r.product_valid)cJSON_AddNumberToObject(j,"product_id",r.product_id);
 cJSON *values=cJSON_AddArrayToObject(j,"values");if(!values){cJSON_Delete(j);return nullptr;}
 for(int i=0;i<r.count;i++){const auto &v=r.values[i];cJSON *o=cJSON_CreateObject();if(!o)continue;cJSON_AddNumberToObject(o,"endpoint",v.endpoint);cJSON_AddNumberToObject(o,"cluster",v.cluster);cJSON_AddNumberToObject(o,"value",v.value);cJSON_AddBoolToObject(o,"valid",v.valid);cJSON_AddNumberToObject(o,"age_s",(now-v.received_us)/1000000);cJSON_AddNumberToObject(o,"unchanged_s",(now-v.changed_us)/1000000);cJSON_AddItemToArray(values,o);}
 cJSON_AddBoolToObject(j,"battery_valid",r.battery_valid);
 if(r.battery_valid){cJSON_AddNumberToObject(j,"battery_percent",r.battery_raw/2.0);cJSON_AddNumberToObject(j,"battery_age_s",(now-r.battery_received_us)/1000000);}
 return j;
}
static esp_err_t status_get(httpd_req_t *req){
    if(!allowed_host(req))return error_reply(req,"Nieprawidlowy adres panelu.","403 Forbidden");
    cJSON *j=cJSON_CreateObject();if(!j)return json_reply(req,nullptr);
    cJSON_AddStringToObject(j,"firmware","0.9.3-stage11e-myggebett-rt1");cJSON_AddStringToObject(j,"token",token);
    const char *role="unavailable";
    if(esp_openthread_lock_acquire(pdMS_TO_TICKS(100))){otInstance *instance=esp_openthread_get_instance();if(instance)role=role_name(otThreadGetDeviceRole(instance));esp_openthread_lock_release();}
    cJSON_AddStringToObject(j,"thread_role",role);
    cJSON_AddNumberToObject(j,"internal_kib",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024);
    cJSON_AddNumberToObject(j,"largest_kib",heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024);
    cJSON_AddNumberToObject(j,"psram_kib",heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024);
    cJSON_AddNumberToObject(j,"uptime_s",esp_timer_get_time()/1000000LL);
    m2s_network_status_t net{};m2s_network_status(&net);
    // Keep the useful fields from the old diagnostics API.
    cJSON_AddStringToObject(j,"ipv4",net.ipv4);cJSON_AddStringToObject(j,"wifi",net.connected?"connected":"disconnected");
    esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");esp_netif_t *backbone=esp_openthread_get_backbone_netif();
    cJSON_AddBoolToObject(j,"same_backbone",sta && sta==backbone);
    cJSON_AddNumberToObject(j,"wifi_index",sta?esp_netif_get_netif_impl_index(sta):-1);
    cJSON_AddNumberToObject(j,"backbone_index",backbone?esp_netif_get_netif_impl_index(backbone):-1);
    m2s_supla_status_t ss{};m2s_supla_status(&ss);cJSON_AddBoolToObject(j,"supla",ss.online);
    cJSON *su=cJSON_AddObjectToObject(j,"supla_status");if(!su){cJSON_Delete(j);return json_reply(req,nullptr);}
    cJSON_AddNumberToObject(j,"supla_channels_limit",M2S_SUPLA_EXTRA_LIMIT);
    cJSON_AddBoolToObject(su,"enabled",ss.enabled);cJSON_AddBoolToObject(su,"configured",ss.configured);cJSON_AddBoolToObject(su,"online",ss.online);cJSON_AddBoolToObject(su,"restart_required",ss.restart_required);
    cJSON_AddNumberToObject(su,"node",ss.node);cJSON_AddNumberToObject(su,"temperature_endpoint",ss.temperature_endpoint);cJSON_AddNumberToObject(su,"humidity_endpoint",ss.humidity_endpoint);cJSON_AddNumberToObject(su,"status",ss.status);cJSON_AddStringToObject(su,"server",ss.server);cJSON_AddStringToObject(su,"email",ss.email);cJSON_AddStringToObject(su,"message",ss.message);
    m2s_supla_sensor_slot_t slots[M2S_SUPLA_EXTRA_LIMIT]{};
    const size_t slotCount=m2s_supla_slots_snapshot(slots,M2S_SUPLA_EXTRA_LIMIT);
    cJSON *sc=cJSON_AddArrayToObject(j,"supla_channels");if(!sc){cJSON_Delete(j);return json_reply(req,nullptr);}
    for(size_t i=0;i<slotCount;i++){
      cJSON *s=cJSON_CreateObject();if(!s)continue;
      cJSON_AddNumberToObject(s,"channel",i+1);cJSON_AddNumberToObject(s,"node",slots[i].node);
      cJSON_AddNumberToObject(s,"type",slots[i].type);cJSON_AddNumberToObject(s,"endpoint",slots[i].endpoint);
      cJSON_AddNumberToObject(s,"aux_endpoint",slots[i].aux_endpoint);cJSON_AddBoolToObject(s,"invert",slots[i].invert);
      cJSON_AddItemToArray(sc,s);
    }
    cJSON *n=cJSON_AddObjectToObject(j,"network");if(!n){cJSON_Delete(j);return json_reply(req,nullptr);}
    cJSON_AddBoolToObject(n,"connected",net.connected);cJSON_AddBoolToObject(n,"ap_active",net.ap_active);cJSON_AddBoolToObject(n,"configured",net.configured);cJSON_AddBoolToObject(n,"testing",net.testing);
    cJSON_AddBoolToObject(n,"backbone_ready",net.backbone_ready);cJSON_AddStringToObject(n,"ipv4",net.ipv4);cJSON_AddStringToObject(n,"ssid",net.ssid);cJSON_AddStringToObject(n,"ap_ssid",net.ap_ssid);cJSON_AddStringToObject(n,"state",net.state);cJSON_AddNumberToObject(n,"last_error",net.last_error);
    m2s_matter_status_t ms{};m2s_matter_get_status(&ms);cJSON *m=cJSON_AddObjectToObject(j,"matter");if(!m){cJSON_Delete(j);return json_reply(req,nullptr);}
    cJSON_AddBoolToObject(m,"started",ms.started);cJSON_AddBoolToObject(m,"start_requested",ms.start_requested);cJSON_AddBoolToObject(m,"ready",ms.ready);cJSON_AddNumberToObject(m,"start_error",ms.start_error);cJSON_AddBoolToObject(m,"pair_busy",ms.pair_busy);cJSON_AddNumberToObject(m,"pair_result",ms.pair_result);
    m2s_ble_scan_snapshot_t ble{};m2s_ble_scan_snapshot(&ble);
    cJSON *bs=cJSON_AddObjectToObject(j,"ble_scan");if(bs){cJSON_AddBoolToObject(bs,"available",ble.available);cJSON_AddBoolToObject(bs,"active",ble.active);cJSON_AddNumberToObject(bs,"count",ble.count);cJSON_AddNumberToObject(bs,"host_error",ble.host_error);}
    m2s_readings_t readings{};m2s_readings_snapshot(&readings);
    cJSON *rd=cJSON_AddObjectToObject(j,"readings");if(!rd){cJSON_Delete(j);return json_reply(req,nullptr);}
    cJSON_AddBoolToObject(j,"readings_paused",m2s_readings_is_paused());
    cJSON_AddNumberToObject(rd,"node",readings.node);cJSON_AddBoolToObject(rd,"busy",readings.busy);cJSON_AddNumberToObject(rd,"state",readings.state);
    const int64_t read_now=esp_timer_get_time();
    cJSON_AddNumberToObject(rd,"elapsed_s",readings.started_us?(read_now-readings.started_us)/1000000:0);
    cJSON *vals=cJSON_AddArrayToObject(rd,"values");if(!vals){cJSON_Delete(j);return json_reply(req,nullptr);}
    for(int i=0;i<readings.count;i++){const auto &v=readings.values[i];cJSON *item=cJSON_CreateObject();if(!item)continue;cJSON_AddNumberToObject(item,"endpoint",v.endpoint);cJSON_AddNumberToObject(item,"cluster",v.cluster);cJSON_AddBoolToObject(item,"valid",v.valid);cJSON_AddNumberToObject(item,"value",v.value);cJSON_AddNumberToObject(item,"age_s",(read_now-v.received_us)/1000000);cJSON_AddNumberToObject(item,"unchanged_s",(read_now-v.changed_us)/1000000);cJSON_AddItemToArray(vals,item);}
    cJSON *devices=cJSON_AddArrayToObject(j,"devices");if(!devices){cJSON_Delete(j);return json_reply(req,nullptr);}
    // Bounded registry; snapshot is under its own lock, never under the OT lock.
    m2s_device_t items[M2S_DEVICE_LIMIT]{};size_t count=m2s_devices_snapshot(items,M2S_DEVICE_LIMIT);
    for(size_t i=0;i<count;i++){cJSON *d=cJSON_CreateObject();if(!d)continue;cJSON_AddStringToObject(d,"name",items[i].name);cJSON_AddNumberToObject(d,"node",items[i].node);cJSON_AddNumberToObject(d,"state",items[i].state);m2s_readings_t cached{};cached.node=items[i].node;m2s_readings_cached(items[i].node,&cached);cJSON_AddStringToObject(d,"matter_model",(cached.vendor_valid&&cached.product_valid)?m2s_ikea_sensor_name(m2s_ikea_sensor(cached.vendor_id,cached.product_id)):"IDENTYFIKACJA W TOKU");cJSON_AddNumberToObject(d,"poll_interval_min",m2s_readings_interval_minutes(items[i].node));m2s_subscription_t si{};m2s_readings_subscription(items[i].node,&si);cJSON *sub=cJSON_AddObjectToObject(d,"subscription");if(sub){cJSON_AddBoolToObject(sub,"requested",si.requested);cJSON_AddBoolToObject(sub,"reporting",si.reporting);cJSON_AddNumberToObject(sub,"report_count",si.report_count);if(si.report_us>0)cJSON_AddNumberToObject(sub,"last_report_age_s",(esp_timer_get_time()-si.report_us)/1000000);}cJSON *cr=readings_json(cached);if(cr)cJSON_AddItemToObject(d,"readings",cr);cJSON_AddItemToArray(devices,d);}
    add_thread_signal_json(j);
    return json_reply(req,j);
}
static esp_err_t scan_get(httpd_req_t *req){
    if(!allowed_host(req))return error_reply(req,"Nieprawidlowy adres panelu.","403 Forbidden");
    m2s_wifi_ap_t entries[12]{};bool busy=false;int count=m2s_network_scan_results(entries,12,&busy);
    cJSON *j=cJSON_CreateObject();if(!j)return json_reply(req,nullptr);cJSON_AddBoolToObject(j,"busy",busy);const int scan_error=m2s_network_scan_last_error();cJSON_AddNumberToObject(j,"error_code",scan_error);cJSON_AddStringToObject(j,"error",esp_err_to_name((esp_err_t)scan_error));cJSON_AddNumberToObject(j,"phase",m2s_network_scan_phase());cJSON *array=cJSON_AddArrayToObject(j,"networks");if(!array){cJSON_Delete(j);return json_reply(req,nullptr);}
    for(int i=0;i<count;i++){cJSON *n=cJSON_CreateObject();if(!n)continue;cJSON_AddStringToObject(n,"ssid",entries[i].ssid);cJSON_AddNumberToObject(n,"rssi",entries[i].rssi);cJSON_AddBoolToObject(n,"secure",entries[i].secure);cJSON_AddNumberToObject(n,"channel",entries[i].channel);cJSON_AddItemToArray(array,n);}return json_reply(req,j);
}
static esp_err_t matter_ble_scan_get(httpd_req_t *req){
    if(!authorized(req))return error_reply(req,"Brak uprawnien do skanowania.","403 Forbidden");
    m2s_ble_scan_snapshot_t ble{};m2s_ble_scan_snapshot(&ble);
    cJSON *j=cJSON_CreateObject();if(!j)return json_reply(req,nullptr);
    cJSON_AddBoolToObject(j,"available",ble.available);cJSON_AddBoolToObject(j,"active",ble.active);
    cJSON_AddNumberToObject(j,"elapsed_ms",ble.elapsed_ms);cJSON_AddNumberToObject(j,"host_error",ble.host_error);
    cJSON *devices=cJSON_AddArrayToObject(j,"devices");if(!devices){cJSON_Delete(j);return json_reply(req,nullptr);}
    for(unsigned i=0;i<ble.count && i<M2S_BLE_SCAN_LIMIT;i++){
        const auto &b=ble.items[i];cJSON *d=cJSON_CreateObject();if(!d)continue;
        cJSON_AddNumberToObject(d,"vendor_id",b.vendor_id);cJSON_AddNumberToObject(d,"product_id",b.product_id);
        cJSON_AddNumberToObject(d,"discriminator",b.discriminator);cJSON_AddNumberToObject(d,"rssi",b.rssi);
        if(b.vendor_id==4476){const auto model=m2s_ikea_sensor_name(m2s_ikea_sensor(b.vendor_id,b.product_id));cJSON_AddStringToObject(d,"model",model);}
        else cJSON_AddStringToObject(d,"model","Matter (producent nieznany)");
        cJSON_AddItemToArray(devices,d);
    }
    return json_reply(req,j);
}
// S7.10: standard 11-digit manual Matter code, validated without storing PIN.
static bool manual_matter_code(const char *raw,uint32_t *pin_out,uint8_t *short_disc){
    if(!raw || !pin_out || !short_disc)return false;
    char digits[12]{};unsigned len=0;
    for(const char *p=raw;*p;p++){
        if(*p=='-' || *p==' ')continue;
        if(*p<'0'||*p>'9'||len>=11)return false;
        digits[len++]=*p;
    }
    if(len!=11 || digits[0]>'3')return false; // 11-digit form: VID/PID flag absent
    static constexpr uint8_t d[10][10]={
        {0,1,2,3,4,5,6,7,8,9},{1,2,3,4,0,6,7,8,9,5},
        {2,3,4,0,1,7,8,9,5,6},{3,4,0,1,2,8,9,5,6,7},
        {4,0,1,2,3,9,5,6,7,8},{5,9,8,7,6,0,4,3,2,1},
        {6,5,9,8,7,1,0,4,3,2},{7,6,5,9,8,2,1,0,4,3},
        {8,7,6,5,9,3,2,1,0,4},{9,8,7,6,5,4,3,2,1,0}};
    static constexpr uint8_t perm[8][10]={
        {0,1,2,3,4,5,6,7,8,9},{1,5,7,6,2,8,3,0,9,4},
        {5,8,0,3,7,9,6,1,4,2},{8,9,1,6,0,4,3,5,2,7},
        {9,4,5,3,1,2,6,8,7,0},{4,2,8,6,5,7,3,9,0,1},
        {2,7,9,3,8,0,6,4,1,5},{7,0,4,6,9,1,3,2,5,8}};
    uint8_t check=0;
    for(unsigned i=0;i<11;i++)check=d[check][perm[i%8][digits[10-i]-'0']];
    if(check!=0)return false;
    unsigned prefix=0,middle=0,tail=0;
    prefix=digits[0]-'0';
    for(int i=1;i<=5;i++)middle=middle*10u+unsigned(digits[i]-'0');
    for(int i=6;i<=9;i++)tail=tail*10u+unsigned(digits[i]-'0');
    *short_disc=uint8_t(((prefix&3u)<<2)|((middle>>14)&3u));
    *pin_out=((middle&0x3fffu)|(tail<<14));
    return true;
}
static bool thread_attached(){
    bool attached=false;
    if(esp_openthread_lock_acquire(pdMS_TO_TICKS(100))){
        otInstance *instance=esp_openthread_get_instance();
        if(instance){otDeviceRole role=otThreadGetDeviceRole(instance);attached=role==OT_DEVICE_ROLE_LEADER || role==OT_DEVICE_ROLE_ROUTER || role==OT_DEVICE_ROLE_CHILD;}
        esp_openthread_lock_release();
    }
    return attached;
}
static bool valid_pin(uint32_t p){if(!p || p>99999998 || p==12345678 || p==87654321)return false;for(uint32_t d=1;d<=9;d++)if(p==d*11111111u)return false;return true;}
static esp_err_t post_handler(httpd_req_t *req){
    if(!authorized(req))return error_reply(req,"Odswiez panel i ponow operacje.","403 Forbidden");
    cJSON *j=receive_json(req);if(!j)return error_reply(req,"Nieprawidlowe dane formularza.");
    m2s_matter_status_t ms{};m2s_matter_get_status(&ms);m2s_network_status_t ns{};m2s_network_status(&ns);
    m2s_readings_t current_read{};m2s_readings_snapshot(&current_read);
    if(current_read.busy && strcmp(req->uri,"/api/matter/pair/cancel")!=0 && strcmp(req->uri,"/api/wifi/scan")!=0){cJSON_Delete(j);return error_reply(req,"Trwa odczyt czujnika. Poczekaj na zakonczenie.","409 Conflict");}
    esp_err_t result=ESP_ERR_INVALID_ARG;uint32_t node=0;
    if(strcmp(req->uri,"/api/wifi")==0){if(ms.pair_busy)result=ESP_ERR_INVALID_STATE;else result=m2s_network_apply(string_field(j,"ssid"),string_field(j,"password"));}
    else if(strcmp(req->uri,"/api/wifi/scan")==0){result=(ms.pair_busy || m2s_ble_scan_active()) ? ESP_ERR_INVALID_STATE :
            m2s_network_scan_ex(string_field(j,"mode") && strcmp(string_field(j,"mode"),"full")==0);}
    else if(strcmp(req->uri,"/api/matter/ble-scan")==0){
        const char *action=string_field(j,"action");
        if(action && strcmp(action,"stop")==0)result=m2s_ble_scan_stop();
        else if(action && strcmp(action,"start")==0){
            const size_t free_ram=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
            const size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
            result=(!ms.ready || ms.pair_busy || ns.testing || !ns.connected || ns.ap_active || free_ram<14*1024 || largest<10*1024)?ESP_ERR_INVALID_STATE:m2s_ble_scan_start();
        }
    }
    else if(strcmp(req->uri,"/api/matter/start")==0){result=(!ns.connected || ns.ap_active || ns.testing)?ESP_ERR_INVALID_STATE:m2s_matter_request_start();}
    else if(strcmp(req->uri,"/api/matter/pair")==0){
        uint32_t pin=0,disc=0;const char *name=string_field(j,"name");
        if(!ms.ready || ms.pair_busy || m2s_ble_scan_active() || ns.testing || !ns.connected || !thread_attached())result=ESP_ERR_INVALID_STATE;
        else if(name && strlen(name)>=1 && strlen(name)<=48){
            const char *manual=string_field(j,"manual_code");
            bool correct=false;
            if(manual && *manual){
                uint8_t short_disc=0;
                if(manual_matter_code(manual,&pin,&short_disc) && valid_pin(pin)){
                    m2s_ble_scan_snapshot_t ble{};m2s_ble_scan_snapshot(&ble);
                    unsigned matches=0;
                    for(unsigned i=0;i<ble.count && i<M2S_BLE_SCAN_LIMIT;i++){
                        const unsigned full=ble.items[i].discriminator;
                        if(full<=4095 && (full>>8)==short_disc){disc=full;matches++;}
                    }
                    uint32_t selected=0;
                    const bool has_selected=integer_field(j,"selected_discriminator",0,4095,&selected);
                    if(has_selected){
                        bool advertised=false;
                        for(unsigned i=0;i<ble.count && i<M2S_BLE_SCAN_LIMIT;i++)
                            if(ble.items[i].discriminator==selected && (selected>>8)==short_disc) advertised=true;
                        if(advertised){disc=selected;correct=true;}
                        else {result=ESP_ERR_NOT_FOUND;ESP_LOGW(TAG,"S711_SELECTED_BLE_DISCRIMINATOR_MISMATCH selected=%lu",(unsigned long)selected);}
                    }else if(matches==1)correct=true;
                    else{
                        ESP_LOGW(TAG,"S711_MANUAL_CODE_BLE_MATCH count=%u",matches);
                        result=ESP_ERR_NOT_FOUND;
                    }
                }else result=ESP_ERR_INVALID_ARG;
            }else correct=integer_field(j,"pin",1,99999998,&pin) && valid_pin(pin) && integer_field(j,"discriminator",0,4095,&disc);
            if(correct){
                result=m2s_devices_add(name,&node);
                if(result==ESP_OK){result=m2s_matter_request_pair(node,pin,static_cast<uint16_t>(disc));if(result!=ESP_OK)m2s_devices_pair_finished(node,3);}
            }
        }
    }
    else if(strcmp(req->uri,"/api/matter/pair/cancel")==0){
        result=ms.pair_busy?m2s_matter_request_pair_cancel():ESP_ERR_INVALID_STATE;
    }
    else if(strcmp(req->uri,"/api/matter/read")==0){
        if(!ms.ready || ms.pair_busy || ns.testing || ns.ap_active || !ns.connected || !thread_attached())result=ESP_ERR_INVALID_STATE;
        else if(integer_field(j,"node",1,65535,&node)){
            m2s_device_t devices[M2S_DEVICE_LIMIT]{};const size_t count=m2s_devices_snapshot(devices,M2S_DEVICE_LIMIT);
            for(size_t i=0;i<count;i++)if(devices[i].node==node){result=m2s_readings_request(node);break;}
        }
    }
    else if(strcmp(req->uri,"/api/matter/read-control")==0){
        const char *action=string_field(j,"action");
        if(!action || ms.pair_busy || ns.testing)result=ESP_ERR_INVALID_STATE;
        else if(strcmp(action,"pause")==0){m2s_readings_set_paused(true);result=ESP_OK;}
        else if(strcmp(action,"run_all")==0){result=ms.ready?m2s_readings_run_all():ESP_ERR_INVALID_STATE;}
        else result=ESP_ERR_INVALID_ARG;
    }
    else if(strcmp(req->uri,"/api/matter/poll-interval")==0){
        uint32_t minutes=0;
        if(!ms.pair_busy && !ns.testing && integer_field(j,"node",1,65535,&node) &&
           integer_field(j,"minutes",1,1440,&minutes))
            result=m2s_readings_set_interval(node,static_cast<uint16_t>(minutes));
    }
    else if(strcmp(req->uri,"/api/devices/remove")==0){
        if(ms.pair_busy || ns.testing)result=ESP_ERR_INVALID_STATE;
        else if(integer_field(j,"node",1,65535,&node))result=m2s_devices_remove(node);
    }
    else if(strcmp(req->uri,"/api/supla/config")==0){
        uint32_t enabled=0,te=0,he=0;
        if(ms.pair_busy||ns.testing)result=ESP_ERR_INVALID_STATE;
        else if(integer_field(j,"enabled",0,1,&enabled)&&integer_field(j,"node",1,65535,&node)&&integer_field(j,"temp_ep",0,65534,&te)&&integer_field(j,"hum_ep",0,65534,&he))result=m2s_supla_configure(enabled,string_field(j,"server"),string_field(j,"email"),node,te,he);
    }
    else if(strcmp(req->uri,"/api/supla/add-sensor")==0){
        uint32_t type=0,ep=0,aux=0,invert=0;
        if(ms.pair_busy||ns.testing)result=ESP_ERR_INVALID_STATE;
        else if(integer_field(j,"node",1,65535,&node)&&integer_field(j,"type",1,6,&type)&&
                integer_field(j,"endpoint",0,65534,&ep)&&integer_field(j,"aux_endpoint",0,65534,&aux)&&
                integer_field(j,"invert",0,1,&invert))
            result=m2s_supla_add_sensor(node,(uint8_t)type,(uint16_t)ep,(uint16_t)aux,invert!=0);
    }
    else if(strcmp(req->uri,"/api/devices/adopt")==0){if(integer_field(j,"node",1,65535,&node))result=m2s_devices_adopt(string_field(j,"name"),node);}
    else if(strcmp(req->uri,"/api/factory-reset")==0){
        const char *confirmation=string_field(j,"confirm");
        if(confirmation && strcmp(confirmation,"USUN WSZYSTKO")==0){
            if(ms.pair_busy || ns.testing)result=ESP_ERR_INVALID_STATE;
            else{
                const bool was_paused=m2s_readings_is_paused();
                m2s_readings_set_paused(true);
                result=m2s_network_factory_reset();
                if(result!=ESP_OK && !was_paused)m2s_readings_set_paused(false);
            }
        }
    }
    // Do not retain credentials or PIN in the parsed request tree.
    for(const char *key:{"password","pin","manual_code"}){cJSON *secret=cJSON_GetObjectItemCaseSensitive(j,key);if(cJSON_IsString(secret) && secret->valuestring){volatile char *p=secret->valuestring;size_t len=strlen(secret->valuestring);while(len--)*p++=0;}}
    cJSON_Delete(j);
    if(result!=ESP_OK && strcmp(req->uri,"/api/matter/poll-interval")==0)
        return error_reply(req,result==ESP_ERR_NOT_FOUND?"Nie ma takiego zapisanego czujnika Matter.":result==ESP_ERR_INVALID_ARG?"Interwal musi miec od 1 do 1440 pelnych minut.":"Nie udalo sie zapisac interwalu w NVS. Sprawdz pamiec i sprobuj ponownie.",result==ESP_ERR_NOT_FOUND?"404 Not Found":"400 Bad Request");
    if(result!=ESP_OK && strcmp(req->uri,"/api/supla/config")==0){
        const char *msg=result==ESP_ERR_INVALID_ARG?
          "SUPLA: sprawdz sam host serwera (bez http:// i portu), e-mail, numer juz zapisanego czujnika Matter oraz endpointy. Wi-Fi i PIN Matter nie sa wymagane.":
          result==ESP_ERR_INVALID_STATE?
          "SUPLA: konfiguracja zablokowana - trwa inne zadanie, oczekuje restart lub probujesz zmienic juz przypisany numer czujnika/endpoint.":
          "SUPLA: blad zapisu do NVS. Sprawdz log monitora COM13.";
        return error_reply(req,msg,result==ESP_ERR_INVALID_STATE?"409 Conflict":"400 Bad Request");
    }
    if(result!=ESP_OK && strcmp(req->uri,"/api/supla/add-sensor")==0)
        return error_reply(req,result==ESP_ERR_NO_MEM?"Za malo pamieci RAM na kolejny kanal SUPLA. Konfiguracja bez zmian.":"Nie mozna dodac kanalu: sprawdz model IKEA, node, endpoint, aktualny pomiar, limit kanalow i stan SUPLA. Przypisanie nie zostalo zmienione.",result==ESP_ERR_INVALID_STATE?"409 Conflict":"400 Bad Request");
    if(result!=ESP_OK && strcmp(req->uri,"/api/matter/ble-scan")==0)return error_reply(req,result==ESP_ERR_NOT_SUPPORTED?"Skaner BLE wymaga aktywnego stosu NimBLE.":result==ESP_ERR_INVALID_STATE?"BLE niedostepny: trwa inne zadanie, brak pamieci lub host BLE nie jest gotowy.":"Nie udalo sie zakonczyc skanowania BLE.",result==ESP_ERR_INVALID_STATE?"409 Conflict":"400 Bad Request");
    if(result!=ESP_OK && strcmp(req->uri,"/api/matter/pair/cancel")==0)
        return error_reply(req,"Nie mozna teraz zatrzymac parowania; sprawdz log kontrolera Matter.","409 Conflict");
    // Stage11E: distinguish pairing submission errors.
    // Actual BLE/PASE/commissioning failures are asynchronous.
    if(result!=ESP_OK && strcmp(req->uri,"/api/matter/pair")==0){
        const char *reason=
            !ms.ready?
              "Matter: kontroler nie jest gotowy. Sprawdz stan kontrolera.":
            ms.pair_busy?
              "Matter: trwa juz parowanie innego czujnika.":
            m2s_ble_scan_active()?
              "Matter: skanowanie BLE nadal trwa. Zatrzymaj skaner i sprobuj ponownie.":
            ns.testing?
              "Matter: trwa test polaczenia Wi-Fi.":
            !ns.connected?
              "Matter: bramka nie ma polaczenia Wi-Fi.":
            !thread_attached()?
              "Matter: siec Thread nie jest polaczona. Sprawdz status Thread.":
            result==ESP_ERR_NO_MEM?
              "Matter: brak wolnej pamieci lub miejsca na kolejny czujnik.":
            result==ESP_ERR_NOT_FOUND?
              "Matter: kod 11-cyfrowy zawiera skrocony discriminator. Uruchom skan BLE w panelu i ponow probe, gdy znaleziono dokladnie jedno zgodne urzadzenie. Przy kilku zgodnych uzyj QR.":
            result==ESP_ERR_INVALID_ARG?
              "Matter: kod 11-cyfrowy jest nieprawidlowy (cyfra kontrolna lub PIN), albo PIN / pelny discriminator sa nieprawidlowe.":
            result==ESP_ERR_INVALID_STATE?
              "Matter: kontroler lub BLE jest zajety. Sprawdz status i sprobuj ponownie.":
              "Matter: nie udalo sie rozpoczac parowania. Sprawdz log ESP32-C5.";
        return error_reply(req,reason,
            result==ESP_ERR_INVALID_STATE?"409 Conflict":
            result==ESP_ERR_NO_MEM?"503 Service Unavailable":
            "400 Bad Request");
    }
    if(result!=ESP_OK)return error_reply(req,result==ESP_ERR_NO_MEM?"Brak pamieci lub miejsca na liscie czujnikow.":result==ESP_ERR_INVALID_STATE?"Operacja niedostepna: trwa inne zadanie lub kontroler/siec nie jest gotowy.":"Sprawdz nazwe, haslo i dane parowania.",result==ESP_ERR_INVALID_STATE?"409 Conflict":"400 Bad Request");
    cJSON *reply=cJSON_CreateObject();if(reply){cJSON_AddBoolToObject(reply,"accepted",true);if(node)cJSON_AddNumberToObject(reply,"node",node);}return json_reply(req,reply,"202 Accepted");
}
static void start_server(){
    if(server)return;
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.stack_size=8192;config.max_uri_handlers=20;config.max_open_sockets=3;config.lru_purge_enable=true;config.recv_wait_timeout=3;config.send_wait_timeout=5;
    if(httpd_start(&server,&config)!=ESP_OK){server=nullptr;ESP_LOGE(TAG,"HTTP start failed");return;}
    struct Route{const char *path;httpd_method_t method;esp_err_t(*handler)(httpd_req_t*);};
    const Route routes[]={{"/",HTTP_GET,index_get},{"/api/status",HTTP_GET,status_get},{"/api/wifi/scan",HTTP_GET,scan_get},{"/api/matter/ble-scan",HTTP_GET,matter_ble_scan_get},{"/api/wifi",HTTP_POST,post_handler},{"/api/wifi/scan",HTTP_POST,post_handler},{"/api/matter/ble-scan",HTTP_POST,post_handler},{"/api/matter/start",HTTP_POST,post_handler},{"/api/matter/pair",HTTP_POST,post_handler},{"/api/matter/pair/cancel",HTTP_POST,post_handler},{"/api/matter/read",HTTP_POST,post_handler},{"/api/matter/read-control",HTTP_POST,post_handler},{"/api/matter/poll-interval",HTTP_POST,post_handler},{"/api/devices/adopt",HTTP_POST,post_handler},{"/api/supla/config",HTTP_POST,post_handler},{"/api/supla/add-sensor",HTTP_POST,post_handler},{"/api/devices/remove",HTTP_POST,post_handler},{"/api/factory-reset",HTTP_POST,post_handler}};
    for(const auto &r:routes){httpd_uri_t uri{};uri.uri=r.path;uri.method=r.method;uri.handler=r.handler;if(httpd_register_uri_handler(server,&uri)!=ESP_OK){httpd_stop(server);server=nullptr;ESP_LOGE(TAG,"HTTP registration failed");return;}}
    ESP_LOGI(TAG,"Configuration panel ready, port 80");
}
static void network_event(void*,esp_event_base_t base,int32_t id,void*){if((base==IP_EVENT && id==IP_EVENT_STA_GOT_IP)||(base==WIFI_EVENT && id==WIFI_EVENT_AP_START))start_server();}
extern "C" void m2s_web_init(){
    uint8_t random[16];esp_fill_random(random,sizeof(random));for(unsigned i=0;i<16;i++)snprintf(token+i*2,3,"%02x",random[i]);
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,network_event,nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_AP_START,network_event,nullptr));
}
