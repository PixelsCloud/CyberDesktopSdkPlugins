#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "plugins/pixel-desk/PixelWeather.h"

namespace pixeldesk {
const WeatherSpec& weatherSpec(std::string_view id) {
    for (const auto& spec : kWeatherCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel Desk weather widget");
}

weatherplugin::Snapshot weatherSnapshot(const cdsdk::WeatherSnapshot& value, const weatherplugin::Location& location) {
    weatherplugin::Snapshot result;
    const auto between = [](double number, double minimum, double maximum) {
        return std::isfinite(number) && number >= minimum && number <= maximum;
    };
    // Match the existing provider's numeric limits at the host ABI boundary.
    result.valid = value.valid() && between(value.temperatureCelsius, -120, 100) &&
                   between(value.feelsCelsius, -150, 150) && between(value.humidityPercent, 0, 100) &&
                   between(value.windMps, 0, 150) && between(value.latitude, -90, 90) &&
                   between(value.longitude, -180, 180);
    result.cached = value.status == CD_DATA_STALE;
    result.fetchedAt = value.fetchedAtUtc / 1000;
    result.observedAt = value.observedAtUtc / 1000;
    result.city = !location.automatic && location.coordinates && !location.city.empty() ? location.city : value.city;
    result.region = value.region;
    result.time = value.localTime;
    result.latitude = value.latitude;
    result.longitude = value.longitude;
    result.temperature = value.temperatureCelsius;
    result.feels = value.feelsCelsius;
    result.humidity = value.humidityPercent;
    result.wind = value.windMps;
    result.code = value.conditionCode;
    result.day = value.isDay;
    for (const auto& day : value.days) {
        result.valid &= between(day.high, -120, 100) && between(day.low, -120, 100) &&
                        between(day.precipitationProbability, 0, 100);
        result.days.push_back({day.date, day.code, day.high, day.low, day.precipitationProbability});
    }
    result.error = value.error == CD_DATA_RATE_LIMITED       ? weatherplugin::Error::RateLimited
                   : value.error == CD_DATA_NO_LOCATION      ? weatherplugin::Error::LocationUnavailable
                   : value.error == CD_DATA_INVALID_RESPONSE ? weatherplugin::Error::InvalidResponse
                   : value.error                             ? weatherplugin::Error::Network
                                                             : weatherplugin::Error::None;
    if (!result.valid) {
        result.temperature = result.feels = result.humidity = result.wind = result.latitude = result.longitude = 0;
        result.days.clear();
        if (value.valid()) {
            result.error = weatherplugin::Error::InvalidResponse;
        }
    }
    return result;
}

WeatherInstance::WeatherInstance(const CdCreateContext& context)
    : settings(weatherplugin::defaults(true)),
      services_(context.host, context.host_context),
      renderer_(context, weatherSpec(context.plugin_id)) {
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    if (subscribe(settings, true) != CD_OK) {
        throw std::runtime_error("Cannot subscribe to weather data");
    }
}

WeatherInstance::~WeatherInstance() {
    watch_.reset();
    renderer_.onChange({});
    renderer_.pause(true);
}

CdResult WeatherInstance::subscribe(cdsdk::Settings next, bool clear) {
    if (hidden_) {
        settings = std::move(next);
        if (clear) {
            snapshot_ = {};
        }
        return CD_OK;
    }
    const auto location = weatherplugin::locationFrom(next);
    const CdLocationQuery query{sizeof(CdLocationQuery),
                                location.automatic     ? CD_LOCATION_DEFAULT
                                : location.coordinates ? CD_LOCATION_COORDINATES
                                                       : CD_LOCATION_CITY,
                                location.latitude,
                                location.longitude,
                                location.city.c_str(),
                                nullptr,
                                "default",
                                location.language.c_str(),
                                uint32_t(next.at("refresh_minutes").integer * 60),
                                5,
                                0,
                                0,
                                location.country.c_str()};

    struct Gate {
        bool committed = false;
        std::optional<cdsdk::WeatherSnapshot> pending;
    };

    const auto gate = std::make_shared<Gate>();
    auto candidate = services_.weather(query, [this, location, gate](const cdsdk::WeatherSnapshot& value) {
        if (!gate->committed) {
            gate->pending = value;
            return;
        }
        receive(value, location);
    });
    if (candidate.result() != CD_OK) {
        return candidate.result();
    }
    // A failed replacement must leave the existing subscription, location and
    // visible values intact, even when a host delivers cached data synchronously.
    watch_ = std::move(candidate);
    settings = std::move(next);
    if (clear) {
        snapshot_ = {};
    }
    loading_ = true;
    gate->committed = true;
    if (gate->pending) {
        receive(*gate->pending, location);
        gate->pending.reset();
    }
    return CD_OK;
}

void WeatherInstance::receive(const cdsdk::WeatherSnapshot& value, const weatherplugin::Location& location) {
    if (hidden_) {
        return;
    }
    auto next = weatherSnapshot(value, location);
    if (!next.valid && snapshot_.valid) {
        const auto error = next.error;
        next = snapshot_;
        next.cached = true;
        next.error = error;
    }
    const bool loading = value.status == CD_DATA_LOADING;
    if (next != snapshot_ || loading != loading_) {
        snapshot_ = std::move(next);
        loading_ = loading;
        invalidate();
    }
}

CdResult WeatherInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    if (const auto result = next.apply(values, count); result != CD_OK) {
        return result;
    }
    const auto location = weatherplugin::locationFrom(next);
    if (!location.automatic && !location.coordinates &&
        location.city.find_first_not_of(" \t\r\n") == std::string::npos) {
        return CD_ERROR_ARGUMENT;
    }
    if (!location.country.empty() &&
        (location.country.size() != 2 || !std::all_of(location.country.begin(), location.country.end(), [](char ch) {
             return ch >= 'A' && ch <= 'Z';
         }))) {
        return CD_ERROR_ARGUMENT;
    }
    try {
        weatherplugin::wide(location.city);
        cdsdk::fontWide(next.at("font_family").text);
    } catch (...) {
        return CD_ERROR_ARGUMENT;
    }
    auto previous = weatherplugin::locationFrom(settings), numerical = location;
    previous.language.clear();
    numerical.language.clear();
    const bool changedLocation = numerical != previous;
    const bool changedInterval = next.at("refresh_minutes").integer != settings.at("refresh_minutes").integer;
    if (changedLocation || changedInterval) {
        if (const auto result = subscribe(std::move(next), changedLocation); result != CD_OK) {
            return result;
        }
    } else {
        settings = std::move(next);
    }
    invalidate();
    return CD_OK;
}

CdResult WeatherInstance::render(const CdFrame& frame) {
    if (frame.struct_size < sizeof(CdFrame)) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    return renderer_.render(settings, snapshot_, loading_, frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

void WeatherInstance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    const auto r = layout.safe_content;
    safe_ = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) && r.x >= 0 &&
                    r.y >= 0 && r.width > 0 && r.height > 0 && r.x + r.width <= layout.logical_width &&
                    r.y + r.height <= layout.logical_height
                ? r
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
}

void WeatherInstance::input(const CdInput& event) {
    if (hidden_ || event.kind != CD_INPUT_DOUBLE_CLICK || !std::isfinite(event.x) || !std::isfinite(event.y) ||
        event.x < safe_.x || event.y < safe_.y || event.x >= safe_.x + safe_.width ||
        event.y >= safe_.y + safe_.height) {
        return;
    }
    if (!watch_.token()) {
        subscribe(settings, false);
        invalidate();
        return;
    }
    CdWeatherServiceApi service{};
    if (services_.query(CD_SERVICE_WEATHER, service) == CD_OK && service.refresh) {
        service.refresh(services_.context(), watch_.token());
    }
}

void WeatherInstance::setPaused(bool paused) {
    if (hidden_ == paused) {
        return;
    }
    hidden_ = paused;
    renderer_.pause(paused);
    redraw_.pause(paused);
    if (paused) {
        watch_.reset();
        loading_ = false;
    } else {
        if (subscribe(settings, false) != CD_OK) {
            loading_ = false;
            snapshot_.cached = snapshot_.valid;
            snapshot_.error = weatherplugin::Error::Network;
        }
        invalidate();
    }
}

void WeatherInstance::invalidate() {
    redraw_.invalidate();
}

const weatherplugin::Snapshot& WeatherInstance::snapshot() const {
    return snapshot_;
}

bool WeatherInstance::loading() const {
    return loading_;
}
}  // namespace pixeldesk
