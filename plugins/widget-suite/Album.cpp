#include <CyberCanvas/Image.h>
#include <CyberCanvas/ResourceAccount.h>

#include <algorithm>
#include <filesystem>
#include <utility>

#include "plugins/widget-suite/Widget.h"
#include "sdk/Update.h"
#include "sdk/cpp/Services.h"
#include "sdk/services/Library.h"
#include "sdk/services/RenderMemory.h"

namespace widgetplugin {
namespace {
struct LeasedImage {
    cybercanvas::ComPtr<IUnknown> lease;
    std::shared_ptr<const cybercanvas::ImageData> image;
};
}  // namespace

struct Album::Impl {
    CdHostApi host{};
    void* context = nullptr;
    CdLibraryServiceApi service{};
    cdsdk::Subscription subscription;
    cdsdk::Timer retryTimer;
    cybercanvas::ResourceAccount account;
    std::function<void()> changed;
    Photo result;
    std::string directory;
    uint64_t revision = 0;
    uint32_t edge = 0;
    uint32_t retryDelay = 1000;
    bool recursive = false, paused = false, ready = false, configured = false;

    ~Impl() {
        subscription.reset();
        retryTimer.stop();
    }

    static void CD_CALL retry(void* pointer, uint64_t) noexcept {
        auto& self = *static_cast<Impl*>(pointer);
        self.retryTimer.stop();
        if (!self.paused && self.subscription && self.service.select) {
            self.service.select(self.context, self.subscription.token(), self.result.index, self.edge);
        }
    }

    static void CD_CALL receive(void* pointer, const CdLibraryInfo* info) noexcept {
        auto& self = *static_cast<Impl*>(pointer);
        if (!info || info->info.struct_size < sizeof(*info) || self.paused || info->info.status == CD_DATA_LOADING) {
            return;
        }
        try {
            Photo next = self.result;
            next.index = size_t(info->index);
            next.count = size_t(info->count);
            next.generation = info->info.sequence;
            if (info->pixels && info->width && info->height && info->width <= 2048 && info->height <= 2048 &&
                info->path) {
                const auto path = std::filesystem::path(
                    std::u8string(info->path, info->path + std::char_traits<char>::length(info->path)));
                if (!next.bitmap || next.filepath != path.wstring() || self.revision != info->revision ||
                    next.bitmap->width() != info->width || next.bitmap->height() != info->height) {
                    const size_t bytes = size_t(info->width) * info->height * 4;
                    auto owned = std::make_shared<LeasedImage>();
                    owned->lease = self.account.acquire(bytes);
                    owned->image = cybercanvas::ImageData::fromPremultipliedBgra(
                        info->width, info->height, std::vector<uint8_t>(info->pixels, info->pixels + bytes));
                    next.bitmap = std::shared_ptr<const cybercanvas::ImageData>(owned, owned->image.get());
                }
                next.filepath = path.wstring();
                next.filename = path.filename().wstring();
                self.revision = info->revision;
            } else if (info->info.status == CD_DATA_READY && !info->count) {
                next = {};
            }
            if (info->error_code && *info->error_code && self.host.log) {
                const std::string message = std::string(info->error_code) + ": " + (info->path ? info->path : "");
                self.host.log(self.context, 2, message.c_str(), uint32_t(message.size()));
            }
            self.result = std::move(next);
            self.ready = true;
            self.retryTimer.stop();
            self.retryDelay = 1000;
        } catch (...) {
            if (self.host.log) {
                constexpr char message[] = "Album image copy exceeded the rendering budget or failed";
                self.host.log(self.context, 2, message, sizeof(message) - 1);
            }
            self.ready = true;
            self.retryTimer.replace(self.retryDelay, retry, &self);
            self.retryDelay = std::min(30000u, self.retryDelay * 2);
        }
        const auto changed = self.changed;
        if (changed) {
            try {
                changed();
            } catch (...) {
            }
        }
    }

    void subscribe() {
        subscription.reset();
        if (paused || !configured || !edge || !service.subscribe) {
            return;
        }
        const CdLibraryQuery query{sizeof(query), recursive ? 1u : 0u, edge, 0, directory.c_str()};
        uint64_t token = 0;
        const auto status = service.subscribe(context, &query, receive, this, &token);
        if (status == CD_OK && token) {
            subscription = cdsdk::Subscription(&host, context, token);
            if (result.index && service.select) {
                service.select(context, token, result.index, edge);
            }
        } else {
            ready = true;
        }
    }
};

Album::Album() : impl(std::make_unique<Impl>()) {}

Album::~Album() = default;

void Album::bind(const CdCreateContext& context, std::function<void()> changed) {
    impl->subscription.reset();
    impl->context = context.host_context;
    impl->host = context.host ? *context.host : CdHostApi{};
    impl->changed = std::move(changed);
    impl->retryTimer.bind(context);
    const cdsdk::Services services(context.host, context.host_context);
    impl->service = {};
    services.query(CD_SERVICE_LIBRARY, impl->service);
    CdRenderMemoryApi memory{};
    if (services.query(CD_SERVICE_RENDER_MEMORY, memory) == CD_OK) {
        impl->account = {memory.resource_context, memory.reserve};
    }
}

void Album::pause(bool paused) {
    if (impl->paused == paused) {
        return;
    }
    impl->paused = paused;
    if (paused) {
        impl->subscription.reset();
        impl->retryTimer.stop();
    } else {
        impl->subscribe();
    }
}

void Album::pixelEdge(uint32_t edge) {
    edge = std::clamp(edge, 1u, 2048u);
    if (impl->edge == edge) {
        return;
    }
    impl->edge = edge;
    if (!impl->subscription) {
        impl->subscribe();
        return;
    }
    if (impl->subscription && impl->service.select) {
        impl->service.select(impl->context, impl->subscription.token(), impl->result.index, edge);
    }
}

void Album::request(const std::string& directory, bool recursive, int step, bool rescan) {
    const bool replace = !impl->configured || impl->directory != directory || impl->recursive != recursive;
    impl->configured = true;
    impl->directory = directory;
    impl->recursive = recursive;
    impl->ready = false;
    if (replace) {
        impl->retryTimer.stop();
        impl->result = {};
        impl->revision = 0;
    }
    if (impl->paused) {
        return;
    }
    if (replace || !impl->subscription) {
        impl->subscribe();
        return;
    }
    const auto count = int64_t(impl->result.count);
    const auto index = count ? uint64_t((int64_t(impl->result.index) + step % count + count) % count) : 0;
    if (rescan && impl->service.refresh) {
        impl->service.refresh(impl->context, impl->subscription.token());
    }
    if (impl->service.select) {
        impl->service.select(impl->context, impl->subscription.token(), index, impl->edge);
    }
}

bool Album::take(Photo& photo) {
    if (!impl->ready) {
        return false;
    }
    photo = impl->result;
    impl->ready = false;
    return true;
}

uint32_t albumTimerInterval(const Photo& photo, float fade, bool motion, bool slideshowPaused, uint64_t intervalMs,
                            uint64_t lastPhoto, uint64_t now) {
    if (fade < 1 || (photo.bitmap && motion && !slideshowPaused)) {
        return 33;
    }
    if (photo.count < 2 || slideshowPaused) {
        return 0;
    }
    const auto elapsed = now >= lastPhoto ? now - lastPhoto : 0;
    return uint32_t(std::clamp<uint64_t>(intervalMs > elapsed ? intervalMs - elapsed : 16, 16, 300000));
}
}  // namespace widgetplugin
