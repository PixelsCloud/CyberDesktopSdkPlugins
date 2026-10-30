#include "CharacterResources/ResourceTree.h"

#include <CharacterChat/Protocol.h>

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>
#include <set>
#include <stdexcept>
#include <vector>

namespace characterresources {
namespace {
void require(bool condition) {
    if (!condition)
        throw std::runtime_error("Resource path, size or ownership validation failed");
}
}  // namespace

using characterchat::safeRelativeResource;
using Handle = NativeFile;

struct ResourceTree::Impl {
public:
    explicit Impl(QString path) {
        root = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
        require(root.size() <= 30000 && !root.startsWith("//") && !root.startsWith("\\\\") &&
                QFileInfo(root).canonicalFilePath().compare(root, Qt::CaseInsensitive) == 0);
        // Keep each ancestor open without FILE_SHARE_DELETE until decoding ends.
        // This pins parent names as well as the actual resource file against rename.
        QString current = root;
        std::vector<QString> parents;
        while (current.size() > 3) {
            parents.push_back(current);
            const auto parent = QFileInfo(current).dir().absolutePath();
            if (parent == current) {
                break;
            }
            current = parent;
        }
        for (auto it = parents.rbegin(); it != parents.rend(); ++it) {
            pinDirectory(*it);
        }
    }

    QByteArray read(const QString& relative, uint64_t maximum) {
        require(safeRelativeResource(relative.toUtf8().toStdString()));
        const auto path = QDir(root).filePath(relative);
        QString current = root;
        const auto components = relative.split('/');
        require(components.size() <= 16 && !relative.contains('\\'));
        for (qsizetype i = 0; i + 1 < components.size(); ++i) {
            current = QDir(current).filePath(components[i]);
            pinDirectory(current);
        }
        const auto raw = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        require(raw != INVALID_HANDLE_VALUE);
        Handle handle(raw, CloseHandle);
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        BY_HANDLE_FILE_INFORMATION identity{};
        LARGE_INTEGER size{};
        require(GetFileType(raw) == FILE_TYPE_DISK &&
                GetFileInformationByHandleEx(raw, FileAttributeTagInfo, &attributes, sizeof(attributes)) &&
                !(attributes.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                GetFileInformationByHandle(raw, &identity) && identity.nNumberOfLinks == 1 &&
                GetFileSizeEx(raw, &size) && size.QuadPart > 0 && uint64_t(size.QuadPart) <= maximum);
        wchar_t resolved[32768]{};
        const auto length = GetFinalPathNameByHandleW(raw, resolved, DWORD(std::size(resolved)), FILE_NAME_NORMALIZED);
        require(length && length < std::size(resolved));
        auto real = QDir::fromNativeSeparators(QString::fromWCharArray(resolved, int(length)));
        if (real.startsWith("//?/")) {
            real.remove(0, 4);
        }
        require(real.startsWith(root + '/', Qt::CaseInsensitive));
        QByteArray bytes(qsizetype(size.QuadPart), Qt::Uninitialized);
        DWORD received = 0;
        require(ReadFile(raw, bytes.data(), DWORD(bytes.size()), &received, nullptr) && received == bytes.size());
        files_.push_back(std::move(handle));
        return bytes;
    }

    void scan(bool video) {
        QDirIterator entries(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                             QDirIterator::Subdirectories);
        std::set<QString> paths;
        uint64_t total = 0;
        unsigned nodes = 0, files = 0;
        while (entries.hasNext()) {
            const auto path = entries.next();
            require(++nodes <= 1024);
            const auto relative = QDir(root).relativeFilePath(path);
            require(relative.split('/').size() <= 16 && paths.insert(relative.toCaseFolded()).second);
            const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
            require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT));
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
                pinDirectory(path);
                continue;
            }
            require(++files <= 256);
            const auto bytes = QFileInfo(path).size();
            require(bytes >= 0 && bytes <= 128ll * 1024 * 1024);
            total += uint64_t(bytes);
            require(total <= uint64_t(video ? 1024 : 256) * 1024 * 1024);
        }
    }

    Handle create(const QString& relative) {
        require(safeRelativeResource(relative.toUtf8().toStdString()) && !relative.contains('\\'));
        const auto components = relative.split('/');
        require(components.size() <= 16);
        QString current = root;
        for (qsizetype i = 0; i + 1 < components.size(); ++i) {
            current = QDir(current).filePath(components[i]);
            require(CreateDirectoryW(reinterpret_cast<LPCWSTR>(current.utf16()), nullptr) ||
                    GetLastError() == ERROR_ALREADY_EXISTS);
            pinDirectory(current);
        }
        const auto path = QDir(root).filePath(relative);
        const auto raw = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        require(raw != INVALID_HANDLE_VALUE);
        return Handle(raw, CloseHandle);
    }

    QString root;

private:
    void pinDirectory(const QString& path) {
        if (directories_.contains(path)) {
            return;
        }
        require(directories_.size() < 1024);
        const auto raw = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        require(raw != INVALID_HANDLE_VALUE);
        Handle handle(raw, CloseHandle);
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        require(GetFileInformationByHandleEx(raw, FileAttributeTagInfo, &attributes, sizeof(attributes)) &&
                (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT));
        directories_.insert(path);
        files_.push_back(std::move(handle));
    }

    std::set<QString> directories_;
    std::vector<Handle> files_;
};

ResourceTree::ResourceTree(QString path) : impl_(std::make_unique<Impl>(std::move(path))) {}

ResourceTree::~ResourceTree() = default;

QString ResourceTree::rootPath() const {
    return impl_->root;
}

QByteArray ResourceTree::read(const QString& path, uint64_t maximum) {
    return impl_->read(path, maximum);
}

NativeFile ResourceTree::create(const QString& path) {
    return impl_->create(path);
}

void ResourceTree::scan(bool video) {
    impl_->scan(video);
}
}  // namespace characterresources
