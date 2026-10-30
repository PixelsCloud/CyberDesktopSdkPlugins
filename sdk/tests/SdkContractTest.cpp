#include <google/protobuf/text_format.h>
#include <sdk/Settings.h>

#include <cstdlib>
#include <fstream>
#include <iostream>

#include "support/Package.h"
#include "tools/Runtime.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct CoupledSettings {
    cdsdk::Settings values{{{"automatic", CD_SETTING_TOGGLE, 1}, {"city", CD_SETTING_TEXT, 0, 0, "", 0, 128}}};
    bool mutateRejected = false, acceptWithoutChange = false;

    static CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* value, uint32_t size) {
        return static_cast<CoupledSettings*>(pointer)->values.get(id, value, size);
    }

    static CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) {
        auto& self = *static_cast<CoupledSettings*>(pointer);
        auto next = self.values;
        const auto result = next.apply(values, count);
        if (result != CD_OK) {
            if (self.mutateRejected) {
                CdSettingValue bad{sizeof(CdSettingValue), CD_SETTING_TEXT, "city", 0, 0, "changed-on-rejection"};
                self.values.apply(&bad, 1);
            }
            return result;
        }
        if (!next.at("automatic").integer && next.at("city").text.empty()) {
            return CD_ERROR_ARGUMENT;
        }
        if (!self.acceptWithoutChange) {
            self.values = std::move(next);
        }
        return CD_OK;
    }

    CdInstanceApi api() {
        CdInstanceApi result{};
        result.struct_size = sizeof(result);
        result.instance = this;
        result.get_setting = get;
        result.apply_settings = apply;
        return result;
    }
};

void settingsProbes() {
    cdsdk::pb::SettingsPage page;
    auto* automatic = page.add_fields();
    automatic->set_id("automatic");
    automatic->set_type(cdsdk::pb::TOGGLE);
    auto* city = page.add_fields();
    city->set_id("city");
    city->set_type(cdsdk::pb::TEXT);
    city->set_max_length(128);
    cdsdk::Error error;
    CoupledSettings first, second;
    require(cdsdk::probeSettings(page, first.api(), second.api(), &error) && first.values.at("automatic").integer &&
                first.values.at("city").text == "x" && second.values.at("city").text.empty(),
            "domain-coupled first field is skipped without weakening plugin validation");
    first = {};
    first.mutateRejected = true;
    require(!cdsdk::probeSettings(page, first.api(), second.api(), &error),
            "rejection changing a different field is detected");
    first = {};
    first.acceptWithoutChange = true;
    require(!cdsdk::probeSettings(page, first.api(), second.api(), &error),
            "accepted setting ignored by a plugin is detected");
    first = {};
    require(!cdsdk::probeSettings(page, first.api(), first.api(), &error), "shared instance settings are detected");
    first = {};
    page.mutable_fields()->RemoveLast();
    require(cdsdk::probeSettings(page, first.api(), second.api(), &error),
            "a schema with only a coupled setting can validate current values atomically");
    page.clear_fields();
    require(!cdsdk::probeSettings(page, first.api(), second.api(), &error),
            "empty setting schemas are rejected safely");
}

cdsdk::pb::PluginDescriptor descriptor() {
    cdsdk::pb::PluginDescriptor value;
    value.set_id("com.example.test");
    value.set_name("Test");
    value.set_author("SDK");
    value.set_version("1.0.0");
    value.set_abi_version(1);
    value.set_entry("plugin.dll");
    auto* page = value.mutable_settings_page();
    page->set_title("Test settings");
    auto* setting = page->add_fields();
    setting->set_id("enabled");
    setting->set_label("Enabled");
    setting->set_type(cdsdk::pb::TOGGLE);
    auto* rule = value.add_footprint_rules();
    rule->set_topology(cdsdk::pb::RECT);
    rule->set_min_cells(1);
    rule->set_max_cells(64);
    rule->set_max_columns(16);
    rule->set_max_rows(16);
    auto* layout = value.add_layouts();
    layout->set_id("one");
    layout->set_name("One");
    layout->set_topology(cdsdk::pb::RECT);
    layout->set_is_default(true);
    layout->add_cells();
    layout->add_allowed_rotations(0);
    return value;
}

cdsdk::pb::PluginPackage package(const std::string& bytes) {
    cdsdk::pb::PluginPackage value;
    value.set_magic(cdsdk::packageMagic);
    value.set_format_version(1);
    *value.mutable_plugin() = descriptor();
    value.set_descriptor_sha256(cdsdk::sha256(cdsdk::deterministicDescriptor(value.plugin())));
    auto* file = value.add_files();
    file->set_path("plugin.dll");
    file->set_content(bytes);
    file->set_size(file->content().size());
    file->set_sha256(cdsdk::sha256(file->content()));
    cdsdk::Error error;
    for (const auto& name : cdsdk::binaryImports(bytes, &error, true)) {
        auto* dependency = value.mutable_plugin()->add_dependencies();
        dependency->set_name(name);
        dependency->set_source(cdsdk::pb::Dependency::WINDOWS);
    }
    require(error.code.empty(), "valid native test fixture");
    value.set_descriptor_sha256(cdsdk::sha256(cdsdk::deterministicDescriptor(value.plugin())));
    return value;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    settingsProbes();
    require(argc == 2 || argc == 3, "native fixture directory and optional fixed scratch directory required");
    std::ifstream input(std::filesystem::path(argv[1]) / "plugin.dll", std::ios::binary);
    const std::string native((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    cdsdk::Error error;
    auto original = package(native);
    require(cdsdk::validatePackage(original, &error), "valid descriptor and checksummed wire package");
    const auto abc = cdsdk::sha256("abc");
    require(abc == std::string("\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde"
                               "\x5d\xae\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c"
                               "\xb4\x10\xff\x61\xf2\x00\x15\xad",
                               32),
            "SHA-256 known vector");
    for (const auto* unsafe :
         {"../a.dll", "/a.dll", "a\\b.dll", "C:a.dll", "con", "a/nul.txt", "a//b", "a.", "a/../b", "a\n.txt"})
        require(!cdsdk::safeRelativePath(unsafe), "unsafe or ambiguous Windows path rejected");
    require(cdsdk::safeRelativePath("assets/normal.png"), "nested resource path accepted");
    auto altered = original;
    altered.mutable_files(0)->mutable_content()->at(0) ^= 1;
    require(!cdsdk::validatePackage(altered, &error) && error.code == "package.hash", "corrupted payload rejected");
    altered = original;
    altered.mutable_plugin()->set_name("changed");
    require(!cdsdk::validatePackage(altered, &error) && error.code == "package.descriptor.hash",
            "corrupted descriptor rejected");
    altered = original;
    *altered.add_files() = altered.files(0);
    altered.mutable_files(1)->set_path("PLUGIN.DLL");
    require(!cdsdk::validatePackage(altered, &error) && error.code == "package.duplicate",
            "case-insensitive duplicate rejected");
    altered = original;
    auto* resource = altered.add_files();
    resource->set_path("assets/\xc3\x9c.txt");
    resource->set_content("data");
    resource->set_size(4);
    resource->set_sha256(cdsdk::sha256("data"));
    *altered.add_files() = altered.files(1);
    altered.mutable_files(2)->set_path("assets/\xc3\xbc.txt");
    require(!cdsdk::validatePackage(altered, &error) && error.code == "package.duplicate",
            "Unicode Windows case collision rejected");
    altered = original;
    altered.mutable_files(0)->set_path("../escape.dll");
    require(!cdsdk::validatePackage(altered, &error) && error.code == "package.path",
            "traversal rejected before extraction");
    altered = original;
    altered.mutable_files(0)->set_size(UINT64_MAX);
    require(!cdsdk::validatePackage(altered, &error) && error.code == "package.size",
            "untrusted size overflow rejected");
    altered = original;
    altered.set_format_version(2);
    require(!cdsdk::validatePackage(altered, &error) && error.code == "package.format",
            "unknown package format rejected");
    auto metadata = descriptor();
    metadata.add_tags("机械");
    metadata.add_tags("科技");
    require(cdsdk::validateDescriptor(metadata, &error), "optional UTF-8 plugin tags are valid");
    cdsdk::pb::PluginDescriptor tagRoundtrip;
    require(tagRoundtrip.ParseFromString(metadata.SerializeAsString()) && tagRoundtrip.tags_size() == 2 &&
                tagRoundtrip.tags(0) == "机械",
            "tags survive protobuf roundtrip");
    metadata.add_tags("机械");
    require(!cdsdk::validateDescriptor(metadata, &error) && error.code == "descriptor.tags", "duplicate tags rejected");
    metadata = descriptor();
    metadata.add_tags("");
    require(!cdsdk::validateDescriptor(metadata, &error) && error.code == "descriptor.tags", "empty tags rejected");
    metadata = descriptor();
    metadata.add_tags(std::string(97, 'x'));
    require(!cdsdk::validateDescriptor(metadata, &error) && error.code == "descriptor.tags", "oversized tags rejected");
    metadata = descriptor();
    for (int i = 0; i < 17; ++i) {
        metadata.add_tags(std::to_string(i));
    }
    require(!cdsdk::validateDescriptor(metadata, &error) && error.code == "descriptor.tags", "tag count is bounded");
    metadata = descriptor();
    metadata.clear_settings_page();
    require(!cdsdk::validateDescriptor(metadata, &error) && error.code == "descriptor.settings",
            "settings page is mandatory");
    metadata = descriptor();
    metadata.set_abi_version(5);
    require(!cdsdk::validateDescriptor(metadata, &error) && error.code == "descriptor.abi",
            "old SDK 5 is not compatible");
    metadata = descriptor();
    *metadata.mutable_layouts(0)->add_cells() = metadata.layouts(0).cells(0);
    require(!cdsdk::validateDescriptor(metadata, &error) && error.code == "descriptor.cells",
            "duplicate footprint cell rejected");
    metadata = descriptor();
    metadata.mutable_layouts(0)->set_is_default(false);
    require(!cdsdk::validateDescriptor(metadata, &error) && error.code == "descriptor.default",
            "default layout required");
    metadata = descriptor();
    metadata.mutable_footprint_rules(0)->set_allow_irregular(true);
    metadata.mutable_layouts(0)->add_cells()->set_a(3);
    require(!cdsdk::validateDescriptor(metadata, &error) && error.code == "descriptor.connected",
            "disconnected footprint rejected");
    altered = original;
    auto* dep = altered.mutable_plugin()->add_dependencies();
    dep->set_name("private.dll");
    dep->set_source(cdsdk::pb::Dependency::EMBEDDED);
    altered.set_descriptor_sha256(cdsdk::sha256(cdsdk::deterministicDescriptor(altered.plugin())));
    require(!cdsdk::validatePackage(altered, &error) && error.code == "package.dependency",
            "missing declared private dependency rejected");
    altered = original;
    altered.mutable_plugin()->clear_dependencies();
    altered.set_descriptor_sha256(cdsdk::sha256(cdsdk::deterministicDescriptor(altered.plugin())));
    require(!cdsdk::validatePackage(altered, &error) && error.code == "package.import",
            "crafted package missing actual imports rejected");
    altered = original;
    altered.mutable_files(0)->set_content("not-a-DLL");
    altered.mutable_files(0)->set_size(9);
    altered.mutable_files(0)->set_sha256(cdsdk::sha256(altered.files(0).content()));
    require(!cdsdk::validatePackage(altered, &error) && error.code == "binary.imports",
            "checksummed invalid native DLL rejected before loading");
    altered = original;
    auto* payload = altered.mutable_files(0);
    auto invalid = native;
    const auto entry = invalid.find("cd_plugin_query");
    require(entry != std::string::npos, "fixture entry export exists");
    invalid[entry] = 'x';
    payload->set_content(invalid);
    payload->set_sha256(cdsdk::sha256(invalid));
    require(!cdsdk::validatePackage(altered, &error) && error.code == "binary.imports",
            "missing new ABI export rejected before loading");
    require(!cdsdk::systemDependency("vcruntime140.dll"),
            "VC redistributables are not treated as built-in Windows DLLs");
    require(cdsdk::systemDependency("XMLLITE.dll"), "Windows XmlLite parser is an OS dependency");
    require(cdsdk::systemDependency("ICU.dll"), "Windows Unicode ICU is an OS dependency");
    require(!cdsdk::systemDependency("icuuc78.dll"), "Private ICU builds must ship their own dependency");
    require(!cdsdk::systemDependency("custom-xmllite.dll"), "OS dependency recognition is exact");
    const auto root =
        argc == 3 ? std::filesystem::path(argv[2])
                  : std::filesystem::temp_directory_path() /
                        std::filesystem::path(L"cyberdesktop-sdk-test-" + std::to_wstring(GetCurrentProcessId()));
    require(!std::filesystem::exists(root) || std::filesystem::is_empty(root), "test scratch directory must be empty");
    std::filesystem::create_directory(root);
    auto editable = descriptor();
    editable.set_name("Old compiled name");
    require(cdsdk::writeDescriptor(root / "descriptor.pb", editable, &error), "write compiled descriptor fixture");
    editable.set_name("Current editable name");
    std::string text;
    require(google::protobuf::TextFormat::PrintToString(editable, &text), "format editable descriptor fixture");
    {
        std::ofstream file(root / "plugin.textproto");
        file << text;
    }
    cdsdk::pb::PluginDescriptor inspected;
    require(cdsdk::loadDirectoryDescriptor(root, &inspected, &error) && inspected.name() == editable.name(),
            "preview and lifecycle inspect editable metadata before stale compiled metadata");
    {
        std::ofstream file(root / "plugin.textproto");
        file << "invalid {";
    }
    require(!cdsdk::loadDirectoryDescriptor(root, &inspected, &error),
            "invalid edited metadata cannot silently fall back to an old binary descriptor");
    require(cdsdk::extractPackage(original, root / "extracted", &error), "verified package extracted");
    require(!cdsdk::extractPackage(original, root / "extracted", &error) && error.code == "extract.destination",
            "existing files cannot be overwritten");
    {
        std::ofstream zip(root / "old.cdpkg", std::ios::binary);
        zip << "PK\x03\x04old-zip";
    }
    cdsdk::pb::PluginPackage loaded;
    require(!cdsdk::readPackage(root / "old.cdpkg", &loaded, &error), "old ZIP format is explicitly rejected");
    if (argc == 2) {
        const std::filesystem::path directory(argv[1]);
        require(cdsdk::packDirectory(directory, root / "native.cdpkg", &error), cdsdk::errorText(error).c_str());
        require(cdsdk::readPackage(root / "native.cdpkg", &loaded, &error), "native DLL package round-trip");
        require(cdsdk::extractPackage(loaded, root / "native", &error), "native DLL extraction");
        require(cdsdk::lifecycle(root / "native", &error), cdsdk::errorText(error).c_str());
    }
    // This directory is created uniquely by this test, under the OS temp root.
    std::filesystem::remove_all(root);
    std::cout << "protobuf integrity, footprint rules, new ABI and package "
                 "lifecycle: PASS\n";
    return 0;
}
