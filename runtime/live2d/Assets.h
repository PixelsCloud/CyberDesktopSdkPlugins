#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <stdexcept>

namespace cd::live2d {

struct Failure : std::runtime_error {
    explicit Failure(const QString& message);
};

// Child-process only. No model or image parser is linked into Desktop.
struct Assets {
    QString root;
    QByteArray descriptor;
    QJsonObject settings;
    QHash<QString, QByteArray> files;
    QList<QImage> textures;
    quint64 textureBytes = 0;
};

QJsonObject checkedJson(const QByteArray& bytes);
Assets loadAssets(const QString& directory, const std::function<void(const QString&)>& progress = {});

}  // namespace cd::live2d
