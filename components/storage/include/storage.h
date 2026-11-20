#pragma once
#include <cstdint>
#include <string>

namespace lz {

// FAT32 microSD on SPI3 (SCK40/MISO39/MOSI14/CS12), mounted at /sdcard.
bool storage_mount();
bool storage_ready();
void storage_unmount();
const char* storage_root();  // "/sdcard"

// Map an SD-root-relative path ("/Music/a.mp3") to the VFS path.
std::string storage_vfs_path(const std::string& sdPath);

bool storage_capacity(uint64_t& totalBytes, uint64_t& usedBytes);

}  // namespace lz
