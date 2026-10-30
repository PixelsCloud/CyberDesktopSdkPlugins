#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sdk/cpp/Services.h"
#include "sdk/services/Library.h"
#include "tools/Runtime.h"

namespace {
void check(bool pass, const char* message) {
    if (!pass) {
        throw std::runtime_error(message);
    }
}

struct Observer {
    cdsdk::MockHost* host = nullptr;
    uint64_t token = 0;
    uint32_t status = CD_DATA_LOADING, width = 0;
    uint64_t index = 0, count = 0;
    unsigned calls = 0;
    bool cancel = false;
    std::string path;
    std::vector<uint8_t> pixels;

    static void CD_CALL receive(void* pointer, const CdLibraryInfo* info) {
        auto& self = *static_cast<Observer*>(pointer);
        ++self.calls;
        self.status = info->info.status;
        self.width = info->width;
        self.index = info->index;
        self.count = info->count;
        self.path = info->path;
        if (info->width) {
            self.pixels.assign(info->pixels, info->pixels + size_t(info->width) * info->height * 4);
        }
        if (self.cancel) {
            self.host->api.unsubscribe(self.host, self.token);
        }
    }
};
}  // namespace

int main() try {
    static_assert(sizeof(CdLibraryQuery) == 24 && sizeof(CdLibraryInfo) == 120 && sizeof(CdLibraryServiceApi) == 32);
    cdsdk::MockHost host;
    cdsdk::Services services(&host.api, &host);
    CdLibraryServiceApi table{};
    check(services.query(CD_SERVICE_LIBRARY, table) == CD_ERROR_UNSUPPORTED, "capability required");
    host.capabilities.insert("MEDIA_LIBRARY");
    check(services.query(CD_SERVICE_LIBRARY, table) == CD_OK, "service available");
    check(host.extended.query_service(&host, CD_SERVICE_LIBRARY, 2, &table, sizeof(table)) == CD_ERROR_VERSION,
          "version rejected");
    check(host.extended.query_service(&host, CD_SERVICE_LIBRARY, 1, &table, 8) == CD_ERROR_BUFFER,
          "short table rejected");
    uint64_t invalidToken = 0;
    CdLibraryQuery invalid{sizeof(invalid), 0, 32, 0, "relative/photos"};
    check(table.subscribe(&host, &invalid, Observer::receive, nullptr, &invalidToken) == CD_ERROR_ARGUMENT,
          "relative path rejected");
    invalid.directory = "C:/\xff";
    check(table.subscribe(&host, &invalid, Observer::receive, nullptr, &invalidToken) == CD_ERROR_ARGUMENT,
          "invalid UTF-8 rejected");
    for (const auto scenario : {cdsdk::MockScenario::Normal, cdsdk::MockScenario::Loading, cdsdk::MockScenario::Empty,
                                cdsdk::MockScenario::Error}) {
        host.scenario = scenario;
        Observer observer;
        observer.host = &host;
        const CdLibraryQuery query{sizeof(query), 0, 32, 0, "C:/selected"};
        check(table.subscribe(&host, &query, Observer::receive, &observer, &observer.token) == CD_OK, "subscribe");
        check(!observer.calls, "queued initial delivery");
        cdsdk::Subscription owned(&host.api, &host, observer.token);
        host.tick(host.virtualTime + 1);
        check(observer.calls == 1, "one initial callback");
        if (scenario == cdsdk::MockScenario::Normal) {
            check(observer.width == 32 && observer.count == 3 && !observer.pixels.empty(), "bounded owned pixel copy");
            check(table.select(&host, observer.token, 8, 16) == CD_OK, "select");
            host.tick(host.virtualTime + 1);
            check(observer.index == 2 && observer.width == 16, "independent index and dimensions");
        } else if (scenario == cdsdk::MockScenario::Empty) {
            check(observer.status == CD_DATA_READY && !observer.count, "valid empty");
        } else {
            check(observer.status == (scenario == cdsdk::MockScenario::Loading ? CD_DATA_LOADING : CD_DATA_ERROR),
                  "loading/error status");
        }
        check(table.refresh(&host, observer.token) == CD_OK && table.refresh(&host, observer.token) == CD_ERROR_BUSY,
              "refresh cooldown");
        auto moved = std::move(owned);
        moved.reset();
        const auto count = observer.calls;
        host.tick(host.virtualTime + 100);
        check(observer.calls == count, "cancel blocks callbacks");
        check(table.select(&host, observer.token, 0, 32) == CD_ERROR_ARGUMENT, "retired token rejected");
    }
    Observer self;
    self.host = &host;
    self.cancel = true;
    const CdLibraryQuery query{sizeof(query), 0, 64, 0, ""};
    check(table.subscribe(&host, &query, Observer::receive, &self, &self.token) == CD_OK,
          "self cancellation registration");
    host.tick(host.virtualTime + 1);
    check(self.calls == 1 && host.subscriptions.empty(), "self cancellation is safe");
    std::cout
        << "PASS library service ABI, capability, version, queued callbacks, snapshots, four states and cancellation\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
