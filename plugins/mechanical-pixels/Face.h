#pragma once

#include <functional>
#include <string_view>

struct CdFrame;
struct CdRect;

namespace cdsdk {
class Settings;
}

namespace widgetplugin {
struct Spectrum;
}

namespace mechanicalpixels {
enum class Kind;

// Controllers own their rendering strategy. Themes share the clock/FFT
// subscription, scheduling and settings behavior without copying it.
class Face {
public:
    virtual ~Face();
    virtual void onChange(std::function<void()> callback) = 0;
    virtual void pause(bool paused) = 0;
    virtual bool render(Kind kind, const cdsdk::Settings& settings, std::string_view text, bool colonVisible,
                        const widgetplugin::Spectrum& spectrum, const CdFrame& frame, CdRect safe) = 0;
};
}  // namespace mechanicalpixels
