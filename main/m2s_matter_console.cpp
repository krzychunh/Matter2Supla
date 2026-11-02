#include "sdkconfig.h"
#include "m2s_matter_controller.h"
#include "m2s_matter_api.h"
#include "m2s_devices.h"
#include "m2s_ble_scan.h"

#include <atomic>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdint>

#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
#include <esp_matter.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_read_command.h>
#include <platform/CHIPDeviceLayer.h>
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include <openthread/dataset.h>
#include <openthread/thread.h>
#endif

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "M2S_MATTER_CLI";
std::atomic_bool s_start_requested{false};
std::atomic_bool s_commissioner_ready{false};
std::atomic_int s_start_error{0};

void controller_task(void *)
{
    ESP_LOGW(TAG, "Starting experimental controller; CHIP may write NVS");
    const int err = m2s_matter_controller_start_experimental();
    if (err == ESP_OK) {
        s_commissioner_ready.store(true);
        ESP_LOGI(TAG, "Matter Controller initialized");
    } else {
        s_start_error.store(err);
        ESP_LOGE(TAG, "Matter Controller start failed: %d", err);
    }
    vTaskDelete(nullptr);
}


#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
std::atomic_bool s_pair_busy{false};
std::atomic_uint s_pair_node{0};
std::atomic_int s_pair_result{0}; // 0 idle, 1 pending, 2 success, 3 failure
struct PairRequest {
    uint64_t node;
    uint32_t pin;
    uint16_t discriminator;
};
PairRequest s_pair_request{};

void wipe_secret(void *data, size_t length)
{
    volatile unsigned char *p = static_cast<volatile unsigned char *>(data);
    while (length--) {
        *p++ = 0;
    }
}

void pair_failed()
{
    m2s_devices_pair_finished(s_pair_node.load(),3);
    s_pair_result.store(3);
    s_pair_busy.store(false);
}

void pair_pase_done(CHIP_ERROR err)
{
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "PASE failed: %s", chip::ErrorStr(err));
        pair_failed();
    } else {
        ESP_LOGI(TAG, "PASE established; commissioning is still pending");
    }
}

void pair_success(chip::ScopedNodeId)
{
    ESP_LOGI(TAG, "M2S_PAIR_RESULT=SUCCESS");
    m2s_devices_pair_finished(s_pair_node.load(),2);
    s_pair_result.store(2);
    s_pair_busy.store(false);
}

void pair_failure(chip::ScopedNodeId, CHIP_ERROR err,
                  chip::Controller::CommissioningStage stage,
                  std::optional<chip::Credentials::AttestationVerificationResult>)
{
    ESP_LOGE(TAG, "M2S_PAIR_RESULT=FAILURE stage=%d error=%s",
             static_cast<int>(stage), chip::ErrorStr(err));
    pair_failed();
}

bool parse_decimal(const char *value, uint64_t maximum, uint64_t &result)
{
    if (!value || !*value) {
        return false;
    }
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (errno || !end || *end || parsed > maximum) {
        return false;
    }
    result = parsed;
    return true;
}

bool valid_pin(uint32_t pin)
{
    if (pin == 0 || pin > 99999998 || pin == 12345678 || pin == 87654321) {
        return false;
    }
    for (uint32_t digit = 1; digit <= 9; ++digit) {
        if (pin == digit * 11111111u) {
            return false;
        }
    }
    return true;
}

void pair_thread_task(void *)
{
    PairRequest request = s_pair_request;
    wipe_secret(&s_pair_request, sizeof(s_pair_request));
    otOperationalDatasetTlvs dataset = {};
    bool dataset_ok = false;

    if (esp_openthread_lock_acquire(pdMS_TO_TICKS(2000))) {
        otInstance *instance = esp_openthread_get_instance();
        if (instance) {
            const otDeviceRole role = otThreadGetDeviceRole(instance);
            if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
                role == OT_DEVICE_ROLE_LEADER) {
                dataset_ok = otDatasetGetActiveTlvs(instance, &dataset) == OT_ERROR_NONE &&
                             dataset.mLength != 0;
            }
        }
        esp_openthread_lock_release();
    }

    if (!dataset_ok) {
        ESP_LOGE(TAG, "Pairing refused: active Thread Dataset unavailable");
        wipe_secret(&request, sizeof(request));
        wipe_secret(&dataset, sizeof(dataset));
        pair_failed();
        vTaskDelete(nullptr);
        return;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;
    {
        // OpenThread lock has already been released.
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        auto *commissioner =
            esp_matter::controller::matter_controller_client::get_instance().get_commissioner();
        if (s_commissioner_ready.load() && commissioner &&
            commissioner->GetPairingDelegate() == nullptr &&
            commissioner->GetNodeId() != request.node) {
            esp_matter::controller::pairing_command_callbacks_t callbacks = {};
            callbacks.pase_callback = pair_pase_done;
            callbacks.commissioning_success_callback = pair_success;
            callbacks.commissioning_failure_callback = pair_failure;
            esp_matter::controller::pairing_command::get_instance().set_callbacks(callbacks);

            result = esp_matter::controller::pairing_command::pairing_ble_thread(
                request.node, request.pin, request.discriminator,
                dataset.mTlvs, dataset.mLength);
        }

        // AutoCommissioner copies Dataset into its own buffer.
        wipe_secret(&dataset, sizeof(dataset));
        wipe_secret(&request, sizeof(request));

        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Pairing request rejected: %d", static_cast<int>(result));
            pair_failed();
        } else {
            ESP_LOGI(TAG, "Pairing request accepted; await M2S_PAIR_RESULT");
        }
    }
    vTaskDelete(nullptr);
}

int submit_pair(uint64_t node, uint32_t pin, uint16_t discriminator)
{
    if (!s_commissioner_ready.load() || node == 0 || node > 65535 ||
        !valid_pin(pin) || discriminator > 4095) {
        return ESP_ERR_INVALID_ARG;
    }
    // Scanning uses the same BLE host as CHIP BLE commissioning.
    if (m2s_ble_scan_active()) return ESP_ERR_INVALID_STATE;
    if (s_pair_busy.exchange(true)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_pair_node.store(static_cast<uint32_t>(node));
    s_pair_result.store(1);
    s_pair_request = {node, pin, discriminator};
    if (xTaskCreate(pair_thread_task, "m2s_pair", 8192, nullptr, 4, nullptr) != pdPASS) {
        wipe_secret(&s_pair_request, sizeof(s_pair_request));
        pair_failed();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGW(TAG, "Pairing requested; commissioning may write NVS");
    return ESP_OK;
}

int pair_thread_command(int argc, char **argv)
{
    if (argc != 6 || std::strcmp(argv[5], "confirm") != 0) {
        ESP_LOGW(TAG, "Usage: m2smatter pair-thread <node 1..65535> <PIN> <discriminator 0..4095> confirm");
        return 1;
    }

    uint64_t node = 0, pin = 0, discriminator = 0;
    const bool valid = parse_decimal(argv[2], 65535, node) && node != 0 &&
        parse_decimal(argv[3], 99999998, pin) && valid_pin(static_cast<uint32_t>(pin)) &&
        parse_decimal(argv[4], 4095, discriminator);

    // Best effort: remove PIN from the mutable console argument buffer.
    wipe_secret(argv[3], std::strlen(argv[3]));

    if (!valid || !s_commissioner_ready.load()) {
        ESP_LOGW(TAG, "Pairing refused: invalid arguments or commissioner not ready");
        return 1;
    }
    return submit_pair(node, static_cast<uint32_t>(pin),
                       static_cast<uint16_t>(discriminator)) == ESP_OK ? 0 : 1;
}
#endif


#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
struct ReadRequest {
    uint64_t node;
    uint16_t endpoint;
    uint32_t cluster;
    uint32_t attribute;
};
ReadRequest s_read_request{};
std::atomic_bool s_read_queued{false};

void read_attribute_work(intptr_t)
{
    const ReadRequest request = s_read_request;
    const esp_err_t err = esp_matter::controller::send_read_attr_command(
        request.node, request.endpoint, request.cluster, request.attribute);
    s_read_queued.store(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read request rejected: %d", static_cast<int>(err));
    } else {
        ESP_LOGI(TAG, "Read dispatched; await response or read error");
    }
}

int read_attribute_command(int argc, char **argv)
{
    if (argc != 6) {
        ESP_LOGW(TAG, "Usage: m2smatter read <node> <endpoint> <cluster> <attribute>; decimal numbers");
        return 1;
    }

    uint64_t node = 0, endpoint = 0, cluster = 0, attribute = 0;
    if (!parse_decimal(argv[2], 65535, node) || node == 0 ||
        !parse_decimal(argv[3], 65535, endpoint) ||
        !parse_decimal(argv[4], UINT32_MAX, cluster) ||
        !parse_decimal(argv[5], UINT32_MAX, attribute)) {
        ESP_LOGW(TAG, "Invalid read arguments");
        return 1;
    }
    if (!s_commissioner_ready.load() || s_pair_busy.load()) {
        ESP_LOGW(TAG, "Read refused: commissioner unavailable or pairing pending");
        return 1;
    }
    if (s_read_queued.exchange(true)) {
        ESP_LOGW(TAG, "Read dispatch already queued");
        return 1;
    }

    s_read_request = {node, static_cast<uint16_t>(endpoint),
                      static_cast<uint32_t>(cluster),
                      static_cast<uint32_t>(attribute)};
    const CHIP_ERROR err =
        chip::DeviceLayer::PlatformMgr().ScheduleWork(read_attribute_work, 0);
    if (err != CHIP_NO_ERROR) {
        s_read_queued.store(false);
        ESP_LOGE(TAG, "Cannot schedule read: %s", chip::ErrorStr(err));
        return 1;
    }
    return 0;
}
#endif

int submit_start()
{
    if (s_start_requested.exchange(true)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(controller_task, "m2s_matter", 12288, nullptr, 4, nullptr) != pdPASS) {
        s_start_requested.store(false);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGW(TAG, "Controller task requested; waiting for Wi-Fi and backbone");
    return ESP_OK;
}

int controller_command(int argc, char **argv)
{
#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
    if (argc >= 2 && std::strcmp(argv[1], "read") == 0) {
        return read_attribute_command(argc, argv);
    }
    if (argc >= 2 && std::strcmp(argv[1], "pair-thread") == 0) {
        return pair_thread_command(argc, argv);
    }
#endif
    if (argc == 2 && std::strcmp(argv[1], "status") == 0) {
        ESP_LOGI(TAG, "started=%d, start_requested=%d, commissioner_ready=%d",
                 m2s_matter_controller_probe(), static_cast<int>(s_start_requested.load()),
                 static_cast<int>(s_commissioner_ready.load()));
#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
        ESP_LOGI(TAG, "pair_busy=%d, pair_result=%d (0=idle 1=pending 2=success 3=failure)",
                 static_cast<int>(s_pair_busy.load()), s_pair_result.load());
#endif
        return 0;
    }

    if (argc != 3 || std::strcmp(argv[1], "start") != 0 || std::strcmp(argv[2], "confirm") != 0) {
        ESP_LOGW(TAG, "Usage: m2smatter status | m2smatter start confirm");
        return 1;
    }

    return submit_start() == ESP_OK ? 0 : 1;
}
} // namespace


extern "C" int m2s_matter_request_start(void)
{
    return submit_start();
}

extern "C" int m2s_matter_request_pair(uint64_t node, uint32_t pin,
                                      uint16_t discriminator)
{
#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
    return submit_pair(node, pin, discriminator);
#else
    (void)node;
    (void)pin;
    (void)discriminator;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

extern "C" void m2s_matter_get_status(m2s_matter_status_t *status)
{
    if (!status) return;
    *status = {};
    status->started = m2s_matter_controller_probe();
    status->start_requested = s_start_requested.load();
    status->ready = s_commissioner_ready.load();
    status->start_error = s_start_error.load();
#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
    status->pair_busy = s_pair_busy.load();
    status->pair_result = s_pair_result.load();
#endif
}

extern "C" int m2s_matter_console_register(void)
{
    esp_console_cmd_t cmd = {};
    cmd.command = "m2smatter";
    cmd.help = "Matter Controller: status | start confirm | pair-thread | read (experimental; may modify NVS)";
    cmd.func = controller_command;
    return esp_console_cmd_register(&cmd);
}
