#pragma once

#include <CyberCanvas/Animation.h>

#include <array>

#include "plugins/mechanical-instruments/Artwork.h"

namespace widgetplugin {

cybercanvas::Rect albumPhotoRect(cybercanvas::Rect bounds, double seconds, bool cover, size_t index);

struct AlbumLayout {
    mechanicalart::Stage stage;
    cybercanvas::Rect photo, dock, counter, digits;
    std::array<cybercanvas::Rect, 3> buttons;
};

AlbumLayout arrangeAlbum(double width, double height);

// Clip a region from the original PNG atlas at draw time; the source image and
// its authored metal shading are cached by the SDK at the actual output DPI.
void albumSprite(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& image, cybercanvas::Rect source,
                 cybercanvas::Rect destination);

}  // namespace widgetplugin
