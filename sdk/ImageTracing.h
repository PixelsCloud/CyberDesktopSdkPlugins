#pragma once

struct CdCreateContext;

namespace cdsdk {
// Optional process-wide diagnostic binding; no instance pointer is retained.
void bindImageTracing(const CdCreateContext& context) noexcept;
}  // namespace cdsdk
