#include <Windows.h>
#include <sdk/cpp/BackgroundReference.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Module {
    explicit Module(const char* path) : handle(LoadLibraryA(path)) {}

    ~Module() {
        if (handle) {
            FreeLibrary(handle);
        }
    }

    HMODULE handle;
};

struct InstanceDeleter {
    void operator()(CdInstanceApi* api) const {
        if (api->instance) {
            api->destroy(api->instance);
        }
        delete api;
    }
};
}  // namespace

int main(int argc, char** argv) try {
    require(argc == 2, "Expected independently built plugin DLL");
    Module module(argv[1]);
    require(module.handle != nullptr, "Load independent plugin DLL");
    const auto query = reinterpret_cast<CdPluginQuery>(GetProcAddress(module.handle, "cd_plugin_query"));
    require(query != nullptr, "Find public C ABI entry");
    CdPluginApi plugin{};
    require(query(CD_PLUGIN_ABI_VERSION, &plugin, sizeof(plugin)) == CD_OK, "Query public C ABI");
    cdsdk::BackgroundReferenceHost background(cdsdk::BackgroundReferenceHost::Mode::Deterministic);
    CdServiceHostApi host{};
    host.input.media.base.struct_size = sizeof(host);
    host.input.media.base.abi_version = CD_PLUGIN_ABI_VERSION;
    host.query_service = [](void* context, const char* id, uint32_t version, void* output, uint32_t size) -> CdResult {
        if (std::strcmp(id, CD_SERVICE_BACKGROUND) != 0 || version != CD_BACKGROUND_VERSION) {
            return CD_ERROR_UNSUPPORTED;
        }
        if (!output || size < sizeof(CdBackgroundApi)) {
            return CD_ERROR_BUFFER;
        }
        *static_cast<CdBackgroundApi*>(output) = static_cast<cdsdk::BackgroundReferenceHost*>(context)->api();
        return CD_OK;
    };
    CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, plugin.id, "test", "", &host.input.media.base,
                            &background};
    std::unique_ptr<CdInstanceApi, InstanceDeleter> instance(new CdInstanceApi{});
    require(plugin.create(&context, instance.get(), sizeof(*instance)) == CD_OK,
            "Create independent background plugin");
    background.tick(0);
    instance->input(instance->instance, nullptr);
    background.tick(.1);
    require(instance->render(instance->instance, nullptr) == CD_OK, "Consume completed immutable snapshot");
    uint64_t value = 0, size = 0;
    require(
        instance->save_state(instance->instance, reinterpret_cast<uint8_t*>(&value), sizeof(value), &size) == CD_OK &&
            size == sizeof(value) && value == 1,
        "Independent plugin did not receive its command");
    instance->set_paused(instance->instance, 1);
    background.tick(.2);
    background.cancel();
    instance.reset();
    background.tick(.3);
    require(background.retire(), "Independent plugin retained callbacks after disposal");
    context.host = nullptr;
    CdInstanceApi unsupported{};
    require(plugin.create(&context, &unsupported, sizeof(unsupported)) == CD_ERROR_UNSUPPORTED && !unsupported.instance,
            "Missing service silently selected another execution mode");
    std::cout << "Independent Qt-free host/plugin: background C ABI, snapshot, cancel, retirement and missing service "
                 "passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
