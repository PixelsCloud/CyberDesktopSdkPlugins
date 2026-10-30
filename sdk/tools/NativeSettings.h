#pragma once
#include <Windows.h>

#include <string>

namespace cyberdesktop::package::v1 {
class SettingsPage;
}

namespace cdsdk {
class NativeInstance;
std::wstring settingsWide(const std::string& text);
void showSettings(HWND owner, NativeInstance& instance, const cyberdesktop::package::v1::SettingsPage& page);
}  // namespace cdsdk
