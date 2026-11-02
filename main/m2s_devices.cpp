#include "m2s_devices.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
namespace {
struct Registry {uint32_t magic=0x4d325244;uint32_t next=2; m2s_device_t items[M2S_DEVICE_LIMIT]{};};
Registry registry;
StaticSemaphore_t guard_storage;
SemaphoreHandle_t guard=nullptr;
esp_err_t persist(){nvs_handle_t h;esp_err_t e=nvs_open("m2s_devices",NVS_READWRITE,&h);if(e!=ESP_OK)return e;e=nvs_set_blob(h,"registry",&registry,sizeof(registry));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);return e;}
bool good_name(const char *name){return name && name[0] && strnlen(name,49)<=48;}
}
extern "C" esp_err_t m2s_devices_init(){
    guard=xSemaphoreCreateMutexStatic(&guard_storage);
    if(!guard)return ESP_ERR_NO_MEM;
    nvs_handle_t h;size_t n=sizeof(registry);
    if(nvs_open("m2s_devices",NVS_READONLY,&h)==ESP_OK){esp_err_t e=nvs_get_blob(h,"registry",&registry,&n);nvs_close(h);if(e!=ESP_OK || n!=sizeof(registry) || registry.magic!=0x4d325244 || registry.next<2 || registry.next>65536)registry=Registry{};}
    for(auto &item:registry.items){item.name[48]=0;if(item.state==1)item.state=0;}
    return ESP_OK;
}
extern "C" size_t m2s_devices_snapshot(m2s_device_t *out,size_t capacity){
    if(!guard || !out)return 0;
    size_t count=0;
    xSemaphoreTake(guard,portMAX_DELAY);for(const auto &i:registry.items)if(i.node && count<capacity)out[count++]=i;xSemaphoreGive(guard);return count;
}
extern "C" esp_err_t m2s_devices_add(const char *name,uint32_t *node){
    if(!guard || !node || !good_name(name))return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(guard,portMAX_DELAY);
    esp_err_t e=ESP_ERR_NO_MEM;
    for(auto &i:registry.items)if(i.node && i.state==3 && strcmp(i.name,name)==0){
        i.state=1;e=persist();if(e==ESP_OK)*node=i.node;else i.state=3;
        xSemaphoreGive(guard);return e;
    }
    if(registry.next<=65535)for(auto &i:registry.items)if(!i.node){
        const uint32_t id=registry.next++;i.node=id;i.state=1;strncpy(i.name,name,48);i.name[48]=0;
        e=persist();if(e==ESP_OK)*node=id;else{i={};registry.next=id;}break;
    }
    xSemaphoreGive(guard);return e;
}
extern "C" esp_err_t m2s_devices_adopt(const char *name,uint32_t node){
    if(!guard || !good_name(name) || node<1 || node>65535)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(guard,portMAX_DELAY);esp_err_t e=ESP_ERR_NO_MEM;
    for(const auto &i:registry.items)if(i.node==node){xSemaphoreGive(guard);return ESP_ERR_INVALID_STATE;}
    uint32_t previous=registry.next;
    for(auto &i:registry.items)if(!i.node){i.node=node;strncpy(i.name,name,48);i.name[48]=0;i.state=0;if(node>=registry.next)registry.next=node+1;e=persist();if(e!=ESP_OK){i={};registry.next=previous;}break;}
    xSemaphoreGive(guard);return e;
}
extern "C" void m2s_devices_pair_finished(uint32_t node,int state){
    if(!guard)return;
    xSemaphoreTake(guard,portMAX_DELAY);for(auto &i:registry.items)if(i.node==node){i.state=state;persist();break;}xSemaphoreGive(guard);
}

// Remove only the local registry entry; do not alter Matter fabric or reuse NodeId.
extern "C" esp_err_t m2s_devices_remove(uint32_t node){
    if(!guard || !node || node>65535)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(guard,portMAX_DELAY);
    esp_err_t e=ESP_ERR_NOT_FOUND;
    for(auto &item:registry.items)if(item.node==node){
        if(item.state==1){e=ESP_ERR_INVALID_STATE;break;}
        const m2s_device_t saved=item;item={};e=persist();
        if(e!=ESP_OK)item=saved;
        break;
    }
    xSemaphoreGive(guard);return e;
}
