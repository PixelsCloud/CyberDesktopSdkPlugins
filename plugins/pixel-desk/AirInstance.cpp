#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "plugins/pixel-desk/PixelAir.h"

namespace pixeldesk {
const AirSpec& airSpec(std::string_view id) {
    for (const auto& spec : kAirCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel Desk air-quality widget");
}

cdsdk::Settings airDefaults(bool) {
    auto values = airplugin::defaults();
    const CdSettingValue defaults[]{
        {sizeof(CdSettingValue), CD_SETTING_CHOICE, "palette", 0, 0, "dark"},
        {sizeof(CdSettingValue), CD_SETTING_FONT, "font_family", 0, 0, "Microsoft YaHei UI"}};
    values.apply(defaults, unsigned(std::size(defaults)));
    return values;
}

airplugin::Snapshot airSnapshot(const cdsdk::AirSnapshot& value, const airplugin::Query& location) {
    const auto valid = [](double number, double minimum, double maximum) {
        return std::isfinite(number) && number >= minimum && number <= maximum;
    };
    airplugin::Snapshot result;
    result.valid = value.valid() && valid(value.aqi, 0, 9999) && valid(value.latitude, -90, 90) &&
                   valid(value.longitude, -180, 180);
    result.cached = value.status == CD_DATA_STALE;
    result.fetchedAt = value.fetchedAtUtc / 1000;
    result.observedAt = value.observedAtUtc;
    result.city = location.mode == "coordinates" && !location.city.empty() ? location.city : value.city;
    result.station = value.station;
    result.url = value.stationUrl;
    result.time = value.localTime;
    result.dominant = value.dominant;
    result.latitude = value.latitude;
    result.longitude = value.longitude;
    result.aqi = value.aqi;
    for (unsigned i = 0; i < result.pollutants.size(); ++i) {
        if (value.pollutantValidFields & (1u << i)) {
            result.valid &= valid(value.pollutantIndices[i], 0, 9999);
            result.pollutants[i] = value.pollutantIndices[i];
        }
    }
    for (const auto& item : value.sources) {
        result.sources.push_back({item.name, item.url});
    }
    for (const auto& item : value.forecast) {
        result.valid &= valid(item.average, 0, 9999) && valid(item.low, 0, 9999) && valid(item.high, 0, 9999);
        result.forecast.push_back({item.date, item.average, item.low, item.high});
    }
    result.error = value.error == CD_DATA_CREDENTIALS_MISSING ? airplugin::Error::CredentialsMissing
                   : value.error == CD_DATA_UNAUTHORIZED      ? airplugin::Error::Unauthorized
                   : value.error == CD_DATA_RATE_LIMITED      ? airplugin::Error::RateLimited
                   : value.error == CD_DATA_NO_LOCATION       ? airplugin::Error::LocationUnavailable
                   : value.error == CD_DATA_NO_DATA           ? airplugin::Error::NoData
                   : value.error == CD_DATA_INVALID_RESPONSE  ? airplugin::Error::InvalidResponse
                   : value.error                              ? airplugin::Error::Network
                                                              : airplugin::Error::None;
    if (!result.valid) {
        result.latitude = result.longitude = result.aqi = 0;
        result.pollutants.fill(-1);
        result.forecast.clear();
        if (value.valid()) {
            result.error = airplugin::Error::InvalidResponse;
        }
    }
    return result;
}

AirInstance::AirInstance(const CdCreateContext& context)
    : settings(airDefaults(airSpec(context.plugin_id).rows > 1)),
      services_(context.host, context.host_context),
      renderer_(context, airSpec(context.plugin_id)) {
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    if (subscribe(settings, true) != CD_OK) {
        throw std::runtime_error("Cannot subscribe to air-quality data");
    }
}

AirInstance::~AirInstance() {
    watch_.reset();
    renderer_.onChange({});
    renderer_.pause(true);
}

CdResult AirInstance::subscribe(cdsdk::Settings next, bool clear) {
    if (hidden_) {
        settings = std::move(next);
        if (clear) {
            snapshot_ = {};
        }
        return CD_OK;
    }
    const auto location = airplugin::queryFrom(next);
    const auto mode = location.mode == "auto"      ? CD_LOCATION_DEFAULT
                      : location.mode == "city"    ? CD_LOCATION_CITY
                      : location.mode == "station" ? CD_LOCATION_STATION
                                                   : CD_LOCATION_COORDINATES;
    const CdLocationQuery query{sizeof(CdLocationQuery),
                                mode,
                                location.latitude,
                                location.longitude,
                                location.city.c_str(),
                                location.station.c_str(),
                                location.credentials.c_str(),
                                location.language.c_str(),
                                uint32_t(next.at("refresh_minutes").integer * 60),
                                5,
                                0,
                                0,
                                nullptr};

    struct Gate {
        bool committed = false;
        std::optional<cdsdk::AirSnapshot> pending;
    };

    const auto gate = std::make_shared<Gate>();
    auto candidate = services_.air(query, [this, location, gate](const cdsdk::AirSnapshot& value) {
        if (!gate->committed) {
            gate->pending = value;
            return;
        }
        receive(value, location);
    });
    if (candidate.result() != CD_OK) {
        return candidate.result();
    }
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

void AirInstance::receive(const cdsdk::AirSnapshot& value, const airplugin::Query& location) {
    if (hidden_) {
        return;
    }
    auto next = airSnapshot(value, location);
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

CdResult AirInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    if (const auto result = next.apply(values, count); result != CD_OK) {
        return result;
    }
    const auto location = airplugin::queryFrom(next);
    if (location.mode == "city" && location.city.find_first_not_of(" \t\r\n") == std::string::npos) {
        return CD_ERROR_ARGUMENT;
    }
    try {
        airplugin::wide(location.city);
        airplugin::wide(location.credentials);
        cdsdk::fontWide(next.at("font_family").text);
        if (location.mode == "station") {
            if (!airplugin::validStation(location.station)) {
                return CD_ERROR_ARGUMENT;
            }
        }
    } catch (...) {
        return CD_ERROR_ARGUMENT;
    }
    auto previous = airplugin::queryFrom(settings), numerical = location;
    previous.language.clear();
    numerical.language.clear();
    const bool changed = numerical != previous;
    if (changed || next.at("refresh_minutes").integer != settings.at("refresh_minutes").integer) {
        if (const auto result = subscribe(std::move(next), changed); result != CD_OK) {
            return result;
        }
    } else {
        settings = std::move(next);
    }
    invalidate();
    return CD_OK;
}

CdResult AirInstance::render(const CdFrame& frame) {
    if (frame.struct_size < sizeof(CdFrame)) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    return renderer_.render(settings, snapshot_, loading_, frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

void AirInstance::layoutChanged(const CdLayout& layout) {
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

void AirInstance::input(const CdInput& event) {
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
    CdAirServiceApi service{};
    if (services_.query(CD_SERVICE_AIR, service) == CD_OK && service.refresh) {
        service.refresh(services_.context(), watch_.token());
    }
}

void AirInstance::setPaused(bool paused) {
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
            snapshot_.error = airplugin::Error::Network;
        }
        invalidate();
    }
}

void AirInstance::invalidate() {
    redraw_.invalidate();
}

const airplugin::Snapshot& AirInstance::snapshot() const {
    return snapshot_;
}

bool AirInstance::loading() const {
    return loading_;
}
}  // namespace pixeldesk
