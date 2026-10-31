#pragma once

#include <memory>
#include <span>
#include <type_traits>
#include <vector>

#include "sdk/PluginApi.h"
#include "sdk/cpp/PluginDiagnostics.h"

namespace cdsdk {
namespace plugin_detail {
CdResult query(uint32_t abi, CdPluginApi* output, uint32_t size, const char* id, const char* version,
               decltype(CdPluginApi::create) create) noexcept;
CdResult copyState(std::span<const uint8_t> state, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept;
bool validContext(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) noexcept;
}  // namespace plugin_detail

// Typed hooks stay inside the plugin DLL. The exported table contains only
// the existing C ABI. T owns its settings and RAII resources, and implements
// render(CdFrame); input/layoutChanged/setPaused/state hooks are optional.
template <class T>
class PluginAdapter {
public:
    static CdResult query(uint32_t abi, CdPluginApi* output, uint32_t size, const char* id,
                          const char* version) noexcept {
        return plugin_detail::query(abi, output, size, id, version, create);
    }

private:
    struct Box {
        explicit Box(const CdCreateContext& context)
            : plugin(context), host(*context.host), context(context.host_context) {
            if constexpr (requires { plugin.physicsCounters(); }) {
                diagnostics.bind(context, this, [](void* pointer) {
                    return static_cast<Box*>(pointer)->plugin.physicsCounters();
                });
            }
        }

        T plugin;
        CdHostApi host;
        void* context = nullptr;
        bool paused = false;
        plugin_detail::PluginDiagnostics diagnostics;
    };

    static void invalidate(Box& box) {
        if constexpr (requires { box.plugin.invalidate(); }) {
            box.plugin.invalidate();
        } else if (box.host.request_redraw) {
            box.host.request_redraw(box.context);
        }
    }

    static void CD_CALL destroy(void* pointer) noexcept {
        // Destruction is an ABI boundary, including plugins with throwing destructors.
        const auto cleanup = [](Box* box) noexcept {
            try {
                std::default_delete<Box>{}(box);
            } catch (...) {
            }
        };
        const auto box = std::unique_ptr<Box, decltype(cleanup)>(static_cast<Box*>(pointer), cleanup);
    }

    static CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept try {
        if (!pointer || !frame || frame->struct_size < sizeof(*frame)) {
            return CD_ERROR_ARGUMENT;
        }
        auto& plugin = static_cast<Box*>(pointer)->plugin;
        static_assert(std::is_same_v<decltype(plugin.render(*frame)), CdResult>, "render must return CdResult");
        return plugin.render(*frame);
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }

    static void CD_CALL input(void* pointer, const CdInput* event) noexcept {
        try {
            if (!pointer || !event || event->struct_size < sizeof(*event)) {
                return;
            }
            auto& box = *static_cast<Box*>(pointer);
            if constexpr (requires { box.plugin.input(*event); }) {
                if (!box.paused) {
                    box.plugin.input(*event);
                }
            }
        } catch (...) {
        }
    }

    static void CD_CALL layoutChanged(void* pointer, const CdLayout* layout) noexcept {
        try {
            if (!pointer || !layout || layout->struct_size < sizeof(*layout)) {
                return;
            }
            auto& box = *static_cast<Box*>(pointer);
            if constexpr (requires { box.plugin.layoutChanged(*layout); }) {
                box.plugin.layoutChanged(*layout);
            }
            invalidate(box);
        } catch (...) {
        }
    }

    static void CD_CALL pause(void* pointer, uint32_t paused) noexcept {
        try {
            if (!pointer || paused > 1) {
                return;
            }
            auto& box = *static_cast<Box*>(pointer);
            if (box.paused == (paused != 0)) {
                return;
            }
            box.paused = paused != 0;
            if constexpr (requires { box.plugin.setPaused(box.paused); }) {
                box.plugin.setPaused(box.paused);
            }
        } catch (...) {
        }
    }

    static CdResult CD_CALL save(void* pointer, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept try {
        if (!pointer || !size) {
            return CD_ERROR_ARGUMENT;
        }
        auto& plugin = static_cast<Box*>(pointer)->plugin;
        std::vector<uint8_t> state;
        if constexpr (requires { plugin.saveState(); }) {
            state = plugin.saveState();
        }
        return plugin_detail::copyState(state, bytes, capacity, size);
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }

    static CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) noexcept try {
        if (!pointer || size > 2 * 1024 * 1024 || (size && !bytes)) {
            return CD_ERROR_ARGUMENT;
        }
        auto& box = *static_cast<Box*>(pointer);
        CdResult result = size ? CD_ERROR_ARGUMENT : CD_OK;
        if constexpr (requires { box.plugin.restoreState(std::span<const uint8_t>{bytes, size_t(size)}); }) {
            result = box.plugin.restoreState(std::span<const uint8_t>{bytes, size_t(size)});
        }
        if (result == CD_OK) {
            invalidate(box);
        }
        return result;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }

    static CdResult CD_CALL getSetting(void* pointer, const char* id, CdSettingValue* output, uint32_t size) noexcept
        try {
        return pointer ? static_cast<Box*>(pointer)->plugin.settings.get(id, output, size) : CD_ERROR_ARGUMENT;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }

    static CdResult CD_CALL applySettings(void* pointer, const CdSettingValue* values, uint32_t count) noexcept try {
        if (!pointer) {
            return CD_ERROR_ARGUMENT;
        }
        auto& box = *static_cast<Box*>(pointer);
        CdResult result;
        if constexpr (requires { box.plugin.applySettings(values, count); }) {
            result = box.plugin.applySettings(values, count);
        } else {
            result = box.plugin.settings.apply(values, count);
        }
        if (result == CD_OK) {
            invalidate(box);
        }
        return result;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }

    static CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) noexcept try {
        if (!plugin_detail::validContext(context, output, size)) {
            return CD_ERROR_ARGUMENT;
        }
        auto box = std::make_unique<Box>(*context);
        *output = {sizeof(*output), CD_PLUGIN_ABI_VERSION, box.get(), destroy, render,
                   input,           layoutChanged,         pause,     save,    restore,
                   getSetting,      applySettings};
        box.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
};
}  // namespace cdsdk
