#include "tools/WindowsApartment.h"

#include <Windows.h>
#include <objbase.h>

#include <stdexcept>

namespace cdsdk {
WindowsApartment::WindowsApartment() {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    initialized_ = SUCCEEDED(result);
    if (!initialized_ && result != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("Cannot initialize preview COM apartment");
    }
}

WindowsApartment::~WindowsApartment() {
    if (initialized_) {
        CoUninitialize();
    }
}
}  // namespace cdsdk
