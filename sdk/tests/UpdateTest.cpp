#include <sdk/Update.h>

#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* label) {
    if (!value) {
        throw std::runtime_error(label);
    }
    std::cout << "PASS " << label << '\n';
}

struct Host {
    struct Entry {
        CdTimerCallback callback;
        void* user;
        uint32_t interval;
    };

    CdHostApi api{};
    std::map<uint64_t, Entry> timers;
    uint64_t next = 1;
    int subscriptions = 0, redraws = 0;
    bool fail = false, partial = false;
    int errors = 0;

    Host() {
        api.struct_size = sizeof(api);
        api.abi_version = CD_PLUGIN_ABI_VERSION;
        api.request_redraw = [](void* pointer) {
            ++static_cast<Host*>(pointer)->redraws;
        };
        api.log = [](void* pointer, uint32_t level, const char*, uint32_t) {
            if (level >= 3) {
                ++static_cast<Host*>(pointer)->errors;
            }
        };
        api.subscribe_timer = [](void* pointer, uint32_t interval, CdTimerCallback callback, void* user,
                                 uint64_t* output) -> CdResult {
            auto& host = *static_cast<Host*>(pointer);
            ++host.subscriptions;
            if (host.fail && !host.partial) {
                return CD_ERROR_BUSY;
            }
            *output = host.next++;
            host.timers.emplace(*output, Entry{callback, user, interval});
            return host.fail ? CD_ERROR_BUSY : CD_OK;
        };
        api.unsubscribe = [](void* pointer, uint64_t token) {
            static_cast<Host*>(pointer)->timers.erase(token);
        };
    }

    CdCreateContext context() {
        return {sizeof(CdCreateContext), CD_PLUGIN_ABI_VERSION, "update", "update", ".", &api, this};
    }

    void tick(uint64_t now) {
        const auto batch = timers;
        for (const auto& [token, timer] : batch) {
            if (timers.contains(token)) {
                timer.callback(timer.user, now);
            }
        }
    }
};

void CD_CALL count(void* pointer, uint64_t) {
    ++*static_cast<int*>(pointer);
}
}  // namespace

int main() {
    try {
        Host host;
        auto context = host.context();
        int calls = 0;
        {
            cdsdk::Timer timer;
            timer.bind(context);
            check(timer.replace(50, count, &calls) == CD_OK && host.timers.size() == 1,
                  "timer owns one host subscription");
            const auto initial = host.timers.begin()->first;
            timer.replace(50, count, &calls);
            check(host.subscriptions == 1, "same interval and callback retain their subscription");
            host.fail = true;
            check(timer.replace(20, count, &calls) == CD_ERROR_BUSY && timer.interval() == 50 &&
                      host.timers.contains(initial),
                  "failed frequency replacement preserves the old timer");
            host.partial = true;
            check(timer.replace(20, count, &calls) == CD_ERROR_BUSY && host.timers.size() == 1,
                  "failed partial subscriptions are cleaned up");
            host.fail = host.partial = false;
            check(
                timer.replace(20, count, &calls) == CD_OK && host.timers.size() == 1 && !host.timers.contains(initial),
                "successful frequency replacement releases its predecessor");
            cdsdk::Timer moved = std::move(timer);
            check(!timer && moved, "moving transfers timer ownership");
            host.tick(10);
            check(calls == 1, "transferred subscriptions retain their callback identity");
        }
        check(host.timers.empty(), "destruction unsubscribes owned timers");
        cdsdk::RedrawRequests redraw;
        redraw.bind(context);
        const auto before = host.redraws;
        redraw.invalidate();
        redraw.invalidate();
        check(host.redraws == before + 1 && redraw.pending(), "instance invalidations coalesce until rendering");
        redraw.beginFrame();
        redraw.invalidate();
        check(host.redraws == before + 2 && redraw.requests() == 2, "a new frame permits the next visible change");
        redraw.pause(true);
        const auto hiddenRedraws = host.redraws;
        redraw.invalidate();
        redraw.invalidate();
        check(host.redraws == hiddenRedraws, "hidden invalidations defer delivery");
        redraw.pause(false);
        check(host.redraws == hiddenRedraws + 1, "resuming delivers one deferred invalidation");

        cdsdk::UpdateLoop loop;
        loop.bind(context);
        double position = 0;
        std::vector<double> deltas;
        loop.onUpdate([&](auto tick) {
            deltas.push_back(tick.unscaledDelta);
            position += tick.delta;
            return cdsdk::UpdateStep{tick.delta > 0, position < .15};
        });
        check(loop.start(50) == CD_OK && loop.start(50) == CD_OK && host.timers.size() == 1,
              "repeated loop starts do not duplicate timers");
        host.tick(1000);
        check(position == 0 && deltas.back() == 0, "the first tick establishes a monotonic anchor");
        loop.beginFrame();
        host.tick(1050);
        check(std::abs(position - .05) < 1e-9, "frame updates use elapsed time rather than frame count");
        loop.pause(true);
        loop.pause(true);
        host.tick(100000);
        check(host.timers.empty() && std::abs(position - .05) < 1e-9, "hidden loops stop subscriptions and updates");
        host.fail = true;
        check(loop.pause(false) == CD_ERROR_BUSY && !loop.active(), "resume reports a failed subscription");
        host.fail = false;
        check(loop.pause(false) == CD_OK && loop.active(), "failed resume can be retried");
        host.tick(200000);
        check(std::abs(position - .05) < 1e-9, "resuming excludes hidden time");
        const auto finalBefore = host.redraws;
        loop.beginFrame();
        host.tick(200250);
        check(!loop.active() && host.timers.empty() && host.redraws == finalBefore + 1,
              "finite work delivers its final changed frame before stopping");
        const auto quiet = host.redraws;
        host.tick(200300);
        check(host.redraws == quiet, "settled work stops requesting frames");
        loop.onUpdate([](auto) -> cdsdk::UpdateStep {
            throw std::runtime_error("callback failure");
        });
        loop.start(20);
        host.tick(200400);
        check(!loop.active() && host.timers.empty(), "callback exceptions stop work at the C boundary");
        check(loop.failed() && host.errors == 1 && loop.start(20) == CD_ERROR_INTERNAL && host.timers.empty(),
              "failed work reports once and audio-style retries cannot restart it");
        loop.resetFailure();
        check(!loop.failed(), "explicit recovery clears failure without silently restarting");
        loop.onUpdate([&](auto) {
            loop.start(80);
            return cdsdk::UpdateStep{false, false};
        });
        loop.start(20);
        host.tick(200500);
        check(loop.active() && loop.interval() == 80, "callback retiming is not overwritten by stale completion");
        loop.stop();
        loop.onUpdate([&](auto tick) {
            position = tick.delta;
            return cdsdk::UpdateStep{false, false};
        });
        loop.speed(.75);
        loop.start(50, 300000);
        host.tick(300100);
        check(std::abs(position - .075) < 1e-9, "explicit anchors preserve the requested playback speed");
        loop.onUpdate([](auto) {
            return cdsdk::UpdateStep{false, true};
        });
        loop.start(50);
        loop.speed(0);
        check(!loop.active() && host.timers.empty(), "zero playback speed suspends visual subscriptions");
        loop.speed(1);
        check(loop.active() && host.timers.size() == 1, "restoring playback speed resumes the desired cadence");
        Host second;
        cdsdk::UpdateLoop other;
        other.bind(second.context());
        other.onUpdate([](auto) {
            return cdsdk::UpdateStep{false, true};
        });
        other.start(30);
        loop.stop();
        check(second.timers.size() == 1 && host.timers.empty(), "one instance cannot stop another instance's work");

        struct Tiny {
            uint32_t size = 8, version = 1;
        } tiny;

        context.host = reinterpret_cast<const CdHostApi*>(&tiny);
        cdsdk::Timer fallback;
        fallback.bind(context);
        check(fallback.replace(10, count, &calls) == CD_ERROR_UNSUPPORTED,
              "undersized hosts are rejected without reading their missing fields");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
