#include <sdk/cpp/Background.h>

#include <cstring>
#include <memory>

namespace {
class Model final : public cdsdk::BackgroundModel {
public:
    CdResult execute(const CdBackgroundWork& work, CdBackgroundOutput& output) override {
        if (work.flags & CD_BACKGROUND_SUSPEND) {
            return CD_OK;
        }
        value_ += work.command_count;
        std::memcpy(output.data, &value_, sizeof(value_));
        output.size = sizeof(value_);
        return CD_OK;
    }

    void dispose() noexcept override {}

private:
    uint64_t value_ = 0;
};

struct Instance {
    explicit Instance(const CdCreateContext& context) : channel(context, std::make_shared<Model>(), sizeof(uint64_t)) {}

    cdsdk::BackgroundChannel channel;
    uint64_t shown = 0;
};

CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t capacity) {
    if (!context || !output || capacity < sizeof(*output)) {
        return CD_ERROR_ARGUMENT;
    }
    try {
        auto instance = std::make_unique<Instance>(*context);
        *output = {};
        output->struct_size = sizeof(*output);
        output->abi_version = CD_PLUGIN_ABI_VERSION;
        output->destroy = [](void* pointer) {
            delete static_cast<Instance*>(pointer);
        };
        output->render = [](void* pointer, const CdFrame*) -> CdResult {
            auto& self = *static_cast<Instance*>(pointer);
            try {
                self.channel.consume(&self.shown, sizeof(self.shown));
                return CD_OK;
            } catch (...) {
                return CD_ERROR_BUSY;
            }
        };
        output->input = [](void* pointer, const CdInput*) {
            static_cast<Instance*>(pointer)->channel.submit(1);
        };
        output->set_paused = [](void* pointer, uint32_t paused) {
            static_cast<Instance*>(pointer)->channel.pause(paused != 0);
        };
        output->save_state = [](void* pointer, uint8_t* bytes, uint64_t capacity, uint64_t* size) -> CdResult {
            if (!size) {
                return CD_ERROR_ARGUMENT;
            }
            *size = sizeof(uint64_t);
            if (bytes && capacity >= *size) {
                std::memcpy(bytes, &static_cast<Instance*>(pointer)->shown, *size);
            }
            return !bytes || capacity >= *size ? CD_OK : CD_ERROR_BUFFER;
        };
        output->get_setting = [](void*, const char*, CdSettingValue*, uint32_t) -> CdResult {
            return CD_ERROR_UNSUPPORTED;
        };
        output->apply_settings = [](void*, const CdSettingValue*, uint32_t count) -> CdResult {
            return count ? CD_ERROR_ARGUMENT : CD_OK;
        };
        output->instance = instance.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_UNSUPPORTED;
    }
}
}  // namespace

CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t version, CdPluginApi* output, uint32_t capacity) {
    if (version != CD_PLUGIN_ABI_VERSION || !output || capacity < sizeof(*output)) {
        return CD_ERROR_VERSION;
    }
    *output = {sizeof(*output), CD_PLUGIN_ABI_VERSION, "org.cyberdesktop.tests.background", "1.0", create};
    return CD_OK;
}
