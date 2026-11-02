#include "m2s_readings.h"
#include "m2s_ikea_catalog.h"
#include "m2s_matter_api.h"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include <esp_matter_controller_read_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <platform/CHIPDeviceLayer.h>
#include <utility>
#include <cmath>
#include <cstdio>
#include "nvs.h"
#include "m2s_devices.h"
#include "m2s_network.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include <openthread/thread.h>

namespace {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
m2s_readings_t snapshot{};
struct Cached {
    uint32_t node;
    int64_t next_us;
    int64_t next_battery_us;
    uint16_t interval_minutes;
    m2s_readings_t report;
    // Volatile status of one combined temperature+humidity Matter subscription.
    bool sub_requested;
    bool sub_reporting;
    int64_t sub_requested_us;
    int64_t sub_report_us;
    uint32_t sub_report_count;
};
Cached *cache=nullptr;
int64_t next_dispatch=0;
unsigned cursor=0;
bool battery_in_flight=false; // Covered by mux, same Matter ReadClient as T/H.
constexpr int64_t kSubscriptionRetryUs=900000000LL; // Retry only after 15 minutes without reports.
constexpr uint16_t kSubscriptionMinSeconds=15;
constexpr uint16_t kSubscriptionMaxSeconds=300;
constexpr int64_t kBatteryIntervalUs=3600000000LL;
constexpr int64_t kBatteryRetryUs=300000000LL;
constexpr uint16_t kDefaultIntervalMinutes=1;
constexpr uint16_t kMinIntervalMinutes=1;
constexpr uint16_t kMaxIntervalMinutes=1440;
constexpr const char *kPollNamespace="m2s_poll9";
constexpr int64_t interval_us(uint16_t minutes){
  return static_cast<int64_t>(minutes)*60LL*1000000LL;
}
void poll_key(uint32_t node,char (&key)[12]){
  snprintf(key,sizeof(key),"n%05lu",static_cast<unsigned long>(node));
}
uint16_t load_interval_nvs(uint32_t node){
  if(node<1 || node>65535)return kDefaultIntervalMinutes;
  nvs_handle_t nvs=0;
  if(nvs_open(kPollNamespace,NVS_READONLY,&nvs)!=ESP_OK)return kDefaultIntervalMinutes;
  char key[12]{};poll_key(node,key);
  uint16_t minutes=kDefaultIntervalMinutes;
  esp_err_t result=nvs_get_u16(nvs,key,&minutes);
  nvs_close(nvs);
  if(result!=ESP_OK || minutes<kMinIntervalMinutes || minutes>kMaxIntervalMinutes)
    return kDefaultIntervalMinutes;
  return minutes;
}
// Caller holds mux; fixed allocation in PSRAM, never written to NVS.
void cache_finish(){
 if(!cache)return;
 for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node==snapshot.node){
   auto previous=cache[i].report;
   cache[i].report=snapshot;
   // Keep previous values with their original timestamps on failed/empty reads.
   if(snapshot.count==0 && snapshot.state>=3){cache[i].report.count=previous.count;for(int k=0;k<previous.count;k++)cache[i].report.values[k]=previous.values[k];}
   // Temperature/humidity poll usually does not query battery. Preserve its
   // last timestamp instead of making a cached reading look freshly sampled.
   if(!snapshot.battery_valid){
     cache[i].report.battery_raw=previous.battery_raw;
     cache[i].report.battery_valid=previous.battery_valid;
     cache[i].report.battery_received_us=previous.battery_received_us;
   }else{
     cache[i].next_battery_us=esp_timer_get_time()+kBatteryIntervalUs;
   }
   if(!snapshot.vendor_valid){cache[i].report.vendor_id=previous.vendor_id;cache[i].report.vendor_valid=previous.vendor_valid;}
   if(!snapshot.product_valid){cache[i].report.product_id=previous.product_id;cache[i].report.product_valid=previous.product_valid;}
   cache[i].next_us=esp_timer_get_time()+interval_us(cache[i].interval_minutes ? cache[i].interval_minutes : kDefaultIntervalMinutes);
   break;
 }
}
using namespace esp_matter::controller;
// One live request, including discovery/CASE and timeout cleanup. No per-click tasks.
void finish(bool success) {
    portENTER_CRITICAL(&mux);
    snapshot.busy=false;
    if(snapshot.state!=4) snapshot.state=success?2:3;
    const bool battery_missing=battery_in_flight && !snapshot.battery_valid;
    const uint32_t node=snapshot.node;
    cache_finish();
    portEXIT_CRITICAL(&mux);
    if(battery_missing)
        ESP_LOGW("M2S_BAT","BAT_NO_REPORT node=%lu retry_s=300",
                 (unsigned long)node);
}
class WebRead final : public read_command {
    bool complete=false;
    bool failed=false;
public:
    WebRead(uint32_t node, ScopedMemoryBufferWithSize<AttributePathParams> &&paths,
            ScopedMemoryBufferWithSize<EventPathParams> &&events)
        : read_command(node,std::move(paths),std::move(events),nullptr,nullptr,nullptr) {}
    ~WebRead() override { if(!complete) finish(false); }
    void OnAttributeData(const chip::app::ConcreteDataAttributePath &path,
                         chip::TLV::TLVReader *data, const chip::app::StatusIB &status) override {
        if(path.mEndpointId==0 && path.mClusterId==0x0028 &&
           (path.mAttributeId==2 || path.mAttributeId==4)){
            if(status.ToChipError()!=CHIP_NO_ERROR || !data || data->GetType()==chip::TLV::kTLVType_Null)return;
            uint16_t id=0;
            if(data->Get(id)!=CHIP_NO_ERROR)return;
            portENTER_CRITICAL(&mux);
            if(snapshot.state==1){
                if(path.mAttributeId==2){snapshot.vendor_id=id;snapshot.vendor_valid=true;}
                else {snapshot.product_id=id;snapshot.product_valid=true;}
            }
            portEXIT_CRITICAL(&mux);
            return;
        }
        if(path.mEndpointId==0 && path.mClusterId==0x002F && path.mAttributeId==0x000C){
            // Optional on some Matter nodes; failure must not invalidate T/H.
            if(status.ToChipError()!=CHIP_NO_ERROR || !data || data->GetType()==chip::TLV::kTLVType_Null)return;
            uint8_t raw=0;
            if(data->Get(raw)!=CHIP_NO_ERROR || raw>200)return;
            const int64_t now=esp_timer_get_time();
            portENTER_CRITICAL(&mux);
            if(snapshot.state==1){snapshot.battery_raw=raw;snapshot.battery_valid=true;snapshot.battery_received_us=now;}
            const uint32_t battery_node=snapshot.node;
            portEXIT_CRITICAL(&mux);
            ESP_LOGI("M2S_BAT","BAT_RX node=%lu raw=%u percent_x10=%u",
                     (unsigned long)battery_node,(unsigned)raw,(unsigned)raw*5);
            return;
        }
        if(path.mAttributeId!=0 || (path.mClusterId!=0x0402 && path.mClusterId!=0x0405 &&
           path.mClusterId!=0x0406 && path.mClusterId!=0x0045 &&
           path.mClusterId!=0x040D && path.mClusterId!=0x042A))return;
        // A wildcard cluster request normally encounters unsupported endpoints;
        // that is not a failure of the sensor or the other paths in the read.
        if(status.ToChipError()!=CHIP_NO_ERROR || !data)return;
        bool valid=data->GetType()!=chip::TLV::kTLVType_Null;
        int value=0;
        if(valid){
            if(path.mClusterId==0x0402){int16_t raw=0;if(data->Get(raw)!=CHIP_NO_ERROR)return;value=raw;valid=raw!=-32768;}
            else if(path.mClusterId==0x0405){uint16_t raw=0;if(data->Get(raw)!=CHIP_NO_ERROR)return;value=raw;valid=raw<=10000;}
            else if(path.mClusterId==0x0406){uint8_t bits=0;if(data->Get(bits)!=CHIP_NO_ERROR)return;value=(bits&1)?1:0;}
            else if(path.mClusterId==0x0045){bool state=false;if(data->Get(state)!=CHIP_NO_ERROR)return;value=state?1:0;}
            else {
                float concentration=0;
                if(data->Get(concentration)!=CHIP_NO_ERROR || !std::isfinite(concentration) ||
                   concentration<0 || concentration>1000000)return;
                value=static_cast<int>(std::lround(concentration*100.0f));
            }
        }
        const int64_t now=esp_timer_get_time();
        bool changed=true;
        portENTER_CRITICAL(&mux);
        if(snapshot.state==1){
            int64_t changed_us=now;
            if(cache)for(int k=0;k<M2S_DEVICE_LIMIT;k++)if(cache[k].node==snapshot.node){
                const auto &old=cache[k].report;
                for(int j=0;j<old.count;j++)if(old.values[j].endpoint==path.mEndpointId &&
                    old.values[j].cluster==path.mClusterId && old.values[j].valid==valid &&
                    old.values[j].value==value && old.values[j].changed_us>0){
                    changed=false;changed_us=old.values[j].changed_us;break;
                }
                break;
            }
            int i=0;
            for(;i<snapshot.count;i++)if(snapshot.values[i].endpoint==path.mEndpointId && snapshot.values[i].cluster==path.mClusterId)break;
            if(i<8){if(i==snapshot.count)snapshot.count++;snapshot.values[i]={path.mEndpointId,path.mClusterId,value,valid,now,changed_us};}
            else failed=true;
        }
        const uint32_t report_node=snapshot.node;
        portEXIT_CRITICAL(&mux);
        ESP_LOGI("M2S_READ","ATTR_RX node=%lu ep=%u cluster=0x%04lx value=%ld valid=%u changed=%u",
           (unsigned long)report_node,(unsigned)path.mEndpointId,(unsigned long)path.mClusterId,
           (long)value,(unsigned)valid,(unsigned)changed);
    }
    void OnError(CHIP_ERROR error) override { failed=true;ESP_LOGW("M2S_READ","READ_ERROR: %s",chip::ErrorStr(error));read_command::OnError(error); }
    void OnDone(chip::app::ReadClient *client) override {
        complete=true;finish(!failed);
        ESP_LOGI("M2S_READ","READ_DONE success=%u",(unsigned)!failed);
        read_command::OnDone(client); // SDK owns its ReadClient; base deletes this command.
    }
};
// TIMMERFLOTTE subscriptions: one subscribe request contains both attributes.
// Attribute callbacks run on the CHIP event loop and update the same cache that
// SUPLA's worker reads every second. No NVS or Thread fabric changes.
void subscribed_attribute(uint64_t node, const chip::app::ConcreteDataAttributePath &path,
                          chip::TLV::TLVReader *data){
    if(!data || path.mAttributeId!=0 ||
       !((path.mEndpointId==1 && path.mClusterId==0x0402) ||
         (path.mEndpointId==2 && path.mClusterId==0x0405)))return;
    if(data->GetType()==chip::TLV::kTLVType_Null)return;
    int raw=0;
    if(path.mClusterId==0x0402){
        int16_t temp=0;
        if(data->Get(temp)!=CHIP_NO_ERROR || temp==-32768)return;
        raw=temp;
    }else{
        uint16_t humidity=0;
        if(data->Get(humidity)!=CHIP_NO_ERROR || humidity>10000)return;
        raw=humidity;
    }
    const int64_t now=esp_timer_get_time();
    bool changed=true;
    bool stored=false;
    portENTER_CRITICAL(&mux);
    if(cache)for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node==node){
        auto &c=cache[i];
        c.sub_requested=true;
        c.sub_reporting=true;
        c.sub_report_us=now;
        ++c.sub_report_count;
        auto &r=c.report;
        r.node=static_cast<uint32_t>(node);
        r.state=2;
        int idx=0;
        for(;idx<r.count;idx++)if(r.values[idx].endpoint==path.mEndpointId &&
             r.values[idx].cluster==path.mClusterId)break;
        if(idx<8){
            int64_t changed_us=now;
            if(idx<r.count && r.values[idx].valid && r.values[idx].value==raw){
                changed=false;
                if(r.values[idx].changed_us>0)changed_us=r.values[idx].changed_us;
            }
            if(idx==r.count)++r.count;
            r.values[idx]={path.mEndpointId,path.mClusterId,raw,true,now,changed_us};
            stored=true;
        }
        break;
    }
    portEXIT_CRITICAL(&mux);
    if(stored)ESP_LOGI("M2S_SUB","SUB_REPORT node=%lu ep=%u cluster=0x%04lx value=%ld changed=%u",
       (unsigned long)node,(unsigned)path.mEndpointId,(unsigned long)path.mClusterId,(long)raw,(unsigned)changed);
}
// ESP-Matter v1.5 uses a 3-argument callback. Newer ESP-Matter versions add
// StatusIB, so overload resolution selects the matching typed function pointer.
void sub_attribute_cb(uint64_t node, const chip::app::ConcreteDataAttributePath &path,
                      chip::TLV::TLVReader *data){
    subscribed_attribute(node,path,data);
}
[[maybe_unused]] void sub_attribute_cb(uint64_t node, const chip::app::ConcreteDataAttributePath &path,
                      chip::TLV::TLVReader *data, const chip::app::StatusIB &status){
    if(status.ToChipError()!=CHIP_NO_ERROR)return;
    subscribed_attribute(node,path,data);
}
void sub_work(intptr_t arg){
    const uint32_t node=static_cast<uint32_t>(arg);
    chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams> paths;
    chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams> events;
    paths.Alloc(2);
    if(!paths.Get()){
        ESP_LOGE("M2S_SUB","SUB_NOMEM node=%lu",(unsigned long)node);
        return;
    }
    paths[0]=chip::app::AttributePathParams(1,0x0402,0);
    paths[1]=chip::app::AttributePathParams(2,0x0405,0);
    // One session for both attributes. Reattempts replace older subscriptions.
    // We cannot assume reporting intervals force a fresh *physical* sample.
    auto *cmd=chip::Platform::New<esp_matter::controller::subscribe_command>(
        node,std::move(paths),std::move(events),kSubscriptionMinSeconds,kSubscriptionMaxSeconds,
        true,static_cast<esp_matter::controller::attribute_report_cb_t>(sub_attribute_cb),
        nullptr,nullptr,nullptr,false);
    if(!cmd){
        ESP_LOGE("M2S_SUB","SUB_ALLOC_FAILED node=%lu",(unsigned long)node);
        return;
    }
    ESP_LOGI("M2S_SUB","SUB_SEND node=%lu temp_ep=1 hum_ep=2 min_s=%u max_s=%u",
       (unsigned long)node,(unsigned)kSubscriptionMinSeconds,(unsigned)kSubscriptionMaxSeconds);
    // SDK takes ownership and frees on connection errors/termination.
    if(cmd->send_command()!=ESP_OK)
        ESP_LOGW("M2S_SUB","SUB_SEND_FAILED node=%lu",(unsigned long)node);
}
void work(intptr_t arg){
    m2s_matter_status_t status{};m2s_matter_get_status(&status);
    if(!status.ready || status.pair_busy){finish(false);return;}
    ScopedMemoryBufferWithSize<AttributePathParams> paths;
    ScopedMemoryBufferWithSize<EventPathParams> events;
    portENTER_CRITICAL(&mux);
    const bool query_battery=battery_in_flight;
    M2sIkeaSensor sensor=M2sIkeaSensor::Unknown;
    if(cache)for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node==static_cast<uint32_t>(arg) &&
       cache[i].report.vendor_valid && cache[i].report.product_valid)
        sensor=m2s_ikea_sensor(cache[i].report.vendor_id,cache[i].report.product_id);
    portEXIT_CRITICAL(&mux);
    // No extra Matter tasks. Keep ReadClient path count small on 20 KiB SRAM.
    const bool identity_unknown=sensor==M2sIkeaSensor::Unknown;
    unsigned count=identity_unknown?4:((sensor==M2sIkeaSensor::AirQuality)?4:
                     (sensor==M2sIkeaSensor::ThermHygro?2:1));
    if(query_battery)++count;
    paths.Alloc(count);
    if(!paths.Get()){finish(false);return;}
    unsigned index=0;
    if(sensor==M2sIkeaSensor::ThermHygro || sensor==M2sIkeaSensor::AirQuality || identity_unknown){
        paths[index++]=AttributePathParams(0xFFFF,0x0402,0);
        paths[index++]=AttributePathParams(0xFFFF,0x0405,0);
    }
    if(sensor==M2sIkeaSensor::Motion) paths[index++]=AttributePathParams(0xFFFF,0x0406,0);
    if(sensor==M2sIkeaSensor::WaterLeak || sensor==M2sIkeaSensor::Contact)
        paths[index++]=AttributePathParams(0xFFFF,0x0045,0);
    if(sensor==M2sIkeaSensor::AirQuality){
        paths[index++]=AttributePathParams(0xFFFF,0x040D,0);
        paths[index++]=AttributePathParams(0xFFFF,0x042A,0);
    }
    if(identity_unknown){
        paths[index++]=AttributePathParams(0,0x0028,2);
        paths[index++]=AttributePathParams(0,0x0028,4);
    }
    if(query_battery) paths[index++]=AttributePathParams(0,0x002F,0x000C);
    if(index!=count){finish(false);return;}
    auto *cmd=chip::Platform::New<WebRead>(static_cast<uint32_t>(arg),std::move(paths),std::move(events));
    if(!cmd){finish(false);return;}
    ESP_LOGI("M2S_READ","READ_SEND node=%lu paths=%u",(unsigned long)arg,index);
    // send_command owns deletion on synchronous/asynchronous connection failure.
    cmd->send_command();
}
}
esp_err_t m2s_readings_request(uint32_t node){
    if(!node || node>65535)return ESP_ERR_INVALID_ARG;
    m2s_matter_status_t status{};m2s_matter_get_status(&status);
    if(!status.ready || status.pair_busy)return ESP_ERR_INVALID_STATE;
    const int64_t now=esp_timer_get_time();
    // NVS calls must run OUTSIDE the FreeRTOS critical section.
    const uint16_t configured_interval=load_interval_nvs(node);
    portENTER_CRITICAL(&mux);
    if(snapshot.busy){portEXIT_CRITICAL(&mux);return ESP_ERR_INVALID_STATE;}
    if(cache){
        int slot=-1;for(int i=0;i<M2S_DEVICE_LIMIT;i++){if(cache[i].node==node){slot=i;break;}if(!cache[i].node)slot=i;}
        if(slot>=0){
            if(cache[slot].node!=node){cache[slot]={};cache[slot].node=node;}
            cache[slot].interval_minutes=configured_interval;
            cache[slot].next_us=now+interval_us(configured_interval);
            battery_in_flight=now>=cache[slot].next_battery_us;
            if(battery_in_flight)cache[slot].next_battery_us=now+kBatteryRetryUs;
        } else battery_in_flight=false;
    }
    snapshot={};snapshot.node=node;snapshot.busy=true;snapshot.state=1;snapshot.started_us=now;
    portEXIT_CRITICAL(&mux);
    if(battery_in_flight)
        ESP_LOGI("M2S_BAT","BAT_QUERY node=%lu",(unsigned long)node);
    ESP_LOGI("M2S_READ","READ_QUEUED node=%lu interval_min=%u",(unsigned long)node,(unsigned)configured_interval);
    if(chip::DeviceLayer::PlatformMgr().ScheduleWork(work,static_cast<intptr_t>(node))!=CHIP_NO_ERROR){finish(false);return ESP_FAIL;}
    return ESP_OK;
}
void m2s_readings_snapshot(m2s_readings_t *out){
    const int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    if(snapshot.busy && snapshot.state==1 && now-snapshot.started_us>=90000000LL)snapshot.state=4;
    *out=snapshot;
    portEXIT_CRITICAL(&mux);
}

extern "C" void m2s_readings_init(void){
 cache=static_cast<Cached*>(heap_caps_calloc(M2S_DEVICE_LIMIT,sizeof(Cached),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!cache)ESP_LOGE("M2S_READ", "Background cache allocation failed");
 next_dispatch=esp_timer_get_time()+180000000LL; // let sleepy children register after boot
}
bool m2s_readings_cached(uint32_t node,m2s_readings_t *out){
 bool found=false;
 portENTER_CRITICAL(&mux);
 if(cache)for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node==node){*out=cache[i].report;out->node=node;found=true;break;}
 if(snapshot.node==node && snapshot.busy){out->node=node;out->busy=true;out->state=snapshot.state;out->started_us=snapshot.started_us;found=true;}
 portEXIT_CRITICAL(&mux);
 return found;
}
void m2s_readings_tick(){
 const int64_t now=esp_timer_get_time();
 if(!cache || now<next_dispatch)return;
 m2s_readings_t active{};m2s_readings_snapshot(&active);if(active.busy)return;
 m2s_network_status_t net{};m2s_network_status(&net);
 m2s_matter_status_t ms{};m2s_matter_get_status(&ms);
 if(!net.connected||net.ap_active||net.testing||!ms.ready||ms.pair_busy)return;
 bool attached=false;
 if(esp_openthread_lock_acquire(pdMS_TO_TICKS(20))){
   auto *instance=esp_openthread_get_instance();
   if(instance){auto role=otThreadGetDeviceRole(instance);attached=role==OT_DEVICE_ROLE_LEADER||role==OT_DEVICE_ROLE_ROUTER||role==OT_DEVICE_ROLE_CHILD;}
   esp_openthread_lock_release();
 }
 if(!attached)return;
 m2s_device_t devices[M2S_DEVICE_LIMIT]{};const size_t count=m2s_devices_snapshot(devices,M2S_DEVICE_LIMIT);
 portENTER_CRITICAL(&mux);
 for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node){bool present=false;for(size_t j=0;j<count;j++)if(devices[j].node==cache[i].node)present=true;if(!present)cache[i]={};}
 portEXIT_CRITICAL(&mux);
 // Establish a single combined Matter subscription once the device was
 // identified by the existing initial read. Retain periodic reads as fallback.
 for(size_t j=0;j<count;j++){
   const auto &d=devices[j];
   if(d.state!=0 && d.state!=2)continue;
   bool start_sub=false;
   portENTER_CRITICAL(&mux);
   for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node==d.node){
       auto &c=cache[i];
       if(c.report.vendor_valid && c.report.product_valid &&
          m2s_ikea_sensor(c.report.vendor_id,c.report.product_id)==M2sIkeaSensor::ThermHygro &&
          (!c.sub_requested ||
           ((now-c.sub_report_us > kSubscriptionRetryUs) &&
            (now-c.sub_requested_us > kSubscriptionRetryUs)))){
           c.sub_requested=true;
           c.sub_reporting=false;
           c.sub_requested_us=now;
           start_sub=true;
       }
       break;
   }
   portEXIT_CRITICAL(&mux);
   if(start_sub){
       if(chip::DeviceLayer::PlatformMgr().ScheduleWork(sub_work,static_cast<intptr_t>(d.node))!=CHIP_NO_ERROR){
           portENTER_CRITICAL(&mux);
           for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node==d.node)cache[i].sub_requested=false;
           portEXIT_CRITICAL(&mux);
           ESP_LOGW("M2S_SUB","SUB_QUEUE_FAILED node=%lu",(unsigned long)d.node);
       }
       next_dispatch=now+10000000LL;
       return; // Avoid starting a poll during subscription setup.
   }
 }
 for(size_t j=0;j<count;j++){
   const size_t index=(cursor+j)%count;const auto &d=devices[index];if(d.state!=0&&d.state!=2)continue;
   int64_t due=0;portENTER_CRITICAL(&mux);for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node==d.node)due=cache[i].next_us;portEXIT_CRITICAL(&mux);
   if(now<due)continue;
   cursor=(index+1)%count;
   next_dispatch=now+10000000LL;
   ESP_LOGI("M2S_READ","POLL_DUE node=%lu due_ms_ago=%lld",(unsigned long)d.node,(long long)((now-due)/1000));
   const esp_err_t result=m2s_readings_request(d.node);
   if(result!=ESP_OK)ESP_LOGW("M2S_READ","POLL_REQUEST_FAILED node=%lu err=%ld",(unsigned long)d.node,(long)result);
   return;
 }
}

uint16_t m2s_readings_interval_minutes(uint32_t node){
 if(node<1 || node>65535)return kDefaultIntervalMinutes;
 uint16_t result=0;
 portENTER_CRITICAL(&mux);
 if(cache)for(int i=0;i<M2S_DEVICE_LIMIT;i++){
   if(cache[i].node==node){result=cache[i].interval_minutes;break;}
 }
 portEXIT_CRITICAL(&mux);
 return result?result:load_interval_nvs(node);
}
esp_err_t m2s_readings_set_interval(uint32_t node,uint16_t minutes){
 if(node<1 || node>65535 || minutes<kMinIntervalMinutes || minutes>kMaxIntervalMinutes)
   return ESP_ERR_INVALID_ARG;
 if(!cache)return ESP_ERR_INVALID_STATE;
 // Only a locally registered node may receive an interval override.
 m2s_device_t devices[M2S_DEVICE_LIMIT]{};
 const size_t count=m2s_devices_snapshot(devices,M2S_DEVICE_LIMIT);
 bool registered=false;
 for(size_t i=0;i<count;i++)if(devices[i].node==node && (devices[i].state==0 || devices[i].state==2)){
   registered=true;break;
 }
 if(!registered)return ESP_ERR_NOT_FOUND;
 nvs_handle_t nvs=0;
 esp_err_t err=nvs_open(kPollNamespace,NVS_READWRITE,&nvs);
 if(err!=ESP_OK)return err;
 char key[12]{};poll_key(node,key);
 err=nvs_set_u16(nvs,key,minutes);
 if(err==ESP_OK)err=nvs_commit(nvs);
 nvs_close(nvs);
 if(err!=ESP_OK)return err;
 // Do not start a read immediately; apply a new due time without reboot.
 const int64_t next=esp_timer_get_time()+interval_us(minutes);
 portENTER_CRITICAL(&mux);
 for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node==node){
   cache[i].interval_minutes=minutes;
   cache[i].next_us=next;
   break;
 }
 portEXIT_CRITICAL(&mux);
 ESP_LOGI("M2S_READ","Poll interval for node %lu: %u min",(unsigned long)node,(unsigned)minutes);
 return ESP_OK;
}

bool m2s_readings_subscription(uint32_t node,m2s_subscription_t *out){
 if(!out)return false;
 *out={};
 bool found=false;
 portENTER_CRITICAL(&mux);
 if(cache)for(int i=0;i<M2S_DEVICE_LIMIT;i++)if(cache[i].node==node){
   const auto &c=cache[i];
   out->requested=c.sub_requested;out->reporting=c.sub_reporting;
   out->requested_us=c.sub_requested_us;out->report_us=c.sub_report_us;
   out->report_count=c.sub_report_count;found=true;break;
 }
 portEXIT_CRITICAL(&mux);
 return found;
}
