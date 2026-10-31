#pragma once

#include <array>
#include <cstdint>
#include <memory>

struct CdCreateContext;

namespace cdsdk::plugin_detail {
// Completed fixed steps, cumulative advance wall ns, awake bodies, maximum
// advance wall ns. Durations are zero unless profiling was explicitly enabled.
using PhysicsCounters = std::array<uint64_t, 4>;

// The adapter owns this registration after the plugin and destroys it first.
// Pull callbacks borrow that plugin on its owner UI thread only.
class PluginDiagnostics {
public:
    PluginDiagnostics();
    ~PluginDiagnostics();
    PluginDiagnostics(const PluginDiagnostics&) = delete;
    PluginDiagnostics& operator=(const PluginDiagnostics&) = delete;
    void bind(const CdCreateContext& context, void* user, PhysicsCounters (*sample)(void*));

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk::plugin_detail
