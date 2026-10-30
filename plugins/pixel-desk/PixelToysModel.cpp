#include "plugins/pixel-desk/PixelToysModel.h"

namespace pixeldesk {
toys::ModelOptions pixelToysOptions() {
    return {.pixels = true, .separateEnclosure = true};
}
}  // namespace pixeldesk
