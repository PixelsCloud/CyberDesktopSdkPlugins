// SPDX-License-Identifier: GPL-3.0-only
#include "libraries/dwmglass/src/HookTransactionPolicy.h"

#include <KNSoft/SlimDetours/SlimDetours.h>

namespace cyberglass::dwm {
HRESULT beginHookTransaction(size_t inlineHookCount, size_t instructionPatchCount) noexcept {
    // Aligned pointer/IAT exchanges are atomic. Suspending the rest of DWM
    // during a lazy vtable update can freeze a thread needed by the renderer.
    // Code patches still require SlimDetours' normal instruction protection.
    const DETOUR_TRANSACTION_OPTIONS options{inlineHookCount || instructionPatchCount ? TRUE : FALSE};
    return SlimDetoursTransactionBeginEx(&options);
}
}  // namespace cyberglass::dwm
