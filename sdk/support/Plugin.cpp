#include "sdk/cpp/Plugin.h"

#include <algorithm>

namespace cdsdk::plugin_detail {
CdResult query(uint32_t abi, CdPluginApi* output, uint32_t size, const char* id, const char* version,
               decltype(CdPluginApi::create) create) noexcept {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!output || size < sizeof(*output)) {
        return CD_ERROR_BUFFER;
    }
    if (!id || !version) {
        return CD_ERROR_ARGUMENT;
    }
    *output = {sizeof(*output), CD_PLUGIN_ABI_VERSION, id, version, create};
    return CD_OK;
}

CdResult copyState(std::span<const uint8_t> state, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept {
    if (state.size() > 2 * 1024 * 1024) {
        return CD_ERROR_ARGUMENT;
    }
    *size = state.size();
    if (!bytes && !capacity) {
        return CD_OK;
    }
    if (capacity < state.size() || (!state.empty() && !bytes)) {
        return CD_ERROR_BUFFER;
    }
    std::copy(state.begin(), state.end(), bytes);
    return CD_OK;
}

bool validContext(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) noexcept {
    return !(!context || context->struct_size < sizeof(*context) || context->abi_version != CD_PLUGIN_ABI_VERSION ||
             !context->host || context->host->struct_size < sizeof(CdHostApi) ||
             context->host->abi_version != CD_PLUGIN_ABI_VERSION || !context->plugin_id || !context->plugin_directory ||
             !context->instance_id || !output || size < sizeof(*output));
}
}  // namespace cdsdk::plugin_detail
