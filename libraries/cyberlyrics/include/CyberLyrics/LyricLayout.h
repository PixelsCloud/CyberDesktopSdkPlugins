#pragma once

#include <QPointF>
#include <QSizeF>
#include <Qt>
#include <memory>

class QPainter;
class QColor;
class QFont;

namespace cyberlyrics {
struct Line;

// Text shaping is cached across progress frames. This view helper owns no timer,
// network request or database; the consumer controls visibility and scheduling.
class LyricLayout final {
public:
    LyricLayout();
    ~LyricLayout();
    void configure(const Line& line, const QFont& font, qreal width, bool wrap,
                   Qt::Alignment alignment = Qt::AlignLeft);
    void paint(QPainter& painter, const QPointF& origin, double position, const QColor& text, const QColor& highlight,
               bool simulateLineProgress = true) const;
    QSizeF size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace cyberlyrics
