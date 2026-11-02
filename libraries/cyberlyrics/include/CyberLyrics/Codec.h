#pragma once

#include <QByteArray>

namespace cyberlyrics {
struct Document;
QByteArray encode(const Document& document);
Document decode(const QByteArray& bytes);
}  // namespace cyberlyrics
