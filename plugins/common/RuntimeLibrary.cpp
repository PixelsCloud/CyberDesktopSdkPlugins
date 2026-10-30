#include "plugins/common/RuntimeLibrary.h"

#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

namespace cdplugin {
namespace {
struct ModuleDeleter {
    void operator()(HMODULE module) const noexcept {
        FreeLibrary(module);
    }
};

std::filesystem::path packagePath(const char* utf8) {
    if (!utf8) {
        return {};
    }
    const auto length = strnlen_s(utf8, 65537);
    if (!length || length > 65536) {
        return {};
    }
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, int(length), nullptr, 0);
    if (!count) {
        return {};
    }
    std::wstring wide(size_t(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, int(length), wide.data(), count);
    const std::filesystem::path path(wide);
    return path.is_absolute() ? path.lexically_normal() : std::filesystem::path{};
}
}  // namespace

struct RuntimeLibrary::Impl {
    std::unique_ptr<std::remove_pointer_t<HMODULE>, ModuleDeleter> module;
};

RuntimeLibrary::RuntimeLibrary(const char* packageDirectory, const wchar_t* filename)
    : impl_(std::make_unique<Impl>()) {
    if (!filename || std::filesystem::path(filename).filename() != filename) {
        return;
    }
    std::vector<std::filesystem::path> locations;
    const auto package = packagePath(packageDirectory);
    if (!package.empty()) {
        locations.push_back(package);
        if (package.parent_path().filename() == L"plugins") {
            locations.push_back(package.parent_path().parent_path());
        }
    }
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, uint32_t(std::size(executable)));
    if (length && length < std::size(executable)) {
        const auto directory = std::filesystem::path(executable).parent_path();
        locations.push_back(directory);
        if (directory.filename() == L"bin" && directory.parent_path().filename() == L"sdk") {
            locations.push_back(directory.parent_path().parent_path());
        }
    }
    for (const auto& directory : locations) {
        const auto file = directory / filename;
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error) || error) {
            continue;
        }
        impl_->module.reset(
            LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
        // An existing but incompatible runtime is an error, not a reason to load
        // some other installation's implementation with the same basename.
        return;
    }
}

RuntimeLibrary::~RuntimeLibrary() = default;

FARPROC RuntimeLibrary::symbol(const char* name) const {
    return impl_->module && name ? GetProcAddress(impl_->module.get(), name) : nullptr;
}
}  // namespace cdplugin
