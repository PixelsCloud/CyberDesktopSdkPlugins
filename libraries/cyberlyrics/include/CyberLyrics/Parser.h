#pragma once

#include "CyberLyrics/Document.h"

class QByteArray;

namespace cyberlyrics {
Document readFile(const QString& path, const Track& track = {});
Document parseLrc(const QByteArray& utf8, const Track& track = {});
Document parseWords(const QString& text, const Track& track = {});
void mergeTranslation(Document& document, const Document& translation);
}  // namespace cyberlyrics
