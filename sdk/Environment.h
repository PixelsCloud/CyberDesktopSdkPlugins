#pragma once

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <stdexcept>

#include "sdk/services/Environment.h"

namespace cdsdk {
class Environment {
public:
    void bind(const CdCreateContext& context);

    static CdEnvironmentSnapshot systemSnapshot();

    CdEnvironmentSnapshot snapshot() const;

    std::tm localTime() const;

    static std::tm localTime(const CdEnvironmentSnapshot& snapshot);

    bool available() const;

private:
    CdEnvironmentApi api_{};
    void* context_ = nullptr;
};
}  // namespace cdsdk
