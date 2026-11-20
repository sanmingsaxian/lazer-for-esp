#include "storage.h"
#include "pins.h"

#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

namespace lz {

namespace {
sdmmc_card_t* s_card = nullptr;
bool s_busReady = false;
constexpr const char* kRoot = "/sdcard";
}  // namespace

const char* storage_root() { return kRoot; }

std::string storage_vfs_path(const std::string& sdPath) {
    if (sdPath.empty() || sdPath[0] != '/') return std::string(kRoot) + "/" + sdPath;
    return std::string(kRoot) + sdPath;
}

bool storage_ready() { return s_card != nullptr; }

bool storage_mount() {
    if (s_card) return true;
    if (!s_busReady) {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = pins::SD_MOSI;
        bus.miso_io_num = pins::SD_MISO;
        bus.sclk_io_num = pins::SD_SCK;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 4096;
        if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
        s_busReady = true;
    }
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = (gpio_num_t)pins::SD_CS;
    slot.gpio_cd = SDSPI_SLOT_NO_CD;
    slot.gpio_wp = SDSPI_SLOT_NO_WP;
    slot.gpio_int = GPIO_NUM_NC;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 25000;  // matches original SD.begin(..., 25 MHz)

    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;
    mount.max_files = 8;
    mount.allocation_unit_size = 16 * 1024;

    esp_err_t err = esp_vfs_fat_sdspi_mount(kRoot, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        s_card = nullptr;
        return false;
    }
    return true;
}

void storage_unmount() {
    if (!s_card) return;
    esp_vfs_fat_sdcard_unmount(kRoot, s_card);
    s_card = nullptr;
}

bool storage_capacity(uint64_t& totalBytes, uint64_t& usedBytes) {
    totalBytes = 0;
    usedBytes = 0;
    if (!s_card) return false;
    uint64_t freeBytes = 0;
    if (esp_vfs_fat_info(kRoot, &totalBytes, &freeBytes) != ESP_OK) return false;
    usedBytes = totalBytes > freeBytes ? totalBytes - freeBytes : 0;
    return true;
}

}  // namespace lz
