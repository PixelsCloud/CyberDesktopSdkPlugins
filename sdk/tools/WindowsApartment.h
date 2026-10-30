#pragma once

namespace cdsdk {
// The preview thread keeps COM initialized until all plugin and host resources are released.
class WindowsApartment {
public:
    WindowsApartment();
    ~WindowsApartment();
    WindowsApartment(const WindowsApartment&) = delete;
    WindowsApartment& operator=(const WindowsApartment&) = delete;

private:
    bool initialized_ = false;
};
}  // namespace cdsdk
