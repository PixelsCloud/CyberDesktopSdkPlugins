#include "sdk/Environment.h"

namespace cdsdk {
auto Environment::bind(const CdCreateContext& context) -> void {
    api_ = {};
    context_ = context.host_context;
    if (!context.host || context.host->struct_size < sizeof(CdServiceHostApi)) {
        return;
    }
    const auto* host = reinterpret_cast<const CdServiceHostApi*>(context.host);
    if (!host->query_service ||
        host->query_service(context_, CD_SERVICE_ENVIRONMENT, 1, &api_, sizeof(api_)) != CD_OK ||
        api_.struct_size < sizeof(api_) || api_.version != 1 || !api_.snapshot) {
        api_ = {};
    }
}

auto Environment::systemSnapshot() -> CdEnvironmentSnapshot {
    CdEnvironmentSnapshot result{sizeof(result), 1};
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    const uint64_t ticks = uint64_t(time.dwHighDateTime) << 32 | time.dwLowDateTime;
    result.unix_ms = (ticks - 116444736000000000ull) / 10000;
    result.monotonic_ms = GetTickCount64();
    TIME_ZONE_INFORMATION zone{};
    const auto mode = GetTimeZoneInformation(&zone);
    result.utc_offset_minutes =
        mode == TIME_ZONE_ID_INVALID
            ? 0
            : -(zone.Bias + (mode == TIME_ZONE_ID_DAYLIGHT ? zone.DaylightBias : zone.StandardBias));
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) {
        const int size =
            WideCharToMultiByte(CP_UTF8, 0, locale, -1, result.locale, sizeof(result.locale), nullptr, nullptr);
        if (!size) {
            std::memcpy(result.locale, "en-US", 6);
        }
    } else {
        std::memcpy(result.locale, "en-US", 6);
    }
    return result;
}

auto Environment::snapshot() const -> CdEnvironmentSnapshot {
    CdEnvironmentSnapshot result{sizeof(result), 1};
    if (api_.snapshot && api_.snapshot(context_, &result) == CD_OK && result.struct_size >= sizeof(result) &&
        result.version == 1 && result.utc_offset_minutes >= -840 && result.utc_offset_minutes <= 840 &&
        result.theme <= CD_ENV_THEME_DARK && result.unix_ms <= 32503680000000ull &&
        std::memchr(result.locale, 0, sizeof(result.locale))) {
        return result;
    }
    return systemSnapshot();
}

auto Environment::localTime() const -> std::tm {
    return localTime(snapshot());
}

auto Environment::localTime(const CdEnvironmentSnapshot& snapshot) -> std::tm {
    if (snapshot.unix_ms > 32503680000000ull || snapshot.utc_offset_minutes < -840 ||
        snapshot.utc_offset_minutes > 840) {
        throw std::invalid_argument("Environment time is out of range");
    }
    using namespace std::chrono;
    const sys_time<milliseconds> utc{milliseconds{int64_t(snapshot.unix_ms)}};
    const auto local = utc + minutes{snapshot.utc_offset_minutes};
    const auto day = floor<days>(local);
    const year_month_day calendar{day};
    const hh_mm_ss clock{floor<seconds>(local - day)};
    std::tm result{};
    result.tm_year = int(calendar.year()) - 1900;
    result.tm_mon = int(unsigned(calendar.month())) - 1;
    result.tm_mday = int(unsigned(calendar.day()));
    result.tm_hour = int(clock.hours().count());
    result.tm_min = int(clock.minutes().count());
    result.tm_sec = int(clock.seconds().count());
    result.tm_wday = int(weekday{day}.c_encoding());
    result.tm_yday = int((day - sys_days{calendar.year() / January / 1}).count());
    result.tm_isdst = -1;
    return result;
}

auto Environment::available() const -> bool {
    return api_.snapshot != nullptr;
}
}  // namespace cdsdk
