// Native ESP-IDF component: deliberately has no Arduino component dependency.
#include "m2s_supla.h"
#include "m2s_network.h"
#include "m2s_devices.h"
#include "m2s_readings.h"
#include "m2s_ikea_catalog.h"
#include "m2s_matter_api.h"
#include <SuplaDevice.h>
#include <supla/network/network.h>
#include <supla/sensor/virtual_therm_hygro_meter.h>
#include <supla/sensor/virtual_binary.h>
#include <supla/sensor/general_purpose_measurement.h>
#include <esp_idf_client.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cctype>
#include <new>
#include <cmath>
namespace {
constexpr char TAG[]="M2S_SUPLA";
struct Config {
 uint32_t magic=0x4d325331;
 uint32_t node=0;
 uint16_t temp_ep=1,hum_ep=2;
 uint8_t enabled=0,reserved[3]{};
 char server[128]{},email[128]{};
 char guid[SUPLA_GUID_SIZE]{},auth[SUPLA_AUTHKEY_SIZE]{};
};
Config config;
struct SlotStore {
 uint32_t magic=0x4d325337; // M2S7, kept separate from original Config blob.
 uint8_t count=0;
 uint8_t reserved[3]{};
 m2s_supla_sensor_slot_t slots[M2S_SUPLA_EXTRA_LIMIT]{};
};
SlotStore slot_store;
bool slots_corrupt=false;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
StaticSemaphore_t config_guard_storage;
SemaphoreHandle_t config_guard=nullptr;
m2s_supla_status_t state{};
esp_timer_handle_t restart_timer=nullptr;
void info(int status,const char *message){
 portENTER_CRITICAL(&mux);state.status=status;state.online=status==STATUS_REGISTERED_AND_READY;
 snprintf(state.message,sizeof(state.message),"%s",message?message:"");portEXIT_CRITICAL(&mux);
 ESP_LOGI(TAG,"status=%d %s",status,message?message:"");
}
void reboot(void*){esp_restart();}
bool known_node(uint32_t node){m2s_device_t list[M2S_DEVICE_LIMIT]{};size_t n=m2s_devices_snapshot(list,M2S_DEVICE_LIMIT);for(size_t i=0;i<n;i++)if(list[i].node==node && (list[i].state==0||list[i].state==2))return true;return false;}
bool supported_slot(const m2s_supla_sensor_slot_t &slot,M2sIkeaSensor sensor){
 switch(slot.type){
 case M2S_SUPLA_SENSOR_TH:return sensor==M2sIkeaSensor::ThermHygro||sensor==M2sIkeaSensor::AirQuality;
 case M2S_SUPLA_SENSOR_MOTION:return sensor==M2sIkeaSensor::Motion;
 case M2S_SUPLA_SENSOR_CONTACT:return sensor==M2sIkeaSensor::Contact;
 case M2S_SUPLA_SENSOR_LEAK:return sensor==M2sIkeaSensor::WaterLeak;
 case M2S_SUPLA_SENSOR_CO2:case M2S_SUPLA_SENSOR_PM25:return sensor==M2sIkeaSensor::AirQuality;
 default:return false;
 }
}
bool fresh_value(const m2s_readings_t &r,uint16_t ep,uint32_t cluster,int64_t now,int *result){
 for(int i=0;i<r.count;i++){
  const auto &v=r.values[i];
  if(v.valid && v.endpoint==ep && v.cluster==cluster && now>=v.received_us &&
     now-v.received_us<900000000LL){*result=v.value;return true;}
 }
 return false;
}
void apply_battery(Supla::Channel *ch,const m2s_readings_t &r,int64_t now){
 if(r.battery_valid && r.battery_raw<=200 && now>=r.battery_received_us &&
    now-r.battery_received_us<86400000000LL){
  ch->setBatteryPowered(true);ch->setBatteryLevel((r.battery_raw+1)/2);
 }
}
struct ExtraChannel {
 Supla::Sensor::VirtualThermHygroMeter *thermo=nullptr;
 Supla::Sensor::VirtualBinary *binary=nullptr;
 Supla::Sensor::GeneralPurposeMeasurement *scalar=nullptr;
 Supla::Channel *channel=nullptr;
};
class ExistingWifi final:public Supla::Network {
public:
 void setup()override{} // m2s_network is the sole owner of the Wi-Fi driver.
 void disable()override{}
 void uninit()override{}
 void setConfigMode()override{}
 void setNormalMode()override{}
 void setOfflineMode()override{}
 bool isWifiConfigRequired()override{return false;}
 bool isReady()override{m2s_network_status_t s{};m2s_network_status(&s);return s.connected&&!s.ap_active&&!s.testing;}
 bool getMacAddr(uint8_t *mac)override{return esp_wifi_get_mac(WIFI_IF_STA,mac)==ESP_OK;}
 uint32_t getIP()override{auto *n=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");esp_netif_ip_info_t ip{};return n&&esp_netif_get_ip_info(n,&ip)==ESP_OK?ip.ip.addr:0;}
 Supla::Client *createClient()override{return new(std::nothrow) Supla::EspIdfClient;}
};
void worker(void*){
 // Copy immutable running configuration. Saved changes take effect only after reboot.
 Config running;SlotStore runningSlots;
 xSemaphoreTake(config_guard,portMAX_DELAY);running=config;runningSlots=slot_store;xSemaphoreGive(config_guard);
 if(slots_corrupt){info(-106,"Niezgodna konfiguracja kanalow; nie rejestruje SUPLA");for(;;)vTaskDelay(pdMS_TO_TICKS(1000));}
 ExistingWifi wifi;
 bool time_started=false;
 const int64_t waitStartedUs=esp_timer_get_time();
 int64_t nextClockReportUs=waitStartedUs;
 while(!wifi.isReady() || time(nullptr)<1704067200){
   if(wifi.isReady()&&!time_started){
     if(!esp_sntp_enabled()){esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);esp_sntp_setservername(0,"time.cloudflare.com");esp_sntp_init();}
     time_started=true;info(-101,"Oczekiwanie na synchronizacje czasu dla TLS");
   }
   const int64_t nowUs=esp_timer_get_time();
   if(nowUs>=nextClockReportUs){
     nextClockReportUs=nowUs+15000000LL;
     ESP_LOGI(TAG,"CLOCK_WAIT elapsed_s=%lld epoch=%lld wifi_ready=%d sntp_started=%d DMA=%u DMA_largest=%u",
      (long long)((nowUs-waitStartedUs)/1000000LL),(long long)time(nullptr),
      (int)wifi.isReady(),(int)time_started,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
   }
   vTaskDelay(pdMS_TO_TICKS(1000));
 }
 ESP_LOGI(TAG,"CLOCK_READY elapsed_s=%lld epoch=%lld",
  (long long)((esp_timer_get_time()-waitStartedUs)/1000000LL),
  (long long)time(nullptr));
 // Delay TLS until Matter startup allocations have settled.

 auto *sensor=new(std::nothrow) Supla::Sensor::VirtualThermHygroMeter;
 if(!sensor){info(-103,"Brak pamieci na kanal SUPLA");for(;;)vTaskDelay(pdMS_TO_TICKS(1000));}
 if(!sensor->getChannel()->setChannelNumber(0)){
   info(-103,"Nie mozna przypisac kanalu 0 SUPLA");for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
 }
 sensor->setInitialCaption("Matter temperatura i wilgotnosc");
 sensor->setRefreshIntervalMs(1000);
 sensor->getChannel()->setStateOffline();
 ExtraChannel extra[M2S_SUPLA_EXTRA_LIMIT]{};
 // Channel IDs 1..N are explicit and append-only, never repurposed after NVS save.
 for(unsigned i=0;i<runningSlots.count;i++){
   const auto &slot=runningSlots.slots[i];
   ExtraChannel &ch=extra[i];
   if(slot.type==M2S_SUPLA_SENSOR_TH){
     ch.thermo=new(std::nothrow) Supla::Sensor::VirtualThermHygroMeter;
     if(ch.thermo){ch.thermo->setRefreshIntervalMs(1000);ch.thermo->setInitialCaption("IKEA Matter temperatura/wilgotnosc");ch.channel=ch.thermo->getChannel();}
   }else if(slot.type==M2S_SUPLA_SENSOR_MOTION||slot.type==M2S_SUPLA_SENSOR_CONTACT||slot.type==M2S_SUPLA_SENSOR_LEAK){
     ch.binary=new(std::nothrow) Supla::Sensor::VirtualBinary;
     if(ch.binary){ch.binary->setUseConfiguredTimeout(false);ch.channel=ch.binary->getChannel();
       ch.binary->setInitialCaption(slot.type==M2S_SUPLA_SENSOR_MOTION?"IKEA ruch":
           slot.type==M2S_SUPLA_SENSOR_CONTACT?"IKEA drzwi/okno":"IKEA zalanie");}
   }else if(slot.type==M2S_SUPLA_SENSOR_CO2||slot.type==M2S_SUPLA_SENSOR_PM25){
     ch.scalar=new(std::nothrow) Supla::Sensor::GeneralPurposeMeasurement;
     if(ch.scalar){ch.scalar->setInitialCaption(slot.type==M2S_SUPLA_SENSOR_CO2?"IKEA CO2":"IKEA PM2.5");
       ch.scalar->setDefaultUnitAfterValue(slot.type==M2S_SUPLA_SENSOR_CO2?"ppm":"ug/m3");
       ch.scalar->setDefaultValuePrecision(1);ch.channel=ch.scalar->getChannel();}
   }
   if(!ch.channel){info(-103,"Za malo RAM na dodatkowy kanal SUPLA");for(;;)vTaskDelay(pdMS_TO_TICKS(1000));}
   if(!ch.channel->setChannelNumber(static_cast<int>(i+1))){
     info(-103,"Nie mozna przypisac numeru dodatkowego kanalu SUPLA");
     for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
   }
   ch.channel->setStateOffline();
 }
 SuplaDevice.setName("Matter2Supla");SuplaDevice.setSwVersion("0.9.3-stage7-ikea");
 SuplaDevice.setStatusFuncImpl(info);SuplaDevice.setServerPort(2016);
 ESP_LOGI(TAG,"before TLS: epoch=%lld CA=%s internal=%u largest=%u DMA=%u DMA_largest=%u psram=%u",
  (long long)time(nullptr),
  SuplaDevice.getSuplaCACert()?"PRESENT":"MISSING",
  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
 if(!SuplaDevice.begin(running.guid,running.server,running.email,running.auth)){
   info(-102,"Nie udalo sie uruchomic SUPLA; sprawdz log");
   for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
 }
 int64_t next_update=0;
 while(true){
   const int64_t now=esp_timer_get_time();
   if(now>=next_update){
     next_update=now+1000000;
     m2s_readings_t r{};const bool registered=known_node(running.node);
     const bool have=registered&&m2s_readings_cached(running.node,&r);
     double t=TEMPERATURE_NOT_AVAILABLE,h=HUMIDITY_NOT_AVAILABLE;
     bool tv=false,hv=false;
     if(have)for(int i=0;i<r.count;i++){
       const auto &v=r.values[i];if(!v.valid||now-v.received_us>900000000LL)continue;
       if(v.cluster==0x0402&&v.endpoint==running.temp_ep){t=v.value/100.0;tv=true;}
       if(v.cluster==0x0405&&v.endpoint==running.hum_ep){h=v.value/100.0;hv=true;}
     }
     // SUPLA channel 0 already supports battery status fields. Matter reports
     // half-percent units, so raw 200 = 100%. Do not add a channel or write NVS.
     if(have && r.battery_valid && r.battery_raw<=200 &&
        now>=r.battery_received_us && now-r.battery_received_us<86400000000LL){
       sensor->getChannel()->setBatteryPowered(true);
       sensor->getChannel()->setBatteryLevel((r.battery_raw+1)/2);
     }
     sensor->setTemp(t);sensor->setHumi(h);
     if(tv&&hv)sensor->getChannel()->setStateOnline();else sensor->getChannel()->setStateOffline();
     for(unsigned i=0;i<runningSlots.count;i++){
       const auto &slot=runningSlots.slots[i];const auto &ch=extra[i];
       m2s_readings_t sr{};bool available=known_node(slot.node) && m2s_readings_cached(slot.node,&sr);
       if(!available||!sr.vendor_valid||!sr.product_valid||
          !supported_slot(slot,m2s_ikea_sensor(sr.vendor_id,sr.product_id))){
         ch.channel->setStateOffline();continue;
       }
       int v=0,other=0;bool valid=false;
       if(ch.thermo){
         const bool t_ok=fresh_value(sr,slot.endpoint,0x0402,now,&v);
         const bool h_ok=fresh_value(sr,slot.aux_endpoint,0x0405,now,&other);
         ch.thermo->setTemp(t_ok?v/100.0:TEMPERATURE_NOT_AVAILABLE);
         ch.thermo->setHumi(h_ok?other/100.0:HUMIDITY_NOT_AVAILABLE);
         valid=t_ok&&h_ok;
       }else if(ch.binary){
         const uint32_t cluster=slot.type==M2S_SUPLA_SENSOR_MOTION?0x0406:0x0045;
         valid=fresh_value(sr,slot.endpoint,cluster,now,&v);
         if(valid){if((v!=0)!=bool(slot.invert))ch.binary->set();else ch.binary->clear();}
       }else if(ch.scalar){
         const uint32_t cluster=slot.type==M2S_SUPLA_SENSOR_CO2?0x040D:0x042A;
         valid=fresh_value(sr,slot.endpoint,cluster,now,&v);
         if(valid)ch.scalar->setValue(v/100.0);
       }
       if(valid){apply_battery(ch.channel,sr,now);ch.channel->setStateOnline();}
       else ch.channel->setStateOffline();
     }
   }
   SuplaDevice.iterate();vTaskDelay(pdMS_TO_TICKS(20));
 }
}
}
extern "C" void m2s_supla_status(m2s_supla_status_t *out){portENTER_CRITICAL(&mux);*out=state;portEXIT_CRITICAL(&mux);}
extern "C" void m2s_supla_init(){
 config_guard=xSemaphoreCreateMutexStatic(&config_guard_storage);
 nvs_handle_t h;size_t size=sizeof(config);
 if(nvs_open("m2s_supla",NVS_READONLY,&h)==ESP_OK){
   auto e=nvs_get_blob(h,"config",&config,&size);nvs_close(h);
   if(e!=ESP_OK||size!=sizeof(config)||config.magic!=0x4d325331)config=Config{};
 }
 size=sizeof(slot_store);
 if(nvs_open("m2s_ikea",NVS_READONLY,&h)==ESP_OK){
   const esp_err_t e=nvs_get_blob(h,"channels",&slot_store,&size);nvs_close(h);
   if(e==ESP_ERR_NVS_NOT_FOUND)slot_store=SlotStore{};
   else if(e!=ESP_OK || size!=sizeof(slot_store) || slot_store.magic!=0x4d325337 ||
           slot_store.count>M2S_SUPLA_EXTRA_LIMIT){
     slots_corrupt=true;slot_store=SlotStore{};
     ESP_LOGE(TAG,"Extra channel configuration invalid; refusing new mapping");
   }
 }
 if(!slots_corrupt)for(unsigned i=0;i<slot_store.count;i++){
   const auto &s=slot_store.slots[i];
   if(!s.node||s.node>65535||s.endpoint==65535||s.aux_endpoint==65535||
      s.type<M2S_SUPLA_SENSOR_TH||s.type>M2S_SUPLA_SENSOR_PM25||s.invert>1){
     slots_corrupt=true;slot_store=SlotStore{};
     ESP_LOGE(TAG,"Invalid saved IKEA channel; SUPLA registration blocked");break;
   }
 }
 config.server[127]=0;config.email[127]=0;
 state.enabled=config.enabled;state.configured=config.node!=0;state.node=config.node;
 state.temperature_endpoint=config.temp_ep;state.humidity_endpoint=config.hum_ep;
 snprintf(state.server,sizeof(state.server),"%s",config.server);snprintf(state.email,sizeof(state.email),"%s",config.email);
 snprintf(state.message,sizeof(state.message),"%s",config.enabled?"Oczekiwanie na Wi-Fi":"Integracja wylaczona");
 esp_timer_create_args_t timer{};timer.callback=reboot;timer.name="m2s_sup_reboot";
 if(esp_timer_create(&timer,&restart_timer)!=ESP_OK)restart_timer=nullptr;

}
extern "C" size_t m2s_supla_slots_snapshot(m2s_supla_sensor_slot_t *out,size_t capacity){
 if(!config_guard || !out)return 0;
 xSemaphoreTake(config_guard,portMAX_DELAY);
 const size_t count=capacity<slot_store.count?capacity:slot_store.count;
 for(size_t i=0;i<count;i++)out[i]=slot_store.slots[i];
 xSemaphoreGive(config_guard);return count;
}
extern "C" esp_err_t m2s_supla_add_sensor(uint32_t node,uint8_t type,uint16_t endpoint,uint16_t aux_endpoint,bool invert){
 if(!config_guard||!restart_timer||!node||node>65535||endpoint==65535||aux_endpoint==65535 ||
    !known_node(node))return ESP_ERR_INVALID_ARG;
 m2s_readings_t r{};
 if(!m2s_readings_cached(node,&r)||!r.vendor_valid||!r.product_valid)return ESP_ERR_INVALID_STATE;
 portENTER_CRITICAL(&mux);const bool supla_online=state.online;portEXIT_CRITICAL(&mux);
 if(!supla_online)return ESP_ERR_INVALID_STATE;
 m2s_supla_sensor_slot_t slot{};
 slot.node=node;slot.type=type;slot.endpoint=endpoint;slot.aux_endpoint=aux_endpoint;slot.invert=invert?1:0;
 if(!supported_slot(slot,m2s_ikea_sensor(r.vendor_id,r.product_id)))return ESP_ERR_INVALID_ARG;
 // Do not permanently register an empty channel just because a model supports
 // a class: the chosen endpoint must have yielded a recent valid attribute.
 const int64_t now=esp_timer_get_time();int value=0,other=0;
 bool seen=false;
 switch(type){
 case M2S_SUPLA_SENSOR_TH:
   seen=fresh_value(r,endpoint,0x0402,now,&value) &&
        fresh_value(r,aux_endpoint,0x0405,now,&other);break;
 case M2S_SUPLA_SENSOR_MOTION:seen=fresh_value(r,endpoint,0x0406,now,&value);break;
 case M2S_SUPLA_SENSOR_CONTACT:case M2S_SUPLA_SENSOR_LEAK:
   seen=fresh_value(r,endpoint,0x0045,now,&value);break;
 case M2S_SUPLA_SENSOR_CO2:seen=fresh_value(r,endpoint,0x040D,now,&value);break;
 case M2S_SUPLA_SENSOR_PM25:seen=fresh_value(r,endpoint,0x042A,now,&value);break;
 default:break;
 }
 if(!seen)return ESP_ERR_INVALID_STATE;
 xSemaphoreTake(config_guard,portMAX_DELAY);
 if(slots_corrupt||state.restart_required||slot_store.count==M2S_SUPLA_EXTRA_LIMIT||
    !config.enabled||!config.node||node==config.node){xSemaphoreGive(config_guard);return ESP_ERR_INVALID_STATE;}
 for(unsigned i=0;i<slot_store.count;i++){
  const auto &previous=slot_store.slots[i];
  if(previous.node==slot.node && previous.type==slot.type){
   xSemaphoreGive(config_guard);return ESP_ERR_INVALID_STATE;
  }
 }
 SlotStore next=slot_store;next.slots[next.count++]=slot;
 nvs_handle_t h;esp_err_t e=nvs_open("m2s_ikea",NVS_READWRITE,&h);
 if(e==ESP_OK){e=nvs_set_blob(h,"channels",&next,sizeof(next));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);}
 if(e==ESP_OK){slot_store=next;portENTER_CRITICAL(&mux);state.restart_required=true;portEXIT_CRITICAL(&mux);
   e=esp_timer_start_once(restart_timer,2000000);}
 xSemaphoreGive(config_guard);return e;
}
extern "C" esp_err_t m2s_supla_configure(bool enabled,const char *server,const char *email,uint32_t node,uint16_t temp_ep,uint16_t hum_ep){
 if(!config_guard||!restart_timer)return ESP_ERR_INVALID_STATE;
 if(!server||!email||strlen(server)>127||strlen(email)>127||!node||node>65535||temp_ep==65535||hum_ep==65535)return ESP_ERR_INVALID_ARG;
 if(enabled && !known_node(node))return ESP_ERR_INVALID_ARG;
 if(!server[0]||!strchr(email,'@'))return ESP_ERR_INVALID_ARG;
 for(const unsigned char *p=(const unsigned char*)server;*p;p++)if(!isalnum(*p)&&*p!='.'&&*p!='-')return ESP_ERR_INVALID_ARG;
 for(const unsigned char *p=(const unsigned char*)email;*p;p++)if(*p<=32||*p>=127)return ESP_ERR_INVALID_ARG;
 xSemaphoreTake(config_guard,portMAX_DELAY);
 if(state.restart_required){xSemaphoreGive(config_guard);return ESP_ERR_INVALID_STATE;}
 // A published channel never silently changes physical sensor or endpoint.
 if(config.node&&(config.node!=node||config.temp_ep!=temp_ep||config.hum_ep!=hum_ep)){xSemaphoreGive(config_guard);return ESP_ERR_INVALID_STATE;}
 Config next=config;next.node=node;next.temp_ep=temp_ep;next.hum_ep=hum_ep;next.enabled=enabled;
 snprintf(next.server,sizeof(next.server),"%s",server);snprintf(next.email,sizeof(next.email),"%s",email);
 if(!config.node){esp_fill_random(next.guid,sizeof(next.guid));esp_fill_random(next.auth,sizeof(next.auth));}
 nvs_handle_t h;esp_err_t e=nvs_open("m2s_supla",NVS_READWRITE,&h);
 if(e==ESP_OK){e=nvs_set_blob(h,"config",&next,sizeof(next));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);}
 if(e==ESP_OK){config=next;portENTER_CRITICAL(&mux);state.restart_required=true;portEXIT_CRITICAL(&mux);e=esp_timer_start_once(restart_timer,2000000);}
 xSemaphoreGive(config_guard);return e;
}

extern "C" void m2s_supla_tick(){
 static bool attempted=false;
 if(attempted||esp_timer_get_time()<90000000LL||!config_guard)return;
 m2s_network_status_t net{};m2s_network_status(&net);
 m2s_matter_status_t ms{};m2s_matter_get_status(&ms);
 if(!ms.ready||ms.pair_busy||!net.connected||net.ap_active||net.testing)return;
 xSemaphoreTake(config_guard,portMAX_DELAY);const bool enabled=config.enabled&&config.node;xSemaphoreGive(config_guard);
 if(!enabled)return;
 attempted=true;
 if(xTaskCreate(worker,"m2s_supla",8192,nullptr,2,nullptr)!=pdPASS)info(-103,"Brak pamieci na zadanie SUPLA");
}
