#pragma once

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include <chrono>
#include <fstream>
#include <thread>

#include "tools/Runtime.h"

namespace cdsdk {
inline constexpr const char* diagnosticNames[]{"frames",
                                               "frameNs",
                                               "copies",
                                               "copyNs",
                                               "surfaceRebuilds",
                                               "surfaceBytes",
                                               "textMeasures",
                                               "textMeasureNs",
                                               "textLayouts",
                                               "textLayoutNs",
                                               "textCacheHits",
                                               "textEntries",
                                               "uploads",
                                               "uploadNs",
                                               "uploadedBytes",
                                               "resourceBytes",
                                               "pixelBytes",
                                               "legacyBitmapBytes",
                                               "legacySourceBytes",
                                               "trackedBytes",
                                               "peakBytes",
                                               "sceneLayouts",
                                               "sceneLayoutNs",
                                               "scenePaints",
                                               "scenePaintNs",
                                               "sceneMeasures",
                                               "sceneMeasureNs",
                                               "listPrefixBuilds",
                                               "listPrefixNs",
                                               "listDraws",
                                               "listPaintNs",
                                               "listRows",
                                               "imageRequests",
                                               "imageReady",
                                               "imagePending",
                                               "imageFailed",
                                               "imageReused",
                                               "slotBytes",
                                               "slotPeakBytes",
                                               "referencedDecodeNs",
                                               "sharedDecodes",
                                               "sharedDecodeNs",
                                               "sharedDecodeBytes",
                                               "sharedDecodePeak",
                                               "invalidations",
                                               "frameRequests",
                                               "coalesced",
                                               "legacyDecodeNs",
                                               "cancelledFrames",
                                               "sharedPending",
                                               "sharedEntries",
                                               "bitmapCacheHits",
                                               "legacyDecodes",
                                               "textDraws",
                                               "sceneReferencedBytes",
                                               "imageNotifications",
                                               "imagePolls",
                                               "listUpdates",
                                               "bitmapEntries",
                                               "transientBytes"};

bool sampleDiagnostics(MockHost& host, std::vector<DiagnosticSample>* output, Error* error);

bool writeDiagnostics(NativeInstance& instance, const std::filesystem::path& path, Error* error);

bool diagnosticDirectory(const std::filesystem::path& root, const std::filesystem::path& output, Error* error);
}  // namespace cdsdk
