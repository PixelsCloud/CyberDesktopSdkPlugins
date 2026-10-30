#include "runtime/live2d/Assets.h"

#include <Windows.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>
#include <memory>

namespace cd::live2d {
namespace {
constexpr qint64 kMaxFile = 32 * 1024 * 1024;
constexpr quint64 kMaxAssets = 256ull * 1024 * 1024;
// Long, shallow motion segment arrays can exceed 500k values while still
// fitting the separate 8-MiB JSON and isolated-process memory budgets.
constexpr int kMaxJsonNodes = 1000000;

void checkTree(const QJsonValue& value, int depth, int& nodes) {
    if (depth > 32 || ++nodes > kMaxJsonNodes) {
        throw Failure("JSON structure exceeds limits");
    }
    if (value.isDouble() && (!std::isfinite(value.toDouble()) || std::abs(value.toDouble()) > 1e9)) {
        throw Failure("Invalid numeric value");
    }
    if (value.isString() && value.toString().size() > 32768) {
        throw Failure("JSON string exceeds limit");
    }
    if (value.isArray()) {
        for (const auto& child : value.toArray()) {
            checkTree(child, depth + 1, nodes);
        }
    } else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            checkTree(it.value(), depth + 1, nodes);
        }
    }
}

QByteArray readFile(const QString& path, const QString& root) {
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw Failure("Missing or inaccessible resource: " + QFileInfo(path).fileName());
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> file(handle, CloseHandle);
    LARGE_INTEGER size{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileSizeEx(handle, &size) || size.QuadPart <= 0 ||
        size.QuadPart > kMaxFile) {
        throw Failure("Missing, empty or oversized resource: " + QFileInfo(path).fileName());
    }
    wchar_t finalName[32768]{};
    const auto length = GetFinalPathNameByHandleW(handle, finalName, DWORD(std::size(finalName)), FILE_NAME_NORMALIZED);
    QString finalPath = QDir::fromNativeSeparators(QString::fromWCharArray(finalName));
    if (finalPath.startsWith("//?/")) {
        finalPath.remove(0, 4);
    }
    if (!length || length >= std::size(finalName) || !finalPath.startsWith(root + '/', Qt::CaseInsensitive)) {
        throw Failure("Opened resource escapes model directory");
    }
    QByteArray bytes(int(size.QuadPart), Qt::Uninitialized);
    DWORD received = 0;
    if (!ReadFile(handle, bytes.data(), DWORD(bytes.size()), &received, nullptr) || received != bytes.size()) {
        throw Failure("Resource read failed");
    }
    return bytes;
}

void checkPhysics(const QJsonObject& json) {
    const auto settings = json.value("PhysicsSettings").toArray();
    const auto meta = json.value("Meta").toObject();
    if (json.value("Version").toInt() != 3 || !json.value("PhysicsSettings").isArray() || settings.size() > 256 ||
        meta.value("PhysicsSettingCount").toInt(-1) != settings.size()) {
        throw Failure("Invalid physics settings");
    }
    int inputs = 0, outputs = 0, vertices = 0;
    for (const auto& value : settings) {
        const auto setting = value.toObject();
        const auto input = setting.value("Input").toArray();
        const auto output = setting.value("Output").toArray();
        const auto particles = setting.value("Vertices").toArray();
        // Cubism initializes strand[0]; a root-only strand and an empty physics
        // document are valid exporter placeholders, including groups without outputs.
        if (!value.isObject() || !setting.value("Input").isArray() || !setting.value("Output").isArray() ||
            !setting.value("Vertices").isArray() || input.size() > 256 || output.size() > 256 || particles.isEmpty() ||
            particles.size() > 256) {
            throw Failure("Physics structure exceeds limits");
        }
        for (const auto& entries : {input, output}) {
            for (const auto& entry : entries) {
                const auto type = entry.toObject().value("Type").toString();
                if (type != "X" && type != "Y" && type != "Angle") {
                    throw Failure("Invalid physics parameter type");
                }
            }
        }
        for (const auto& entry : output) {
            const auto index = entry.toObject().value("VertexIndex");
            if (!index.isDouble() || index.toDouble() != index.toInt()) {
                throw Failure("Invalid physics vertex index");
            }
            // The pinned SDK skips indices outside [1, ParticleCount) in both
            // Stabilization and Evaluate before accessing particles. Preserve that
            // compatibility instead of rejecting the entire otherwise usable model.
        }
        inputs += int(input.size());
        outputs += int(output.size());
        vertices += int(particles.size());
    }
    if (inputs > 4096 || outputs > 4096 || vertices > 4096 || meta.value("TotalInputCount").toInt(-1) != inputs ||
        meta.value("TotalOutputCount").toInt(-1) != outputs || meta.value("VertexCount").toInt(-1) != vertices) {
        throw Failure("Physics counts do not match bounded data");
    }
}
}  // namespace

QJsonObject checkedJson(const QByteArray& bytes) {
    if (bytes.isEmpty() || bytes.size() > 8 * 1024 * 1024) {
        throw Failure("JSON size exceeds limit");
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        throw Failure("Malformed JSON");
    }
    int nodes = 0;
    checkTree(document.object(), 0, nodes);
    return document.object();
}

Assets loadAssets(const QString& directory, const std::function<void(const QString&)>& progress) {
    Assets result;
    const QFileInfo selection(directory);
    const QString root = selection.isFile() ? selection.absolutePath() : selection.absoluteFilePath();
    result.root = QFileInfo(root).canonicalFilePath();
    if (result.root.isEmpty() || result.root.startsWith("//") || result.root.startsWith("\\\\")) {
        throw Failure("Select a local model directory");
    }
    const auto names = QDir(result.root).entryList({"*.model3.json"}, QDir::Files, QDir::Name);
    QString entry = selection.isFile() ? selection.fileName() : (names.size() == 1 ? names.front() : QString());
    if (entry.isEmpty() || !entry.endsWith(".model3.json", Qt::CaseInsensitive)) {
        throw Failure("Select a directory containing exactly one model3.json");
    }
    quint64 bytes = 0;
    auto read = [&](const QString& relative) -> QByteArray {
        if (relative.isEmpty() || relative.size() > 1024 || QDir::isAbsolutePath(relative) || relative.contains(':') ||
            relative.contains('\\') || relative.split('/').contains("..")) {
            throw Failure("Invalid resource path");
        }
        QString current = result.root;
        for (const auto& part : relative.split('/')) {
            current = QDir(current).filePath(part);
            const DWORD flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(current.utf16()));
            if (flags == INVALID_FILE_ATTRIBUTES || flags & FILE_ATTRIBUTE_REPARSE_POINT) {
                throw Failure("Missing resource or reparse point: " + relative);
            }
        }
        const QString canonical = QFileInfo(current).canonicalFilePath();
        if (!canonical.startsWith(result.root + '/', Qt::CaseInsensitive)) {
            throw Failure("Resource escapes model directory");
        }
        if (result.files.contains(relative)) {
            return result.files.value(relative);
        }
        if (result.files.size() >= 256) {
            throw Failure("Too many model resources");
        }
        const auto data = readFile(canonical, result.root);
        bytes += quint64(data.size());
        if (bytes > kMaxAssets) {
            throw Failure("Model resources exceed budget");
        }
        if (relative.endsWith(".json", Qt::CaseInsensitive)) {
            checkedJson(data);
        }
        result.files.insert(relative, data);
        if (progress) {
            progress(relative);
        }
        return data;
    };
    result.descriptor = read(entry);
    result.settings = checkedJson(result.descriptor);
    const auto references = result.settings.value("FileReferences").toObject();
    if (result.settings.value("Version").toInt() != 3 || references.isEmpty()) {
        throw Failure("Unsupported model descriptor version");
    }
    const auto moc = read(references.value("Moc").toString());
    if (moc.size() < 64 || moc.left(4) != "MOC3") {
        throw Failure("Invalid MOC3 header");
    }
    for (const auto& key : {"Physics", "Pose", "UserData", "DisplayInfo"}) {
        const auto reference = references.value(key);
        // Exporters also represent absent optional resources as JSON null or an
        // empty string. Match Cubism's optional-resource semantics.
        if (reference.isUndefined() || reference.isNull() || (reference.isString() && reference.toString().isEmpty())) {
            continue;
        }
        const auto json = checkedJson(read(reference.toString()));
        if (strcmp(key, "Physics") == 0) {
            checkPhysics(json);
        }
    }
    if (references.contains("Expressions") && !references.value("Expressions").isArray()) {
        throw Failure("Invalid expression list");
    }
    if (references.value("Expressions").toArray().size() > 128) {
        throw Failure("Too many model expressions");
    }
    QSet<QString> expressionNames;
    for (const auto& expression : references.value("Expressions").toArray()) {
        const auto name = expression.toObject().value("Name").toString();
        if (name.isEmpty() || name.toUtf8().size() > 128 || expressionNames.contains(name)) {
            throw Failure("Invalid or duplicate expression name");
        }
        expressionNames.insert(name);
        read(expression.toObject().value("File").toString());
    }
    if (references.contains("Motions") && !references.value("Motions").isObject()) {
        throw Failure("Invalid motion list");
    }
    const auto motions = references.value("Motions").toObject();
    int motionCount = 0;
    for (auto it = motions.begin(); it != motions.end(); ++it) {
        if (it.key().isEmpty() || it.key().toUtf8().size() > 128) {
            throw Failure("Motion group name exceeds limit");
        }
        if (!it.value().isArray()) {
            throw Failure("Invalid motion group");
        }
        for (const auto& motion : it.value().toArray()) {
            if (++motionCount > 256) {
                throw Failure("Too many model motions");
            }
            read(motion.toObject().value("File").toString());
        }
    }
    const auto textures = references.value("Textures").toArray();
    if (textures.isEmpty() || textures.size() > 16) {
        throw Failure("Invalid texture count");
    }
    QImageReader::setAllocationLimit(256);
    for (const auto& texture : textures) {
        auto data = read(texture.toString());
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "png");
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 4096 || size.height() > 4096) {
            throw Failure("Invalid texture dimensions");
        }
        result.textureBytes += quint64(size.width()) * quint64(size.height()) * 4;
        if (result.textureBytes > kMaxAssets) {
            throw Failure("Decoded textures exceed budget");
        }
        auto image = reader.read();
        if (image.isNull() || image.size() != size) {
            throw Failure("Damaged PNG texture");
        }
        result.textures.append(image.convertToFormat(QImage::Format_RGBA8888_Premultiplied));
    }
    return result;
}
}  // namespace cd::live2d

namespace cd::live2d {
Failure::Failure(const QString& message) : std::runtime_error(message.toUtf8().constData()) {}
}  // namespace cd::live2d
