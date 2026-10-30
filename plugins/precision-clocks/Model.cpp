#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

#include "eph.h"
#include "plugins/precision-clocks/Clock.h"

namespace precisionclock {
namespace {
constexpr double kTau = 2 * std::numbers::pi;
constexpr std::array<Spec, 3> kSpecs{{
    {Kind::Skeleton, "com.cyberdesktop.clock.precision.skeleton.2x2", "skeleton", {497, 502}, 410, {38, 40, 956, 934}},
    {Kind::Green, "com.cyberdesktop.clock.precision.green.2x2", "green", {491, 504}, 410, {71, 69, 904, 894}},
    {Kind::Dress, "com.cyberdesktop.clock.precision.dress.2x2", "dress", {498, 499}, 402, {72, 50, 892, 912}},
}};
}  // namespace

const Spec& specification(Kind kind) {
    const auto index = static_cast<unsigned>(kind);
    if (index >= kSpecs.size()) {
        throw std::invalid_argument("Unknown precision clock kind");
    }
    return kSpecs[index];
}

Kind identify(std::string_view id) {
    for (const auto& spec : kSpecs) {
        if (id == spec.id) {
            return spec.kind;
        }
    }
    throw std::invalid_argument("Unknown precision clock identity");
}

bool Options::moving() const {
    return animation && !reduced;
}

cdsdk::Settings defaults() {
    return {{"motion", CD_SETTING_CHOICE, 0, 0, "beat", 0, 0, {"sweep", "beat", "tick"}},
            {"seconds", CD_SETTING_TOGGLE, 1},
            {"background", CD_SETTING_TOGGLE, 1},
            {"detail", CD_SETTING_TOGGLE, 1},
            {"animation", CD_SETTING_TOGGLE, 1},
            {"reduced_motion", CD_SETTING_TOGGLE, 0}};
}

Options options(const cdsdk::Settings& settings) {
    const auto& mode = settings.at("motion").text;
    return {mode == "sweep"  ? Motion::Sweep
            : mode == "tick" ? Motion::Tick
                             : Motion::Beat,
            settings.at("animation").integer != 0,
            settings.at("reduced_motion").integer != 0,
            settings.at("seconds").integer != 0,
            settings.at("background").integer != 0,
            settings.at("detail").integer != 0};
}

uint32_t cadence(Kind kind, const Options& value, uint64_t unixMs, bool pendingAssets) {
    if (pendingAssets || (value.moving() && ((kind == Kind::Skeleton && value.detail) ||
                                             (value.seconds && value.motion == Motion::Sweep)))) {
        return 33;
    }
    const unsigned quantum = value.moving() && value.seconds && value.motion == Motion::Beat ? 125 : 1000;
    // Timer subscriptions require at least 16ms, even immediately before a beat.
    return std::max(16u, quantum - uint32_t(unixMs % quantum));
}

double moonPhase(uint64_t unixMs) {
    if (unixMs > 32503680000000ull) {
        throw std::invalid_argument("Moon phase time is outside the host range");
    }
    // The existing sxtwl ephemeris expects TT centuries from J2000. dt_T uses
    // UT days from J2000 and returns the TT-UT correction in days.
    const long double ut = static_cast<long double>(unixMs) / 86400000 + 2440587.5L - 2451545;
    const long double tt = (ut + dt_T(ut)) / 36525;
    return double(rad2mrad(XL::MS_aLon(tt, -1, 60))) / kTau;
}

double gearAngle(double seconds, unsigned teeth, bool reverse) {
    if (!std::isfinite(seconds) || !teeth) {
        throw std::invalid_argument("Invalid mechanical drive");
    }
    const double turns = std::fmod(seconds / (20. * teeth / 52), 1.);
    return turns * 360 * (reverse ? -1 : 1);
}

Model::Model(Kind kind) : kind_(kind) {}

bool Model::synchronize(const CdEnvironmentSnapshot& environment, const Options& value) {
    const auto time = cdsdk::Environment::localTime(environment);
    State next;
    const double raw = time.tm_sec + (environment.unix_ms % 1000) / 1000.;
    const auto motion = value.moving() && value.seconds ? value.motion : Motion::Tick;
    const double seconds = motion == Motion::Sweep  ? raw
                           : motion == Motion::Beat ? std::floor(raw * 8) / 8
                                                    : std::floor(raw);
    next.hour = (time.tm_hour % 12 + (time.tm_min + seconds / 60) / 60) * 30;
    next.minute = (time.tm_min + seconds / 60) * 6;
    next.second = seconds * 6;
    next.year = time.tm_year + 1900;
    next.month = time.tm_mon + 1;
    next.day = time.tm_mday;
    if (kind_ == Kind::Skeleton && value.detail && value.moving()) {
        next.mechanismSeconds = double(environment.unix_ms) / 1000;
        // Slow the display balance deliberately so a small desktop dial reads as
        // a controlled oscillation instead of undersampled high-frequency jitter.
        next.balance = 10 * std::sin(std::fmod(next.mechanismSeconds, .8) * kTau / .8);
    }
    if (kind_ == Kind::Dress) {
        const auto minute = environment.unix_ms / 60000;
        next.lunarPhase = !moonMinute_ || *moonMinute_ != minute ? moonPhase(environment.unix_ms) : state_.lunarPhase;
        moonMinute_ = minute;
    }
    const bool changed = next != state_;
    state_ = next;
    return changed;
}

const State& Model::state() const {
    return state_;
}
}  // namespace precisionclock
