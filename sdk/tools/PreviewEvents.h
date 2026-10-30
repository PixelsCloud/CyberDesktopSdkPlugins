#pragma once

#include <memory>

namespace cdsdk {
class PreviewEvents {
public:
    PreviewEvents();
    ~PreviewEvents();
    void pump();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace cdsdk
