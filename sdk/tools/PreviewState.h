#pragma once

#include <fstream>
#include <set>

#include "tools/Runtime.h"

namespace cdsdk {
pb::PreviewState capturePreviewState(const NativeInstance& instance, const pb::PluginDescriptor& descriptor);

void restorePreviewState(NativeInstance& instance, const pb::PluginDescriptor& descriptor,
                         const pb::PreviewState& state);

pb::PreviewState readPreviewState(const std::filesystem::path& path);

void writePreviewState(const std::filesystem::path& path, const pb::PreviewState& state);
}  // namespace cdsdk
