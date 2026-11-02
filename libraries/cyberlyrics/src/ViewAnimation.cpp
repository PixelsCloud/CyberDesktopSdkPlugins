#include "CyberLyrics/ViewAnimation.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace cyberlyrics {
bool systemViewAnimationsEnabled() {
#ifdef _WIN32
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        return enabled != FALSE;
    }
#endif
    return true;
}
}  // namespace cyberlyrics
