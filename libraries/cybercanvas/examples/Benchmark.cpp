#include <CyberCanvas/DataWidgets.h>
#include <CyberCanvas/Images.h>
#include <CyberCanvas/List.h>
#include <CyberCanvas/Scene.h>
#include <intrin.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {
using namespace cybercanvas;
constexpr int kWarmup = 20, kSteps = 200;
constexpr double kStep = 1. / 60;

uint64_t percentile(std::vector<uint64_t> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[size_t(std::ceil(values.size() * fraction)) - 1];
}

struct Card {
    Canvas canvas;
    LineChart chart;
    MeterGauge gauge;
    Scene scene;
    ListView list;
    WidgetTheme theme = WidgetTheme::dark();
    std::vector<uint8_t> pixels;
    FrameBuffer frame;
    Scene::Node slider = 0;
    uint64_t requests = 0;
    double phase = 0;
    bool scrolling = false;

    explicit Card(double dpi, bool scroll = false) : scrolling(scroll) {
        const uint32_t w = uint32_t(320 * dpi), h = uint32_t(240 * dpi);
        pixels.resize(size_t(w) * h * 4);
        frame = {w, h, w * 4, pixels.size(), pixels.data(), dpi};
        theme.font.size = 12;
        theme.font.ellipsis = true;
        scene.theme(theme);
        scene.onInvalidate([this] {
            ++requests;
        });
        list.onInvalidate([this] {
            ++requests;
        });
        auto root = scene.view(0).layout;
        root.padding = {12, 12, 12, 12};
        scene.style(0, root);
        View title;
        title.kind = ViewKind::Text;
        title.text = L"System overview / 系统概览";
        title.layout.height = Length::points(22);
        scene.add(0, title);
        for (const wchar_t* text : {L"CPU 23%  ·  Memory 78%", L"Network 124 KiB/s", L"Fixed data and fixed time"}) {
            title.text = text;
            title.layout.height = Length::points(18);
            scene.add(0, title);
        }
        View volume;
        volume.kind = ViewKind::Slider;
        volume.key = 1;
        volume.maximum = 100;
        volume.step = 1;
        volume.layout.height = Length::points(4);
        slider = scene.add(0, volume);
        chart.samples({5, 8, 6, 12, 15, 13, 19, 17, 23, 20, 25, 22, 29, 26});
        View graphic;
        graphic.kind = ViewKind::Decoration;
        graphic.layout.grow = 1;
        graphic.intrinsic = {};
        graphic.paint = [this](auto& draw, Rect area, const auto& colors) {
            chart.draw(draw, {area.x, area.y + 8, std::max(0., area.width - 72), std::max(0., area.height - 16)},
                       colors);
            gauge.draw(draw, {area.x + area.width - 55, area.y + 8, 48, 48}, .5 + .4 * std::sin(phase), colors);
        };
        scene.add(0, graphic);
        if (scroll) {
            std::vector<ListItem> items;
            for (int i = 0; i < 10000; ++i) {
                items.push_back({"Fixed article " + std::to_string(i), 28});
            }
            list.items(std::move(items));
            list.arrange({0, 0, 320, 240}, dpi);
        } else {
            scene.arrange(canvas, {0, 0, 320, 240}, dpi);
        }
        draw();
    }

    void draw(bool cached = false, const FrameBuffer* tile = nullptr) {
        canvas.render(frame, {}, [&](auto&, auto) {
            if (cached) {
                canvas.pixels(L"static-candidate", *tile, {0, 0, 320, 240});
                return;
            }
            canvas.box(0, 0, 320, 240, 0, theme.surface);
            if (scrolling) {
                list.draw(canvas, theme, [&](auto& draw, const ListRow& row) {
                    draw.text(std::wstring(row.id.begin(), row.id.end()), theme.font, row.bounds, theme.foreground);
                });
            } else {
                scene.draw(canvas);
            }
        });
    }
};

bool scenario(std::ostream& report, const char* name, double dpi) {
    const bool scrolling = std::strcmp(name, "scroll") == 0;
    const bool interaction = std::strcmp(name, "interaction") == 0;
    const bool animation = std::strcmp(name, "animation") == 0;
    Card card(dpi, scrolling);
    const auto slider = scrolling ? Rect{} : card.scene.bounds(card.slider);
    if (interaction) {
        card.scene.input({PointerKind::Press, {slider.x, slider.y + 2}, true});
    }
    const auto step = [&](int index) {
        if (scrolling) {
            card.list.wheel(5);
            card.list.advance(kStep);
        } else {
            if (interaction) {
                card.scene.input(
                    {PointerKind::Move, {slider.x + slider.width * (index % 101) / 100, slider.y + 2}, true});
            }
            if (animation) {
                card.phase = index * kStep;
                card.scene.invalidate();
            }
            card.scene.advance(kStep);
        }
        if (scrolling ? card.list.needsPaint() : card.scene.needsPaint()) {
            card.draw();
        }
    };
    for (int i = 0; i < kWarmup; ++i) {
        step(i);
    }
    const auto before = card.canvas.stats();
    const auto sceneBefore = card.scene.stats();
    const auto listBefore = card.list.stats();
    const auto requests = card.requests;
    std::vector<uint64_t> samples;
    samples.reserve(kSteps);
    for (int i = 0; i < kSteps; ++i) {
        const auto started = std::chrono::steady_clock::now();
        step(kWarmup + i);
        samples.push_back(nanosecondsSince(started));
    }
    const auto after = card.canvas.stats();
    const auto sceneAfter = card.scene.stats();
    const auto listAfter = card.list.stats();
    const auto frames = after.frames.count - before.frames.count;
    const auto frameNs = after.frames.nanoseconds - before.frames.nanoseconds;
    const auto p95 = percentile(samples, .95);
    const bool idle = !scrolling && !interaction && !animation;
    const bool structural = (!idle || (frames == 0 && card.requests == requests)) &&
                            sceneAfter.layouts == sceneBefore.layouts &&
                            listAfter.prefixBuilds == listBefore.prefixBuilds &&
                            (!scrolling || listAfter.drawnRows - listBefore.drawnRows <= frames * 11);
    report << "{\"name\":" << std::quoted(name) << ",\"dpi\":" << dpi << ",\"steps\":" << kSteps
           << ",\"frames\":" << frames << ",\"requests\":" << card.requests - requests
           << ",\"stepP50Ns\":" << percentile(samples, .5) << ",\"stepP95Ns\":" << p95
           << ",\"meanCanvasFrameNs\":" << (frames ? frameNs / frames : 0)
           << ",\"copyNs\":" << after.pixelCopies.nanoseconds - before.pixelCopies.nanoseconds
           << ",\"layoutNs\":" << sceneAfter.layoutNanoseconds - sceneBefore.layoutNanoseconds
           << ",\"paintCommandsNs\":" << sceneAfter.paintNanoseconds - sceneBefore.paintNanoseconds
           << ",\"textLayouts\":" << after.textLayouts.count - before.textLayouts.count
           << ",\"textCacheHits\":" << after.textCacheHits - before.textCacheHits
           << ",\"drawnRows\":" << listAfter.drawnRows - listBefore.drawnRows
           << ",\"trackedBytes\":" << after.trackedBytes << ",\"peakBytes\":" << after.peakTrackedBytes
           << ",\"structuralBudgetPassed\":" << (structural ? "true" : "false")
           << ",\"timingBudgetPassed\":" << (p95 <= 16666667 ? "true" : "false") << '}';
    if (!structural) {
        throw std::runtime_error("Idle/layout/virtual-row budget failed");
    }
    return p95 <= 16666667;
}

bool cacheExperiment(std::ostream& report, double dpi, const std::filesystem::path& fixture) {
    Card card(dpi);
    std::vector<uint64_t> immediate, cached;
    for (int i = 0; i < kSteps; ++i) {
        const auto start = std::chrono::steady_clock::now();
        card.draw();
        immediate.push_back(nanosecondsSince(start));
    }
    auto reference = card.pixels;
    auto tile = card.frame;
    tile.rgba = reference.data();
    if (!card.canvas.save(fixture.wstring(), card.frame)) {
        throw std::runtime_error("Benchmark fixture export failed");
    }
    card.draw(true, &tile);
    for (int i = 0; i < kSteps; ++i) {
        const auto start = std::chrono::steady_clock::now();
        card.draw(true, &tile);
        cached.push_back(nanosecondsSince(start));
    }
    const auto direct95 = percentile(immediate, .95), cache95 = percentile(cached, .95);
    const bool required = direct95 > 8000000 && cache95 * 1.2 < direct95;
    const bool equal = reference == card.pixels;
    report << "{\"dpi\":" << dpi << ",\"directP95Ns\":" << direct95 << ",\"candidateP95Ns\":" << cache95
           << ",\"extraPixelPayload\":" << reference.size() + card.canvas.stats().pixelBytes
           << ",\"identicalPixels\":" << (equal ? "true" : "false")
           << ",\"requiredByBudget\":" << (required ? "true" : "false") << '}';
    if (!equal) {
        throw std::runtime_error("Cache experiment changed pixels");
    }
    return required;
}

void resourceProbe(std::ostream& report, const std::filesystem::path& fixture) {
    ImageStore store;
    const auto started = std::chrono::steady_clock::now();
    auto handle = store.request(fixture);
    const auto requestNs = nanosecondsSince(started);
    for (int i = 0; i < 5000 && handle.snapshot().state == ImageState::Loading; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto image = handle.snapshot();
    if (image.state != ImageState::Ready) {
        throw std::runtime_error("Benchmark image did not decode");
    }
    Card card(2);
    for (int i = 0; i < 100; ++i) {
        card.canvas.render(card.frame, {}, [&](auto&, auto) {
            card.canvas.image(*image.image, {0, 0, 320, 240});
        });
    }
    const auto stats = card.canvas.stats();
    report << "{\"requestNs\":" << requestNs << ",\"decodes\":" << store.stats().decodes
           << ",\"decodeNs\":" << store.stats().decodeNanoseconds << ",\"decodedBytes\":" << store.stats().decodedBytes
           << ",\"uploads\":" << stats.uploads.count << ",\"uploadNs\":" << stats.uploads.nanoseconds
           << ",\"cacheHits\":" << stats.bitmapCacheHits << '}';
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "benchmark.json";
        const bool enforce = argc > 2 && std::strcmp(argv[2], "--enforce") == 0;
        if (argc > 3 || (argc > 2 && !enforce)) {
            return 2;
        }
        if (output.has_parent_path()) {
            std::filesystem::create_directories(output.parent_path());
        }
        std::ofstream report(output);
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        OSVERSIONINFOW os{sizeof(os)};
        using Version = LONG(WINAPI*)(OSVERSIONINFOW*);
        const auto version = reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        if (version) {
            version(&os);
        }
        MEMORYSTATUSEX memory{sizeof(memory)};
        GlobalMemoryStatusEx(&memory);
        int cpu[4]{};
        std::string brand;
        for (int i = 0; i < 3; ++i) {
            __cpuid(cpu, int(0x80000002u + i));
            brand.append(reinterpret_cast<char*>(cpu), 16);
        }
        brand.erase(std::find(brand.begin(), brand.end(), '\0'), brand.end());
        report
            << "{\"renderer\":\"Direct2D software/WIC\",\"cpu\":" << std::quoted(brand)
            << ",\"logicalProcessors\":" << system.dwNumberOfProcessors << ",\"msvc\":" << _MSC_FULL_VER
            << ",\"windowsBuild\":" << os.dwBuildNumber << ",\"physicalMemoryBytes\":" << memory.ullTotalPhys
            << ",\"availableMemoryBytes\":" << memory.ullAvailPhys
            << ",\"engineVersion\":" << std::quoted(CYBERCANVAS_VERSION)
            << ",\"buildConfig\":" << std::quoted(CYBERCANVAS_BENCHMARK_CONFIG) << ",\"crt\":\"/MT\""
            << ",\"release\":"
            <<
#ifdef NDEBUG
            "true"
#else
            "false"
#endif
            << ",\"logicalSize\":[320,240],\"fixedStepSeconds\":" << kStep << ",\"warmup\":" << kWarmup
            << ",\"timingBudgetNs\":16666667,\"cacheDecisionThresholdNs\":8000000,"
               "\"includes\":\"model/input/layout when dirty, drawing, EndDraw rasterization, WIC conversion\","
               "\"excludes\":\"initial frame allocation, startup, PNG encoding, host upload/composition\",\"cases\":[";
        bool first = true, timing = true, cacheRequired = false;
        for (double dpi : {1., 2., 3.}) {
            for (const char* name : {"static", "interaction", "scroll", "animation"}) {
                if (!first) {
                    report << ',';
                }
                first = false;
                timing = scenario(report, name, dpi) && timing;
            }
        }
        report << "],\"cacheExperiments\":[";
        first = true;
        std::filesystem::path fixture;
        for (double dpi : {1., 2., 3.}) {
            if (!first) {
                report << ',';
            }
            first = false;
            fixture = output.parent_path() / (output.stem().string() + "-static-" + std::to_string(int(dpi)) + ".png");
            cacheRequired = cacheExperiment(report, dpi, fixture) || cacheRequired;
        }
        report << "],\"resources\":";
        resourceProbe(report, fixture);
        report << ",\"timingBudgetPassed\":" << (timing ? "true" : "false")
               << ",\"subtreeCacheRequired\":" << (cacheRequired ? "true" : "false") << "}\n";
        if (!report) {
            return 1;
        }
        std::cout << "PASS fixed data/time benchmark; timing=" << timing << ", subtree cache required=" << cacheRequired
                  << '\n';
        return enforce && (!timing || cacheRequired) ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
