#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "netease_types.h"

namespace lz {

// Pure cJSON parsing of NeteaseCloudMusicApi responses, separated from the
// HTTP transport so the contract is host-testable.

std::string jsonMessageOf(const char* json, const std::string& fallback);

bool parseSearchResponse(const char* json, std::vector<OnlineTrack>& out, std::string& error);

bool parseResolveResponse(const char* json, OnlinePlayback& out, std::string& error);

// Returns the QR status code (800/801/802/803) or -1 on error. On 803 the
// cookie/nickname/userId fields are filled (cookie may be empty; the caller
// falls back to the Set-Cookie header).
int parseQrCheckResponse(const char* json, std::string& cookie, std::string& nickname,
                         uint64_t& userId, std::string& error);

bool parseLoginResponse(const char* json, std::string& cookie, std::string& nickname,
                        uint64_t& userId, std::string& error);

// hasProfile=false means the login is no longer valid.
bool parseStatusResponse(const char* json, bool& hasProfile, std::string& nickname,
                         uint64_t& userId);

}  // namespace lz
