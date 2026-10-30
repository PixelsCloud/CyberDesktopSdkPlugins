#pragma once

#include <Windows.h>

#include <cstdint>
#include <memory>

class QByteArray;
class QString;

namespace characterresources {
using NativeFile = std::unique_ptr<void, decltype(&CloseHandle)>;

// Pins each parent and file against rename and rejects reparse points before reading.
class ResourceTree final {
public:
    explicit ResourceTree(QString path);
    ~ResourceTree();
    ResourceTree(const ResourceTree&) = delete;
    ResourceTree& operator=(const ResourceTree&) = delete;
    QString rootPath() const;
    QByteArray read(const QString& relative, uint64_t maximum);
    NativeFile create(const QString& relative);
    void scan(bool video);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace characterresources
