#include <algorithm>
#include <cmath>
#include <format>

#include "controllers/productivity/Productivity.h"

namespace mechanicaldesk_productivity {
using mechanicaldesk::Rect;
using mechanicaldesk::Skin;

Renderer::Renderer() : skin_(std::make_unique<Skin>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    skin_->bind(context, std::move(action));
}

void Renderer::input(const CdInput& event) {
    skin_->input(event);
}

void Renderer::cancelInput() {
    skin_->cancel();
}

void Renderer::pause(bool paused) {
    skin_->pause(paused);
}

void Renderer::syncInput(Spec spec, const cdsdk::Settings&, const Data&, CdRect safe) {
    std::vector<cdsdk::Controls::Item> items;
    if (spec.kind == Kind::Timer) {
        items.push_back({1, skin_->region("start"), "Start or pause timer"});
        items.push_back({2, skin_->region("reset"), "Reset timer"});
    }
    skin_->controls(safe, std::move(items));
}

bool Renderer::savePng(const std::filesystem::path& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

namespace {
std::wstring bytes(uint64_t value, uint64_t basis, bool rate = false) {
    constexpr const wchar_t* units[]{L"B", L"KiB", L"MiB", L"GiB", L"TiB", L"PiB", L"EiB"};
    unsigned unit = 0;
    double divisor = 1;
    while (basis >= 1024 && unit + 1 < std::size(units)) {
        basis /= 1024;
        divisor *= 1024;
        ++unit;
    }
    const double number = value / divisor;
    return std::format(L"{:.{}f} {}{}", number, rate && unit && number < 100 ? 1 : 0, units[unit], rate ? L"/s" : L"");
}

void history(mechanicaldesk::Canvas& canvas, const Data& data, Rect area, bool upload) {
    uint64_t maximum = 1024;
    for (const auto& value : data.history) {
        maximum = std::max(maximum, upload ? value.second : value.first);
    }
    const double step = area.width / 60;
    for (size_t i = 0; i < data.history.size(); ++i) {
        const auto& value = data.history[i];
        const double h = area.height * double(upload ? value.second : value.first) / maximum;
        const double x = area.x + (60 - data.history.size() + i) * step;
        canvas.line(x, area.y + area.height, x, area.y + area.height - h, upload ? mechanicaldesk::kBrass : 0xadcfb6ff,
                    std::max(.8, step * .5));
    }
}
}  // namespace

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, const Data& data, const CdFrame& frame, CdRect safe) {
    syncInput(spec, settings, data, safe);
    const auto family = skin_->font(settings.at("font_family").text);
    return skin_->render(frame, safe, [&](auto& canvas) {
        const auto label = [&](std::string_view key, const std::wstring& value, double font,
                               uint32_t color = mechanicaldesk::kIvory, bool bold = false) {
            skin_->label(canvas, value, skin_->region(key), font, color, bold, family);
        };
        if (spec.kind == Kind::Timer) {
            const auto remaining = std::max(int64_t(0), data.remaining);
            label("time", std::format(L"{:02}:{:02}", remaining / 60, remaining % 60), 66, mechanicaldesk::kIvory,
                  true);
            skin_->icon(canvas, skin_->region("start"), data.running ? "pause" : "play", mechanicaldesk::kInk);
            skin_->icon(canvas, skin_->region("reset"), "rotate-ccw", mechanicaldesk::kInk);
        } else if (spec.kind == Kind::Disk) {
            const bool valid = data.valid && data.total > 0;
            const uint64_t used = valid ? data.total - std::min(data.free, data.total) : 0;
            const int percent = valid ? int(std::lround(double(used) / data.total * 100)) : 0;
            auto drive = wide(settings.at("drive").text);
            if (drive.size() >= 2 && drive[1] == L':') {
                drive.resize(2);
            }
            label("drive", drive, 142, mechanicaldesk::kSteel);
            label("percent", valid ? std::to_wstring(percent) + L"%" : L"--%", 246, mechanicaldesk::kIvory, true);
            if (valid) {
                const auto total = bytes(data.total, data.total);
                const auto amount = bytes(used, data.total);
                label("capacity", amount.substr(0, amount.find(L' ')) + L" / " + total, 120);
            } else {
                label("capacity", data.loading ? L"Loading" : L"-- / --", 120);
            }
        } else if (spec.kind == Kind::Network) {
            label("download", data.valid ? bytes(data.down, data.down, true) : L"--", 32);
            label("upload", data.valid ? bytes(data.up, data.up, true) : L"--", 32, mechanicaldesk::kBrass);
            if (settings.at("history").integer) {
                history(canvas, data, skin_->region("downloadHistory"), false);
                history(canvas, data, skin_->region("uploadHistory"), true);
            }
        } else if (spec.kind == Kind::Countdown) {
            const bool en = settings.at("language").text == "en";
            const auto title = wide(settings.at("title").text);
            label("date", wide(settings.at("date").text), skin_->rows() == 1 ? 18 : 22, mechanicaldesk::kInk);
            label("title", title, 18);
            label("days", std::to_wstring(data.days) + (en ? L" days" : L" 天"), spec.columns == 4 ? 31 : 40,
                  mechanicaldesk::kIvory, true);
        }
    });
}
}  // namespace mechanicaldesk_productivity
