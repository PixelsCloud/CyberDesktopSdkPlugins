#include <iostream>
#include <stdexcept>
#include <string>

#include "plugins/productivity-suite/Productivity.h"
#include "plugins/productivity-suite/Volume.h"
#include "sdk/cpp/Snapshots.h"
#include "sdk/services/Storage.h"
#include "tools/Runtime.h"

namespace {
void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Plugin {
    cdsdk::MockHost host;
    CdInstanceApi api{};

    Plugin() {
        const CdCreateContext context{
            sizeof(context), CD_PLUGIN_ABI_VERSION, "volume-test", "instance", "", &host.api, &host};
        check(
            productivity::create({"volume-test", productivity::Kind::Disk, 1, 2}, &context, &api, sizeof(api)) == CD_OK,
            "disk creates");
    }

    ~Plugin() {
        if (api.instance) {
            api.destroy(api.instance);
        }
    }
};
}  // namespace

int main() try {
    Plugin plugin;
    check(plugin.host.subscriptions.size() == 1 && plugin.host.subscriptions.begin()->second.volume &&
              !plugin.host.subscriptions.begin()->second.timer,
          "disk uses one volume subscription and no polling timer");
    plugin.host.tick(0);
    plugin.host.dirty = false;
    plugin.host.tick(30);
    check(!plugin.host.dirty, "unchanged capacity does not redraw");
    const auto old = plugin.host.subscriptions.begin()->first;
    const CdSettingValue drive{sizeof(drive), CD_SETTING_TEXT, "drive", 0, 0, "D:/"};
    check(plugin.api.apply_settings(plugin.api.instance, &drive, 1) == CD_OK && plugin.host.subscriptions.size() == 1 &&
              !plugin.host.subscriptions.contains(old) &&
              plugin.host.subscriptions.begin()->second.volumeRoot == "D:\\",
          "changing drive replaces subscription");
    plugin.api.set_paused(plugin.api.instance, 1);
    check(plugin.host.subscriptions.empty(), "pause cancels volume polling at the host");
    const CdSettingValue interval{sizeof(interval), CD_SETTING_INTEGER, "refresh_seconds", 60};
    check(plugin.api.apply_settings(plugin.api.instance, &interval, 1) == CD_OK && plugin.host.subscriptions.empty(),
          "settings while paused never resubscribe");
    plugin.api.set_paused(plugin.api.instance, 0);
    check(plugin.host.subscriptions.size() == 1 && plugin.host.subscriptions.begin()->second.interval == 60,
          "resume uses current query");
    plugin.api.destroy(plugin.api.instance);
    plugin.api.instance = nullptr;
    check(plugin.host.subscriptions.empty(), "destruction cancels callback lifetime");

    productivity::Data data;
    CdVolumeInfo raw{{sizeof(raw), CD_DATA_READY, CD_DATA_VALID, 0, 1, 1, 1, "test", ""}, "C:\\", 100, 1000, 200};
    auto change = productivity::applyVolume(data, cdsdk::VolumeSnapshot(raw));
    check(change.changed && change.capacityChanged && change.used == .8 && data.available == 100,
          "all themes use quota-aware capacity transition");
    change = productivity::applyVolume(data, cdsdk::VolumeSnapshot(raw));
    check(!change.changed && !change.capacityChanged, "unchanged state does not restart animation");
    raw.info.status = CD_DATA_STALE;
    raw.info.error = CD_VOLUME_UNAVAILABLE;
    change = productivity::applyVolume(data, cdsdk::VolumeSnapshot(raw));
    check(change.changed && !change.capacityChanged && data.valid && data.stale && data.total == 1000,
          "failed refresh retains last capacity without restarting motion");
    std::cout << "Disk plugin: zero polling timers, no idle redraw, query replacement and shared theme state PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
