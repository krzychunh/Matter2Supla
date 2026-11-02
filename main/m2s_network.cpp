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
#include <openthread/ip6.h>
namespace {
constexpr const char *TAG="M2S_WIFI";
constexpr uint32_t kMagic=0x4d325731;
struct Credentials { uint32_t magic; char ssid[33]; char password[65]; };
Credentials saved{}, trial{};
StaticQueue_t queue_control;
uint8_t queue_storage[sizeof(Credentials)];
QueueHandle_t requests=nullptr;
esp_netif_t *sta=nullptr, *ap=nullptr;
portMUX_TYPE status_mux=portMUX_INITIALIZER_UNLOCKED;
m2s_network_status_t status{};
std::atomic_bool ip_ready{false}, request_busy{false}, reset_requested{false};
std::atomic_bool scan_requested{false};
m2s_wifi_ap_t scan_results[12]{};
int scan_count=0;
int64_t disconnected_at=0, deadline=0, ap_stop_at=0;
bool testing=false, ap_enabled=false, have_saved=false, br_started=false;
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
    esp_err_t e=esp_wifi_set_mode(
        (have_saved || testing) ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if(e!=ESP_OK)return e;
    wifi_config_t cfg{};
    uint8_t mac[6]{};esp_wifi_get_mac(WIFI_IF_STA,mac);
    snprintf(reinterpret_cast<char*>(cfg.ap.ssid),sizeof(cfg.ap.ssid),"Matter2Supla-%02X%02X",mac[4],mac[5]);
    cfg.ap.ssid_len=strlen(reinterpret_cast<char*>(cfg.ap.ssid));
    memcpy(cfg.ap.password,"12345678",8);
    cfg.ap.channel=1;cfg.ap.authmode=WIFI_AUTH_WPA2_PSK;cfg.ap.max_connection=2;
    cfg.ap.pmf_cfg.capable=true;cfg.ap.pmf_cfg.required=false;
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
    esp_err_t mode_error = esp_wifi_set_mode(
        ap_enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (mode_error != ESP_OK) return mode_error;
    wifi_config_t cfg{};
    memcpy(cfg.sta.ssid,value.ssid,strlen(value.ssid));
    memcpy(cfg.sta.password,value.password,strlen(value.password));
    cfg.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.authmode=value.password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable=true;
    portENTER_CRITICAL(&status_mux);
    snprintf(status.ssid,sizeof(status.ssid),"%s",value.ssid);status.connected=false;
    portEXIT_CRITICAL(&status_mux);
    ip_ready.store(false);
    esp_wifi_disconnect();
    esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&cfg);
    wipe(&cfg,sizeof(cfg));
    return e==ESP_OK?esp_wifi_connect():e;
}
void event(void*,esp_event_base_t base,int32_t id,void *data){
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_CONNECTED){esp_netif_create_ip6_linklocal(sta);}
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){
        ip_ready.store(false);
        portENTER_CRITICAL(&status_mux);status.connected=false;snprintf(status.ipv4,sizeof(status.ipv4),"0.0.0.0");portEXIT_CRITICAL(&status_mux);
    }
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){
        auto *ev=static_cast<ip_event_got_ip_t*>(data);
        if(ev->esp_netif!=sta)return;
        portENTER_CRITICAL(&status_mux);status.connected=true;snprintf(status.ipv4,sizeof(status.ipv4),IPSTR,IP2STR(&ev->ip_info.ip));portEXIT_CRITICAL(&status_mux);
        ip_ready.store(true);
    }
}
void worker(void*){
    int64_t next_retry=0;
    for(;;){
        const int64_t now=esp_timer_get_time();
        if(reset_requested.load()){
            vTaskDelay(pdMS_TO_TICKS(1800));
            esp_wifi_stop();
            const esp_err_t e=nvs_flash_erase();
            if(e==ESP_OK)esp_restart();
            state("reset_failed",e);reset_requested.store(false);
        }
        if(request_busy.load() && !testing && uxQueueMessagesWaiting(requests)){
            if(enable_ap()!=ESP_OK){
                Credentials discard{};xQueueReceive(requests,&discard,0);wipe(&discard,sizeof(discard));
                state("ap_failed",ESP_FAIL);request_busy.store(false);
            }else{
                // Return the HTTP response before the STA connection changes.
                vTaskDelay(pdMS_TO_TICKS(1500));
                if(xQueueReceive(requests,&trial,0)==pdTRUE){
                    wipe(queue_storage,sizeof(queue_storage));
                    testing=true;deadline=esp_timer_get_time()+45000000;
                    portENTER_CRITICAL(&status_mux);status.testing=true;portEXIT_CRITICAL(&status_mux);
                    state("testing");esp_err_t e=connect(trial);if(e!=ESP_OK)deadline=0;
                }
            }
        }
        if(scan_requested.load() && !testing && !request_busy.load()){
            auto *records=static_cast<wifi_ap_record_t*>(heap_caps_calloc(12,sizeof(wifi_ap_record_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
            uint16_t count=12;
            wifi_scan_config_t config{};
            // Scanning requires STA; keep Thread paused while setup AP is present.
            const bool restore_ap_only = ap_enabled && !have_saved;
            esp_err_t e = records ? esp_wifi_set_mode(
                ap_enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA) : ESP_ERR_NO_MEM;
            if (e == ESP_OK) e = esp_wifi_scan_start(&config, true);
            if (restore_ap_only) {
                esp_err_t mode_error = esp_wifi_set_mode(WIFI_MODE_AP);
                if (mode_error != ESP_OK) ESP_LOGE(TAG, "Restore AP mode failed: %s", esp_err_to_name(mode_error));
            }
            if(e==ESP_OK)e=esp_wifi_scan_get_ap_records(&count,records);
            portENTER_CRITICAL(&status_mux);
            scan_count=0;
            if(e==ESP_OK)for(unsigned i=0;i<count;i++){
                memcpy(scan_results[i].ssid,records[i].ssid,32);scan_results[i].ssid[32]=0;
                scan_results[i].rssi=records[i].rssi;scan_results[i].secure=records[i].authmode!=WIFI_AUTH_OPEN;scan_count++;
            }
            portEXIT_CRITICAL(&status_mux);
            if(records)heap_caps_free(records);
            scan_requested.store(false);
        }
        if(testing){
            wifi_ap_record_t connected_ap{};
            if(ip_ready.load() && esp_wifi_sta_get_ap_info(&connected_ap)==ESP_OK &&
               strncmp(reinterpret_cast<const char*>(connected_ap.ssid),trial.ssid,32)==0){
                esp_err_t e=save(trial);
                if(e==ESP_OK){
                    saved=trial;have_saved=true;testing=false;request_busy.store(false);wipe(&trial,sizeof(trial));
                    portENTER_CRITICAL(&status_mux);status.testing=false;status.configured=true;portEXIT_CRITICAL(&status_mux);
                    state("connected");ap_stop_at=esp_timer_get_time()+90000000;
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
            disconnected_at=0;
            m2s_network_status_t current{};m2s_network_status(&current);
            if(!testing && !request_busy.load() && (strcmp(current.state,"connecting")==0 || strcmp(current.state,"recovery_ap")==0)){
                state("connected");if(ap_enabled)ap_stop_at=now+90000000;
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
        }else if(!testing && have_saved){
            if(!disconnected_at)disconnected_at=now;
            if(now-disconnected_at>=45000000 && !ap_enabled){enable_ap();state("recovery_ap");}
            if(now>=next_retry){esp_wifi_connect();next_retry=now+10000000;}
        }
        if(br_started && ip_ready.load() && !ap_enabled && !testing){
            if (set_thread_radio_enabled(true) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            m2s_matter_status_t ms{};m2s_matter_get_status(&ms);
            if(!ms.start_requested)m2s_matter_request_start();
        }
        m2s_readings_tick();
        m2s_supla_tick();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
}
extern "C" void m2s_network_status(m2s_network_status_t *out){if(!out)return;portENTER_CRITICAL(&status_mux);*out=status;portEXIT_CRITICAL(&status_mux);}
extern "C" esp_err_t m2s_network_apply(const char *ssid,const char *password){
    if(!ssid || !password || !requests || reset_requested.load())return ESP_ERR_INVALID_STATE;
    size_t sn=strnlen(ssid,33),pn=strnlen(password,65);
    if(sn<1 || sn>32 || pn>63 || (pn>0 && pn<8))return ESP_ERR_INVALID_ARG;
    if(request_busy.exchange(true))return ESP_ERR_INVALID_STATE;
    Credentials value{};value.magic=kMagic;memcpy(value.ssid,ssid,sn);memcpy(value.password,password,pn);
    bool ok=xQueueSend(requests,&value,0)==pdTRUE;wipe(&value,sizeof(value));
    if(!ok){request_busy.store(false);return ESP_ERR_NO_MEM;}state("queued");return ESP_OK;
}
extern "C" esp_err_t m2s_network_scan(){
    if(!requests || request_busy.load() || scan_requested.exchange(true))return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
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
    ESP_ERROR_CHECK(enable_ap());
    if(have_saved){
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));ap_enabled=false;
        portENTER_CRITICAL(&status_mux);status.ap_active=false;portEXIT_CRITICAL(&status_mux);
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    if(have_saved){state("connecting");connect(saved);}else state("setup_ap");
    if(xTaskCreate(worker,"m2s_wifi",6144,nullptr,3,nullptr)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
}
