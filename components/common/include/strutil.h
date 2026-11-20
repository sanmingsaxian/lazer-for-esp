#pragma once
#include <cstdint>
#include <string>

namespace lz {

std::string trim(const std::string& value);
std::string toLower(std::string value);
bool startsWith(const std::string& value, const std::string& prefix);
bool endsWith(const std::string& value, const std::string& suffix);
uint32_t fnv1a(const std::string& value);
std::string urlEncode(const std::string& value);
std::string urlDecode(const std::string& value);
std::string jsonQuote(const std::string& value);
std::string u64str(uint64_t value);

}  // namespace lz
