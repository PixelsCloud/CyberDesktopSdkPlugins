#pragma once

#include <functional>
#include <memory>

namespace liquidglass::native {

class ServiceWatcher final {
public:
    explicit ServiceWatcher(std::function<void(bool)> changed);
    ~ServiceWatcher();
    bool refresh(bool reopen);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace liquidglass::native
