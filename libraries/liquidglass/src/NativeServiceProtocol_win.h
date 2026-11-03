#pragma once

#include <cstdint>

namespace liquidglass::native {

inline constexpr wchar_t kServiceName[] = L"CyberDesktopNativeGlass";
inline constexpr wchar_t kServicePipe[] = L"\\\\.\\pipe\\CyberDesktop.NativeGlass.v1";
inline constexpr uint32_t kHostMagic = 0x43474831, kBootMagic = 0x43474231;
enum class HostOperation : uint32_t { Acquire = 1, Release = 2, Status = 3 };

struct HostRequest {
    uint32_t magic = kHostMagic, version = 1;
    HostOperation operation = HostOperation::Status;
    uint32_t reserved = 0;
    uint64_t lease = 0;
};

struct HostResponse {
    uint32_t magic = kHostMagic, version = 1, error = 0, process = 0;
    uint64_t generation = 0, watchHandle = 0;
};

struct BootRequest {
    uint32_t magic = kBootMagic, version = 1, pathCharacters = 0, reserved = 0;
    uint64_t generation = 0;
};

static_assert(sizeof(HostRequest) == 24);
static_assert(sizeof(HostResponse) == 32);
static_assert(sizeof(BootRequest) == 24);

}  // namespace liquidglass::native
