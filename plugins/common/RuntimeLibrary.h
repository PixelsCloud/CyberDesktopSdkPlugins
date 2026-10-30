#pragma once

#include <Windows.h>

#include <memory>

namespace cdplugin {
// Public theme runtimes live next to the distribution's plugins/ directory or
// inside a self-contained package. The host executable is an optional fallback.
class RuntimeLibrary {
public:
    RuntimeLibrary(const char* packageDirectory, const wchar_t* filename);
    ~RuntimeLibrary();
    RuntimeLibrary(const RuntimeLibrary&) = delete;
    RuntimeLibrary& operator=(const RuntimeLibrary&) = delete;
    FARPROC symbol(const char* name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdplugin
