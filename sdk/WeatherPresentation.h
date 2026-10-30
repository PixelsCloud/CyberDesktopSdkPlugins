#pragma once

#include <string>

namespace cdsdk::weather {

// WMO condition codes mapped to the bundled Meteocons asset basenames.
// This utility performs no I/O and does not own image resources.
std::string iconName(int code, bool day);
std::wstring condition(int code, bool english);

}  // namespace cdsdk::weather
