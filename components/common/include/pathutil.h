#pragma once
#include <string>

namespace lz {

std::string baseName(const std::string& path);
std::string parentPath(const std::string& path);
std::string joinPath(const std::string& parent, const std::string& name);
// Port of WebControl normalizePath: absolute, <=180 bytes, no "..", no control
// chars or backslashes; collapses "." and duplicate slashes.
bool normalizePath(const std::string& input, std::string& output);
bool validChildName(const std::string& name);
bool protectedSystemPath(const std::string& path);
bool textFilePath(const std::string& path);
std::string contentTypeFor(const std::string& path);

}  // namespace lz
