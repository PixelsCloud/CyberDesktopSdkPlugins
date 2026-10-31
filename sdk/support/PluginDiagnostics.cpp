#include "sdk/cpp/PluginDiagnostics.h"

#include "sdk/PluginApi.h"
#include "sdk/services/Diagnostics.h"

namespace cdsdk::plugin_detail {
class PluginDiagnostics::Impl {
public:
    CdDiagnosticsApi api{};
    void* context = nullptr;
    void* user = nullptr;
    PhysicsCounters (*sample)(void*) = nullptr;
    uint64_t token = 0;

    ~Impl() {
        if (token) {
            api.unregister_source(context, token);
        }
    }

    static CdResult CD_CALL read(void* pointer, CdDiagnosticStats* output, CdDebugNode*, uint32_t,
                                 uint32_t* required) noexcept {
        try {
            auto& self = *static_cast<Impl*>(pointer);
            if (!output || !required || output->struct_size < sizeof(*output)) {
                return CD_ERROR_ARGUMENT;
            }
            *output = {sizeof(*output), 1};
            *required = 0;
            const auto values = self.sample(self.user);
            output->values[CD_DIAG_PHYSICS_STEPS] = values[0];
            output->values[CD_DIAG_PHYSICS_NS] = values[1];
            output->values[CD_DIAG_PHYSICS_AWAKE] = values[2];
            output->values[CD_DIAG_PHYSICS_MAX_NS] = values[3];
            return CD_OK;
        } catch (...) {
            return CD_ERROR_INTERNAL;
        }
    }
};

PluginDiagnostics::PluginDiagnostics() = default;
PluginDiagnostics::~PluginDiagnostics() = default;

void PluginDiagnostics::bind(const CdCreateContext& context, void* user, PhysicsCounters (*sample)(void*)) {
    impl_.reset();
    if (!sample || !context.host || context.host->struct_size < sizeof(CdServiceHostApi)) {
        return;
    }
    const auto* host = reinterpret_cast<const CdServiceHostApi*>(context.host);
    if (!host->query_service) {
        return;
    }
    auto binding = std::make_unique<Impl>();
    if (host->query_service(context.host_context, CD_SERVICE_DIAGNOSTICS, 1, &binding->api, sizeof(binding->api)) !=
            CD_OK ||
        binding->api.struct_size < sizeof(binding->api) || binding->api.version != 1 || !binding->api.register_source ||
        !binding->api.unregister_source) {
        return;
    }
    binding->context = context.host_context;
    binding->user = user;
    binding->sample = sample;
    if (binding->api.register_source(binding->context, Impl::read, binding.get(), &binding->token) == CD_OK &&
        binding->token) {
        impl_ = std::move(binding);
    }
}
}  // namespace cdsdk::plugin_detail
