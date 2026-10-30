#include <sdk/Environment.h>

#include <iostream>

namespace {
CdEnvironmentSnapshot current{sizeof(current), 1, 0, 123, 0, CD_ENV_THEME_LIGHT, "en-US"};

CdResult CD_CALL snapshot(void*, CdEnvironmentSnapshot* output) {
    *output = current;
    return CD_OK;
}

CdResult CD_CALL query(void*, const char* id, uint32_t version, void* output, uint32_t size) {
    if (std::strcmp(id, CD_SERVICE_ENVIRONMENT) || version != 1 || size < sizeof(CdEnvironmentApi)) {
        return CD_ERROR_UNSUPPORTED;
    }
    *static_cast<CdEnvironmentApi*>(output) = {sizeof(CdEnvironmentApi), 1, snapshot};
    return CD_OK;
}
}  // namespace

int main() {
    int failed = 0;
    const auto check = [&](const char* name, bool passed) {
        failed += !passed;
        std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
    };
    CdServiceHostApi host{};
    host.input.media.base.struct_size = sizeof(host);
    host.query_service = query;
    CdCreateContext context{sizeof(context), 1, "test", "test", ".", &host.input.media.base};
    cdsdk::Environment environment;
    environment.bind(context);
    check("optional environment table binds", environment.available());
    auto time = environment.localTime();
    check("epoch UTC calendar uses standard chrono",
          time.tm_year == 70 && time.tm_mon == 0 && time.tm_mday == 1 && time.tm_wday == 4);
    current.utc_offset_minutes = -840;
    time = environment.localTime();
    check("negative offset can cross the epoch date",
          time.tm_year == 69 && time.tm_mday == 31 && time.tm_hour == 10 && time.tm_yday == 364);
    current.unix_ms = 1709164800000ull;
    current.utc_offset_minutes = 480;
    time = environment.localTime();
    check("leap-day conversion is timezone independent",
          time.tm_year == 124 && time.tm_mon == 1 && time.tm_mday == 29 && time.tm_hour == 8 && time.tm_yday == 59);
    current.monotonic_ms += 100;
    check("fixed wall time and monotonic time are independent",
          environment.snapshot().unix_ms == 1709164800000ull && environment.snapshot().monotonic_ms == 223);
    current.utc_offset_minutes = 10000;
    check("invalid host snapshots use the system fallback", environment.snapshot().utc_offset_minutes != 10000);
    CdHostApi legacy{};
    legacy.struct_size = sizeof(legacy);
    context.host = &legacy;
    environment.bind(context);
    check("legacy hosts use system time", !environment.available() &&
                                              environment.snapshot().unix_ms > 1700000000000ull &&
                                              environment.snapshot().locale[0]);
    bool rejected = false;
    try {
        cdsdk::Environment::localTime(current);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check("unsupported offsets are rejected explicitly", rejected);
    return failed ? 1 : 0;
}
