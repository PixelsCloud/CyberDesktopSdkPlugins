#pragma once

#include <Windows.h>

#include <memory>

#include "sdk/services/TextInput.h"

namespace cdsdk {
// Native EDIT owns selection, clipboard, accessibility and IME composition.
class PreviewTextInput {
public:
    PreviewTextInput();
    ~PreviewTextInput();
    CdResult connect(CdTextInputCallback callback, void* user);
    CdResult configure(const CdRect* bounds, const char* placeholder, uint32_t length);
    CdResult setText(const char* bytes, uint32_t length);
    CdResult focus(bool focused);
    void placement(HWND parent, double dpr);
    void pause(bool paused);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
