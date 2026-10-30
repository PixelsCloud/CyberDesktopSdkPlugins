#include "Protocol.h"

#include <QJsonDocument>
#include <QtEndian>

namespace cd::live2d {
QByteArray encode(const QJsonObject& message) {
    auto data = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (data.size() > kMaxPacket) {
        return {};
    }
    QByteArray frame(4, '\0');
    qToLittleEndian<quint32>(quint32(data.size()), frame.data());
    return frame + data;
}

bool consume(QByteArray& buffer, const std::function<void(const QJsonObject&)>& callback) {
    if (buffer.size() > kMaxPacket * 4) {
        return false;
    }
    while (buffer.size() >= 4) {
        const auto size = qFromLittleEndian<quint32>(buffer.constData());
        if (!size || size > kMaxPacket) {
            return false;
        }
        if (buffer.size() < qsizetype(size) + 4) {
            return true;
        }
        QJsonParseError error;
        const auto message = QJsonDocument::fromJson(buffer.mid(4, size), &error);
        if (error.error != QJsonParseError::NoError || !message.isObject()) {
            return false;
        }
        buffer.remove(0, size + 4);
        callback(message.object());
    }
    return true;
}
}  // namespace cd::live2d

namespace cd::live2d {
bool validSurfaceSize(int width, int height) {
    return width > 0 && height > 0 && width <= kMaxSurfaceEdge && height <= kMaxSurfaceEdge &&
           quint64(width) * quint64(height) <= kMaxSurfacePixels;
}
}  // namespace cd::live2d
