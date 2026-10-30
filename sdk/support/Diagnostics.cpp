#include "sdk/Diagnostics.h"

namespace {
template <size_t N>
static void copyText(char (&destination)[N], const std::string& source) {
    size_t end = std::min(source.size(), N - 1);
    if (end < source.size()) {
        while (end && (uint8_t(source[end]) & 0xC0) == 0x80) {
            --end;
        }
    }
    std::copy_n(source.data(), end, destination);
    destination[end] = 0;
}

template <size_t N>
static void copyText(char (&destination)[N], const std::wstring& source) {
    const int count = int(std::min(source.size(), N - 1));
    const int length = WideCharToMultiByte(CP_UTF8, 0, source.data(), count, nullptr, 0, nullptr, nullptr);
    std::string encoded(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, source.data(), count, encoded.data(), length, nullptr, nullptr);
    copyText(destination, encoded);
}
}  // namespace

namespace cdsdk {
CdDiagnosticStats diagnosticValues(const InstanceStats& stats) {
    CdDiagnosticStats result{sizeof(result), 1};
    auto& values = result.values;
    values[CD_DIAG_FRAMES] = stats.canvas.frames.count;
    values[CD_DIAG_FRAME_NS] = stats.canvas.frames.nanoseconds;
    values[CD_DIAG_COPIES] = stats.canvas.pixelCopies.count;
    values[CD_DIAG_COPY_NS] = stats.canvas.pixelCopies.nanoseconds;
    values[CD_DIAG_SURFACE_REBUILDS] = stats.canvas.surfaceRebuilds;
    values[CD_DIAG_SURFACE_BYTES] = stats.canvas.surfaceBytes;
    values[CD_DIAG_TEXT_MEASURES] = stats.canvas.textMeasures.count;
    values[CD_DIAG_TEXT_MEASURE_NS] = stats.canvas.textMeasures.nanoseconds;
    values[CD_DIAG_TEXT_LAYOUTS] = stats.canvas.textLayouts.count;
    values[CD_DIAG_TEXT_LAYOUT_NS] = stats.canvas.textLayouts.nanoseconds;
    values[CD_DIAG_TEXT_CACHE_HITS] = stats.canvas.textCacheHits;
    values[CD_DIAG_TEXT_ENTRIES] = stats.canvas.textEntries;
    values[CD_DIAG_UPLOADS] = stats.canvas.uploads.count;
    values[CD_DIAG_UPLOAD_NS] = stats.canvas.uploads.nanoseconds;
    values[CD_DIAG_UPLOADED_BYTES] = stats.canvas.uploadedBytes;
    values[CD_DIAG_RESOURCE_BYTES] = stats.canvas.resourceBytes;
    values[CD_DIAG_PIXEL_BYTES] = stats.canvas.pixelBytes;
    values[CD_DIAG_LEGACY_BITMAP_BYTES] = stats.canvas.legacyBitmapBytes;
    values[CD_DIAG_LEGACY_SOURCE_BYTES] = stats.canvas.legacySourceBytes;
    values[CD_DIAG_TRACKED_BYTES] = stats.canvas.trackedBytes;
    values[CD_DIAG_PEAK_BYTES] = stats.canvas.peakTrackedBytes;
    values[CD_DIAG_SCENE_LAYOUTS] = stats.scene.layouts;
    values[CD_DIAG_SCENE_LAYOUT_NS] = stats.scene.layoutNanoseconds;
    values[CD_DIAG_SCENE_PAINTS] = stats.scene.paints;
    values[CD_DIAG_SCENE_PAINT_NS] = stats.scene.paintNanoseconds;
    values[CD_DIAG_SCENE_MEASURES] = stats.scene.measurements;
    values[CD_DIAG_SCENE_MEASURE_NS] = stats.scene.measureNanoseconds;
    values[CD_DIAG_LIST_PREFIX_BUILDS] = stats.list.prefixBuilds;
    values[CD_DIAG_LIST_PREFIX_NS] = stats.list.prefixNanoseconds;
    values[CD_DIAG_LIST_DRAWS] = stats.list.draws;
    values[CD_DIAG_LIST_PAINT_NS] = stats.list.paintNanoseconds;
    values[CD_DIAG_LIST_ROWS] = stats.list.drawnRows;
    values[CD_DIAG_IMAGE_REQUESTS] = stats.images.requests;
    values[CD_DIAG_IMAGE_READY] = stats.images.ready;
    values[CD_DIAG_IMAGE_PENDING] = stats.images.pending;
    values[CD_DIAG_IMAGE_FAILED] = stats.images.failed;
    values[CD_DIAG_IMAGE_REUSED] = stats.images.reusedRequests;
    values[CD_DIAG_SLOT_BYTES] = stats.images.referencedBytes;
    values[CD_DIAG_SLOT_PEAK_BYTES] = stats.images.peakReferencedBytes;
    values[CD_DIAG_REFERENCED_DECODE_NS] = stats.images.referencedDecodeNanoseconds;
    values[CD_DIAG_SHARED_DECODES] = stats.sharedImages.decodes;
    values[CD_DIAG_SHARED_DECODE_NS] = stats.sharedImages.decodeNanoseconds;
    values[CD_DIAG_SHARED_DECODE_BYTES] = stats.sharedImages.decodedBytes;
    values[CD_DIAG_SHARED_DECODE_PEAK] = stats.sharedImages.peakDecodedBytes;
    values[CD_DIAG_INVALIDATIONS] = stats.redraw.invalidations;
    values[CD_DIAG_FRAME_REQUESTS] = stats.redraw.requests;
    values[CD_DIAG_COALESCED] = stats.redraw.coalesced;
    values[CD_DIAG_LEGACY_DECODE_NS] = stats.canvas.legacyDecodes.nanoseconds;
    values[CD_DIAG_CANCELLED_FRAMES] = stats.canvas.cancelledFrames;
    values[CD_DIAG_SHARED_PENDING] = stats.sharedImages.pending;
    values[CD_DIAG_SHARED_ENTRIES] = stats.sharedImages.entries;
    values[CD_DIAG_BITMAP_CACHE_HITS] = stats.canvas.bitmapCacheHits;
    values[CD_DIAG_LEGACY_DECODES] = stats.canvas.legacyDecodes.count;
    values[CD_DIAG_TEXT_DRAWS] = stats.canvas.textDraws;
    values[CD_DIAG_SCENE_REFERENCED_BYTES] = stats.scene.referencedImageBytes;
    values[CD_DIAG_IMAGE_NOTIFICATIONS] = stats.images.notifications;
    values[CD_DIAG_IMAGE_POLLS] = stats.images.polls;
    values[CD_DIAG_LIST_UPDATES] = stats.list.updates;
    values[CD_DIAG_BITMAP_ENTRIES] = stats.canvas.bitmapEntries;
    values[CD_DIAG_TRANSIENT_BYTES] = stats.canvas.transientBytes;
    return result;
}

DiagnosticSource::~DiagnosticSource() {
    unbind();
}

auto DiagnosticSource::onSample(std::function<InstanceStats()> callback) -> void {
    sample_ = std::move(callback);
}

auto DiagnosticSource::onTree(std::function<std::vector<cybercanvas::DebugNode>()> callback) -> void {
    tree_ = std::move(callback);
}

auto DiagnosticSource::extras(std::function<void(InstanceStats&)> callback) -> void {
    extras_ = std::move(callback);
}

auto DiagnosticSource::bind(const CdCreateContext& context) -> void {
    unbind();
    if (!context.host || context.host->struct_size < sizeof(CdServiceHostApi)) {
        return;
    }
    const auto* host = reinterpret_cast<const CdServiceHostApi*>(context.host);
    CdDiagnosticsApi api{};
    if (!host->query_service ||
        host->query_service(context.host_context, CD_SERVICE_DIAGNOSTICS, 1, &api, sizeof(api)) != CD_OK ||
        api.struct_size < sizeof(api) || api.version != 1 || !api.register_source || !api.unregister_source) {
        return;
    }
    uint64_t token = 0;
    const auto result = api.register_source(context.host_context, read, this, &token);
    if (result != CD_OK || !token) {
        if (token) {
            api.unregister_source(context.host_context, token);
        }
        return;
    }
    api_ = api;
    context_ = context.host_context;
    token_ = token;
}

auto DiagnosticSource::unbind() -> void {
    if (token_) {
        api_.unregister_source(context_, std::exchange(token_, 0));
    }
    api_ = {};
    context_ = nullptr;
}

auto DiagnosticSource::available() const -> bool {
    return token_ != 0;
}

CdResult CD_CALL DiagnosticSource::read(void* user, CdDiagnosticStats* stats, CdDebugNode* nodes, uint32_t capacity,
                                        uint32_t* required) noexcept {
    if (!user || !stats || stats->struct_size < sizeof(*stats) || !required) {
        return CD_ERROR_ARGUMENT;
    }
    try {
        const auto& self = *static_cast<DiagnosticSource*>(user);
        auto sample = self.sample_ ? self.sample_() : InstanceStats{};
        if (self.extras_) {
            self.extras_(sample);
        }
        const auto tree = self.tree_ ? self.tree_() : std::vector<cybercanvas::DebugNode>{};
        if (tree.size() > 4096) {
            return CD_ERROR_ARGUMENT;
        }
        *required = uint32_t(tree.size());
        *stats = diagnosticValues(sample);
        if (capacity < tree.size()) {
            return CD_ERROR_BUFFER;
        }
        if (!tree.empty() && !nodes) {
            return CD_ERROR_ARGUMENT;
        }
        const auto rect = [](cybercanvas::Rect value) {
            return CdRect{value.x, value.y, value.width, value.height};
        };
        for (size_t i = 0; i < tree.size(); ++i) {
            const auto& node = tree[i];
            const uint32_t state = (node.visible ? CD_DEBUG_VISIBLE : 0) | (node.enabled ? CD_DEBUG_ENABLED : 0) |
                                   (node.interactive ? CD_DEBUG_INTERACTIVE : 0) |
                                   (node.focused ? CD_DEBUG_FOCUSED : 0) |
                                   (node.layoutDirty ? CD_DEBUG_LAYOUT_DIRTY : 0) |
                                   (node.loading ? CD_DEBUG_LOADING : 0) | (node.failed ? CD_DEBUG_ERROR : 0);
            nodes[i] = {sizeof(CdDebugNode), node.id,         node.kind,     state,
                        rect(node.bounds),   rect(node.clip), rect(node.hit)};
            copyText(nodes[i].name, node.name);
            copyText(nodes[i].resource, node.resource);
            copyText(nodes[i].error, node.error);
        }
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace cdsdk
