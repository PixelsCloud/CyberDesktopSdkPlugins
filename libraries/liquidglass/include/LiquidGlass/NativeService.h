#pragma once

#include <cstdint>
#include <memory>

namespace liquidglass {

struct NativeBackendIdentity {
    uint32_t process = 0;
    uint64_t generation = 0;
    bool operator==(const NativeBackendIdentity&) const = default;
};

struct NativeServiceStatus {
    NativeBackendIdentity backend;
    uint32_t error = 0;
};

class NativeServiceConnection final {
public:
    ~NativeServiceConnection();
    NativeServiceStatus status() const;
    void observeWindow(uintptr_t window, uint32_t cookie);
    void forgetWindow(uintptr_t window, uint32_t cookie);

private:
    friend class NativeServiceRuntime;
    struct Impl;
    explicit NativeServiceConnection(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

// Own at the process's composition root. Connections contain value state only;
// destroying the runtime cancels and joins its transport before releasing IO.
class NativeServiceRuntime final {
public:
    NativeServiceRuntime();
    ~NativeServiceRuntime();
    std::shared_ptr<NativeServiceConnection> connect();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace liquidglass
