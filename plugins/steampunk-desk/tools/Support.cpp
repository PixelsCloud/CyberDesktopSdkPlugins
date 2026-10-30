#include "plugins/steampunk-desk/tools/Support.h"

namespace steamdesk::testing {
void check(bool result, const char* message) {
    if (!result) {
        throw std::runtime_error(message);
    }
    std::cout << "PASS " << message << '\n';
}

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

Host::Host() {
    api.input.media.base.subscribe_timer = [](void* context, uint32_t interval, CdTimerCallback callback, void* user,
                                              uint64_t* token) -> CdResult {
        auto& host = *static_cast<Host*>(context);
        return interval < 16 || interval > 3600000 ? CD_ERROR_ARGUMENT
                                                   : host.timerFallback(context, interval, callback, user, token);
    };
    api.query_service = [](void* context, const char* service, uint32_t version, void* result,
                           uint32_t size) -> CdResult {
        auto& host = *static_cast<Host*>(context);
        if (service && std::string_view(service) == CD_SERVICE_INTERACTION && version == 1 &&
            size >= sizeof(CdInteractionApi)) {
            *static_cast<CdInteractionApi*>(result) = {sizeof(CdInteractionApi), 1,
                                                       [](void*, uint32_t) -> CdResult {
                                                           return CD_OK;
                                                       },
                                                       [](void* pointer, uint32_t captured) -> CdResult {
                                                           static_cast<Host*>(pointer)->captured = captured != 0;
                                                           return CD_OK;
                                                       }};
            return CD_OK;
        }
        return host.fallback(context, service, version, result, size);
    };
}

Frame::Frame(unsigned width, unsigned height, double dpi)
    : bytes(size_t(width * 4 + 16) * height + 64, 0xa5),
      value{sizeof(CdFrame), width, height, width * 4 + 16, size_t(width * 4 + 16) * height, bytes.data() + 32, dpi} {}

auto Frame::valid(CdRect safe) const -> bool {
    for (unsigned i = 0; i < 32; ++i) {
        if (bytes[i] != 0xa5 || bytes[bytes.size() - 1 - i] != 0xa5) {
            return false;
        }
    }
    for (unsigned y = 0; y < value.height; ++y) {
        for (unsigned byte = value.width * 4; byte < value.stride; ++byte) {
            if (value.rgba[size_t(y) * value.stride + byte] != 0xa5) {
                return false;
            }
        }
        for (unsigned x = 0; x < value.width; ++x) {
            const auto* pixel = value.rgba + size_t(y) * value.stride + x * 4;
            if (pixel[0] > pixel[3] || pixel[1] > pixel[3] || pixel[2] > pixel[3]) {
                return false;
            }
            const double px = x / value.device_pixel_ratio, py = y / value.device_pixel_ratio;
            if ((px < safe.x - 1 || px > safe.x + safe.width + 1 || py < safe.y - 1 || py > safe.y + safe.height + 1) &&
                pixel[3]) {
                return false;
            }
        }
    }
    return true;
}

CdSettingValue toggle(const char* id, bool value) {
    return {sizeof(CdSettingValue), CD_SETTING_TOGGLE, id, value};
}
}  // namespace steamdesk::testing
