#include "tests/ComponentServicesTest.h"

#include <limits>

#include "templates/components/ServiceModel.h"
#include "tools/Runtime.h"

void componentServicesTest(const std::function<void(const char*, bool)>& check) {
    using Model = componentdemo::ServiceModel;
    using State = Model::State;
    cdsdk::MockHost host;
    CdCreateContext context{sizeof(context), 1, "test", "services", ".", &host.api, &host};
    int changes = 0;
    {
        Model info(context, 0, [&] {
            ++changes;
        });
        info.active(true);
        check("live info begins loading", info.view().state == State::Loading);
        host.cpuPercent = 62;
        host.tick(0);
        check("live info reads CPU and memory",
              info.view().state == State::Ready && info.view().cpu == 62 && info.view().memory == 50);
        const auto unchanged = changes;
        host.tick(1);
        check("unchanged telemetry does not invalidate", changes == unchanged);
        auto delivery = host.subscriptions.begin()->second;
        CdSystemInfoEx invalid{};
        invalid.base.struct_size = sizeof(invalid);
        delivery.system(delivery.user, &invalid.base);
        check("missing telemetry is an empty state", info.view().state == State::Empty);
        invalid.base.cpu_percent = std::numeric_limits<double>::quiet_NaN();
        delivery.system(delivery.user, &invalid.base);
        check("invalid telemetry is an error", info.view().state == State::Error);
        info.retry();
        host.tick(2);
        check("retry resubscribes and recovers", info.view().state == State::Ready && host.subscriptions.size() == 1);
        info.active(false);
        check("hidden info releases subscriptions and deadlines", host.subscriptions.empty());
        host.cpuPercent = 81;
        info.active(true);
        check("resume clears stale presentation", info.view().state == State::Loading);
        host.tick(3);
        check("resume receives fresh data", info.view().cpu == 81);
        info.active(false);
        host.capabilities.erase("SYSTEM_INFO");
        info.active(true);
        check("capability failure is unavailable",
              info.view().state == State::Unavailable && host.subscriptions.empty());
        host.capabilities.insert("SYSTEM_INFO");
        info.retry();
        // Suppress the producer while still delivering the host deadline timer.
        for (auto& [token, sub] : host.subscriptions) {
            if (sub.system) {
                sub.nextTime = 1000;
            }
        }
        host.tick(19);
        check("missing initial delivery times out", info.view().state == State::Timeout);
    }
    check("info destruction cancels remaining watch", host.subscriptions.empty());
    {
        Model media(context, 2, [&] {
            ++changes;
        });
        host.hasMediaSession = false;
        media.active(true);
        host.tick(20);
        check("no player clears media and disables commands",
              media.view().state == State::Empty && !media.can(CD_MEDIA_TOGGLE));
        host.hasMediaSession = true;
        host.tick(21);
        check("new player arrives without retry",
              media.view().state == State::Ready && media.view().title == "Demo track" && media.can(CD_MEDIA_TOGGLE));
        const auto wasPlaying = media.view().playing;
        media.command(CD_MEDIA_TOGGLE);
        check("commands wait for host acknowledgement",
              media.view().pending && media.view().playing == wasPlaying && !media.can(CD_MEDIA_TOGGLE));
        host.tick(22);
        check("busy acknowledgement retains pending request", media.view().pending);
        host.tick(23);
        check("matching completion releases controls", !media.view().pending && !media.view().commandError);
        media.command(CD_MEDIA_NEXT);
        ++host.mediaGeneration;
        host.tick(24);
        check("session replacement invalidates pending command", !media.view().pending && !media.view().commandError);
        media.command(CD_MEDIA_PREVIOUS);
        for (auto& [token, sub] : host.subscriptions) {
            if (sub.media) {
                sub.commandResult = CD_ERROR_INTERNAL;
            }
        }
        host.tick(25);
        check("async command failure is visible and retryable",
              media.view().commandError && !media.view().pending && media.can(CD_MEDIA_PREVIOUS));
        media.command(CD_MEDIA_PREVIOUS);
        for (auto& [token, sub] : host.subscriptions) {
            if (sub.media) {
                sub.nextTime = 1000;
            }
        }
        host.tick(41);
        check("stuck media command times out", media.view().commandError && !media.view().pending);
        media.active(false);
        check("hidden media releases every subscription", host.subscriptions.empty());
        media.command(CD_MEDIA_TOGGLE);
        check("hidden media cannot issue commands", !media.view().pending);
        media.active(true);
        host.tick(42);
        auto delivery = host.subscriptions.begin()->second;
        CdMediaInfoEx readonly{};
        readonly.base.struct_size = sizeof(readonly);
        readonly.base.source_id = "Readonly";
        readonly.base.title = "No controls";
        delivery.media(delivery.user, &readonly.base);
        check("per-player capabilities disable unsupported controls",
              media.view().state == State::Ready && !media.can(CD_MEDIA_NEXT) && !media.can(CD_MEDIA_TOGGLE));
    }
    check("media destruction releases watch", host.subscriptions.empty());
}
