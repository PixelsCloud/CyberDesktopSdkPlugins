#include "sdk/cpp/DiagnosticHost.h"

namespace cdsdk {
auto DiagnosticHost::detach(uint64_t token) -> void {
    sources_.erase(token);
}

auto DiagnosticHost::clear() -> void {
    sources_.clear();
}

auto DiagnosticHost::empty() const -> bool {
    return sources_.empty();
}

auto DiagnosticHost::read(std::vector<DiagnosticSample>* output, DiagnosticError* error) const -> bool {
    if (!output) {
        if (error) {
            *error = {"diagnostics.output", "Missing diagnostic output"};
        }
        return false;
    }
    std::vector<DiagnosticSample> result;
    const auto sources = sources_;
    for (const auto& [token, source] : sources) {
        if (!sources_.contains(token)) {
            continue;
        }
        DiagnosticSample sample;
        sample.token = token;
        uint32_t required = 0;
        auto status = invoke(source, &sample.stats, nullptr, 0, &required);
        if ((status != CD_OK && status != CD_ERROR_BUFFER) || required > 4096 ||
            sample.stats.struct_size < sizeof(sample.stats) || sample.stats.version != 1) {
            if (error) {
                *error = {"diagnostics.sample", "Invalid source snapshot: " + std::to_string(token)};
            }
            return false;
        }
        sample.nodes.resize(required);
        status = invoke(source, &sample.stats, sample.nodes.data(), required, &required);
        if (status != CD_OK || required > sample.nodes.size()) {
            if (error) {
                *error = {"diagnostics.tree", "Unstable or invalid node snapshot: " + std::to_string(token)};
            }
            return false;
        }
        sample.nodes.resize(required);
        for (const auto& node : sample.nodes) {
            if (node.struct_size < sizeof(node) || !std::memchr(node.name, 0, sizeof(node.name)) ||
                !std::memchr(node.resource, 0, sizeof(node.resource)) ||
                !std::memchr(node.error, 0, sizeof(node.error))) {
                if (error) {
                    *error = {"diagnostics.node", "Invalid or unterminated diagnostic node payload"};
                }
                return false;
            }
            for (auto area : {node.bounds, node.clip, node.hit}) {
                if (!std::isfinite(area.x) || !std::isfinite(area.y) || !std::isfinite(area.width) ||
                    !std::isfinite(area.height) || area.width < 0 || area.height < 0 ||
                    std::max({std::abs(area.x), std::abs(area.y), area.width, area.height}) > 1e7) {
                    if (error) {
                        *error = {"diagnostics.geometry", "Nonfinite or out-of-range diagnostic bounds"};
                    }
                    return false;
                }
            }
        }
        result.push_back(std::move(sample));
    }
    *output = std::move(result);
    return true;
}

auto DiagnosticHost::invoke(const std::pair<CdDiagnosticCallback, void*>& source, CdDiagnosticStats* stats,
                            CdDebugNode* nodes, uint32_t capacity, uint32_t* required) -> CdResult {
    __try {
        return source.first(source.second, stats, nodes, capacity, required);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return CD_ERROR_INTERNAL;
    }
}

auto DiagnosticHost::attach(CdDiagnosticCallback callback, void* user, uint64_t* token) noexcept -> CdResult try {
    if (!callback || !token) {
        return CD_ERROR_ARGUMENT;
    }
    *token = 0;
    if (sources_.size() >= 16 || next_ == std::numeric_limits<uint64_t>::max()) {
        return CD_ERROR_BUSY;
    }
    const auto id = next_++;
    sources_.emplace(id, std::make_pair(callback, user));
    *token = id;
    return CD_OK;
} catch (...) {
    return CD_ERROR_INTERNAL;
}
}  // namespace cdsdk
