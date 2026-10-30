#pragma once

#include <CyberCanvas/List.h>
#include <CyberCanvas/Scene.h>

#include "sdk/Images.h"
#include "sdk/Update.h"
#include "services/Diagnostics.h"

namespace cdsdk {
struct InstanceStats {
    cybercanvas::CanvasStats canvas;
    cybercanvas::SceneStats scene;
    cybercanvas::ListStats list;
    ImageSlotStats images;
    cybercanvas::ImageStats sharedImages;
    RedrawStats redraw;
};

CdDiagnosticStats diagnosticValues(const InstanceStats& stats);

// Optional, pull-based diagnostic registration. Callbacks and their borrowed
// objects must outlive this registration and run on the owning UI thread.
class DiagnosticSource {
public:
    ~DiagnosticSource();

    DiagnosticSource() = default;
    DiagnosticSource(const DiagnosticSource&) = delete;
    DiagnosticSource& operator=(const DiagnosticSource&) = delete;

    void onSample(std::function<InstanceStats()> callback);

    void onTree(std::function<std::vector<cybercanvas::DebugNode>()> callback);

    void extras(std::function<void(InstanceStats&)> callback);

    void bind(const CdCreateContext& context);

    void unbind();

    bool available() const;

private:
    static CdResult CD_CALL read(void* user, CdDiagnosticStats* stats, CdDebugNode* nodes, uint32_t capacity,
                                 uint32_t* required) noexcept;

    CdDiagnosticsApi api_{};
    void* context_ = nullptr;
    uint64_t token_ = 0;
    std::function<InstanceStats()> sample_;
    std::function<std::vector<cybercanvas::DebugNode>()> tree_;
    std::function<void(InstanceStats&)> extras_;
};
}  // namespace cdsdk
