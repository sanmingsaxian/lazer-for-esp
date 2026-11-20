#pragma once
#include <string>
#include <vector>

namespace lz {

struct WifiScanEntry {
    std::string ssid;
    int rssi = 0;
    bool secure = true;
};

// AP+STA radio management ported from WebControl::applyNetwork/update.
// AP: channel 1, max 2 clients, 8.5 dBm TX power (v1.1.1 current-peak policy).
void wifi_start();
void wifi_stop();
bool wifi_sta_connected();
std::string wifi_sta_ip();
std::string wifi_ap_ip();
int wifi_rssi();
// Arduino wl_status_t-compatible code for the WebUI state contract.
int wifi_status_code();
bool wifi_scan(std::vector<WifiScanEntry>& out, size_t maxEntries = 16);
void wifi_connect_now();      // (re)connect STA with saved settings
void wifi_disconnect_sta();
void wifi_retry_if_needed();  // periodic reconnect, 20 s cadence

}  // namespace lz
