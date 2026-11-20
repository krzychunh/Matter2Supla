#include "m2s_network.h"
#include "m2s_readings.h"
#include "m2s_supla.h"
#include "m2s_matter_api.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <openthread/ip6.h>
extern "C" void m2s_readings_set_paused(bool paused);
namespace {
constexpr const char *TAG="M2S_WIFI";
constexpr uint32_t kMagic=0x4d325731;
// S7.5: APSTA stays enabled; FAST channel bitmap avoids scanning all DFS channels.
// This avoids mode transitions while a browser client is attached to SoftAP.
constexpr bool kS7DiagnosticApOnly=false; // S7.10: boot must connect saved Wi-Fi
constexpr int64_t kScanCooldownUs=5000000LL;
constexpr int64_t kFastScanTimeoutUs=25000000LL; // driver-default active dwell needs a relaxed timeout
constexpr int64_t kFullScanTimeoutUs=60000000LL;
struct Credentials { uint32_t magic; char ssid[33]; char password[65]; };
Credentials saved{}, trial{};
StaticQueue_t queue_control;
uint8_t queue_storage[sizeof(Credentials)];
QueueHandle_t requests=nullptr;
esp_netif_t *sta=nullptr, *ap=nullptr;
portMUX_TYPE status_mux=portMUX_INITIALIZER_UNLOCKED;
m2s_network_status_t status{};
std::atomic_bool ip_ready{false}, request_busy{false}, reset_requested{false};
std::atomic_bool scan_requested{false}, scan_done_event{false}, scan_full_requested{false};
std::atomic<int> scan_phase{0}, scan_event_code{0}, scan_last_error{ESP_OK};
std::atomic<int64_t> scan_finished_us{0};
// phase: 0 idle; 1 queued; 2 scanning; 3 completed; 4 failed.
m2s_wifi_ap_t scan_results[12]{};
int scan_count=0;
int64_t scan_started_us=0, scan_retry_at_us=0;
bool active_scan_full=false;
unsigned scan_retry_count=0;
int64_t disconnected_at=0, deadline=0, ap_stop_at=0, reset_wait_started_us=0;
bool testing=false, ap_enabled=false, have_saved=false, br_started=false;
// S7.14: retain a usable setup AP when a saved SSID is absent.
// Do not continuously disrupt AP clients with unsuccessful STA scans.
static std::atomic<bool> sta_retry_requested{false};
static std::atomic<unsigned> no_ap_found_streak{0};
static std::atomic<bool> recovery_hold{false};
static std::atomic<int64_t> recovery_hold_since_us{0};
constexpr int64_t kRecoveryProbeIntervalUs = 300000000LL; // 5 min, when no AP clients
void wipe(void *p,size_t n){volatile uint8_t *v=static_cast<volatile uint8_t*>(p);while(n--)*v++=0;}
void state(const char *value,int error=0){portENTER_CRITICAL(&status_mux);snprintf(status.state,sizeof(status.state),"%s",value);status.last_error=error;portEXIT_CRITICAL(&status_mux);}
esp_err_t save(const Credentials &value){
    nvs_handle_t h; esp_err_t e=nvs_open("m2s_wifi",NVS_READWRITE,&h);
    if(e!=ESP_OK)return e;
    e=nvs_set_blob(h,"credentials",&value,sizeof(value));
    if(e==ESP_OK)e=nvs_commit(h);
    nvs_close(h);return e;
}
bool load(){
    nvs_handle_t h;size_t n=sizeof(saved);
    if(nvs_open("m2s_wifi",NVS_READONLY,&h)!=ESP_OK)return false;
    esp_err_t e=nvs_get_blob(h,"credentials",&saved,&n);nvs_close(h);
    return e==ESP_OK && n==sizeof(saved) && saved.magic==kMagic &&
        saved.ssid[0] && memchr(saved.ssid,0,sizeof(saved.ssid)) &&
        memchr(saved.password,0,sizeof(saved.password));
}
// M2S_AP_THREAD_SEPARATION_V1: never run a Thread router with setup AP.
esp_err_t set_thread_radio_enabled(bool enabled) {
    if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))) return ESP_ERR_TIMEOUT;
    otInstance *instance = esp_openthread_get_instance();
    otError error = OT_ERROR_NONE;
    if (!instance) {
        esp_openthread_lock_release();
        return ESP_ERR_INVALID_STATE;
    }
    if (enabled) {
        if (!otDatasetIsCommissioned(instance)) {
            esp_openthread_lock_release();
            return ESP_ERR_INVALID_STATE;
        }
        if (otThreadGetDeviceRole(instance) == OT_DEVICE_ROLE_DISABLED) {
            error = otIp6SetEnabled(instance, true);
            if (error == OT_ERROR_NONE) error = otThreadSetEnabled(instance, true);
        }
    } else {
        if (otThreadGetDeviceRole(instance) != OT_DEVICE_ROLE_DISABLED)
            error = otThreadSetEnabled(instance, false);
        if (error == OT_ERROR_NONE) error = otIp6SetEnabled(instance, false);
    }
    esp_openthread_lock_release();
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Thread radio transition failed: enabled=%d error=%d", enabled, (int)error);
        return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t enable_ap(){
    if(ap_enabled)return ESP_OK;
    esp_err_t thread_error = set_thread_radio_enabled(false);
    if (thread_error != ESP_OK) return thread_error;
    ESP_LOGI(TAG, "Setup AP: Thread and Thread IPv6 paused; Dataset preserved");
    // Configure APSTA BEFORE esp_wifi_start(); keep both interfaces active while scanning.
    // The STA interface remains disconnected until the user submits credentials.
    esp_err_t e=esp_wifi_set_mode(WIFI_MODE_APSTA);
    if(e!=ESP_OK)return e;
    // S7.1: Do not call esp_wifi_set_band_mode() before esp_wifi_start().
    // ESP32-C5 returns ESP_ERR_WIFI_NOT_STARTED and ESP_ERROR_CHECK aborts.
    // SoftAP uses the configured 2.4 GHz channel 1 below; verify at AP_START.
    wifi_config_t cfg{};
    uint8_t mac[6]{};esp_wifi_get_mac(WIFI_IF_STA,mac);
    snprintf(reinterpret_cast<char*>(cfg.ap.ssid),sizeof(cfg.ap.ssid),"Matter2Supla-%02X%02X",mac[4],mac[5]);
    cfg.ap.ssid_len=strlen(reinterpret_cast<char*>(cfg.ap.ssid));
    memcpy(cfg.ap.password,"12345678",8);
    cfg.ap.channel=1;cfg.ap.authmode=WIFI_AUTH_WPA2_PSK;cfg.ap.max_connection=2;
    // S7.8: compatibility setup AP, WPA2-PSK without 802.11w PMF.
    // Clients previously entered repeated SA Query and disassociated (reason 209)
    // immediately after off-channel scanning. Do not negotiate optional PMF here.
    cfg.ap.pmf_cfg.capable=false;cfg.ap.pmf_cfg.required=false;
    e=esp_wifi_set_config(WIFI_IF_AP,&cfg);
    if(e!=ESP_OK){esp_wifi_set_mode(WIFI_MODE_STA);return e;}
    wifi_protocols_t ap_protocols{};
    ap_protocols.ghz_2g = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G;
    ap_protocols.ghz_5g = WIFI_PROTOCOL_11A;
    e = esp_wifi_set_protocols(WIFI_IF_AP, &ap_protocols);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "AP protocol configuration failed: %s", esp_err_to_name(e));
        return e;
    }
    ESP_LOGI(TAG, "Setup AP compatibility mode: 802.11b/g");
    wifi_bandwidths_t ap_bandwidth{};
    ap_bandwidth.ghz_2g = WIFI_BW_HT20;
    ap_bandwidth.ghz_5g = WIFI_BW_HT20;
    e = esp_wifi_set_bandwidths(WIFI_IF_AP, &ap_bandwidth);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "AP bandwidth configuration failed: %s", esp_err_to_name(e));
        return e;
    }
    ESP_LOGI(TAG, "Setup AP bandwidth: 20 MHz");
    ap_enabled=true;
    portENTER_CRITICAL(&status_mux);
    status.ap_active=true;snprintf(status.ap_ssid,sizeof(status.ap_ssid),"%s",cfg.ap.ssid);
    portEXIT_CRITICAL(&status_mux);
    ESP_LOGI(TAG,"Setup AP: %s, http://192.168.4.1",cfg.ap.ssid);
    return ESP_OK;
}
esp_err_t connect(const Credentials &value){
    // A submitted configuration explicitly authorizes STA testing. Allow 5 GHz again.
    if(kS7DiagnosticApOnly){
        esp_err_t be=esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
        if(be!=ESP_OK)ESP_LOGW(TAG,"S7 band AUTO switch failed: %s",esp_err_to_name(be));
    }
    esp_err_t mode_error = esp_wifi_set_mode(
        ap_enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (mode_error != ESP_OK) return mode_error;
    wifi_config_t cfg{};
    memcpy(cfg.sta.ssid,value.ssid,strlen(value.ssid));
    memcpy(cfg.sta.password,value.password,strlen(value.password));
    cfg.sta.scan_method=WIFI_FAST_SCAN; // S7.9: direct connection to known SSID; avoid full scan
    cfg.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.authmode=value.password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable=true;
    portENTER_CRITICAL(&status_mux);
    snprintf(status.ssid,sizeof(status.ssid),"%s",value.ssid);status.connected=false;
    portEXIT_CRITICAL(&status_mux);
    ip_ready.store(false);
    // An explicit new network selection always exits missing-network recovery.
    recovery_hold.store(false);
    recovery_hold_since_us.store(0);
    no_ap_found_streak.store(0);
    sta_retry_requested.store(false);
    esp_wifi_disconnect();
    esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&cfg);
    wipe(&cfg,sizeof(cfg));
    return e==ESP_OK?esp_wifi_connect():e;
}

void event(void*,esp_event_base_t base,int32_t id,void *data){
    if(base==WIFI_EVENT && id==WIFI_EVENT_AP_START){
        uint8_t channel=0;wifi_second_chan_t second=WIFI_SECOND_CHAN_NONE;
        esp_err_t ce=esp_wifi_get_channel(&channel,&second);
        ESP_LOGW(TAG,"S75_DIAG_AP_START channel=%u channel_status=%s",(unsigned)channel,esp_err_to_name(ce));
    }
    if(base==WIFI_EVENT && id==WIFI_EVENT_AP_STOP){ESP_LOGW(TAG,"DIAG_AP_STOP");}
    if(base==WIFI_EVENT && id==WIFI_EVENT_AP_STACONNECTED){
        auto *ev=static_cast<wifi_event_ap_staconnected_t*>(data);
        ESP_LOGW(TAG,"S75_DIAG_AP_CLIENT_CONNECTED aid=%u",ev?(unsigned)ev->aid:0U);
    }
    if(base==WIFI_EVENT && id==WIFI_EVENT_AP_STADISCONNECTED){
        auto *ev=static_cast<wifi_event_ap_stadisconnected_t*>(data);
        ESP_LOGW(TAG,"S78_AP_CLIENT_DISCONNECTED aid=%u reason=%u scan_phase=%d sta_ip=%u",
                 ev?(unsigned)ev->aid:0U,ev?(unsigned)ev->reason:0U,
                 scan_phase.load(),(unsigned)ip_ready.load());
    }
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){auto *ev=static_cast<wifi_event_sta_disconnected_t*>(data);ESP_LOGW(TAG,"DIAG_STA_DISCONNECTED reason=%u",ev?(unsigned)ev->reason:0U);}

    if(base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE && scan_phase.load()==2){
        ESP_LOGW(TAG,"S75_SCAN_DONE_EVENT elapsed_ms=%lld",(long long)((esp_timer_get_time()-scan_started_us)/1000));
        auto *ev=static_cast<wifi_event_sta_scan_done_t*>(data);
        scan_event_code.store(ev ? (int)ev->status : -1);
        scan_done_event.store(true);
    }
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_CONNECTED){
        esp_netif_create_ip6_linklocal(sta);
        uint8_t channel=0;wifi_second_chan_t second=WIFI_SECOND_CHAN_NONE;
        const esp_err_t ce=esp_wifi_get_channel(&channel,&second);
        ESP_LOGW(TAG,"S78_STA_CHANNEL_CHANGE channel=%u query=%s ap_active=%u; APSTA may follow STA channel",
                 (unsigned)channel,esp_err_to_name(ce),(unsigned)ap_enabled);
    }
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){
        auto *ev=static_cast<wifi_event_sta_disconnected_t*>(data);
        if(ev && ev->reason==WIFI_REASON_NO_AP_FOUND && !testing && have_saved){
            const unsigned failures=no_ap_found_streak.fetch_add(1)+1;
            if(failures>=3){
                if(!recovery_hold.exchange(true)){
                    recovery_hold_since_us.store(esp_timer_get_time());
                    ESP_LOGW(TAG,"S714_RECOVERY_AP_HOLD reason=NO_AP_FOUND failures=%u; setup AP remains available at http://192.168.4.1",failures);
                }
                sta_retry_requested.store(false);
            }else sta_retry_requested.store(true);
        }else{
            if(ev && ev->reason!=WIFI_REASON_NO_AP_FOUND)no_ap_found_streak.store(0);
            sta_retry_requested.store(!recovery_hold.load());
        }
        ip_ready.store(false);
        portENTER_CRITICAL(&status_mux);status.connected=false;snprintf(status.ipv4,sizeof(status.ipv4),"0.0.0.0");portEXIT_CRITICAL(&status_mux);
    }
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){
        auto *ev=static_cast<ip_event_got_ip_t*>(data);
        if(ev->esp_netif!=sta)return;
        portENTER_CRITICAL(&status_mux);status.connected=true;snprintf(status.ipv4,sizeof(status.ipv4),IPSTR,IP2STR(&ev->ip_info.ip));portEXIT_CRITICAL(&status_mux);
        ip_ready.store(true);
        no_ap_found_streak.store(0);
        recovery_hold.store(false);
        recovery_hold_since_us.store(0);
        sta_retry_requested.store(false);
        ESP_LOGW(TAG,"S79_STA_GOT_IP ssid=%s ip=" IPSTR " gateway=" IPSTR " mask=" IPSTR, status.ssid,IP2STR(&ev->ip_info.ip),IP2STR(&ev->ip_info.gw),IP2STR(&ev->ip_info.netmask));
    }
}
void worker(void*){
    int64_t next_retry=esp_timer_get_time()+10000000LL;
    int64_t recovery_ap_since=0;
    for(;;){
        const int64_t now=esp_timer_get_time();
        if(reset_requested.load()){
            // S7.9: do not erase NVS while Matter ReadClient has a live transaction.
            m2s_readings_t current_read{};m2s_readings_snapshot(&current_read);
            if(current_read.busy){
                if(!reset_wait_started_us)reset_wait_started_us=now;
                if(now-reset_wait_started_us<100000000LL){vTaskDelay(pdMS_TO_TICKS(100));continue;}
                ESP_LOGE(TAG,"S79_FACTORY_RESET_TIMEOUT: Matter read still busy; NVS preserved");
                state("reset_wait_timeout",ESP_ERR_TIMEOUT);reset_requested.store(false);reset_wait_started_us=0; m2s_readings_set_paused(false);
            } else {
            vTaskDelay(pdMS_TO_TICKS(1800));
            esp_wifi_stop();
            const esp_err_t e=nvs_flash_erase();
            if(e==ESP_OK)esp_restart();
            state("reset_failed",e);reset_requested.store(false);reset_wait_started_us=0;
            }
        }
        if(!scan_requested.load() && request_busy.load() && !testing && uxQueueMessagesWaiting(requests)){
            if(enable_ap()!=ESP_OK){
                Credentials discard{};xQueueReceive(requests,&discard,0);wipe(&discard,sizeof(discard));
                state("ap_failed",ESP_FAIL);request_busy.store(false);
            }else{
                // Return the HTTP response before the STA connection changes.
                vTaskDelay(pdMS_TO_TICKS(200)); // S7.9: HTTP reply settle; previously 1500ms
                if(xQueueReceive(requests,&trial,0)==pdTRUE){
                    wipe(queue_storage,sizeof(queue_storage));
                    testing=true;deadline=esp_timer_get_time()+25000000; // S7.9 known SSID fast connect; 25s bounded fallback
                    portENTER_CRITICAL(&status_mux);status.testing=true;portEXIT_CRITICAL(&status_mux);
                    state("testing");esp_err_t e=connect(trial);if(e!=ESP_OK)deadline=0;
                }
            }
        }
        // S4: async scanning; never block the network/Thread worker on a scan.
        // ESP-IDF signals WIFI_EVENT_SCAN_DONE only for a non-blocking explicit scan.
        if(scan_requested.load() && !testing){
            auto finish_scan = [&](esp_err_t result){
                // Never call esp_wifi_set_mode() here: it disconnects clients from Setup AP.
                if(result!=ESP_OK){
                    portENTER_CRITICAL(&status_mux);scan_count=0;portEXIT_CRITICAL(&status_mux);
                    ESP_LOGW(TAG,"S75_SCAN_FAILED: %s",esp_err_to_name(result));
                }else ESP_LOGI(TAG,"S75_SCAN_FINISHED: %d APs in %lld ms",scan_count,(long long)((esp_timer_get_time()-scan_started_us)/1000));
                scan_last_error.store(result);
                scan_phase.store(result==ESP_OK?3:4);
                scan_retry_count=0;scan_retry_at_us=0;
                scan_finished_us.store(esp_timer_get_time());
                scan_requested.store(false);
            };
            if(scan_phase.load()==1 && !request_busy.load() && now>=scan_retry_at_us){
                wifi_mode_t mode=WIFI_MODE_NULL;
                esp_err_t e=esp_wifi_get_mode(&mode);
                if(e==ESP_OK && mode!=WIFI_MODE_APSTA){
                    ESP_LOGE(TAG,"S75_SCAN_BAD_MODE=%d; APSTA required",(int)mode);
                    e=ESP_ERR_INVALID_STATE;
                }
                if(e==ESP_OK){
                    // FAST: deliberately scan a subset of common 2.4/5 GHz channels.
                    // FULL: all allowed channels; may take tens of seconds (DFS).
                    // ESP-IDF v5.5.6 bitmap entries are BIT values, not channel IDs.
                    // Both modes are asynchronous and retain APSTA throughout.
                    active_scan_full=scan_full_requested.load();
                    wifi_scan_config_t config{};
                    config.scan_type=WIFI_SCAN_TYPE_ACTIVE;
                    // S7.8: 0/0 uses ESP-IDF defaults. Custom 10..30 ms
                    // triggered a Wi-Fi/Bluetooth coexistence warning on ESP32-C5.
                    config.scan_time.active.min=0;
                    config.scan_time.active.max=0;
                    // Spend more time on the home channel while the browser is
                    // connected to setup AP. No Wi-Fi mode changes during scan.
                    config.home_chan_dwell_time=120;
                    config.show_hidden=false;
                    config.channel=0;
                    if(!active_scan_full){
                        config.channel_bitmap.ghz_2_channels =
                            WIFI_CHANNEL_1 | WIFI_CHANNEL_6 | WIFI_CHANNEL_7 | WIFI_CHANNEL_11;
                        config.channel_bitmap.ghz_5_channels =
                            WIFI_CHANNEL_36 | WIFI_CHANNEL_40 | WIFI_CHANNEL_44 | WIFI_CHANNEL_48;
                    }
                    scan_done_event.store(false);
                    scan_event_code.store(0);
                    scan_phase.store(2);
                    scan_started_us=esp_timer_get_time();
                    wifi_sta_list_t ap_clients{};
                    const esp_err_t client_err=esp_wifi_ap_get_sta_list(&ap_clients);
                    ESP_LOGW(TAG,"S78_SCAN_CLIENTS count=%u query=%s PMF_AP_DISABLED=1",
                             client_err==ESP_OK?(unsigned)ap_clients.num:0U,esp_err_to_name(client_err));
                    ESP_LOGW(TAG,"S78_SCAN_BEGIN mode=%s APSTA active=%s home=%u ms timeout=%lld ms",
                             active_scan_full ? "FULL" : "FAST",
                             "ESP_IDF_DEFAULT",
                             (unsigned)config.home_chan_dwell_time,
                             (long long)((active_scan_full ? kFullScanTimeoutUs : kFastScanTimeoutUs)/1000));
                    e=esp_wifi_scan_start(&config,false);
                }
                if(e!=ESP_OK){
                    if(e==ESP_ERR_WIFI_STATE && scan_retry_count++<2){
                        scan_phase.store(1);
                        scan_retry_at_us=esp_timer_get_time()+3000000LL;
                        ESP_LOGW(TAG,"S75_SCAN_BUSY retry in 3s");
                    }else finish_scan(e);
                }else ESP_LOGI(TAG,"S75_SCAN_ASYNC_STARTED");
            }
            if(scan_phase.load()==2){
                const bool completed=scan_done_event.exchange(false);
                const bool timed_out=esp_timer_get_time()-scan_started_us>
                    (active_scan_full ? kFullScanTimeoutUs : kFastScanTimeoutUs);
                if(completed || timed_out){
                    esp_err_t e=ESP_OK;
                    if(timed_out && !completed){
                        e=ESP_ERR_TIMEOUT;
                        ESP_LOGE(TAG,"S75_SCAN_TIMEOUT after %lld ms; aborting scan",(long long)((esp_timer_get_time()-scan_started_us)/1000));
                        esp_wifi_scan_stop();
                        esp_wifi_clear_ap_list();
                    }else if(scan_event_code.load()!=0){
                        e=ESP_FAIL;
                        esp_wifi_clear_ap_list();
                    }else{
                        uint16_t count=12;
                        auto *records=static_cast<wifi_ap_record_t*>(heap_caps_calloc(12,sizeof(wifi_ap_record_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
                        if(!records){e=ESP_ERR_NO_MEM;esp_wifi_clear_ap_list();}
                        else{
                            e=esp_wifi_scan_get_ap_records(&count,records);
                            if(e==ESP_OK){
                                portENTER_CRITICAL(&status_mux);
                                scan_count=0;
                                for(unsigned i=0;i<count;i++){
                                    memcpy(scan_results[i].ssid,records[i].ssid,32);
                                    scan_results[i].ssid[32]=0;
                                    scan_results[i].rssi=records[i].rssi;
                                    scan_results[i].secure=records[i].authmode!=WIFI_AUTH_OPEN;
                                    scan_results[i].channel=records[i].primary;
                                    scan_count++;
                                }
                                portEXIT_CRITICAL(&status_mux);
                            }else esp_wifi_clear_ap_list();
                            heap_caps_free(records);
                        }
                    }
                    finish_scan(e);
                }
            }
        }
        if(testing){
            wifi_ap_record_t connected_ap{};
            if(ip_ready.load() && esp_wifi_sta_get_ap_info(&connected_ap)==ESP_OK &&
               strncmp(reinterpret_cast<const char*>(connected_ap.ssid),trial.ssid,32)==0){
                esp_err_t e=save(trial);
                if(e==ESP_OK){
                    saved=trial;have_saved=true;testing=false;request_busy.store(false);wipe(&trial,sizeof(trial));
                    portENTER_CRITICAL(&status_mux);status.testing=false;status.configured=true;portEXIT_CRITICAL(&status_mux);
                    state("connected");ap_stop_at=esp_timer_get_time()+15000000LL; ESP_LOGW(TAG,"S79_WIFI_CONNECTED new_ip=%s; setup AP closing in 15s",status.ipv4);
                }else{deadline=0;state("save_failed",e);}
            }
            if(testing && esp_timer_get_time()>=deadline){
                testing=false;request_busy.store(false);wipe(&trial,sizeof(trial));
                portENTER_CRITICAL(&status_mux);status.testing=false;portEXIT_CRITICAL(&status_mux);
                state("test_failed",ESP_FAIL);
                if(have_saved)connect(saved);else{esp_wifi_disconnect();ip_ready.store(false);}
                disconnected_at=esp_timer_get_time();next_retry=disconnected_at+10000000;ap_stop_at=0;
            }
        }
        if(ip_ready.load()){
            recovery_ap_since=0;
            disconnected_at=0;
            m2s_network_status_t current{};m2s_network_status(&current);
            if(!testing && !request_busy.load() && (strcmp(current.state,"connecting")==0 || strcmp(current.state,"recovery_ap")==0)){
                state("connected");if(ap_enabled)ap_stop_at=now+15000000LL;
            }
            if(!ap_enabled && !br_started){
                if(esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))){
                    esp_openthread_set_backbone_netif(sta);
                    esp_err_t e=esp_openthread_border_router_init();
                    esp_openthread_lock_release();
                    if(e==ESP_OK){
                        // Create a Thread network only when there is no existing Dataset.
                        if(esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))){
                            otInstance *instance=esp_openthread_get_instance();
                            if(instance && !otDatasetIsCommissioned(instance)){
                                otOperationalDataset dataset{};
                                if(otDatasetCreateNewNetwork(instance,&dataset)==OT_ERROR_NONE && otDatasetSetActive(instance,&dataset)==OT_ERROR_NONE){
                                    otIp6SetEnabled(instance,true);otThreadSetEnabled(instance,true);
                                }
                                wipe(&dataset,sizeof(dataset));
                            }else if(instance && otThreadGetDeviceRole(instance)==OT_DEVICE_ROLE_DISABLED){otIp6SetEnabled(instance,true);otThreadSetEnabled(instance,true);}
                            esp_openthread_lock_release();
                        }
                        br_started=true;portENTER_CRITICAL(&status_mux);status.backbone_ready=true;portEXIT_CRITICAL(&status_mux);}
                    else ESP_LOGE(TAG,"Border router init failed: %d",e);
                }
            }
            if(ap_stop_at && now>=ap_stop_at && !testing && !request_busy.load()){
                if(esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK){ap_enabled=false;ap_stop_at=0;portENTER_CRITICAL(&status_mux);status.ap_active=false;portEXIT_CRITICAL(&status_mux);}
            }
        }else if(!testing && have_saved && !scan_requested.load()){
            // Following repeated NO_AP_FOUND errors keep SoftAP stable while
            // waiting for manual Wi-Fi selection; check for the old SSID only
            // occasionally and only with no web client connected.
            if(recovery_hold.load()){
                if(ap_enabled){
                    state("recovery_ap");
                    ap_stop_at=0;
                }
                const int64_t held_since=recovery_hold_since_us.load();
                if(held_since && now-held_since>=kRecoveryProbeIntervalUs &&
                   !request_busy.load() && !scan_requested.load()){
                    wifi_sta_list_t clients{};
                    if(!ap_enabled || (esp_wifi_ap_get_sta_list(&clients)==ESP_OK && clients.num==0)){
                        recovery_hold.store(false);
                        recovery_hold_since_us.store(0);
                        no_ap_found_streak.store(0);
                        sta_retry_requested.store(true);
                        next_retry=now;
                        ESP_LOGW(TAG,"S714_SAVED_WIFI_PROBE old_ssid=%s; config AP retained",saved.ssid);
                    }
                }
            }
            if(!disconnected_at){
                disconnected_at=now;
                // A previously scheduled AP shutdown must not survive loss of IP.
                ap_stop_at=0;
                if(ap_enabled)state("recovery_ap");
            }
            if(now-disconnected_at>=30000000LL && !ap_enabled){
                const esp_err_t ae=enable_ap();
                if(ae==ESP_OK){
                    const esp_err_t me=esp_wifi_set_mode(WIFI_MODE_APSTA);
                    if(me==ESP_OK){
                        state("recovery_ap");recovery_ap_since=0;
                        ESP_LOGW(TAG,"S714_RECOVERY_AP_ENABLED http://192.168.4.1 (APSTA kept for scan)");
                    }else{ESP_LOGE(TAG,"S714_RECOVERY_AP_MODE_FAILED: %s",esp_err_to_name(me));state("ap_failed",me);}
                }else{ESP_LOGE(TAG,"S714_RECOVERY_AP_FAILED: %s",esp_err_to_name(ae));state("ap_failed",ae);}
            }
            // S7.14: NEVER automatically stop the recovery AP. It must remain
            // available so a user can select another SSID without erasing NVS.
            // S5: do not strand the saved STA in APSTA after its first failed
            // connection. Retrying is allowed with AP enabled, but only while
            // no configuration client and no Wi-Fi scan/credential test is active.
            // Preserve AP availability if a user is configuring the device.
            if(now>=next_retry && sta_retry_requested.load() && !recovery_hold.load() &&
               !scan_requested.load() && !request_busy.load()){
                wifi_sta_list_t clients{};
                const bool ap_client_present=ap_enabled &&
                    (esp_wifi_ap_get_sta_list(&clients)!=ESP_OK || clients.num>0);
                // S7.10: reconnect must not get stuck forever merely because a browser
                // is connected to the setup AP. Retain AP while attempting STA.
                sta_retry_requested.store(false);
                ESP_LOGW(TAG,"S710_STA_RECOVERY_ATTEMPT ap_client=%d",ap_client_present?1:0);
                const esp_err_t e=esp_wifi_connect();
                if(e!=ESP_OK){
                    ESP_LOGW(TAG,"S710_STA_RECOVERY_DEFERRED error=%s",esp_err_to_name(e));
                    sta_retry_requested.store(true);
                }
                next_retry=now+(ap_client_present?15000000LL:10000000LL);
            }
        }
        if(br_started && ip_ready.load() && !ap_enabled && !testing){
            if (set_thread_radio_enabled(true) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            m2s_matter_status_t ms{};m2s_matter_get_status(&ms);
            if(!ms.start_requested)m2s_matter_request_start();
        }
        // S7.6: observe the live Thread child table, not only persisted ChildInfo.
        // Read-only diagnostics, inside the OpenThread lock, at most once / 30 s.
        // Do not reset Thread / SRP / Matter or NVS based on this observation.
        if (br_started && ip_ready.load() && !ap_enabled && !testing) {
            static int64_t next_thread_diag_us = 0;
            if (now >= next_thread_diag_us) {
                next_thread_diag_us = now + 30000000LL;
                if (esp_openthread_lock_acquire(pdMS_TO_TICKS(100))) {
                    otInstance *ot = esp_openthread_get_instance();
                    if (ot) {
                        const unsigned role = static_cast<unsigned>(otThreadGetDeviceRole(ot));
                        const unsigned commissioned = otDatasetIsCommissioned(ot) ? 1U : 0U;
                        unsigned live_children = 0;
                        unsigned restoring_children = 0;
                        for (uint16_t idx = 0; idx < 32; ++idx) {
                            otChildInfo child{};
                            if (otThreadGetChildInfoByIndex(ot, idx, &child) == OT_ERROR_NONE) {
                                ++live_children;
                                if (child.mIsStateRestoring) ++restoring_children;
                                ESP_LOGI("M2S_S76", "THREAD_CHILD index=%u rloc16=0x%04x age_s=%lu timeout_s=%lu lqi=%u restoring=%u",
                                    static_cast<unsigned>(idx), static_cast<unsigned>(child.mRloc16),
                                    static_cast<unsigned long>(child.mAge), static_cast<unsigned long>(child.mTimeout),
                                    static_cast<unsigned>(child.mLinkQualityIn), static_cast<unsigned>(child.mIsStateRestoring));
                            }
                        }
                        ESP_LOGW("M2S_S76", "THREAD_LIVE role=%u commissioned=%u children=%u restoring=%u backbone=%u wifi_ip=%u",
                            role, commissioned, live_children, restoring_children,
                            static_cast<unsigned>(br_started), static_cast<unsigned>(ip_ready.load()));
                        if (live_children == 0) {
                            ESP_LOGW("M2S_S76", "THREAD_NO_DIRECT_CHILDREN: check whether sleepy sensors attach via another router; discovery may fail if none is reachable");
                        }
                    }
                    esp_openthread_lock_release();
                } else {
                    ESP_LOGW("M2S_S76", "THREAD_DIAG_LOCK_BUSY");
                }
            }
        }
        m2s_readings_tick();
        m2s_supla_tick();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
}
extern "C" void m2s_network_status(m2s_network_status_t *out){if(!out)return;portENTER_CRITICAL(&status_mux);*out=status;portEXIT_CRITICAL(&status_mux);}
extern "C" esp_err_t m2s_network_apply(const char *ssid,const char *password){
    if(!ssid || !password || !requests || reset_requested.load() || scan_requested.load())return ESP_ERR_INVALID_STATE;
    size_t sn=strnlen(ssid,33),pn=strnlen(password,65);
    if(sn<1 || sn>32 || pn>63 || (pn>0 && pn<8))return ESP_ERR_INVALID_ARG;
    if(request_busy.exchange(true))return ESP_ERR_INVALID_STATE;
    Credentials value{};value.magic=kMagic;memcpy(value.ssid,ssid,sn);memcpy(value.password,password,pn);
    bool ok=xQueueSend(requests,&value,0)==pdTRUE;wipe(&value,sizeof(value));
    if(!ok){request_busy.store(false);return ESP_ERR_NO_MEM;}state("queued");return ESP_OK;
}
extern "C" esp_err_t m2s_network_scan_ex(bool full){
    if(!requests || request_busy.load() || testing || scan_requested.load())return ESP_ERR_INVALID_STATE;
    const int64_t last=scan_finished_us.load();
    if(last && esp_timer_get_time()-last<kScanCooldownUs){
        ESP_LOGW(TAG,"S75_SCAN_COOLDOWN remaining_ms=%lld",(long long)((kScanCooldownUs-(esp_timer_get_time()-last))/1000));
        return ESP_ERR_INVALID_STATE;
    }
    if(scan_requested.exchange(true))return ESP_ERR_INVALID_STATE;
    scan_full_requested.store(full);
    portENTER_CRITICAL(&status_mux);scan_count=0;portEXIT_CRITICAL(&status_mux);
    scan_last_error.store(ESP_OK);
    scan_phase.store(1);
    ESP_LOGW(TAG,"S75_SCAN_QUEUED mode=%s",full?"FULL":"FAST");
    return ESP_OK;
}
extern "C" esp_err_t m2s_network_scan(){return m2s_network_scan_ex(false);}
extern "C" int m2s_network_scan_last_error(){return scan_last_error.load();}
extern "C" int m2s_network_scan_phase(){return scan_phase.load();}
extern "C" int m2s_network_scan_results(m2s_wifi_ap_t *out,int capacity,bool *busy){
    if(busy)*busy=scan_requested.load();
    if(!out || capacity<0)return 0;
    portENTER_CRITICAL(&status_mux);int n=scan_count<capacity?scan_count:capacity;memcpy(out,scan_results,n*sizeof(*out));portEXIT_CRITICAL(&status_mux);return n;
}
extern "C" esp_err_t m2s_network_factory_reset(){if(!requests || request_busy.load())return ESP_ERR_INVALID_STATE;reset_requested.store(true);return ESP_OK;}
extern "C" esp_err_t m2s_network_start(){
    requests=xQueueCreateStatic(1,sizeof(Credentials),queue_storage,&queue_control);
    have_saved=load();if(!have_saved)wipe(&saved,sizeof(saved));
    portENTER_CRITICAL(&status_mux);status.configured=have_saved;snprintf(status.ipv4,sizeof(status.ipv4),"0.0.0.0");portEXIT_CRITICAL(&status_mux);
    sta=esp_netif_create_default_wifi_sta();ap=esp_netif_create_default_wifi_ap();
    if(!sta || !ap)return ESP_ERR_NO_MEM;
    esp_netif_ip_info_t ip{};IP4_ADDR(&ip.ip,192,168,4,1);IP4_ADDR(&ip.gw,192,168,4,1);IP4_ADDR(&ip.netmask,255,255,255,0);
    esp_netif_dhcps_stop(ap);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap,&ip));ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,nullptr));
    // Configure the protected AP before the radio can advertise it.
    // S5 BOOT-AP: advertise the configuration AP immediately even when
    // credentials are stored in NVS. enable_ap() chooses APSTA for saved Wi-Fi.
    // Do not switch to STA-only before esp_wifi_start(), or AP never broadcasts.
    ESP_ERROR_CHECK(enable_ap());
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGW(TAG,"S714_WIFI_STARTED APSTA=1 MANAGED_STA_RECONNECT=ON NO_AP_HOLD_AFTER_3=ON AP_PMF=OFF");
    ESP_LOGW(TAG,"S710_BOOT_AP_ON ssid=%s saved_wifi=%d; STA connect on boot, AP closes after DHCP + 15s",status.ap_ssid,(int)have_saved);
    if(kS7DiagnosticApOnly){
        state("setup_ap");
        ESP_LOGW(TAG,"S75_DIAG_APSTA_2G_AP channel=1 WPA2 saved_wifi=%d STA_AUTOCONNECT=OFF",(int)have_saved);
    }else if(have_saved){
        state("connecting");
        ESP_LOGW(TAG,"S710_BOOT_STA_CONNECT_START ssid=%s",saved.ssid);
        const esp_err_t e=connect(saved);
        if(e!=ESP_OK){
            ESP_LOGW(TAG,"S710_BOOT_STA_CONNECT_DEFERRED: %s",esp_err_to_name(e));
            sta_retry_requested.store(true);
        }
    }else state("setup_ap");
    if(xTaskCreate(worker,"m2s_wifi",6144,nullptr,3,nullptr)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
}
