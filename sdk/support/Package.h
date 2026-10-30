#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "plugin_package.pb.h"

namespace cdsdk {
namespace pb = cyberdesktop::package::v1;
inline constexpr uint64_t maximumPackageBytes = 256ull * 1024 * 1024;
inline constexpr uint64_t maximumFileBytes = 64ull * 1024 * 1024;
inline constexpr size_t maximumFiles = 4096;
inline constexpr uint32_t abiVersion = 1;
inline constexpr char packageMagic[] = "CyberDesktopPlugin";

struct Error {
    std::string code, message;
};

bool validateDescriptor(const pb::PluginDescriptor&, Error*);
bool readDescriptor(const std::filesystem::path&, pb::PluginDescriptor*, Error*);
bool writeDescriptor(const std::filesystem::path&, const pb::PluginDescriptor&, Error*);
bool readPackage(const std::filesystem::path&, pb::PluginPackage*, Error*);
bool validatePackage(const pb::PluginPackage&, Error*);
bool extractPackage(const pb::PluginPackage&, const std::filesystem::path&, Error*);
bool packDirectory(const std::filesystem::path&, const std::filesystem::path&, Error*);
std::string descriptorJson(const pb::PluginDescriptor&);
std::string sha256(const std::string&);
std::string deterministicDescriptor(const pb::PluginDescriptor&);
std::string errorText(const Error&);
bool safeRelativePath(const std::string&);
std::filesystem::path utf8Path(const std::string&);
std::vector<std::string> binaryImports(const std::filesystem::path&, Error*);
std::vector<std::string> binaryImports(const std::string&, Error*, bool requireEntry);
bool systemDependency(const std::string&);
}  // namespace cdsdk
