// SPDX-License-Identifier: GPL-3.0-only
#include <KNSoft/NDK/NDK.h>
#include <KNSoft/SlimDetours/SlimDetours.h>

#include "libraries/dwmglass/src/HookTransactionPolicy.h"

// Keep the pinned upstream implementation unchanged and compile it exactly once.
// Its private transaction collections select the public BeginEx policy at the
// existing Begin call. An upstream field/API change fails compilation here.
#define SlimDetoursTransactionBegin() \
    ::cyberglass::dwm::beginHookTransaction(m_inlineHooks.size(), m_instructionPatches.size())
#include "third_party/OpenGlass/Common/HookHelper.cpp"
#undef SlimDetoursTransactionBegin
