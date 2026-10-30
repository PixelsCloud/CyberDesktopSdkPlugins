#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>

#include "support/Package.h"

namespace cdsdk {
namespace {
struct Pe {
    std::vector<uint8_t> bytes;

    struct Section {
        uint32_t virtualAddress, virtualSize, rawOffset, rawSize;
    };

    std::vector<Section> sections;
    uint64_t directories = 0;
    uint32_t directoryCount = 0;

    bool range(uint64_t offset, uint64_t size) const {
        return offset <= bytes.size() && size <= bytes.size() - offset;
    }

    uint16_t u16(uint64_t offset) const {
        if (!range(offset, 2))
            throw std::runtime_error("PE offset outside file");
        return uint16_t(bytes[size_t(offset)]) | uint16_t(bytes[size_t(offset + 1)]) << 8;
    }

    uint32_t u32(uint64_t offset) const {
        return uint32_t(u16(offset)) | uint32_t(u16(offset + 2)) << 16;
    }

    uint64_t rva(uint32_t value, uint32_t size) const {
        for (const auto& section : sections) {
            if (value >= section.virtualAddress && uint64_t(value) - section.virtualAddress < section.rawSize) {
                const uint64_t delta = uint64_t(value) - section.virtualAddress;
                const uint64_t offset = section.rawOffset + delta;
                if (size <= section.rawSize - delta && range(offset, size))
                    return offset;
            }
        }
        throw std::runtime_error("Invalid PE RVA");
    }

    std::string name(uint32_t value) const {
        const auto offset = rva(value, 1);
        std::string text;
        for (uint64_t i = offset; i < bytes.size() && i < offset + 256; ++i) {
            const auto c = bytes[size_t(i)];
            if (!c) {
                if (text.empty() || !safeRelativePath(text) || text.find('/') != std::string::npos)
                    throw std::runtime_error("Unsafe import DLL name");
                return text;
            }
            if (c < 32 || c > 126)
                throw std::runtime_error("Invalid import name");
            text.push_back(char(c));
        }
        throw std::runtime_error("Unterminated import name");
    }
};
}  // namespace

std::vector<std::string> binaryImports(const std::string& bytes, Error* error, bool requireEntry) {
    if (error)
        *error = {};
    try {
        Pe pe;
        if (bytes.size() < 64 || bytes.size() > maximumFileBytes)
            throw std::runtime_error("Invalid DLL size");
        pe.bytes.assign(bytes.begin(), bytes.end());
        if (pe.u16(0) != 0x5a4d)
            throw std::runtime_error("Not a Windows PE DLL");
        const uint32_t nt = pe.u32(60);
        if (pe.u32(nt) != 0x00004550 || pe.u16(nt + 4) != 0x8664 || !(pe.u16(nt + 22) & 0x2000))
            throw std::runtime_error("An x64 DLL is required");
        const uint16_t count = pe.u16(nt + 6), optionalSize = pe.u16(nt + 20);
        const uint64_t optional = uint64_t(nt) + 24;
        if (count > 96 || optionalSize < 120 || pe.u16(optional) != 0x20b || !pe.range(optional, optionalSize))
            throw std::runtime_error("Invalid PE optional header");
        pe.directoryCount = std::min(16u, pe.u32(optional + 108));
        pe.directories = optional + 112;
        if (pe.directoryCount * 8 > optionalSize - 112)
            throw std::runtime_error("Invalid PE data directories");
        const uint64_t table = optional + optionalSize;
        for (uint16_t i = 0; i < count; ++i) {
            const uint64_t section = table + uint64_t(i) * 40;
            if (!pe.range(section, 40))
                throw std::runtime_error("Truncated section table");
            pe.sections.push_back(
                {pe.u32(section + 12), pe.u32(section + 8), pe.u32(section + 20), pe.u32(section + 16)});
        }
        if (requireEntry) {
            if (!pe.directoryCount)
                throw std::runtime_error("Missing cd_plugin_query export");
            const auto exportRva = pe.u32(pe.directories), exportSize = pe.u32(pe.directories + 4);
            if (!exportRva || exportSize < 40)
                throw std::runtime_error("Missing export directory");
            const auto exports = pe.rva(exportRva, 40);
            const auto count = pe.u32(exports + 24), functionCount = pe.u32(exports + 20);
            if (count > 65536 || functionCount > 65536)
                throw std::runtime_error("Invalid export count");
            const auto namesAt = pe.rva(pe.u32(exports + 32), count * 4);
            const auto ordinals = pe.rva(pe.u32(exports + 36), count * 2);
            const auto functions = pe.rva(pe.u32(exports + 28), functionCount * 4);
            bool found = false;
            for (uint32_t i = 0; i < count; ++i) {
                if (pe.name(pe.u32(namesAt + i * 4)) != "cd_plugin_query")
                    continue;
                const auto ordinal = pe.u16(ordinals + i * 2);
                if (ordinal >= functionCount)
                    throw std::runtime_error("Invalid entry ordinal");
                const auto code = pe.u32(functions + ordinal * 4);
                if (code >= exportRva && uint64_t(code) < uint64_t(exportRva) + exportSize)
                    throw std::runtime_error("Forwarded entry exports are unsupported");
                pe.rva(code, 1);
                found = true;
                break;
            }
            if (!found)
                throw std::runtime_error(
                    "Missing cd_plugin_query export; old plugin interfaces are "
                    "unsupported");
        }
        std::set<std::string> names;
        for (uint32_t directory : {1u, 13u}) {
            if (directory >= pe.directoryCount)
                continue;
            const uint32_t address = pe.u32(pe.directories + directory * 8),
                           size = pe.u32(pe.directories + directory * 8 + 4);
            if (!address && !size)
                continue;
            const uint32_t step = directory == 1 ? 20 : 32;
            const auto offset = pe.rva(address, size);
            bool terminated = false;
            for (uint32_t i = 0; uint64_t(i) + step <= size; i += step) {
                const auto item = offset + i;
                bool zero = true;
                for (uint32_t j = 0; j < step; j += 4)
                    zero &= pe.u32(item + j) == 0;
                if (zero) {
                    terminated = true;
                    break;
                }
                if (directory == 13 && !(pe.u32(item) & 1))
                    throw std::runtime_error("Unsupported absolute delay import");
                const auto nameRva = pe.u32(item + (directory == 1 ? 12 : 4));
                auto text = pe.name(nameRva);
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                    return char(std::tolower(c));
                });
                names.insert(std::move(text));
                if (names.size() > 512)
                    throw std::runtime_error("Too many import dependencies");
            }
            if (!terminated)
                throw std::runtime_error("Unterminated import directory");
        }
        return {names.begin(), names.end()};
    } catch (const std::exception& failure) {
        if (error)
            *error = {"binary.imports", failure.what()};
        return {};
    }
}

std::vector<std::string> binaryImports(const std::filesystem::path& path, Error* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 64 || uint64_t(input.tellg()) > maximumFileBytes) {
        if (error)
            *error = {"binary.imports", "Invalid DLL size"};
        return {};
    }
    std::string bytes(size_t(input.tellg()), '\0');
    input.seekg(0);
    input.read(bytes.data(), std::streamsize(bytes.size()));
    if (!input) {
        if (error)
            *error = {"binary.imports", "DLL read failed"};
        return {};
    }
    return binaryImports(bytes, error, false);
}

bool systemDependency(const std::string& name) {
    std::string value(name);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    if (value.starts_with("api-ms-win-") || value.starts_with("ext-ms-win-"))
        return true;
    // Exact known OS/runtime names; arbitrary files in System32 are not trusted.
    static const std::set<std::string> names{
        "kernel32.dll",  "kernelbase.dll", "ntdll.dll",          "user32.dll",   "gdi32.dll",
        "gdi32full.dll", "advapi32.dll",   "shell32.dll",        "ole32.dll",    "oleaut32.dll",
        "comdlg32.dll",  "comctl32.dll",   "ws2_32.dll",         "winmm.dll",    "imm32.dll",
        "version.dll",   "uxtheme.dll",    "dwmapi.dll",         "bcrypt.dll",   "bcryptprimitives.dll",
        "crypt32.dll",   "shlwapi.dll",    "rpcrt4.dll",         "setupapi.dll", "dbghelp.dll",
        "d3d11.dll",     "dxgi.dll",       "d3dcompiler_47.dll", "d2d1.dll",     "dwrite.dll",
        "winspool.drv",  "psapi.dll",      "iphlpapi.dll",       "powrprof.dll", "hid.dll",
        "msvcrt.dll",    "ucrtbase.dll",   "winhttp.dll",        "wininet.dll",  "dnsapi.dll",
        "normaliz.dll",  "secur32.dll",    "netapi32.dll",       "wtsapi32.dll", "propsys.dll",
        "avrt.dll",      "wintrust.dll",   "msimg32.dll",        "opengl32.dll", "xmllite.dll",
        "icu.dll"};
    return names.count(value) != 0;
}
}  // namespace cdsdk
