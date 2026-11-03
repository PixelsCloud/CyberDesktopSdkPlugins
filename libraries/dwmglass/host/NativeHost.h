// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>

namespace cyberglass::dwm::host {

class NativeHost final {
public:
    NativeHost(std::filesystem::path library, std::filesystem::path symbols);
    ~NativeHost();
    void run();
    void requestStop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace cyberglass::dwm::host
