#pragma once

#include <excpt.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "sdk/services/Diagnostics.h"

namespace cdsdk {
struct DiagnosticSample {
    uint64_t token = 0;
    CdDiagnosticStats stats{sizeof(CdDiagnosticStats)};
    std::vector<CdDebugNode> nodes;
};

struct DiagnosticError {
    std::string code, message;
};

// UI-thread host registry shared by the native application and SDK tools.
// Clearing removes callbacks without invoking DLL code. Destroy the plugin
// before its module is unloaded; sources must unregister during destruction.
class DiagnosticHost {
public:
    CdResult attach(CdDiagnosticCallback callback, void* user, uint64_t* token) noexcept;

    void detach(uint64_t token);

    void clear();

    bool empty() const;

    bool read(std::vector<DiagnosticSample>* output, DiagnosticError* error = nullptr) const;

private:
    static CdResult invoke(const std::pair<CdDiagnosticCallback, void*>& source, CdDiagnosticStats* stats,
                           CdDebugNode* nodes, uint32_t capacity, uint32_t* required);

    std::map<uint64_t, std::pair<CdDiagnosticCallback, void*>> sources_;
    uint64_t next_ = 1;
};
}  // namespace cdsdk
