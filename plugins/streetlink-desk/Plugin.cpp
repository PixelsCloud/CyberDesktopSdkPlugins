#include <Windows.h>

#include <filesystem>
#include <memory>
#include <type_traits>

#include "plugins/common/RuntimeLibrary.h"
#include "plugins/mecha-desk/Runtime.h"
#include "plugins/streetlink-desk/Runtime.h"

namespace {
class Runtime {
public:
    explicit Runtime(const char* directory);
    CdResult create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) const;

private:
    cdplugin::RuntimeLibrary library_;
#if CD_STREETLINK_NATIVE_CLOCK
    decltype(&cd_streetlink_create) create_ = nullptr;
#else
    decltype(&cd_artwork_create) create_ = nullptr;
#endif
};

Runtime::Runtime(const char* directory) : library_(directory, CD_STREETLINK_RUNTIME_NAME) {
    create_ = reinterpret_cast<decltype(create_)>(library_.symbol(CD_STREETLINK_FACTORY));
}

CdResult Runtime::create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) const {
#if CD_STREETLINK_NATIVE_CLOCK
    return create_ ? create_(context, output, size) : CD_ERROR_UNSUPPORTED;
#else
    return create_ ? create_(CD_STREETLINK_FAMILY, CD_STREETLINK_SOURCE, CD_STREETLINK_KIND, CD_STREETLINK_ROWS,
                             CD_STREETLINK_COLUMNS, context, output, size)
                   : CD_ERROR_UNSUPPORTED;
#endif
}

CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    try {
        if (!context || !context->plugin_directory) {
            return CD_ERROR_ARGUMENT;
        }
        static Runtime runtime(context->plugin_directory);
        return runtime.create(context, output, size);
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!output || size < sizeof(*output)) {
        return CD_ERROR_BUFFER;
    }
    *output = {sizeof(*output), CD_PLUGIN_ABI_VERSION, CD_STREETLINK_ID, "1.0.0", create};
    return CD_OK;
}
