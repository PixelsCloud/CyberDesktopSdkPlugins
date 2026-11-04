// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace cyberglass::dwm::host {
// Owns one remote operation, including its argument, through completion.
class RemoteCall final {
public:
    explicit RemoteCall(void* process);
    ~RemoteCall();
    RemoteCall(const RemoteCall&) = delete;
    RemoteCall& operator=(const RemoteCall&) = delete;
    uint32_t invoke(uintptr_t address, std::span<const std::byte> input = {}, std::span<std::byte> output = {},
                    uint32_t timeoutMs = 1000);
    uintptr_t pendingArgument() const;
    bool pending() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cyberglass::dwm::host
