#pragma once

#include <cstddef>
#include <string>

namespace cdcontent {
std::wstring wide(const std::string& value);
std::string clean(const std::string& value, size_t limit = 700);
bool webUrl(const std::string& value);
std::string resolveUrl(const std::string& base, const std::string& relative);
}  // namespace cdcontent
