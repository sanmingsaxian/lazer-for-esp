#include "wifi_mgr.h"
#include "board.h"
#include "settings.h"
#include "strutil.h"

#include <cstring>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

namespace lz {

namespace {
constexpr char kTag[] = "WifiMgr";
esp_netif_t* s_apNetif = nullptr;
esp_netif_t* s_staNetif = nullptr;
bool s_started = false;
bool s_connected = false;
bool s_connecting = false;
uint32_t s_lastReconnect = 0;
std::string s_ip = "0.0.0.0";

void applyStaConfig() {
    wifi_config_t sta = {};
    strncpy((char*)sta.sta.ssid, Settings.staSsid.c_str(), sizeof(sta.sta.ssid) - 1);
    strncpy((char*)sta.sta.password, Settings.staPassword.c_str(), sizeof(sta.sta.password) - 1);
    sta.sta.threshold.authmode = Settings.staPassword.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &sta);
}

void eventHandler(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (!Settings.staSsid.empty()) {
            s_connecting = true;
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip = "0.0.0.0";
        if (!Settings.staSsid.empty()) {
            s_connecting = true;
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(data);
        char buf[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, buf, sizeof(buf));
        s_ip = buf;
        s_connected = true;
        s_connecting = false;
        ESP_LOGI(kTag, "STA connected: %s", buf);
    }
}
}  // namespace

void wifi_start() {
    if (s_started) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_apNetif = esp_netif_create_default_wifi_ap();
    s_staNetif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_staNetif, "lazer-cardputer");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        eventHandler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        eventHandler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap = {};
    strncpy((char*)ap.ap.ssid, Settings.apSsid.c_str(), sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = (uint8_t)Settings.apSsid.size();
    strncpy((char*)ap.ap.password, Settings.apPassword.c_str(), sizeof(ap.ap.password) - 1);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = Settings.apPassword.size() >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ap.ap.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    applyStaConfig();
    ESP_ERROR_CHECK(esp_wifi_start());
    // 8.5 dBm in 0.25 dBm units; reduces current peaks with screen + amplifier.
    esp_wifi_set_max_tx_power(34);
    s_started = true;
    ESP_LOGI(kTag, "AP %s started, maxClients=2, txPower=8.5dBm", Settings.apSsid.c_str());
    if (!Settings.staSsid.empty())
        ESP_LOGI(kTag, "STA connecting to %s", Settings.staSsid.c_str());
}

void wifi_stop() {
    if (!s_started) return;
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy(s_apNetif);
    esp_netif_destroy(s_staNetif);
    s_apNetif = nullptr;
    s_staNetif = nullptr;
    s_started = false;
    s_connected = false;
}

bool wifi_sta_connected() { return s_connected; }

std::string wifi_sta_ip() { return s_ip; }

std::string wifi_ap_ip() {
    if (!s_apNetif) return "0.0.0.0";
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_apNetif, &info) != ESP_OK) return "0.0.0.0";
    char buf[16];
    esp_ip4addr_ntoa(&info.ip, buf, sizeof(buf));
    return buf;
}

int wifi_rssi() {
    if (!s_connected) return 0;
    wifi_ap_record_t record;
    if (esp_wifi_sta_get_ap_info(&record) != ESP_OK) return 0;
    return record.rssi;
}

int wifi_status_code() {
    if (s_connected) return 3;   // WL_CONNECTED
    if (s_connecting) return 0;  // WL_IDLE_STATUS
    return 6;                    // WL_DISCONNECTED
}

bool wifi_scan(std::vector<WifiScanEntry>& out, size_t maxEntries) {
    out.clear();
    if (!s_started) return false;
    wifi_scan_config_t config = {};
    config.show_hidden = false;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.max = pdMS_TO_TICKS(250);
    if (esp_wifi_scan_start(&config, true) != ESP_OK) return false;
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > maxEntries) count = (uint16_t)maxEntries;
    if (!count) return true;
    std::vector<wifi_ap_record_t> records(count);
    if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) return false;
    for (uint16_t i = 0; i < count; ++i) {
        WifiScanEntry entry;
        entry.ssid = (const char*)records[i].ssid;
        entry.rssi = records[i].rssi;
        entry.secure = records[i].authmode != WIFI_AUTH_OPEN;
        out.push_back(entry);
    }
    return true;
}

void wifi_connect_now() {
    if (!s_started) return;
    applyStaConfig();
    esp_wifi_disconnect();
    s_connecting = true;
    s_lastReconnect = millis();
    esp_wifi_connect();
}

void wifi_disconnect_sta() {
    if (s_started) esp_wifi_disconnect();
    s_connected = false;
    s_connecting = false;
}

void wifi_retry_if_needed() {
    if (!s_started || Settings.staSsid.empty() || s_connected) return;
    if (millis() - s_lastReconnect > 20000) {
        s_lastReconnect = millis();
        esp_wifi_connect();
    }
}

}  // namespace lz
