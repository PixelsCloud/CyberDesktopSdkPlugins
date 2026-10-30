#include <sdk/Canvas.h>
#include <sdk/Update.h>

#include <iostream>
#include <map>

// A deterministic headless host. A GUI supplies the same ABI from its event loop.
struct DemoHost {
    struct Entry {
        CdTimerCallback callback;
        void* user;
    };

    std::map<uint64_t, Entry> timers;
    uint64_t next = 1;
    bool dirty = false;
    CdHostApi api{};

    DemoHost() {
        api.struct_size = sizeof(api);
        api.abi_version = CD_PLUGIN_ABI_VERSION;
        api.request_redraw = [](void* p) {
            static_cast<DemoHost*>(p)->dirty = true;
        };
        api.subscribe_timer = [](void* p, uint32_t, CdTimerCallback callback, void* user, uint64_t* token) -> CdResult {
            auto& host = *static_cast<DemoHost*>(p);
            *token = host.next++;
            host.timers.emplace(*token, Entry{callback, user});
            return CD_OK;
        };
        api.unsubscribe = [](void* p, uint64_t token) {
            static_cast<DemoHost*>(p)->timers.erase(token);
        };
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

int wmain(int argc, wchar_t** argv) {
    try {
        DemoHost host;
        CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, "demo", "demo", ".", &host.api, &host};
        cdsdk::UpdateLoop loop;
        loop.bind(context);
        double progress = 0;
        loop.onUpdate([&](auto tick) {
            const auto previous = progress;
            progress = std::min(1., progress + tick.delta);
            return cdsdk::UpdateStep{progress != previous, progress < 1};
        });
        if (loop.start(50) != CD_OK) {
            return 1;
        }
        cdsdk::Canvas canvas;
        std::vector<uint8_t> pixels(800 * 360 * 4);
        CdFrame frame{sizeof(frame), 800, 360, 3200, pixels.size(), pixels.data(), 2};
        int frames = 0;
        for (uint64_t now = 1000; loop.active() && now < 10000; now += 50) {
            host.tick(now);
            if (host.dirty) {
                host.dirty = false;
                loop.beginFrame();
                canvas.render(frame, {}, [&](auto& draw, auto) {
                    draw.box(0, 0, 400, 180, 16, 0x152936FF);
                    draw.text(L"Host-driven updates", 22, {20, 20, 360, 42}, 0xE2F3F2FF);
                    draw.box(30, 92, 340, 9, 4.5, 0x364B59FF);
                    draw.box(30, 92, 340 * progress, 9, 4.5, 0x73D5BFFF);
                    draw.text(L"Final frame delivered · timer released", 12, {20, 120, 360, 30}, 0x9BB5C4FF);
                });
                ++frames;
            }
        }
        if (loop.active() || !host.timers.empty() || progress != 1 || !frames ||
            !canvas.save(argc > 1 ? argv[1] : L"update.png", frame)) {
            return 1;
        }
        std::cout << "{\"frames\":" << frames << ",\"activeTimers\":" << host.timers.size() << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
