#include <iostream>
#include <optional>
#include <stdexcept>

#include "sdk/cpp/Services.h"
#include "tools/Runtime.h"

static_assert(sizeof(CdContentQuery) == 48);
static_assert(sizeof(CdContentItem) == 88);
static_assert(sizeof(CdContentInfo) == 88);
static_assert(sizeof(CdContentServiceApi) == 32);

namespace {
void check(bool pass, const char* message) {
    if (!pass) {
        throw std::runtime_error(message);
    }
}
}  // namespace

int main() try {
    cdsdk::MockHost host;
    cdsdk::Services services(&host.api, &host);
    CdContentServiceApi api{};
    check(services.query(CD_SERVICE_CONTENT, api) == CD_ERROR_UNSUPPORTED, "capability gate");
    host.capabilities.insert("CONTENT");
    check(services.query(CD_SERVICE_CONTENT, api) == CD_OK, "discovery");
    check(services.query(CD_SERVICE_CONTENT, api, 99) == CD_ERROR_VERSION, "version check");
    check(host.extended.query_service(&host, CD_SERVICE_CONTENT, 1, &api, sizeof(api) - 1) == CD_ERROR_BUFFER,
          "short table");
    const char* urls[]{"https://example.org/feed"};
    const CdContentQuery query{sizeof(query), CD_CONTENT_FEEDS, 300, 1, urls};
    std::optional<cdsdk::ContentSnapshot> saved;
    auto watch = services.content(query, [&](const auto& value) {
        saved = value;
    });
    check(watch.result() == CD_OK && !saved, "initial result queued");
    host.tick(0);
    check(saved && saved->valid() && saved->items.size() == 1 && saved->items[0].title == "Preview headline",
          "snapshot owns text");
    host.scenario = cdsdk::MockScenario::Empty;
    check(services.refreshContent(watch.token()) == CD_OK, "refresh command");
    host.tick(1);
    check(saved->valid() && saved->items.empty(), "empty is a successful feed");
    host.scenario = cdsdk::MockScenario::Error;
    services.refreshContent(watch.token());
    host.tick(2);
    check(saved->status == CD_DATA_ERROR && saved->errorCode == "network", "error preview");
    host.scenario = cdsdk::MockScenario::Loading;
    services.refreshContent(watch.token());
    host.tick(3);
    check(saved->status == CD_DATA_LOADING, "loading preview");
    check(services.openUrl("file:///C:/private") == CD_ERROR_ARGUMENT, "invalid link");
    check(services.openUrl("https://example.org/read") == CD_OK && host.openedUrls.size() == 1,
          "preview records command without launch");
    cdsdk::ContentWatch moved(std::move(watch));
    check(!watch.token() && moved.token(), "move ownership");
    moved.reset();
    check(host.subscriptions.empty(), "unsubscribe");
    auto self = services.content(query, [&](const auto&) {
        moved.reset();
    });
    moved = std::move(self);
    host.tick(4);
    check(host.subscriptions.empty(), "self unsubscribe");
    std::cout << "SDK content ABI, ownership, capability and four preview states PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what();
    return 1;
}
