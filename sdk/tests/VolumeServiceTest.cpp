#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>

#include "sdk/cpp/Services.h"
#include "sdk/services/Storage.h"
#include "tools/Runtime.h"

static_assert(sizeof(CdVolumeQuery) == 16);
static_assert(sizeof(CdVolumeInfo) == 88);
static_assert(sizeof(CdVolumeServiceApi) == 16);

namespace {
void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
}  // namespace

int main() try {
    cdsdk::MockHost host;
    cdsdk::Services services(&host.api, &host);
    CdVolumeServiceApi api{};
    check(services.query(CD_SERVICE_VOLUME, api) == CD_OK, "volume discovery");
    check(services.query(CD_SERVICE_VOLUME, api, 99) == CD_ERROR_VERSION, "version rejected");
    check(host.extended.query_service(&host, CD_SERVICE_VOLUME, 1, &api, sizeof(api) - 1) == CD_ERROR_BUFFER,
          "short table rejected");
    const CdVolumeQuery query{sizeof(query), 10, "c:/"};
    int calls = 0;
    std::optional<cdsdk::VolumeSnapshot> snapshot;
    auto watch = services.volume(query, [&](const auto& value) {
        ++calls;
        snapshot = value;
    });
    check(watch.result() == CD_OK && calls == 0, "registration never delivers inline");
    host.tick(0);
    check(calls == 1 && snapshot->valid() && snapshot->root == "C:\\" && snapshot->total > snapshot->free,
          "typed snapshot owns normalized data");
    host.tick(1);
    check(calls == 1, "refresh cadence respected");
    host.paused = true;
    host.tick(10);
    check(calls == 1, "paused mock suppresses callbacks");
    host.paused = false;
    host.scenario = cdsdk::MockScenario::Loading;
    host.tick(11);
    check(snapshot->status == CD_DATA_LOADING && !snapshot->valid(), "loading preview");
    host.scenario = cdsdk::MockScenario::Error;
    host.tick(21);
    check(snapshot->status == CD_DATA_ERROR && snapshot->error == CD_VOLUME_UNAVAILABLE, "error preview");
    host.scenario = cdsdk::MockScenario::Empty;
    host.tick(31);
    check(snapshot->status == CD_DATA_UNAVAILABLE, "empty preview");
    cdsdk::VolumeWatch moved(std::move(watch));
    moved.reset();
    check(host.subscriptions.empty(), "RAII cancels moved subscription");
    host.scenario = cdsdk::MockScenario::Normal;
    cdsdk::VolumeWatch reentrant;
    reentrant = services.volume(query, [&](const auto&) {
        reentrant.reset();
    });
    host.tick(41);
    check(host.subscriptions.empty(), "callback can cancel itself");
    host.capabilities.erase("SYSTEM_INFO");
    check(services.volume(query,
                          [](const auto&) {
                          })
                  .result() == CD_ERROR_UNSUPPORTED,
          "capability enforced");
    CdHostApi legacy = host.api;
    legacy.struct_size = sizeof(legacy);
    check(cdsdk::Services(&legacy, &host)
                  .volume(query,
                          [](const auto&) {
                          })
                  .result() == CD_ERROR_UNSUPPORTED,
          "old host reports unsupported without probing filesystem");
    std::cout << "Volume ABI, typed snapshots, preview scenarios and subscription lifetime: PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
