#include "tools/PreviewReplay.h"

namespace cdsdk {
PreviewReplay::PreviewReplay(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    const auto script = readPreviewJson(path);
    if (script.fields().size() != 1 || !script.fields().contains("events") ||
        script.fields().at("events").kind_case() != google::protobuf::Value::kListValue) {
        throw std::invalid_argument("Replay must contain an events array");
    }
    for (const auto& value : script.fields().at("events").list_value().values()) {
        if (events_.size() >= 4096 || value.kind_case() != google::protobuf::Value::kStructValue) {
            throw std::invalid_argument("Replay requires at most 4096 event objects");
        }
        const auto& data = value.struct_value().fields();
        const auto number = [&](const char* key, double low, double high, double fallback = 0, bool integral = false) {
            const auto field = data.find(key);
            if (field == data.end()) {
                return fallback;
            }
            const auto result = field->second.number_value();
            if (field->second.kind_case() != google::protobuf::Value::kNumberValue || !std::isfinite(result) ||
                result < low || result > high || (integral && result != std::floor(result))) {
                throw std::invalid_argument(std::string("Invalid replay field: ") + key);
            }
            return result;
        };
        if (!data.contains("frame")) {
            throw std::invalid_argument("Replay requires a frame index");
        }
        Event next;
        next.frame = uint32_t(number("frame", 0, 35999, 0, true));
        if (data.contains("key")) {
            next.key = uint32_t(number("key", 1, 10, 0, true));
            next.modifiers = uint32_t(number("modifiers", 0, 1, 0, true));
        } else {
            if (!data.contains("kind")) {
                throw std::invalid_argument("Replay requires a key or pointer kind");
            }
            next.input = {sizeof(CdInput),
                          uint32_t(number("kind", 0, CD_INPUT_CANCEL, 0, true)),
                          uint32_t(number("buttons", 0, UINT32_MAX, 1, true)),
                          0,
                          number("x", -65536, 65536),
                          number("y", -65536, 65536),
                          number("wheel", INT32_MIN, INT32_MAX)};
        }
        events_.push_back(next);
    }
    std::stable_sort(events_.begin(), events_.end(), [](const auto& a, const auto& b) {
        return a.frame < b.frame;
    });
}

auto PreviewReplay::dispatch(uint32_t frame, NativeInstance& instance) -> void {
    while (cursor_ < events_.size() && events_[cursor_].frame <= frame) {
        const auto& event = events_[cursor_++];
        if (event.frame != frame) {
            continue;
        }
        if (event.key) {
            instance.host.uiKey(event.key, event.modifiers);
        } else {
            instance.input(event.input);
        }
    }
}
}  // namespace cdsdk
