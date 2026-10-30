#include <filesystem>
#include <iostream>
#include <string>

#include "plugins/koi-suite/Pond.h"

int wmain(int argc, wchar_t** argv) {
    using namespace koiplugin;
    if (argc < 3) {
        return 1;
    }
    const std::filesystem::path output(argv[1]), source(argv[2]);
    std::filesystem::create_directories(output);
    Renderer renderer;
    const Spec catalog[]{{"2x4", 2, 4, 5}, {"4x4", 4, 4, 8}, {"3x6", 3, 6, 9}};
    for (const auto& spec : catalog) {
        for (int variant = 0; variant < 3; ++variant) {
            const double dpr = variant == 0 ? 2 : variant == 1 ? 1 : 3;
            const int cellWidth = variant == 0 ? 96 : 72;
            const int cellHeight = variant == 2 ? 150 : cellWidth;
            const double w = spec.columns * cellWidth - 8, h = spec.rows * cellHeight - 8;
            const uint32_t width = uint32_t(w * dpr), height = uint32_t(h * dpr);
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpr};
            Scene scene;
            scene.configure(w, h, spec.fishCount, 7, fishLengthForGrid(w, h, spec.rows, spec.columns));
            for (int step = 0; step < 90; ++step) {
                scene.advance(1.0 / 30, 1);
            }
            for (int night : {0, 1}) {
                if (!renderer.render(scene, {double(night)}, frame, {})) {
                    return 2;
                }
                const auto size = std::to_wstring(spec.rows) + L"x" + std::to_wstring(spec.columns);
                const auto name = size + (night ? L"-night" : L"-day") + L"-" + std::to_wstring(variant) + L".png";
                if (!renderer.savePng((output / name).wstring(), frame)) {
                    return 3;
                }
                if (variant == 0 && !night) {
                    const auto preview = source / "packages" / spec.id / "preview.png";
                    std::filesystem::create_directories(preview.parent_path());
                    std::filesystem::copy_file(output / name, preview,
                                               std::filesystem::copy_options::overwrite_existing);
                }
            }
            if (variant == 0) {
                Appearance transparent;
                transparent.background = false;
                const auto size = std::to_wstring(spec.rows) + L"x" + std::to_wstring(spec.columns);
                if (!renderer.render(scene, transparent, frame, {}) ||
                    !renderer.savePng((output / (size + L"-transparent.png")).wstring(), frame)) {
                    return 6;
                }
            }
        }
    }
    if (argc > 3) {
        const std::filesystem::path frames(argv[3]);
        std::filesystem::create_directories(frames);
        // Twelve seconds cover a startle, complete exit and opposite-side re-entry.
        for (int night : {0, 1}) {
            Scene scene;
            scene.configure(568, 280, 5, 7, fishLengthForGrid(568, 280, 2, 4));
            const uint32_t width = 568, height = 280;
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 1};
            for (int index = 0; index < 720; ++index) {
                if (index == 120) {
                    scene.click({284, 140});
                }
                scene.advance(1.0 / 60, 1);
                Appearance appearance{double(night)};
                appearance.redrawPond = index % 3 == 0;
                if (!renderer.render(scene, appearance, frame, {})) {
                    return 4;
                }
                const auto number = std::to_wstring(1000 + index).substr(1);
                const auto name = (night ? L"night-" : L"day-") + number + L".png";
                if (!renderer.savePng((frames / name).wstring(), frame)) {
                    return 5;
                }
            }
        }
        // A second export keeps the 2x4 card's actual logical size, without zooming.
        for (int night : {0, 1}) {
            Scene scene;
            scene.configure(280, 136, 5, 7, fishLengthForGrid(280, 136, 2, 4));
            std::vector<uint8_t> pixels(280 * 136 * 4);
            CdFrame frame{sizeof(frame), 280, 136, 280 * 4, pixels.size(), pixels.data(), 1};
            for (int index = 0; index < 480; ++index) {
                scene.advance(1.0 / 60, 1);
                Appearance appearance{double(night)};
                appearance.redrawPond = index % 3 == 0;
                const auto number = std::to_wstring(1000 + index).substr(1);
                const auto name = (night ? L"actual-night-" : L"actual-day-") + number + L".png";
                if (!renderer.render(scene, appearance, frame, {}) ||
                    !renderer.savePng((frames / name).wstring(), frame)) {
                    return 10;
                }
            }
        }
    }
    {
        Scene scene;
        scene.configure(300, 300, 1, 7);
        scene.setMouseBehavior(MouseBehavior::Follow);
        scene.pointer({150, 150});
        for (int step = 0; step < 90; ++step) {
            scene.advance(1.0 / 60, 1);
        }
        std::vector<uint8_t> pixels(900 * 900 * 4);
        CdFrame frame{sizeof(frame), 900, 900, 900 * 4, pixels.size(), pixels.data(), 3};
        Appearance portrait;
        portrait.background = false;
        portrait.leaves = false;
        portrait.flowers = false;
        portrait.shimmer = 0;
        portrait.blessings = false;
        if (!renderer.render(scene, portrait, frame, {}) ||
            !renderer.savePng((output / L"reference-fins.png").wstring(), frame)) {
            return 7;
        }
    }
    std::cout << "18 native pond renders and 3 updated catalog previews exported\n";
    {
        Scene scene;
        scene.configure(280, 136, 3, 7, fishLengthForGrid(280, 136, 2, 4));
        // The scene itself is mutable; only this deterministic preview replaces poses.
        auto& fish = const_cast<std::vector<Fish>&>(scene.fish());
        for (size_t index = 0; index < fish.size(); ++index) {
            auto& pose = fish[index];
            pose.finPhase = index == 0 ? 0 : index == 1 ? kPi / 2 : 3 * kPi / 2;
            pose.depth = .1;
            for (size_t joint = 0; joint < pose.joints.size(); ++joint) {
                pose.angles[joint] = kPi / 2;
                pose.joints[joint] = Vec{55.0 + index * 85, 105} - direction(kPi / 2) * (pose.link * joint);
            }
        }
        std::vector<uint8_t> pixels(1120 * 544 * 4);
        CdFrame frame{sizeof(frame), 1120, 544, 1120 * 4, pixels.size(), pixels.data(), 4};
        Appearance portrait;
        portrait.leaves = false;
        portrait.flowers = false;
        portrait.shimmer = 0;
        portrait.blessings = false;
        for (int night : {0, 1}) {
            portrait.night = night;
            if (!renderer.render(scene, portrait, frame, {}) ||
                !renderer.savePng((output / (night ? L"tail-straight-night.png" : L"tail-straight-day.png")).wstring(),
                                  frame)) {
                return 8;
            }
        }
        fish.resize(1);
        auto& white = fish.front();
        white.pattern = 4;
        for (size_t joint = 0; joint < white.joints.size(); ++joint) {
            white.angles[joint] = kPi + joint * .13;
            white.joints[joint] =
                joint ? white.joints[joint - 1] - direction(white.angles[joint]) * white.link : Vec{120, 90};
        }
        for (int night : {0, 1}) {
            portrait.night = night;
            if (!renderer.render(scene, portrait, frame, {}) ||
                !renderer.savePng((output / (night ? L"white-koi-night.png" : L"white-koi-day.png")).wstring(),
                                  frame)) {
                return 9;
            }
        }
    }
    return 0;
}
