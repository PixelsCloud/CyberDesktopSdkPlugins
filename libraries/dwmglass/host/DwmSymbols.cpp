// SPDX-License-Identifier: GPL-3.0-only
#include "libraries/dwmglass/host/DwmSymbols.h"

#include <Windows.h>
#include <cpr/cpr.h>
#include <dbghelp.h>
#include <spdlog/spdlog.h>
#include <wil/resource.h>
#include <wil/stl.h>
#include <wil/win32_helpers.h>
#include <winrt/base.h>

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "third_party/OpenGlass/Common/PeCodeViewIdentity.hpp"

namespace cyberglass::dwm::host {
namespace {

bool matches(const std::filesystem::path& pdb, const OpenGlass::PeCodeViewIdentity& image) {
    SYMSRV_INDEX_INFOW index{};
    index.sizeofstruct = sizeof(index);
    return SymSrvGetFileIndexInfoW(pdb.c_str(), &index, 0) && index.age == image.pdbAge &&
           InlineIsEqualGUID(index.guid, image.pdbGuid);
}

void download(const std::filesystem::path& imagePath, const std::filesystem::path& directory) {
    OpenGlass::PeCodeViewIdentity identity;
    winrt::check_hresult(OpenGlass::ReadFilePeCodeViewIdentity(imagePath.c_str(), identity));
    const auto expectedName = imagePath.stem().wstring() + L".pdb";
    if (_wcsicmp(identity.pdbName.c_str(), expectedName.c_str()) != 0) {
        throw std::runtime_error("Unexpected DWM symbol filename");
    }
    const auto destination = directory / identity.pdbName;
    if (matches(destination, identity)) {
        spdlog::info("Exact symbols cached: {}", winrt::to_string(identity.pdbName));
        return;
    }
    const auto temporary = destination.wstring() + L".download";
    const auto cleanup = wil::scope_exit([&] {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    });
    std::ofstream file(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot create DWM symbol cache file");
    }
    const auto url = OpenGlass::BuildSymbolServerUrl(L"https://msdl.microsoft.com/download/symbols/", identity);
    spdlog::info("Downloading exact Microsoft symbols: {}", winrt::to_string(identity.pdbName));
    const auto response = cpr::Download(
        file, cpr::Url{winrt::to_string(url)}, cpr::Timeout{120000}, cpr::ConnectTimeout{10000}, cpr::Redirect{true},
        cpr::ProgressCallback([](cpr::cpr_off_t, cpr::cpr_off_t received, cpr::cpr_off_t, cpr::cpr_off_t, intptr_t) {
            return received <= 256ll * 1024 * 1024;
        }));
    file.close();
    if (response.error.code != cpr::ErrorCode::OK || response.status_code != 200 || !file) {
        throw std::runtime_error("Microsoft symbol download failed: HTTP " + std::to_string(response.status_code) +
                                 ", " + response.error.message);
    }
    if (!matches(temporary, identity)) {
        throw std::runtime_error("Downloaded PDB does not match the DWM image GUID and age");
    }
    winrt::check_bool(
        MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    spdlog::info("Exact symbols ready: {}", winrt::to_string(identity.pdbName));
}
}  // namespace

void prepareSymbols(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const std::filesystem::path systemDirectory(wil::GetSystemDirectoryW<std::wstring>());
    download(systemDirectory / L"udwm.dll", directory);
    download(systemDirectory / L"dwmcore.dll", directory);
}

}  // namespace cyberglass::dwm::host
