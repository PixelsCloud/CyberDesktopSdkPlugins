// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace cyberglass::dwm {
struct DiagnosticPacket;
}

namespace cyberglass::dwm::host {

// A session's DWM process, never an application background source. Loading is
// persistent until detach or DWM exit; destroying this controller closes handles.
class DwmAttachment final {
public:
    explicit DwmAttachment(uint32_t session);
    ~DwmAttachment();
    void attach(const std::filesystem::path& library, const std::filesystem::path& symbols);
    void detach(const std::filesystem::path& library);
    uint32_t process() const;
    uint64_t generation() const;
    DiagnosticPacket diagnostics(const std::filesystem::path& library, bool enabled);
    bool alive() const;
    void* processHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace cyberglass::dwm::host
