// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <Windows.h>

#include <cstddef>

namespace cyberglass::dwm {
HRESULT beginHookTransaction(size_t inlineHookCount, size_t instructionPatchCount) noexcept;
}
