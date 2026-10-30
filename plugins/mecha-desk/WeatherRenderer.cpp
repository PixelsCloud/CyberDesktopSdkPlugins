#include <cmath>
#include <format>

#include "controllers/weather/Weather.h"
#include "plugins/mecha-desk/Skin.h"

namespace {
using aerisskin::Canvas;
using aerisskin::Skin;

void field(Skin& skin, Canvas& c, std::string_view name, const std::wstring& text, double factor = .77) {
    const auto r = skin.region(name);
    skin.label(c, text, r, r.height * factor);
}
}  // namespace

namespace aeris_weather {
Renderer::Renderer(std::filesystem::path assets) : skin_(std::make_unique<Skin>(assets)) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context) {
    skin_->bind(context);
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

bool Renderer::savePng(const std::filesystem::path& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

bool Renderer::render(Spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading, const CdFrame& frame,
                      CdRect safe) {
    skin_->appearance(settings);
    return skin_->render(frame, safe, [&](auto& canvas) {
        const bool imperial = settings.at("units").text == "imperial";
        const bool english = settings.at("language").text == "en";
        const bool traditional = settings.at("language").text == "zh-TW";
        const auto city = skin_->region("city");
        skin_->caption(canvas, data.city.empty() ? (english ? L"Weather" : L"天气") : wide(data.city), city,
                       city.height * .72);
        field(*skin_, canvas, "temperature",
              data.valid ? std::format(L"{}°{}", int(std::round(displayTemperature(data.temperature, imperial))),
                                       imperial ? L"F" : L"C")
                         : L"--°",
              skin_->rows() == 2      ? .95
              : skin_->columns() == 2 ? .80
                                      : .70);
        skin_->metric(canvas, data.valid ? std::format(L"{}", int(data.humidity)) : L"--", L"%",
                      skin_->region("humidity"));
        skin_->metric(canvas, data.valid ? std::format(L"{:.1f}", data.wind * (imperial ? 3600 / 1609.344 : 1)) : L"--",
                      imperial ? L"mph" : L"m/s", skin_->region("wind"), 20, 12);
        field(*skin_, canvas, "humidityLabel", english ? L"RH" : traditional ? L"濕度" : L"湿度", .68);
        field(*skin_, canvas, "windLabel", english ? L"Wind" : traditional ? L"風速" : L"风速", .68);
        auto icon = skin_->region("icon");
        skin_->clear(canvas, icon);
        skin_->weather(canvas, icon, data.valid ? iconName(data.code, data.day) : "not-available", "current");
        const auto status = loading       ? (english       ? L"Updating"
                                             : traditional ? L"更新中"
                                                           : L"更新中")
                            : data.cached ? (english       ? L"Cached"
                                             : traditional ? L"快取資料"
                                                           : L"缓存数据")
                            : !data.valid ? (english       ? L"Unavailable"
                                             : traditional ? L"暫無資料"
                                                           : L"暂无数据")
                                          : L"";
        field(*skin_, canvas, "status", status, .75);
        for (unsigned i = 0; i < 3; ++i) {
            const auto r = skin_->region("forecast" + std::to_string(i));
            if (r.width <= 0) {
                continue;
            }
            skin_->clear(canvas, r);
            const std::array<std::wstring, 3> days =
                english ? std::array<std::wstring, 3>{L"Today", L"Next", L"Later"}
                        : std::array<std::wstring, 3>{L"今天", L"明天", traditional ? L"後天" : L"后天"};
            const bool compact = r.height < 24;
            // A shallow forecast strip cannot support a three-line miniature card.
            // Its ordered temperatures are useful; a 3px heading/icon is not.
            if (!compact) {
                skin_->label(canvas, days[i], {r.x, r.y, r.width, 13}, 13, aerisskin::kIvory, false);
            }
            if (r.height >= 44) {
                skin_->weather(canvas, {r.x + r.width * .19, r.y + 12, r.width * .62, r.height - 25},
                               data.valid && i < data.days.size() ? iconName(data.days[i].code, true) : "not-available",
                               "forecast" + std::to_string(i));
            }
            skin_->label(canvas,
                         data.valid && i < data.days.size()
                             ? std::format(L"{}/{}°", int(std::round(displayTemperature(data.days[i].low, imperial))),
                                           int(std::round(displayTemperature(data.days[i].high, imperial))))
                             : L"--/--°",
                         {r.x, compact ? r.y : r.y + r.height - 15, r.width, compact ? r.height : 15}, 14,
                         aerisskin::kIvory, false);
        }
    });
}

bool Renderer::renderReference(Spec spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading,
                               const CdFrame& frame, CdRect safe) {
    return render(spec, settings, data, loading, frame, safe);
}
}  // namespace aeris_weather
