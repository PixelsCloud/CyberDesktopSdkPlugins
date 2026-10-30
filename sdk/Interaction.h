#pragma once

#include <CyberCanvas/Interaction.h>

#include "sdk/services/Interaction.h"

namespace cdsdk {
// Optional host binding. The router itself remains independent of this SDK.
class Interaction {
private:
    cybercanvas::InputRouter ownedRouter_;

public:
    Interaction();

    explicit Interaction(cybercanvas::InputRouter& shared);

    Interaction(const Interaction&) = delete;
    Interaction& operator=(const Interaction&) = delete;

    cybercanvas::InputRouter& router;

    cybercanvas::PointerResult regions(std::vector<cybercanvas::HitRegion> values);

    void bind(const CdCreateContext& context);

    cybercanvas::PointerResult input(const CdInput& event);

    cybercanvas::PointerResult cancel();

    bool streaming() const;

private:
    CdInteractionApi api_{};
    void* context_ = nullptr;
    bool streaming_ = false;
};
}  // namespace cdsdk
