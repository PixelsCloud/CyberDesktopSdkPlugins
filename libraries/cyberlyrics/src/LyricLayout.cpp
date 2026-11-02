#include "CyberLyrics/LyricLayout.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>
#include <cmath>

#include "CyberLyrics/Document.h"

namespace cyberlyrics {
struct LyricLayout::Impl {
    struct Segment {
        QRectF bounds;
        double start, end;
        bool rightToLeft;
    };

    QTextLayout layout;
    QVector<Segment> segments;
    QVector<Segment> rows;
    QString text;
    QFont font;
    qreal width = 0;
    QSizeF size;
    bool wrap = false;
    Qt::Alignment alignment = Qt::AlignLeft;
    qreal totalAdvance = 0;
    double start = 0, end = 0;
    QVector<Word> words;
};

LyricLayout::LyricLayout() : m_impl(std::make_unique<Impl>()) {}

LyricLayout::~LyricLayout() = default;

void LyricLayout::configure(const Line& line, const QFont& font, qreal width, bool wrap, Qt::Alignment alignment) {
    auto& self = *m_impl;
    width = std::max(qreal(1), width);
    bool equalWords = self.words.size() == line.words.size();
    for (qsizetype index = 0; equalWords && index < line.words.size(); ++index) {
        const auto& left = self.words[index];
        const auto& right = line.words[index];
        equalWords &= left.text == right.text && left.start == right.start && left.end == right.end;
    }
    if (self.text == line.text && self.font == font && self.width == width && self.wrap == wrap && equalWords &&
        self.start == line.start && self.end == line.end && self.alignment == alignment) {
        return;
    }
    self.text = line.text;
    self.font = font;
    self.width = width;
    self.wrap = wrap;
    self.alignment = alignment;
    self.start = line.start;
    self.end = line.end;
    self.words = line.words;
    self.layout.clearLayout();
    self.layout.setText(line.text);
    self.layout.setFont(font);
    self.layout.setCacheEnabled(true);
    QTextOption option;
    option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setAlignment(alignment);
    self.layout.setTextOption(option);
    self.layout.beginLayout();
    self.rows.clear();
    self.totalAdvance = 0;
    qreal y = 0, widest = 0;
    while (true) {
        auto shaped = self.layout.createLine();
        if (!shaped.isValid()) {
            break;
        }
        shaped.setLineWidth(width);
        shaped.setPosition({0, y});
        y += shaped.height();
        widest = std::max(widest, shaped.naturalTextWidth());
        const auto bounds = shaped.naturalTextRect();
        self.rows.push_back({bounds, 0, 0, line.text.mid(shaped.textStart(), shaped.textLength()).isRightToLeft()});
        self.totalAdvance += bounds.width();
    }
    self.layout.endLayout();
    self.size = {std::min(widest, width), y};
    self.segments.clear();
    int offset = 0;
    for (const auto& word : line.words) {
        const int end = offset + int(word.text.size());
        const auto first = self.segments.size();
        qreal advance = 0;
        for (int index = 0; index < self.layout.lineCount(); ++index) {
            const auto shaped = self.layout.lineAt(index);
            const int from = std::max(offset, shaped.textStart());
            const int to = std::min(end, shaped.textStart() + shaped.textLength());
            if (from >= to) {
                continue;
            }
            const qreal x1 = shaped.cursorToX(from), x2 = shaped.cursorToX(to);
            self.segments.push_back(
                {{std::min(x1, x2), shaped.y(), std::abs(x2 - x1), shaped.height()}, word.start, word.end, x2 < x1});
            advance += std::abs(x2 - x1);
        }
        qreal consumed = 0;
        for (qsizetype index = first; index < self.segments.size() && advance > 0; ++index) {
            auto& segment = self.segments[index];
            segment.start = word.start + (word.end - word.start) * consumed / advance;
            consumed += segment.bounds.width();
            segment.end = word.start + (word.end - word.start) * consumed / advance;
        }
        offset = end;
    }
}

void LyricLayout::paint(QPainter& painter, const QPointF& origin, double position, const QColor& text,
                        const QColor& highlight, bool simulateLineProgress) const {
    const auto& self = *m_impl;
    painter.save();
    painter.translate(origin);
    painter.setClipRect(QRectF(QPointF(), QSizeF(self.width, self.size.height())), Qt::IntersectClip);
    painter.setPen(text);
    self.layout.draw(&painter, {});
    if (text == highlight) {
        painter.restore();
        return;
    }
    painter.setPen(highlight);
    if (!self.words.isEmpty()) {
        for (const auto& segment : self.segments) {
            if (position <= segment.start) {
                continue;
            }
            const double progress =
                segment.end > segment.start
                    ? std::clamp((position - segment.start) / (segment.end - segment.start), 0.0, 1.0)
                    : 1;
            auto clip = segment.bounds;
            clip.setWidth(clip.width() * progress);
            if (segment.rightToLeft) {
                clip.moveRight(segment.bounds.right());
            }
            painter.save();
            painter.setClipRect(clip, Qt::IntersectClip);
            self.layout.draw(&painter, {});
            painter.restore();
        }
    } else if (simulateLineProgress && self.end > self.start) {
        const double progress = std::clamp((position - self.start) / (self.end - self.start), 0.0, 1.0);
        qreal remaining = self.totalAdvance * progress;
        for (const auto& row : self.rows) {
            if (remaining <= 0) {
                break;
            }
            auto clip = row.bounds;
            clip.setWidth(std::min(remaining, clip.width()));
            if (row.rightToLeft) {
                clip.moveRight(row.bounds.right());
            }
            painter.save();
            painter.setClipRect(clip, Qt::IntersectClip);
            self.layout.draw(&painter, {});
            painter.restore();
            remaining -= row.bounds.width();
        }
    }
    painter.restore();
}

QSizeF LyricLayout::size() const {
    return m_impl->size;
}
}  // namespace cyberlyrics
