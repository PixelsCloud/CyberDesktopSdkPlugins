#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "sdk/cpp/Services.h"

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Probe {
    int redraws = 0;
    int queries = 0;
    std::vector<uint64_t> cancelled;
    std::function<void()> onCancel;

    static void CD_CALL redraw(void* context) {
        ++static_cast<Probe*>(context)->redraws;
    }

    static void CD_CALL unsubscribe(void* context, uint64_t token) {
        auto& probe = *static_cast<Probe*>(context);
        probe.cancelled.push_back(token);
        if (probe.onCancel) {
            probe.onCancel();
        }
    }

    static CdResult CD_CALL query(void* context, const char*, uint32_t version, void* output, uint32_t size) {
        ++static_cast<Probe*>(context)->queries;
        auto& table = *static_cast<CdInputServiceApi*>(output);
        table.struct_size = size;
        table.version = version;
        return CD_OK;
    }
};

CdServiceHostApi hostTable() {
    CdServiceHostApi host{};
    host.input.media.base.struct_size = sizeof(host);
    host.input.media.base.abi_version = CD_PLUGIN_ABI_VERSION;
    host.input.media.base.request_redraw = Probe::redraw;
    host.input.media.base.unsubscribe = Probe::unsubscribe;
    host.query_service = Probe::query;
    return host;
}
}  // namespace

int main() {
    try {
        Probe probe;
        cdsdk::Subscription retained;
        {
            auto table = hostTable();
            cdsdk::Services services(&table.input.media.base, &probe);
            table = {};

            services.requestRedraw();
            CdInputServiceApi api{};
            check(services.query("test", api) == CD_OK, "service callback survives the caller's table reset");
            check(probe.redraws == 1 && probe.queries == 1, "copied callbacks retain the host context");
            retained = services.adopt(7);
        }
        probe.onCancel = [&retained] {
            retained.reset();
        };
        retained.reset();
        check(probe.cancelled == std::vector<uint64_t>{7}, "reentrant reset cancels exactly once");
        probe.onCancel = {};

        auto table = hostTable();
        cdsdk::Subscription first(&table.input.media.base, &probe, 8);
        cdsdk::Subscription second(&table.input.media.base, &probe, 9);
        second = std::move(first);
        check(!first && second.token() == 8, "move transfers the only cancellation owner");
        second.reset();
        check(probe.cancelled == std::vector<uint64_t>({7, 9, 8}), "move assignment releases the previous token");

        CdHostApi legacy = table.input.media.base;
        legacy.struct_size = sizeof(legacy);
        cdsdk::Services legacyServices(&legacy, &probe);
        CdInputServiceApi api{};
        check(legacyServices.query("test", api) == CD_ERROR_UNSUPPORTED, "base ABI does not expose extension bytes");
        legacy.struct_size = sizeof(legacy) - 1;
        cdsdk::Services truncated(&legacy, &probe);
        truncated.requestRedraw();
        check(probe.redraws == 1, "truncated host tables are not read");
        std::cout << "Service table lifetime, reentrant cancellation and move ownership: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
