#include "tools/PreviewServices.h"

#include <CyberCanvas/Canvas.h>
#include <Windows.h>

#include <array>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>
#include <set>
#include <string_view>

#include "CyberCanvas/MemoryBudget.h"
#include "CyberCanvas/ResourceAccount.h"
#include "sdk/services/Composition.h"
#include "sdk/services/Icons.h"
#include "sdk/services/ImageSkins.h"
#include "sdk/services/ImageViewer.h"
#include "sdk/services/RenderImages.h"
#include "sdk/services/RenderMemory.h"
#include "sdk/services/SettingsChoices.h"
#include "tools/PreviewIcons.h"
#include "tools/PreviewOutput.h"
#include "tools/PreviewTextInput.h"
#include "tools/Runtime.h"
#ifdef CD_SDK_PREVIEW_LIVE2D
#include "sdk/services/Live2DActions.h"
#include "tools/PreviewAssets.h"
#include "tools/PreviewChat.h"
#include "tools/PreviewLive2D.h"
#endif

namespace cdsdk {
namespace {

bool validUtf8(std::string_view text) {
    return text.find('\0') == std::string_view::npos &&
           (text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                static_cast<int>(text.size()), nullptr, 0) != 0);
}

bool within(const std::filesystem::path& parent, const std::filesystem::path& child) {
    auto first = parent.begin();
    auto second = child.begin();
    for (; first != parent.end() && second != child.end(); ++first, ++second) {
        if (_wcsicmp(first->c_str(), second->c_str()) != 0) {
            return false;
        }
    }
    return first == parent.end() && second != child.end();
}

cybercanvas::MemoryBudget& previewBudget() {
    static cybercanvas::MemoryBudget budget(64 * 1024 * 1024);
    return budget;
}

template <class Table>
CdResult write(Table value, void* output, uint32_t capacity) {
    if (capacity < sizeof(Table)) {
        return CD_ERROR_BUFFER;
    }
    std::memcpy(output, &value, sizeof(value));
    return CD_OK;
}

}  // namespace

struct PreviewServices::Impl {
    explicit Impl(MockHost& owner) : host(owner) {}

    MockHost& host;
    std::filesystem::path root;
    std::map<std::string, bool> settings;
    nlohmann::json choices = nlohmann::json::object();
    std::vector<std::string> openedImages;
    std::map<uint64_t, std::array<uint32_t, 3>> imageSources;
    std::map<uint64_t, std::pair<CdImageSkinChanged, void*>> skinSubscriptions;
    std::function<void()> choicesChanged;
    PreviewTextInput textInput;
    PreviewIcons icons;
    PreviewOutput output;
    bool composition = false;
    uint32_t pass = CD_COMPOSITION_ALL;
    CdRect modelBounds{};
    std::vector<uint8_t> background, foreground, modelPixels;
    std::shared_ptr<cybercanvas::MemoryBudget::Lease> layerLease;
    std::unique_ptr<cybercanvas::Canvas> compositor;
#ifdef CD_SDK_PREVIEW_LIVE2D
    std::unique_ptr<PreviewLive2D> live2d;
    std::unique_ptr<PreviewAssets> assets;
    std::unique_ptr<PreviewChat> chat;
    bool chatMetadataLoaded = false;

    PreviewChat& characterChat() {
        characterAssets();
        if (!chat) {
            chat = std::make_unique<PreviewChat>([this] {
                host.dirty = true;
            });
        }
        return *chat;
    }

    PreviewAssets& characterAssets() {
        if (!assets) {
            assets = std::make_unique<PreviewAssets>([this] {
                host.dirty = true;
            });
            assets->root(root);
        }
        return *assets;
    }

    PreviewLive2D& model() {
        if (!live2d) {
            live2d = std::make_unique<PreviewLive2D>([this] {
                host.dirty = true;
            });
            live2d->root(root);
        }
        return *live2d;
    }
#endif

    template <class Callback>
    static CdResult guarded(Callback&& callback) noexcept {
        try {
            return callback();
        } catch (...) {
            return CD_ERROR_INTERNAL;
        }
    }

    static Impl& from(void* context) {
        return *static_cast<MockHost*>(context)->preview->impl_;
    }

    static CdResult CD_CALL openImage(void* context, const char* path, uint32_t length) noexcept {
        try {
            if (!context || !path || !length || length > 65536 || !validUtf8({path, length})) {
                return CD_ERROR_ARGUMENT;
            }
            const std::string text(path, length);
            if (!utf8Path(text).is_absolute()) {
                return CD_ERROR_ARGUMENT;
            }
            auto& requests = from(context).openedImages;
            if (requests.size() == 64) {
                requests.erase(requests.begin());
            }
            requests.push_back(text);
            return CD_OK;
        } catch (...) {
            return CD_ERROR_INTERNAL;
        }
    }

    static void CD_CALL updateImages(void* context, uint64_t source, uint32_t ready, uint32_t pending,
                                     uint32_t failed) noexcept {
        try {
            if (!context || !source || uint64_t(ready) + pending + failed > 8192) {
                return;
            }
            auto& sources = from(context).imageSources;
            if (!ready && !pending && !failed) {
                sources.erase(source);
            } else if (sources.contains(source) || sources.size() < 128) {
                sources[source] = {ready, pending, failed};
            }
        } catch (...) {
            // Diagnostics cannot turn an otherwise successful render into a failure.
        }
    }

    static void* CD_CALL reserve(void* context, uint64_t bytes) noexcept {
        try {
            if (context != &previewBudget() || !bytes || bytes > 16 * 1024 * 1024) {
                return nullptr;
            }
            auto lease = previewBudget().reserve(bytes);
            if (!lease) {
                return nullptr;
            }
            return cybercanvas::makeMemoryLease(std::move(lease)).Detach();
        } catch (...) {
            return nullptr;
        }
    }

    static CdResult CD_CALL resolveSkin(void* context, const char* source, char* output, uint32_t capacity,
                                        uint64_t* revision) noexcept {
        try {
            if (!context || !source || !output || !revision) {
                return CD_ERROR_ARGUMENT;
            }
            const auto length = strnlen_s(source, 65537);
            if (!length || length > 65536 || !validUtf8({source, length})) {
                return CD_ERROR_ARGUMENT;
            }
            const auto input = utf8Path(std::string(source, length));
            const auto& root = from(context).root;
            std::error_code error;
            const auto assets = std::filesystem::canonical(root / "assets", error);
            if (error || root.empty() || !within(root, assets) || !input.is_absolute()) {
                return CD_ERROR_ARGUMENT;
            }
            const auto canonical = std::filesystem::canonical(input, error);
            if (error || !within(assets, input.lexically_normal()) || !within(assets, canonical) ||
                !std::filesystem::is_regular_file(canonical, error) || error ||
                std::string_view(source, length).substr(2).find(':') != std::string_view::npos) {
                return CD_ERROR_ARGUMENT;
            }
            if (length + 1 > capacity) {
                return CD_ERROR_BUFFER;
            }
            std::memcpy(output, source, length + 1);
            *revision = 0;
            return CD_OK;
        } catch (...) {
            return CD_ERROR_INTERNAL;
        }
    }

    static CdResult CD_CALL subscribeSkin(void* context, CdImageSkinChanged callback, void* user,
                                          uint64_t* token) noexcept {
        try {
            if (!context || !callback || !token) {
                return CD_ERROR_ARGUMENT;
            }
            auto& self = from(context);
            if (self.skinSubscriptions.size() >= 64) {
                return CD_ERROR_INTERNAL;
            }
            const auto next = self.host.next++;
            self.skinSubscriptions.emplace(next, std::make_pair(callback, user));
            *token = next;
            return CD_OK;
        } catch (...) {
            return CD_ERROR_INTERNAL;
        }
    }

    static void CD_CALL unsubscribeSkin(void* context, uint64_t token) noexcept {
        if (context) {
            from(context).skinSubscriptions.erase(token);
        }
    }

    static CdResult CD_CALL publishChoices(void* context, const char* bytes, uint32_t length) noexcept {
        try {
            if (!context || !bytes || !length || length > 256 * 1024 || !validUtf8({bytes, length})) {
                return CD_ERROR_ARGUMENT;
            }
            const auto document = nlohmann::json::parse(bytes, bytes + length, nullptr, false);
            if (!document.is_object() || document.size() != 1 || !document.contains("fields") ||
                !document["fields"].is_object() || document["fields"].size() > 32) {
                return CD_ERROR_ARGUMENT;
            }
            auto& self = from(context);
            auto next = self.choices;
            for (const auto& [id, field] : document["fields"].items()) {
                const auto declared = self.settings.find(id);
                if (declared == self.settings.end() || !field.is_object()) {
                    return CD_ERROR_ARGUMENT;
                }
                for (const auto& [key, value] : field.items()) {
                    if (key != "context" && key != "choices") {
                        return CD_ERROR_ARGUMENT;
                    }
                    if (key == "context" && (!value.is_string() || value.get_ref<const std::string&>().size() > 256)) {
                        return CD_ERROR_ARGUMENT;
                    }
                }
                if (field.contains("choices")) {
                    const auto& choices = field["choices"];
                    if (!declared->second || !choices.is_array() || choices.empty() || choices.size() > 257) {
                        return CD_ERROR_ARGUMENT;
                    }
                    std::set<std::string> unique;
                    for (const auto& choice : choices) {
                        if (!choice.is_object() || choice.size() != 2 || !choice.contains("value") ||
                            !choice.contains("label") || !choice["value"].is_string() || !choice["label"].is_string()) {
                            return CD_ERROR_ARGUMENT;
                        }
                        const auto value = choice["value"].get<std::string>();
                        const auto label = choice["label"].get<std::string>();
                        if (value.empty() || label.empty() || value.size() > 256 || label.size() > 256 ||
                            !validUtf8(value) || !validUtf8(label) || !unique.insert(value).second) {
                            return CD_ERROR_ARGUMENT;
                        }
                    }
                }
                next[id] = field;
            }
            if (self.choices == next) {
                return CD_OK;
            }
            self.choices = std::move(next);
            if (const auto notify = self.choicesChanged) {
                notify();
            }
            return CD_OK;
        } catch (...) {
            return CD_ERROR_INTERNAL;
        }
    }
};

PreviewServices::PreviewServices(MockHost& host) : impl_(std::make_unique<Impl>(host)) {}

PreviewServices::~PreviewServices() = default;

void PreviewServices::configure(std::filesystem::path root, std::map<std::string, bool> settings) {
    std::error_code error;
    impl_->root = std::filesystem::canonical(root, error);
    impl_->settings = std::move(settings);
    impl_->choices.clear();
}

std::optional<CdResult> PreviewServices::query(const char* id, uint32_t version, void* output, uint32_t capacity) {
    if (!id || !output) {
        return CD_ERROR_ARGUMENT;
    }
    if (version != 1) {
        return CD_ERROR_VERSION;
    }
    const std::string_view name(id);
#ifdef CD_SDK_PREVIEW_LIVE2D
    if (name == CD_SERVICE_CHARACTER_CHAT) {
        return write(
            CdCharacterChatApi{sizeof(CdCharacterChatApi), 1,
                               [](void* context, CdCharacterChatCallback callback, void* user) noexcept -> CdResult {
                                   return Impl::guarded([&] {
                                       return context ? Impl::from(context).characterChat().connect(callback, user)
                                                      : CD_ERROR_ARGUMENT;
                                   });
                               },
                               [](void* context, const char* bytes, uint32_t length) noexcept -> CdResult {
                                   return Impl::guarded([&] {
                                       return context ? Impl::from(context).characterChat().command(bytes, length)
                                                      : CD_ERROR_ARGUMENT;
                                   });
                               }},
            output, capacity);
    }
    if (name == CD_SERVICE_CHARACTER_IMAGES) {
        return write(
            CdCharacterImagesApi{
                sizeof(CdCharacterImagesApi), 1,
                [](void* context, const char* id, uint32_t edge, CdCharacterImage* output) noexcept -> CdResult {
                    return Impl::guarded([&] {
                        return context ? Impl::from(context).characterAssets().request(nullptr, id, edge, output)
                                       : CD_ERROR_ARGUMENT;
                    });
                }},
            output, capacity);
    }
    if (name == CD_SERVICE_CHARACTER_VERSIONED_IMAGES) {
        return write(CdCharacterVersionedImagesApi{sizeof(CdCharacterVersionedImagesApi), 1,
                                                   [](void* context, const char* pack, const char* id, uint32_t edge,
                                                      CdCharacterImage* output) noexcept -> CdResult {
                                                       return Impl::guarded([&] {
                                                           return context && pack
                                                                      ? Impl::from(context).characterAssets().request(
                                                                            pack, id, edge, output)
                                                                      : CD_ERROR_ARGUMENT;
                                                       });
                                                   }},
                     output, capacity);
    }
    if (name == CD_SERVICE_LIVE2D) {
        return write(
            CdLive2DApi{sizeof(CdLive2DApi), 1,
                        [](void* context, const CdLive2DOptions* options) noexcept -> CdResult {
                            return Impl::guarded([&] {
                                return context ? Impl::from(context).model().configure(options) : CD_ERROR_ARGUMENT;
                            });
                        },
                        [](void* context, CdLive2DStatus* status) noexcept -> CdResult {
                            return Impl::guarded([&] {
                                return context ? Impl::from(context).model().snapshot(status) : CD_ERROR_ARGUMENT;
                            });
                        },
                        [](void* context, double x, double y, uint32_t tap) noexcept {
                            Impl::guarded([&] {
                                if (context)
                                    Impl::from(context).model().pointer(x, y, tap);
                                return CD_OK;
                            });
                        }},
            output, capacity);
    }
    if (name == CD_SERVICE_LIVE2D_ACTIONS) {
        return write(CdLive2DActionsApi{sizeof(CdLive2DActionsApi), 1,
                                        [](void* context, const char* motion, uint32_t priority) noexcept -> CdResult {
                                            return Impl::guarded([&] {
                                                return context ? Impl::from(context).model().perform(motion, priority)
                                                               : CD_ERROR_ARGUMENT;
                                            });
                                        }},
                     output, capacity);
    }
#endif
    if (name == CD_SERVICE_RENDER_OUTPUT) {
        return write(CdRenderOutputApi{
                         sizeof(CdRenderOutputApi), 1,
                         [](void* context) noexcept -> CdResult {
                             return context ? Impl::from(context).output.configure() : CD_ERROR_ARGUMENT;
                         },
                         [](void* context, const CdFrame* frame, CdRenderSession* result) noexcept -> CdResult {
                             return context ? Impl::from(context).output.session(frame, result) : CD_ERROR_ARGUMENT;
                         },
                         [](void* context, const CdRenderReport* report) noexcept -> CdResult {
                             return context ? Impl::from(context).output.complete(report) : CD_ERROR_ARGUMENT;
                         }},
                     output, capacity);
    }
#ifdef CD_SDK_PREVIEW_LIVE2D
    if (name == CD_SERVICE_COMPOSITION) {
        return write(CdCompositionApi{
                         sizeof(CdCompositionApi), 1,
                         [](void* context, const CdRect* bounds, uint32_t enabled) noexcept -> CdResult {
                             if (!context || enabled > 1 ||
                                 (enabled && (!bounds || !std::isfinite(bounds->x) || !std::isfinite(bounds->y) ||
                                              !std::isfinite(bounds->width) || !std::isfinite(bounds->height) ||
                                              bounds->x < 0 || bounds->y < 0 || bounds->width <= 0 ||
                                              bounds->height <= 0 || bounds->width > 4096 || bounds->height > 4096)))
                                 return CD_ERROR_ARGUMENT;
                             auto& self = Impl::from(context);
                             self.composition = enabled != 0;
                             if (!enabled) {
                                 std::vector<uint8_t>().swap(self.background);
                                 std::vector<uint8_t>().swap(self.foreground);
                                 std::vector<uint8_t>().swap(self.modelPixels);
                                 self.layerLease.reset();
                                 self.compositor.reset();
                             }
                             self.modelBounds = enabled ? *bounds : CdRect{};
                             self.host.dirty = true;
                             return CD_OK;
                         },
                         [](void* context) noexcept -> uint32_t {
                             return context ? Impl::from(context).pass : CD_COMPOSITION_ALL;
                         },
                         [](void* context, uint32_t layers) noexcept {
                             if (context && (layers & 3))
                                 Impl::from(context).host.dirty = true;
                         }},
                     output, capacity);
    }
#endif
    if (name == CD_SERVICE_ICONS) {
        return write(
            CdIconsApi{sizeof(CdIconsApi), 1,
                       [](void* context, const char* name, uint32_t color, const CdFrame* frame) noexcept -> CdResult {
                           return Impl::guarded([&] {
                               return context ? Impl::from(context).icons.raster(name, color, frame)
                                              : CD_ERROR_ARGUMENT;
                           });
                       }},
            output, capacity);
    }
    if (name == CD_SERVICE_TEXT_INPUT) {
        return write(
            CdTextInputApi{
                sizeof(CdTextInputApi), 1,
                [](void* context, CdTextInputCallback callback, void* user) noexcept -> CdResult {
                    return Impl::guarded([&] {
                        return context ? Impl::from(context).textInput.connect(callback, user) : CD_ERROR_ARGUMENT;
                    });
                },
                [](void* context, const CdRect* bounds, const char* placeholder, uint32_t length) noexcept -> CdResult {
                    return Impl::guarded([&] {
                        return context ? Impl::from(context).textInput.configure(bounds, placeholder, length)
                                       : CD_ERROR_ARGUMENT;
                    });
                },
                [](void* context, const char* bytes, uint32_t length) noexcept -> CdResult {
                    return Impl::guarded([&] {
                        return context ? Impl::from(context).textInput.setText(bytes, length) : CD_ERROR_ARGUMENT;
                    });
                },
                [](void* context, uint32_t focused) noexcept -> CdResult {
                    return Impl::guarded([&] {
                        return context && focused <= 1 ? Impl::from(context).textInput.focus(focused != 0)
                                                       : CD_ERROR_ARGUMENT;
                    });
                }},
            output, capacity);
    }
    if (name == CD_SERVICE_IMAGE_VIEWER) {
        return write(CdImageViewerApi{sizeof(CdImageViewerApi), 1, Impl::openImage}, output, capacity);
    }
    if (name == CD_SERVICE_RENDER_IMAGES) {
        return write(CdRenderImagesApi{sizeof(CdRenderImagesApi), 1, Impl::updateImages}, output, capacity);
    }
    if (name == CD_SERVICE_RENDER_MEMORY) {
        return write(CdRenderMemoryApi{sizeof(CdRenderMemoryApi), 1, Impl::reserve, &previewBudget()}, output,
                     capacity);
    }
    if (name == CD_SERVICE_SETTINGS_CHOICES) {
        return write(CdSettingsChoicesApi{sizeof(CdSettingsChoicesApi), 1, Impl::publishChoices}, output, capacity);
    }
    if (name == CD_SERVICE_IMAGE_SKINS) {
        if (!impl_->host.capabilities.contains("IMAGE_SKINS")) {
            return CD_ERROR_UNSUPPORTED;
        }
        return write(
            CdImageSkinsApi{sizeof(CdImageSkinsApi), 1, Impl::resolveSkin, Impl::subscribeSkin, Impl::unsubscribeSkin},
            output, capacity);
    }
    return std::nullopt;
}

const std::vector<std::string>& PreviewServices::openedImages() const {
    return impl_->openedImages;
}

void PreviewServices::settingsChanged(std::function<void()> callback) {
    impl_->choicesChanged = std::move(callback);
}

void PreviewServices::applySettingsChoices(pb::SettingsPage& page) const {
    for (auto& field : *page.mutable_fields()) {
        const auto found = impl_->choices.find(field.id());
        if (found == impl_->choices.end() || !found->contains("choices")) {
            continue;
        }
        field.clear_choices();
        for (const auto& value : (*found)["choices"]) {
            auto* choice = field.add_choices();
            choice->set_value(value["value"].get<std::string>());
            choice->set_label(value["label"].get<std::string>());
        }
    }
}

std::string PreviewServices::settingsContext(const std::string& id) const {
    const auto found = impl_->choices.find(id);
    return found != impl_->choices.end() ? found->value("context", std::string{}) : std::string{};
}

std::string PreviewServices::settingsChoices() const {
    return nlohmann::json{{"fields", impl_->choices}}.dump();
}

uint64_t PreviewServices::imageSourceCount() const {
    return impl_->imageSources.size();
}

void PreviewServices::unsubscribe(uint64_t token) {
    impl_->skinSubscriptions.erase(token);
}

void PreviewServices::placement(HWND parent, double dpr) {
    impl_->textInput.placement(parent, dpr);
}

void PreviewServices::pause(bool paused) {
    impl_->textInput.pause(paused);
#ifdef CD_SDK_PREVIEW_LIVE2D
    if (impl_->live2d)
        impl_->live2d->pause(paused);
#endif
}

void PreviewServices::tick() {
#ifdef CD_SDK_PREVIEW_LIVE2D
    if (impl_->assets)
        impl_->assets->tick();
    if (impl_->live2d)
        impl_->live2d->tick();
    if (impl_->chat) {
        if (!impl_->chatMetadataLoaded) {
            const auto metadata = impl_->assets->metadata();
            impl_->chat->metadata(metadata, impl_->assets->error());
            impl_->chatMetadataLoaded = !metadata.empty();
        }
        const auto seconds = impl_->host.virtualTime;
        const auto scenario = impl_->host.scenario;
        const auto paused = impl_->host.paused;
        impl_->chat->tick(seconds, scenario, paused);
    }
#endif
}

CdResult PreviewServices::renderPlugin(const CdInstanceApi& instance, const CdFrame& frame) {
    auto draw = [&](const CdFrame& target, uint32_t pass) {
        impl_->pass = pass;
        impl_->output.begin(target);
        const auto result = instance.render(instance.instance, &target);
        impl_->output.end();
        impl_->pass = CD_COMPOSITION_ALL;
        return result;
    };
    try {
#ifdef CD_SDK_PREVIEW_LIVE2D
        if (impl_->composition) {
            const auto bytes = size_t(frame.width) * frame.height * 4;
            if (impl_->background.size() != bytes || impl_->foreground.size() != bytes ||
                impl_->modelPixels.size() != bytes) {
                std::vector<uint8_t>().swap(impl_->background);
                std::vector<uint8_t>().swap(impl_->foreground);
                std::vector<uint8_t>().swap(impl_->modelPixels);
                impl_->layerLease.reset();
                impl_->layerLease = previewBudget().reserve(bytes * 3);
                if (!impl_->layerLease)
                    return CD_ERROR_INTERNAL;
                impl_->background.resize(bytes);
                impl_->foreground.resize(bytes);
                impl_->modelPixels.resize(bytes);
            }
            const auto bounds = impl_->modelBounds;
            const uint32_t width = std::min(frame.width, uint32_t(std::ceil(bounds.width * frame.device_pixel_ratio)));
            const uint32_t height =
                std::min(frame.height, uint32_t(std::ceil(bounds.height * frame.device_pixel_ratio)));
            CdFrame background{sizeof(CdFrame),         frame.width, frame.height,
                               frame.width * 4,         bytes,       impl_->background.data(),
                               frame.device_pixel_ratio};
            CdFrame foreground = background;
            foreground.rgba = impl_->foreground.data();
            std::fill(impl_->background.begin(), impl_->background.end(), 0);
            std::fill(impl_->foreground.begin(), impl_->foreground.end(), 0);
            std::fill(impl_->modelPixels.begin(), impl_->modelPixels.end(), 0);
            if (draw(background, CD_COMPOSITION_BACKGROUND) != CD_OK ||
                draw(foreground, CD_COMPOSITION_FOREGROUND) != CD_OK)
                return CD_ERROR_INTERNAL;
            CdFrame model{sizeof(CdFrame),
                          width,
                          height,
                          width * 4,
                          size_t(width) * height * 4,
                          impl_->modelPixels.data(),
                          frame.device_pixel_ratio};
            const bool ready = impl_->live2d && impl_->live2d->render(model);
            auto buffer = [](const CdFrame& value) {
                return cybercanvas::FrameBuffer{value.width,    value.height, value.stride,
                                                value.capacity, value.rgba,   value.device_pixel_ratio};
            };
            const cybercanvas::Rect area{0, 0, frame.width / frame.device_pixel_ratio,
                                         frame.height / frame.device_pixel_ratio};
            if (!impl_->compositor)
                impl_->compositor = std::make_unique<cybercanvas::Canvas>();
            impl_->compositor->render(buffer(frame), {}, [&](auto& canvas, auto) {
                canvas.blit(buffer(background), area);
                if (ready)
                    canvas.blit(buffer(model), {bounds.x, bounds.y, bounds.width, bounds.height});
                canvas.blit(buffer(foreground), area);
            });
            return CD_OK;
        }
#endif
        const auto result = draw(frame, CD_COMPOSITION_ALL);
        if (result == CD_OK)
            render(frame);
        return result;
    } catch (...) {
        impl_->output.end();
        impl_->pass = CD_COMPOSITION_ALL;
        return CD_ERROR_INTERNAL;
    }
}

void PreviewServices::render(const CdFrame& frame) {
#ifdef CD_SDK_PREVIEW_LIVE2D
    if (impl_->live2d)
        impl_->live2d->render(frame);
#else
    (void)frame;
#endif
}

}  // namespace cdsdk
