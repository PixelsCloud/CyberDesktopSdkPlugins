#pragma once

#include "tools/PreviewOptions.h"

namespace cdsdk {
class PreviewReplay {
public:
    explicit PreviewReplay(const std::filesystem::path& path);

    void dispatch(uint32_t frame, NativeInstance& instance);

private:
    struct Event {
        uint32_t frame = 0, key = 0, modifiers = 0;
        CdInput input{};
    };

    std::vector<Event> events_;
    size_t cursor_ = 0;
};
}  // namespace cdsdk
