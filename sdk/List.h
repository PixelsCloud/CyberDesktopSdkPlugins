#pragma once

#include <CyberCanvas/List.h>

#include "sdk/Interaction.h"

namespace cdsdk {
class ListView : public cybercanvas::ListView {
public:
    explicit ListView(cybercanvas::ScrollStyle style = {});

    ~ListView();

    void bind(const CdCreateContext& context);

    cybercanvas::ListEvent input(const CdInput& event, cybercanvas::Point origin = {});

private:
    using cybercanvas::ListView::onInputReset;
    Interaction interaction_;
    double wheelStep_;
};
}  // namespace cdsdk
