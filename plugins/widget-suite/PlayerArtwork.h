#pragma once

#include "plugins/mechanical-instruments/Artwork.h"
#include "plugins/widget-suite/Widget.h"

namespace widgetplugin {

// Each casing has its own authored aspect ratio. All material layers and hit
// targets use this same uniform transform, including at nonstandard cell sizes.
mechanicalart::Stage playerArtworkStage(Spec spec, double width, double height);

mechanicalart::Stage playerTonearmStage(const PlayerLayout& layout);

cybercanvas::Canvas::InkText playerTitleInk(cybercanvas::Canvas& canvas, const std::wstring& title,
                                            const PlayerLayout& layout);

}  // namespace widgetplugin
