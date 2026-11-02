#include <iostream>
#include <stdexcept>
#include <utility>

#include "sdk/cpp/Background.h"
#include "sdk/services/DataServices.h"

extern "C" int background_c_abi(void);

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

CdResult CD_CALL unsupported(void*, const char*, uint32_t, void*, uint32_t) {
    return CD_ERROR_UNSUPPORTED;
}

CdResult CD_CALL wrongVersion(void*, const char*, uint32_t, void* output, uint32_t) {
    auto& api = *static_cast<CdBackgroundApi*>(output);
    api.struct_size = sizeof(api);
    api.version = 2;
    return CD_OK;
}
}  // namespace

int main() try {
    require(background_c_abi(), "C ABI compilation/ordinary queue byte limit");
    CdCreateContext context{};
    cdsdk::BackgroundTask missing(context);
    require(!missing.available() && missing.create({}) == CD_ERROR_UNSUPPORTED,
            "Missing service silently created a background implementation");
    CdServiceHostApi host{};
    host.input.media.base.struct_size = sizeof(host);
    host.input.media.base.abi_version = CD_PLUGIN_ABI_VERSION;
    host.query_service = unsupported;
    context.host = &host.input.media.base;
    cdsdk::BackgroundTask old(context);
    require(!old.available(), "Unsupported optional service accepted");
    host.query_service = wrongVersion;
    cdsdk::BackgroundTask wrong(context);
    require(!wrong.available(), "Wrong service version accepted");
    cdsdk::BackgroundSnapshot empty;
    require(!empty && empty.bytes().empty(), "An empty snapshot contains a read lease");
    unsigned released = 0;
    CdBackgroundSnapshot raw{};
    raw.release_context = &released;
    raw.release = [](void* pointer) {
        ++*static_cast<unsigned*>(pointer);
    };
    {
        cdsdk::BackgroundSnapshot first(raw);
        auto moved = std::move(first);
        require(!first && bool(moved), "Read lease move duplicated ownership");
        moved.reset();
        moved.reset();
    }
    require(released == 1, "Read lease was not released exactly once");
    std::cout << "Qt-free C/C++ background ABI, optional service and read-lease ownership passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
