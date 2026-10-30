#include "support/Package.h"

#include <Windows.h>
#include <bcrypt.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <queue>
#include <regex>
#include <set>
#include <sstream>

namespace cdsdk {
namespace {
bool fail(Error* error, std::string code, std::string message) {
    if (error)
        *error = {std::move(code), std::move(message)};
    return false;
}

std::string lower(std::string text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!count)
        return text;
    std::wstring wide(size_t(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), wide.data(), count);
    const int mapped =
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wide.data(), count, nullptr, 0, nullptr, nullptr, 0);
    std::wstring folded(size_t(mapped), L'\0');
    if (!mapped || !LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wide.data(), count, folded.data(), mapped,
                                  nullptr, nullptr, 0))
        return text;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, folded.data(), mapped, nullptr, 0, nullptr, nullptr);
    std::string result(size_t(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, folded.data(), mapped, result.data(), bytes, nullptr, nullptr);
    for (auto& c : result)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return result;
}

bool read(const std::filesystem::path& path, uint64_t limit, std::string* bytes, Error* error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > limit)
        return fail(error, "file.read", "Missing file or file exceeds size limit: " + path.filename().string());
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return fail(error, "file.read", "Cannot read input file");
    bytes->resize(size_t(size));
    stream.read(bytes->data(), std::streamsize(size));
    return stream.gcount() == std::streamsize(size) || fail(error, "file.read", "Input changed or read failed");
}

bool write(const std::filesystem::path& path, const std::string& bytes, Error* error) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return fail(error, "file.write", "Cannot create output file");
    stream.write(bytes.data(), std::streamsize(bytes.size()));
    stream.close();
    return bool(stream) || fail(error, "file.write", "Output write failed");
}

template <class Message>
bool parse(const std::string& bytes, Message* result, Error* error) {
    google::protobuf::io::ArrayInputStream input(bytes.data(), int(bytes.size()));
    google::protobuf::io::CodedInputStream coded(&input);
    coded.SetTotalBytesLimit(int(maximumPackageBytes));
    coded.SetRecursionLimit(24);
    if (!result->ParseFromCodedStream(&coded) || !coded.ConsumedEntireMessage())
        return fail(error, "protobuf.parse", "Malformed protobuf message");
    return true;
}

bool validateFiles(const pb::PluginPackage& package, Error* error) {
    if (package.files_size() == 0 || size_t(package.files_size()) > maximumFiles)
        return fail(error, "package.files", "Invalid file count");
    std::set<std::string> paths;
    uint64_t total = 0;
    for (const auto& file : package.files()) {
        const auto path = lower(file.path());
        if (!safeRelativePath(file.path()) || path == "descriptor.pb" || path == "plugin.textproto")
            return fail(error, "package.path", "Unsafe or reserved path: " + file.path());
        if (!paths.insert(path).second)
            return fail(error, "package.duplicate", "Duplicate path: " + file.path());
        if (file.size() != file.content().size() || file.size() > maximumFileBytes ||
            total > maximumPackageBytes - file.size())
            return fail(error, "package.size", "File size or expanded size is invalid");
        total += file.size();
        if (file.sha256().size() != 32 || sha256(file.content()) != file.sha256())
            return fail(error, "package.hash", "File hash mismatch: " + file.path());
    }
    for (const auto& path : paths) {
        size_t pos = 0;
        while ((pos = path.find('/', pos)) != std::string::npos) {
            if (paths.count(path.substr(0, pos)))
                return fail(error, "package.collision", "File/directory path collision");
            ++pos;
        }
    }
    if (!paths.count(lower(package.plugin().entry())))
        return fail(error, "package.entry", "Entry DLL missing");
    if (!package.plugin().icon().empty() && !paths.count(lower(package.plugin().icon())))
        return fail(error, "package.icon", "Declared icon is missing");
    if (!package.plugin().preview().empty() && !paths.count(lower(package.plugin().preview())))
        return fail(error, "package.preview", "Declared preview image is missing");
    for (const auto& dependency : package.plugin().dependencies()) {
        if (!safeRelativePath(dependency.name()) || dependency.name().find('/') != std::string::npos)
            return fail(error, "package.dependency", "Dependencies must have safe DLL basenames");
        if (dependency.source() == pb::Dependency::EMBEDDED && !paths.count(lower(dependency.name())))
            return fail(error, "package.dependency", "Private dependency DLL missing: " + dependency.name());
        if (dependency.source() != pb::Dependency::EMBEDDED && dependency.source() != pb::Dependency::WINDOWS)
            return fail(error, "package.dependency", "Unknown dependency source");
        if (dependency.source() == pb::Dependency::WINDOWS && !systemDependency(dependency.name()))
            return fail(error, "package.dependency", "Non-system DLL marked as Windows dependency");
    }
    std::map<std::string, pb::Dependency::Source> declared;
    for (const auto& dependency : package.plugin().dependencies())
        if (!declared.emplace(lower(dependency.name()), dependency.source()).second)
            return fail(error, "package.dependency", "Duplicate dependency declaration");
    for (const auto& file : package.files()) {
        if (lower(utf8Path(file.path()).extension().string()) != ".dll")
            continue;
        if (systemDependency(utf8Path(file.path()).filename().string()))
            return fail(error, "package.system", "Windows DLLs must not be embedded");
        if (file.path().find('/') != std::string::npos)
            return fail(error, "package.dependency", "Place plugin and private DLLs in the package root");
        Error binaryError;
        const auto imports =
            binaryImports(file.content(), &binaryError, lower(file.path()) == lower(package.plugin().entry()));
        if (!binaryError.code.empty()) {
            if (error)
                *error = binaryError;
            return false;
        }
        for (const auto& name : imports) {
            const auto source = systemDependency(name) ? pb::Dependency::WINDOWS : pb::Dependency::EMBEDDED;
            if (!declared.count(name) || declared.at(name) != source ||
                (source == pb::Dependency::EMBEDDED && !paths.count(name)))
                return fail(error, "package.import", "DLL dependency closure is incomplete: " + name);
        }
    }
    return true;
}

bool noReparseAncestors(const std::filesystem::path& target) {
    auto current = std::filesystem::absolute(target).lexically_normal();
    for (;;) {
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        const auto parent = current.parent_path();
        if (parent == current || parent.empty())
            break;
        current = parent;
    }
    return true;
}
}  // namespace

std::filesystem::path utf8Path(const std::string& value) {
    return std::filesystem::u8path(value);
}

std::string errorText(const Error& error) {
    return error.code + ": " + error.message;
}

bool safeRelativePath(const std::string& value) {
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0) <= 0)
        return false;
    if (value.empty() || value.size() > 1024 || value.front() == '/' || value.back() == '/')
        return false;
    std::stringstream stream(value);
    std::string part;
    static const std::regex reserved("^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:[.].*)?$", std::regex::icase);
    while (std::getline(stream, part, '/')) {
        if (part.empty() || part == "." || part == ".." || part.size() > 240 || part.back() == '.' ||
            part.back() == ' ' || std::regex_match(part, reserved))
            return false;
        for (unsigned char c : part)
            if (c < 32 || c == ':' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                return false;
    }
    return true;
}

std::string sha256(const std::string& bytes) {
    std::string result(32, '\0');
    const auto status =
        BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                   ULONG(bytes.size()), reinterpret_cast<PUCHAR>(result.data()), ULONG(result.size()));
    if (status < 0)
        throw std::runtime_error("SHA-256 failed");
    return result;
}

std::string deterministicDescriptor(const pb::PluginDescriptor& descriptor) {
    std::string bytes;
    google::protobuf::io::StringOutputStream stream(&bytes);
    {
        google::protobuf::io::CodedOutputStream coded(&stream);
        coded.SetSerializationDeterministic(true);
        if (!descriptor.SerializeToCodedStream(&coded))
            throw std::runtime_error("Descriptor serialization failed");
    }
    return bytes;
}

std::string descriptorJson(const pb::PluginDescriptor& descriptor) {
    std::string json;
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    options.always_print_fields_with_no_presence = true;
    if (!google::protobuf::util::MessageToJsonString(descriptor, &json, options).ok())
        throw std::runtime_error("Descriptor JSON conversion failed");
    return json;
}

bool validateDescriptor(const pb::PluginDescriptor& descriptor, Error* error) {
    if (descriptor.has_content_padding() && (!std::isfinite(descriptor.content_padding()) ||
                                             descriptor.content_padding() < 0 || descriptor.content_padding() > 24))
        return fail(error, "descriptor.content_padding", "Content padding must be between 0 and 24 logical pixels");
    if (error)
        *error = {};
    static const std::regex id("^[a-z][a-z0-9_-]*(?:[.][a-z0-9_-]+)+$");
    static const std::regex version("^[0-9]+[.][0-9]+[.][0-9]+(?:[-+][A-Za-z0-9._-]+)?$");
    if (descriptor.id().size() > 128 || !std::regex_match(descriptor.id(), id))
        return fail(error, "descriptor.id", "Use a lowercase reverse-domain plugin id");
    if (descriptor.name().empty() || descriptor.name().size() > 512 || descriptor.author().empty() ||
        descriptor.author().size() > 512 || descriptor.version().size() > 64 ||
        !std::regex_match(descriptor.version(), version))
        return fail(error, "descriptor.metadata", "Name, author and semantic version are required");
    if (descriptor.abi_version() != abiVersion)
        return fail(error, "descriptor.abi", "Only new C ABI 1 plugins are supported");
    if (!safeRelativePath(descriptor.entry()) || descriptor.entry().find('/') != std::string::npos ||
        lower(utf8Path(descriptor.entry()).extension().string()) != ".dll")
        return fail(error, "descriptor.entry", "Entry must be a safe relative DLL path");
    if (descriptor.tags_size() > 16) {
        return fail(error, "descriptor.tags", "At most 16 plugin tags are supported");
    }
    std::set<std::string> tags;
    for (const auto& tag : descriptor.tags()) {
        if (tag.empty() || tag.size() > 96 || tag.front() == ' ' || tag.back() == ' ' ||
            std::any_of(tag.begin(), tag.end(),
                        [](unsigned char c) {
                            return c < 32 || c == 127;
                        }) ||
            !tags.insert(tag).second) {
            return fail(error, "descriptor.tags",
                        "Tags must be unique, nonempty, unpadded labels up to 96 UTF-8 bytes");
        }
    }
    if (!descriptor.icon().empty() && !safeRelativePath(descriptor.icon()))
        return fail(error, "descriptor.icon", "Invalid icon path");
    if (!descriptor.preview().empty() && !safeRelativePath(descriptor.preview()))
        return fail(error, "descriptor.preview", "Invalid preview image path");
    const auto& page = descriptor.settings_page();
    const auto validLocale = [](const std::string& language) {
        return language == "zh-CN" || language == "zh-TW" || language == "en";
    };
    for (const auto& [language, text] : page.localized())
        if (!validLocale(language) || text.title().empty() || text.title().size() > 512 ||
            text.description().size() > 4096)
            return fail(error, "descriptor.locale", "Settings translations support zh-CN, zh-TW and en");
    if (!descriptor.has_settings_page() || page.title().empty() || page.title().size() > 512 ||
        page.description().size() > 4096 || page.fields_size() < 1 || page.fields_size() > 32)
        return fail(error, "descriptor.settings", "Every plugin must declare a settings page with 1..32 fields");
    std::set<std::string> settingIds;
    for (const auto& field : page.fields()) {
        if (field.id().empty() || field.id().size() > 128 ||
            !std::regex_match(field.id(), std::regex("^[a-z][a-z0-9_]*$")) || !settingIds.insert(field.id()).second ||
            field.label().empty() || field.label().size() > 512 || field.description().size() > 4096 ||
            field.section().size() > 512)
            return fail(error, "descriptor.settings", "Invalid or duplicate setting id/label");
        if (field.type() < pb::TOGGLE || field.type() > pb::FONT)
            return fail(error, "descriptor.settings", "Unknown setting type");
        if (field.text_editor() != pb::SettingField::PLAIN &&
            (field.type() != pb::TEXT || (field.text_editor() != pb::SettingField::DIRECTORY &&
                                          field.text_editor() != pb::SettingField::MULTILINE)))
            return fail(error, "descriptor.settings", "Invalid text editor hint");
        if (field.type() == pb::FONT && (field.text_default().empty() || field.text_default().size() > 128))
            return fail(error, "descriptor.settings", "Invalid font family default");
        for (const auto& [language, text] : field.localized())
            if (!validLocale(language) || text.label().empty() || text.label().size() > 512 ||
                text.section().size() > 512 || text.description().size() > 4096)
                return fail(error, "descriptor.locale", "Invalid field translation");
        for (const auto& choice : field.choices())
            for (const auto& [language, label] : choice.localized_labels())
                if (!validLocale(language) || label.empty() || label.size() > 512)
                    return fail(error, "descriptor.locale", "Invalid choice translation");
        if (field.type() == pb::INTEGER || field.type() == pb::NUMBER) {
            if (!std::isfinite(field.minimum()) || !std::isfinite(field.maximum()) || !std::isfinite(field.step()) ||
                field.minimum() > field.maximum() || field.step() <= 0)
                return fail(error, "descriptor.settings", "Invalid numeric setting range");
            const auto value = field.type() == pb::INTEGER ? double(field.integer_default()) : field.number_default();
            if (!std::isfinite(value) || value < field.minimum() || value > field.maximum() ||
                (field.type() == pb::INTEGER &&
                 (field.minimum() < INT32_MIN || field.maximum() > INT32_MAX ||
                  std::floor(field.step()) != field.step() || std::floor(field.minimum()) != field.minimum() ||
                  std::floor(field.maximum()) != field.maximum())))
                return fail(error, "descriptor.settings", "Numeric default/range is invalid");
        }
        if (field.type() == pb::TEXT &&
            (field.max_length() < 1 || field.max_length() > 4096 || field.text_default().size() > field.max_length()))
            return fail(error, "descriptor.settings", "Invalid text length/default");
        if (field.type() == pb::CHOICE) {
            std::set<std::string> choices;
            if (field.choices_size() < 1 || field.choices_size() > 64)
                return fail(error, "descriptor.settings", "Choice needs 1..64 options");
            for (const auto& choice : field.choices())
                if (choice.value().empty() || choice.value().size() > 512 || choice.label().empty() ||
                    choice.label().size() > 512 || !choices.insert(choice.value()).second)
                    return fail(error, "descriptor.settings", "Invalid/duplicate choice");
            if (!choices.count(field.text_default()))
                return fail(error, "descriptor.settings", "Default choice is missing");
        }
    }
    std::map<int, pb::FootprintRule> rules;
    for (const auto& rule : descriptor.footprint_rules()) {
        if ((rule.topology() != pb::RECT && rule.topology() != pb::HEX) ||
            !rules.emplace(int(rule.topology()), rule).second || rule.min_cells() < 1 || rule.max_cells() > 64 ||
            rule.min_cells() > rule.max_cells() || rule.max_columns() < 1 || rule.max_columns() > 16 ||
            rule.max_rows() < 1 || rule.max_rows() > 16)
            return fail(error, "descriptor.rules", "Invalid or duplicate footprint rule (max 64 cells, 16x16 span)");
    }
    if (descriptor.layouts_size() < 1 || descriptor.layouts_size() > 64)
        return fail(error, "descriptor.layouts", "Declare 1 to 64 layouts");
    std::set<std::string> ids;
    std::set<int> topologies;
    std::map<int, int> defaults;
    for (const auto& layout : descriptor.layouts()) {
        if (!ids.insert(layout.id()).second || layout.id().empty() || layout.id().size() > 128 ||
            !rules.count(int(layout.topology())))
            return fail(error, "descriptor.layout", "Unique layout id and matching topology rule required");
        const auto& rule = rules.at(int(layout.topology()));
        if (uint32_t(layout.cells_size()) < rule.min_cells() || uint32_t(layout.cells_size()) > rule.max_cells())
            return fail(error, "descriptor.cells", "Cell count violates layout rules");
        std::set<std::pair<int, int>> cells;
        int minA = 64, minB = 64, maxA = -64, maxB = -64;
        for (const auto& cell : layout.cells()) {
            if (cell.a() < -64 || cell.a() > 64 || cell.b() < -64 || cell.b() > 64 ||
                !cells.emplace(cell.a(), cell.b()).second)
                return fail(error, "descriptor.cells", "Duplicate or out-of-range cell");
            minA = std::min(minA, cell.a());
            maxA = std::max(maxA, cell.a());
            minB = std::min(minB, cell.b());
            maxB = std::max(maxB, cell.b());
        }
        if (maxA - minA + 1 > int(rule.max_columns()) || maxB - minB + 1 > int(rule.max_rows()))
            return fail(error, "descriptor.span", "Layout span exceeds limits");
        if (layout.topology() == pb::RECT && !rule.allow_irregular() &&
            size_t((maxA - minA + 1) * (maxB - minB + 1)) != cells.size())
            return fail(error, "descriptor.rectangle", "Strict rectangular layout must fill its bounding rectangle");
        std::set<std::pair<int, int>> reached;
        std::queue<std::pair<int, int>> queue;
        queue.push(*cells.begin());
        reached.insert(queue.front());
        while (!queue.empty()) {
            auto [a, b] = queue.front();
            queue.pop();
            const std::pair<int, int> neighbors[]{{a + 1, b}, {a - 1, b},     {a, b + 1},
                                                  {a, b - 1}, {a + 1, b - 1}, {a - 1, b + 1}};
            for (int i = 0; i < (layout.topology() == pb::HEX ? 6 : 4); ++i)
                if (cells.count(neighbors[i]) && reached.insert(neighbors[i]).second)
                    queue.push(neighbors[i]);
        }
        if (reached.size() != cells.size())
            return fail(error, "descriptor.connected", "Layout cells must form one connected region");
        for (auto rotation : layout.allowed_rotations())
            if (rotation >= (layout.topology() == pb::HEX ? 6u : 4u))
                return fail(error, "descriptor.rotation", "Invalid rotation for topology");
        topologies.insert(int(layout.topology()));
        if (layout.is_default())
            ++defaults[int(layout.topology())];
    }
    for (auto topology : topologies)
        if (defaults[topology] != 1)
            return fail(error, "descriptor.default", "Each topology needs exactly one default layout");
    for (auto capability : descriptor.capabilities())
        if (!pb::Capability_IsValid(capability) || capability == pb::CAPABILITY_UNSPECIFIED)
            return fail(error, "descriptor.capability", "Unknown service capability");
    if (descriptor.required_services_size() > 32)
        return fail(error, "descriptor.services", "Too many required services");
    std::set<std::string> services;
    for (const auto& service : descriptor.required_services()) {
        if (service.id().empty() || service.id().size() > 64 || service.version() == 0 || service.version() > 65535 ||
            !services.insert(service.id()).second ||
            service.id().find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos)
            return fail(error, "descriptor.services", "Invalid or duplicate required service");
        const std::map<std::string, pb::Capability> known{
            {"input.stats", pb::INPUT_STATS},      {"system.telemetry", pb::SYSTEM_INFO},
            {"media.playback", pb::MEDIA_CONTROL}, {"media.sessions", pb::MEDIA_CONTROL},
            {"audio.spectrum", pb::AUDIO_FFT},     {"audio.pcm", pb::AUDIO_PCM},
            {"audio.ambient", pb::MEDIA_CONTROL},  {"location.current", pb::LOCATION},
            {"weather.forecast", pb::WEATHER},     {"air.quality", pb::AIR_QUALITY},
            {"storage.volume", pb::SYSTEM_INFO},   {"content.feeds", pb::CONTENT},
            {"media.library", pb::MEDIA_LIBRARY},  {"render.image_skins", pb::IMAGE_SKINS}};
        const auto required = known.find(service.id());
        if (required != known.end() && std::find(descriptor.capabilities().begin(), descriptor.capabilities().end(),
                                                 required->second) == descriptor.capabilities().end())
            return fail(error, "descriptor.services", "Required service needs its declared capability");
    }
    return true;
}

bool readDescriptor(const std::filesystem::path& path, pb::PluginDescriptor* descriptor, Error* error) {
    std::string bytes;
    if (!read(path, 1024 * 1024, &bytes, error))
        return false;
    if (path.extension() == ".textproto") {
        if (!google::protobuf::TextFormat::ParseFromString(bytes, descriptor))
            return fail(error, "descriptor.textproto", "Invalid textproto");
    } else if (!parse(bytes, descriptor, error))
        return false;
    return validateDescriptor(*descriptor, error);
}

bool writeDescriptor(const std::filesystem::path& path, const pb::PluginDescriptor& descriptor, Error* error) {
    return validateDescriptor(descriptor, error) && write(path, deterministicDescriptor(descriptor), error);
}

bool validatePackage(const pb::PluginPackage& package, Error* error) {
    if (package.ByteSizeLong() > maximumPackageBytes)
        return fail(error, "package.size", "Encoded package exceeds size limit");
    if (package.magic() != packageMagic || package.format_version() != 1)
        return fail(error, "package.format", "Only new protobuf .cdpkg format 1 is accepted");
    if (!validateDescriptor(package.plugin(), error))
        return false;
    if (package.descriptor_sha256().size() != 32 ||
        package.descriptor_sha256() != sha256(deterministicDescriptor(package.plugin())))
        return fail(error, "package.descriptor.hash", "Descriptor hash mismatch");
    return validateFiles(package, error);
}

bool readPackage(const std::filesystem::path& path, pb::PluginPackage* package, Error* error) {
    std::string bytes;
    return read(path, maximumPackageBytes, &bytes, error) && parse(bytes, package, error) &&
           validatePackage(*package, error);
}

bool extractPackage(const pb::PluginPackage& package, const std::filesystem::path& destination, Error* error) {
    if (!validatePackage(package, error))
        return false;
    if (!noReparseAncestors(destination))
        return fail(error, "extract.link", "Extraction through links/reparse points is forbidden");
    std::error_code ec;
    if (std::filesystem::exists(destination, ec) &&
        (!std::filesystem::is_directory(destination, ec) || !std::filesystem::is_empty(destination, ec)))
        return fail(error, "extract.destination", "Extraction destination must be an empty directory");
    std::filesystem::create_directories(destination, ec);
    if (ec)
        return fail(error, "extract.destination", "Cannot create extraction directory");
    for (const auto& file : package.files()) {
        const auto path = destination / utf8Path(file.path());
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec || !write(path, file.content(), error))
            return false;
    }
    return writeDescriptor(destination / "descriptor.pb", package.plugin(), error);
}

bool packDirectory(const std::filesystem::path& directory, const std::filesystem::path& output, Error* error) {
    Error localError;
    if (!error)
        error = &localError;
    *error = {};
    if (!noReparseAncestors(directory))
        return fail(error, "pack.link", "Package source cannot be a reparse point");
    pb::PluginPackage package;
    const auto binary = directory / "descriptor.pb";
    if (!readDescriptor(
            std::filesystem::exists(directory / "plugin.textproto") ? directory / "plugin.textproto" : binary,
            package.mutable_plugin(), error))
        return false;
    package.set_magic(packageMagic);
    package.set_format_version(1);
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec)) {
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return fail(error, "pack.link", "Links/reparse points cannot be packed");
        if (entry.is_regular_file() && entry.path() != binary && entry.path() != directory / "plugin.textproto")
            files.push_back(entry.path());
    }
    if (ec)
        return fail(error, "pack.directory", "Cannot enumerate plugin directory");
    std::sort(files.begin(), files.end());
    std::set<std::string> names;
    for (const auto& path : files)
        names.insert(lower(path.filename().string()));
    std::map<std::string, pb::Dependency::Source> dependencies;
    for (const auto& existing : package.plugin().dependencies())
        dependencies[lower(existing.name())] = existing.source();
    for (const auto& path : files) {
        const auto relative = std::filesystem::relative(path, directory).generic_u8string();
        auto* embedded = package.add_files();
        embedded->set_path(reinterpret_cast<const char*>(relative.data()), relative.size());
        if (!read(path, maximumFileBytes, embedded->mutable_content(), error))
            return false;
        embedded->set_size(embedded->content().size());
        embedded->set_sha256(sha256(embedded->content()));
        if (lower(path.extension().string()) == ".dll") {
            const auto imports = binaryImports(path, error);
            if (error && !error->code.empty())
                return false;
            for (const auto& name : imports) {
                if (systemDependency(name))
                    dependencies[lower(name)] = pb::Dependency::WINDOWS;
                else if (names.count(lower(name)))
                    dependencies[lower(name)] = pb::Dependency::EMBEDDED;
                else
                    return fail(error, "pack.dependency", "Missing private DLL dependency: " + name);
            }
        }
    }
    package.mutable_plugin()->clear_dependencies();
    for (const auto& [name, source] : dependencies) {
        auto* dependency = package.mutable_plugin()->add_dependencies();
        dependency->set_name(name);
        dependency->set_source(source);
    }
    package.set_descriptor_sha256(sha256(deterministicDescriptor(package.plugin())));
    if (!validatePackage(package, error))
        return false;
    const auto bytes = package.SerializeAsString();
    if (bytes.size() > maximumPackageBytes)
        return fail(error, "pack.size", "Package exceeds 256 MiB");
    const auto temporary = std::filesystem::path(output.wstring() + L".tmp");
    if (!write(temporary, bytes, error))
        return false;
    if (!MoveFileExW(temporary.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, ec);
        return fail(error, "pack.output", "Cannot atomically publish package");
    }
    return true;
}
}  // namespace cdsdk
