#include <sdk/Canvas.h>
#include <sdk/Images.h>
#include <sdk/services/ImageSkins.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
struct Host {
    struct Timer {
        CdTimerCallback callback;
        void* user;
    };

    CdHostApi api{};
    CdServiceHostApi services{};
    std::map<uint64_t, std::pair<CdImageSkinChanged, void*>> observers;
    std::filesystem::path replacement;
    uint64_t skinRevision = 0;
    std::map<uint64_t, Timer> timers;
    uint64_t next = 1;
    int redraws = 0;

    Host() {
        api.struct_size = sizeof(api);
        api.request_redraw = [](void* p) {
            ++static_cast<Host*>(p)->redraws;
        };
        api.subscribe_timer = [](void* p, uint32_t, CdTimerCallback callback, void* user, uint64_t* token) -> CdResult {
            auto& host = *static_cast<Host*>(p);
            *token = host.next++;
            host.timers.emplace(*token, Timer{callback, user});
            return CD_OK;
        };
        api.unsubscribe = [](void* p, uint64_t token) {
            static_cast<Host*>(p)->timers.erase(token);
        };
        services.input.media.base = api;
        services.input.media.base.struct_size = sizeof(services);
        services.query_service = [](void*, const char* name, uint32_t version, void* result,
                                    uint32_t size) -> CdResult {
            if (std::strcmp(name, CD_SERVICE_IMAGE_SKINS) || version != 1 || size < sizeof(CdImageSkinsApi)) {
                return CD_ERROR_UNSUPPORTED;
            }
            *static_cast<CdImageSkinsApi*>(result) = {
                sizeof(CdImageSkinsApi), 1,
                [](void* p, const char* source, char* output, uint32_t capacity, uint64_t* revision) -> CdResult {
                    auto& host = *static_cast<Host*>(p);
                    const auto target = host.replacement.empty() ? std::filesystem::u8path(source) : host.replacement;
                    const auto utf8 = target.u8string();
                    if (utf8.size() + 1 > capacity) {
                        return CD_ERROR_ARGUMENT;
                    }
                    std::memcpy(output, utf8.c_str(), utf8.size() + 1);
                    *revision = host.skinRevision;
                    return CD_OK;
                },
                [](void* p, CdImageSkinChanged callback, void* user, uint64_t* token) -> CdResult {
                    auto& host = *static_cast<Host*>(p);
                    *token = host.next++;
                    host.observers.emplace(*token, std::make_pair(callback, user));
                    return CD_OK;
                },
                [](void* p, uint64_t token) {
                    static_cast<Host*>(p)->observers.erase(token);
                }};
            return CD_OK;
        };
    }

    void select(const std::filesystem::path& path, uint64_t revision) {
        replacement = path;
        skinRevision = revision;
        const auto copy = observers;
        for (const auto& [id, callback] : copy) {
            if (observers.contains(id)) {
                callback.first(callback.second, revision);
            }
        }
    }

    void pump() {
        const auto batch = timers;
        for (const auto& [token, timer] : batch) {
            if (timers.contains(token)) {
                timer.callback(timer.user, 0);
            }
        }
    }
};

void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
    std::cout << "PASS " << message << '\n';
}

void settle(cdsdk::Images& images, Host& host) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        host.pump();
        images.poll();
        if (std::chrono::steady_clock::now() > deadline) {
            throw std::runtime_error("SDK image timeout");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (images.pending() || images.timerActive());
}
}  // namespace

int main() {
    try {
        Host first, second;
        CdCreateContext one{sizeof(one), CD_PLUGIN_ABI_VERSION, "one", "one", ".", &first.api, &first};
        CdCreateContext two{sizeof(two), CD_PLUGIN_ABI_VERSION, "two", "two", ".", &second.api, &second};
        auto store = std::make_shared<cybercanvas::ImageStore>();
        cdsdk::Images images(store), other(store);
        images.bind(one);
        other.bind(two);
        const auto path =
            std::filesystem::temp_directory_path() / (L"sdk-image-" + std::to_wstring(GetCurrentProcessId()) + L".png");
        cdsdk::Canvas canvas;
        std::vector<uint8_t> pixels(16 * 16 * 4, 255);
        const CdFrame frame{sizeof(frame), 16, 16, 64, pixels.size(), pixels.data(), 1};
        check(canvas.save(path.wstring(), frame), "SDK fixture saved");
        const auto owner = std::this_thread::get_id();
        bool uiThread = true;
        int changes = 0;
        images.onChange([&] {
            uiThread &= owner == std::this_thread::get_id();
            ++changes;
        });
        images.set("cover", path);
        other.set("cover", path);
        settle(images, first);
        settle(other, second);
        check(uiThread && images.get("cover").image == other.get("cover").image && store->stats().decodes == 1,
              "two instances share decoded pixels and callbacks stay on the UI thread");
        check(first.timers.empty() && second.timers.empty(), "settled resources release their completion timers");
        const auto quiet = changes;
        check(!images.set("cover", path) && !images.poll() && changes == quiet,
              "identical requests and settled polling do not redraw");
        images.set("region", path, 0, 8, {0, 0, 8, 16});
        settle(images, first);
        const auto region = images.get("region").image;
        check(
            region && region->width() == 4 && region->height() == 8 && images.set("region", path, 0, 8, {8, 0, 8, 16}),
            "SDK atlas slots scale the selected source region and invalidate changed coordinates");
        settle(images, first);
        check(images.get("region").image != region && !images.set("region", path, 0, 8, {8, 0, 8, 16}),
              "different atlas regions retain independent cache identities and stable requests stay quiet");
        images.erase("region");
        images.set("cover", path.parent_path() / L"missing-first.png");
        images.set("cover", path, 1);
        settle(images, first);
        check(images.get("cover").state == cybercanvas::ImageState::Ready,
              "rapid replacement ignores the old request's completion");
        images.pause(true);
        const auto pausedChanges = changes;
        images.set("cover", path, 2);
        check(first.timers.empty() && changes == pausedChanges && !images.poll(),
              "hidden instances stop completion delivery and unsubscribe timers");
        images.pause(false);
        settle(images, first);
        check(changes > pausedChanges && uiThread, "resume delivers the latest result on the UI thread");
        images.erase("cover");
        check(images.get("cover").state == cybercanvas::ImageState::Empty && first.timers.empty() &&
                  other.get("cover").state == cybercanvas::ImageState::Ready,
              "clearing one instance preserves another instance's resource");
        const auto previous = first.redraws;
        {
            cdsdk::Images transient(store);
            transient.bind(one);
            transient.set("cover", path, 3);
        }
        const auto afterDestroy = first.redraws;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        first.pump();
        check(afterDestroy > previous && first.timers.empty() && first.redraws == afterDestroy,
              "destruction cancels delivery without late host callbacks");
        cdsdk::Images unbound(store);
        unbound.set("cover", path);
        settle(unbound, first);
        check(unbound.get("cover").state == cybercanvas::ImageState::Ready,
              "headless clients can poll without any host services");
        const auto alternate = path.parent_path() / L"sdk-image-skin.png";
        const auto missing = path.parent_path() / L"sdk-image-skin-recovery.png";
        std::filesystem::remove(missing);
        pixels.assign(pixels.size(), 0);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i + 1] = 180;
            pixels[i + 3] = 255;
        }
        check(canvas.save(alternate.wstring(), frame), "replacement fixture saved");
        Host skinHost;
        auto skinContext = one;
        skinContext.host = &skinHost.services.input.media.base;
        skinContext.host_context = &skinHost;
        {
            cdsdk::Images skinned(store);
            skinned.bind(skinContext);
            skinned.set("a", path);
            skinned.set("b", path);
            settle(skinned, skinHost);
            const auto original = skinned.get("a").image;
            skinned.pause(true);
            skinHost.select(alternate, 9);
            skinned.erase("b");
            skinned.set("c", path);
            const auto stagedRequests = skinned.stats().requests;
            skinned.set("c", path);
            check(skinned.stats().requests == stagedRequests,
                  "an identical staged-only layer does not re-request pixels");
            skinned.set("c", path, 0, 8);
            skinned.set("c", path);
            check(skinned.stats().requests == stagedRequests + 2,
                  "a newly staged layer follows size changes before the skin commits");
            check(skinned.get("a").image == original && !skinned.get("c").image && skinHost.timers.empty(),
                  "paused skin preparation retains the complete previous frame");
            skinned.pause(false);
            settle(skinned, skinHost);
            const auto switched = skinned.get("a").image;
            check(switched && switched != original && skinned.get("c").image == switched && !skinned.get("b").image &&
                      skinned.stats().ready == 2,
                  "atomic switch includes added layers and never resurrects an erased layer");
            skinHost.select(missing, 10);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!skinned.stats().failed && std::chrono::steady_clock::now() < deadline) {
                skinned.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            check(skinned.stats().failed && skinned.get("a").image == switched,
                  "a failed replacement preserves all previously visible layers");
            check(canvas.save(missing.wstring(), frame) && skinned.retry("a") && skinned.retry("c"),
                  "failed staged layers can be retried after a resource recovers");
            settle(skinned, skinHost);
            check(!skinned.stats().failed && skinned.get("a").image != switched,
                  "recovered replacement commits without recreating the Images object");
            skinHost.select({}, 0);
            skinHost.select(alternate, 9);
            skinHost.select({}, 0);
            settle(skinned, skinHost);
            check(skinned.get("a").image == original && skinned.get("c").image == original,
                  "the latest skin request wins and original decoded pixels are reused");
            const auto decodes = store->stats().decodes;
            for (int i = 0; i < 12; ++i) {
                skinHost.select(i % 2 ? std::filesystem::path{} : alternate, i % 2 ? 0 : 9);
                settle(skinned, skinHost);
            }
            check(store->stats().decodes == decodes, "repeated skin changes reuse stable content revisions");
            skinHost.select(alternate, 9);
            settle(skinned, skinHost);
            skinned.bind(one);
            settle(skinned, first);
            check(skinned.get("a").image == original && skinHost.observers.empty(),
                  "rebinding to a host without skin services restores original paths and cancels the old subscription");
            skinned.bind(skinContext);
            settle(skinned, skinHost);
            check(skinned.get("a").image == switched,
                  "constructor-time image slots adopt an already selected skin on bind");
        }
        check(skinHost.observers.empty() && skinHost.timers.empty(),
              "image destruction cancels skin notifications and completion timers");
        std::filesystem::remove(alternate);
        std::filesystem::remove(missing);
        std::filesystem::remove(path);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
