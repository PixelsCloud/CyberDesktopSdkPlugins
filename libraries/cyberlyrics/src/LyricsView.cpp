#include "CyberLyrics/LyricsView.h"

#include <QAbstractItemView>
#include <QColor>
#include <QEasingCurve>
#include <QFont>
#include <QFontMetricsF>
#include <QFrame>
#include <QHideEvent>
#include <QPainter>
#include <QPoint>
#include <QPointer>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <utility>

#include "CyberLyrics/Document.h"
#include "CyberLyrics/LyricLayout.h"
#include "CyberLyrics/ViewAnimation.h"

namespace cyberlyrics {
namespace {
int64_t monotonicNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool sameOriginal(const Document& previous, const Document& next) {
    if (previous.lines.isEmpty() || previous.track.source != next.track.source ||
        previous.track.title != next.track.title || previous.track.artist != next.track.artist ||
        previous.track.album != next.track.album || previous.track.version != next.track.version ||
        previous.track.duration != next.track.duration || previous.lines.size() != next.lines.size()) {
        return false;
    }
    for (qsizetype row = 0; row < previous.lines.size(); ++row) {
        const auto& left = previous.lines[row];
        const auto& right = next.lines[row];
        if (left.start != right.start || left.end != right.end || left.text != right.text ||
            left.words.size() != right.words.size()) {
            return false;
        }
        for (qsizetype word = 0; word < left.words.size(); ++word) {
            if (left.words[word].text != right.words[word].text || left.words[word].start != right.words[word].start ||
                left.words[word].end != right.words[word].end) {
                return false;
            }
        }
    }
    return true;
}

struct RowLayout {
    cyberlyrics::LyricLayout original, emphasized, translation;
    bool translated = false;
    qreal translationHeight = 0;
};

struct RowFade {
    int row = -1;
    qreal start = 0, value = 0;
};

class LyricDelegate final : public QStyledItemDelegate {
public:
    explicit LyricDelegate(LyricsView* view) : QStyledItemDelegate(view), m_view(view) {}

    void clearLayouts() {
        m_layouts.clear();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const int width = std::max(40, m_view->viewport()->width() - 40);
        auto& layout = row(item, index);
        const auto height =
            std::max(layout.original.size().height(), layout.emphasized.size().height()) + layout.translationHeight;
        return {width + 40, std::max(56, int(std::ceil(height)) + 24)};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const qreal emphasis = m_view->lineEmphasis(index.row());
        painter->save();
        painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        const auto foreground = item.palette.color(QPalette::Mid);
        const QRect bounds = item.rect.adjusted(20, 0, -20, 0);
        auto& layout = row(item, index);
        const auto originalHeight = std::max(layout.original.size().height(), layout.emphasized.size().height());
        const auto height = originalHeight + layout.translationHeight;
        const auto rise = (index.row() < m_view->activeLine() ? -1 : 1) * kLineTransitionRise * (1 - emphasis);
        const QPointF origin(bounds.left(), bounds.top() + (bounds.height() - height) / 2 + rise);
        const qreal opacity = painter->opacity();
        painter->setOpacity(opacity * (1 - emphasis));
        layout.original.paint(*painter, origin, m_view->lyricTime(), foreground, foreground, false);
        painter->setOpacity(opacity * emphasis);
        layout.emphasized.paint(*painter, origin, m_view->lyricTime(), item.palette.color(QPalette::Text),
                                item.palette.color(QPalette::Highlight), true);
        if (layout.translated) {
            const auto color = item.palette.color(QPalette::Mid);
            painter->setOpacity(opacity * m_view->translationOpacity(index.row()));
            layout.translation.paint(*painter, origin + QPointF(0, originalHeight + 6), 0, color, color, false);
        }
        painter->restore();
    }

private:
    RowLayout& row(const QStyleOptionViewItem& option, const QModelIndex& index) const {
        auto found = m_layouts.find(index.row());
        if (found == m_layouts.end()) {
            if (m_layouts.size() >= 64) {
                m_layouts.erase(m_layouts.begin());
            }
            found = m_layouts.emplace(index.row(), std::make_unique<RowLayout>()).first;
        }
        auto& layout = *found->second;
        const auto line = m_view->lyricLine(index.row());
        const int width = std::max(40, m_view->viewport()->width() - 40);
        auto regularFont = option.font;
        regularFont.setBold(false);
        layout.original.configure(line, regularFont, width, true, Qt::AlignHCenter);
        auto emphasizedFont = regularFont;
        emphasizedFont.setBold(true);
        layout.emphasized.configure(line, emphasizedFont, width, true, Qt::AlignHCenter);
        layout.translated = m_view->translationsVisible() && !line.translation.isEmpty();
        auto translatedFont = regularFont;
        translatedFont.setBold(false);
        translatedFont.setPixelSize(14);
        layout.translationHeight = m_view->translationsVisible() ? QFontMetricsF(translatedFont).height() + 6 : 0;
        if (layout.translated) {
            const cyberlyrics::Line translated{line.start, line.end, line.translation, {}, {}};
            layout.translation.configure(translated, translatedFont, width, true, Qt::AlignHCenter);
            layout.translationHeight = std::max(layout.translationHeight, layout.translation.size().height() + 6);
        }
        return layout;
    }

    QPointer<LyricsView> m_view;
    mutable std::map<int, std::unique_ptr<RowLayout>> m_layouts;
};
}  // namespace

class LyricsView::Impl {
public:
    explicit Impl(LyricsView* owner, std::function<bool()> policy) : view(owner), animationPolicy(std::move(policy)) {
        ticker.setInterval(33);
        ticker.setTimerType(Qt::PreciseTimer);
        QObject::connect(&ticker, &QTimer::timeout, owner, [this] {
            update();
        });
        scroll.setTargetObject(owner->verticalScrollBar());
        scroll.setPropertyName("value");
        scroll.setDuration(kLineTransitionMilliseconds);
        scroll.setEasingCurve(QEasingCurve::OutCubic);
        const auto initializeFade = [this, owner](QVariantAnimation& fade, std::array<RowFade, 64>& rows, bool line) {
            fade.setStartValue(0.0);
            fade.setEndValue(1.0);
            fade.setEasingCurve(QEasingCurve::OutCubic);
            QObject::connect(&fade, &QVariantAnimation::valueChanged, owner,
                             [this, &rows, line](const QVariant& value) {
                                 for (auto& row : rows) {
                                     if (row.row >= 0) {
                                         const qreal target = !line || row.row == active ? 1 : 0;
                                         row.value = row.start + (target - row.start) * value.toReal();
                                         view->viewport()->update(view->visualItemRect(view->item(row.row)));
                                     }
                                 }
                             });
            QObject::connect(&fade, &QVariantAnimation::finished, owner, [this, &rows] {
                clearRows(rows);
            });
        };
        initializeFade(lineFade, emphasized, true);
        initializeFade(translationFade, translated, false);
        QObject::connect(owner->verticalScrollBar(), &QScrollBar::sliderPressed, owner, [owner] {
            owner->setFollowing(false);
        });
    }

    ~Impl() {
        ticker.stop();
        scroll.stop();
        lineFade.stop();
        translationFade.stop();
    }

    bool animations() const {
        return animationEnabled && (animationPolicy ? animationPolicy() : systemViewAnimationsEnabled());
    }

    void clearRows(std::array<RowFade, 64>& rows) {
        for (auto& row : rows) {
            if (row.row >= 0) {
                view->viewport()->update(view->visualItemRect(view->item(row.row)));
            }
            row = {};
        }
    }

    void finishFades() {
        lineFade.stop();
        translationFade.stop();
        clearRows(emphasized);
        clearRows(translated);
    }

    void emphasize(int old, int next, bool animate) {
        lineFade.stop();
        if (!animate || old < 0 || next < 0 || !animations()) {
            clearRows(emphasized);
            return;
        }
        const auto add = [this](int index, qreal initial) {
            auto found = std::find_if(emphasized.begin(), emphasized.end(), [index](const RowFade& row) {
                return row.row == index;
            });
            if (found != emphasized.end()) {
                return;
            }
            found = std::find_if(emphasized.begin(), emphasized.end(), [](const RowFade& row) {
                return row.row < 0;
            });
            if (found == emphasized.end()) {
                found = std::min_element(emphasized.begin(), emphasized.end(), [](const RowFade& a, const RowFade& b) {
                    return a.value < b.value;
                });
            }
            *found = {index, initial, initial};
        };
        add(old, 1);
        add(next, 0);
        for (auto& row : emphasized) {
            row.start = row.value;
        }
        lineFade.setDuration(kLineTransitionMilliseconds * 100 / animationPercent);
        lineFade.start();
    }

    void prepareTranslations(const Document& next) {
        if (!animations() || !view->isVisible()) {
            translationFade.stop();
            clearRows(translated);
            return;
        }
        bool changed = false;
        const int first = std::max(0, view->indexAt(QPoint(view->viewport()->width() / 2, 0)).row());
        for (int row = first; row < next.lines.size() && row < first + 64; ++row) {
            const auto bounds = view->visualItemRect(view->item(row));
            if (bounds.top() >= view->viewport()->height()) {
                break;
            }
            if (lyrics.lines[row].translation == next.lines[row].translation || next.lines[row].translation.isEmpty() ||
                !bounds.intersects(view->viewport()->rect())) {
                continue;
            }
            auto found = std::find_if(translated.begin(), translated.end(), [row](const RowFade& entry) {
                return entry.row == row;
            });
            if (found == translated.end()) {
                found = std::find_if(translated.begin(), translated.end(), [](const RowFade& entry) {
                    return entry.row < 0;
                });
            }
            if (found == translated.end()) {
                break;
            }
            *found = {int(row), 0, 0};
            changed = true;
        }
        if (changed) {
            translationFade.stop();
            for (auto& row : translated) {
                row.start = row.value;
            }
            translationFade.setDuration(kLineTransitionMilliseconds * 100 / animationPercent);
            translationFade.start();
        }
    }

    double position() const {
        return timeline.positionAt(monotonicNow());
    }

    void updateTimer() {
        const bool visible = view->isVisible() && !view->window()->isMinimized();
        if (playing && timeline.valid() && visible && !lyrics.lines.isEmpty()) {
            if (!ticker.isActive()) {
                ticker.start();
            }
        } else {
            ticker.stop();
        }
        if (!visible || !timeline.valid() || lyrics.lines.isEmpty()) {
            scroll.stop();
            finishFades();
        }
    }

    void follow(bool snap) {
        if (!following || active < 0 || !view->isVisible()) {
            return;
        }
        auto* bar = view->verticalScrollBar();
        const QRect rect = view->visualItemRect(view->item(active));
        const int target = std::clamp(bar->value() + rect.center().y() - view->viewport()->height() / 2, bar->minimum(),
                                      bar->maximum());
        if (!snap && scroll.state() == QAbstractAnimation::Running && scroll.endValue().toInt() == target) {
            return;
        }
        scroll.stop();
        if (snap || !animations()) {
            bar->setValue(target);
        } else if (target != bar->value()) {
            scroll.setStartValue(bar->value());
            scroll.setEndValue(target);
            scroll.start();
        }
    }

    void update(bool snap = false) {
        // mpv positions already include playback speed. Interpolate only the
        // elapsed time between authoritative observations, never scale the PTS.
        const double time = qRound64(position() * 1000.0) / 1000.0;
        const int next = timeline.valid() ? lyrics.indexAt(time, delay) : -1;
        const bool changed = next != active;
        if (changed) {
            const int oldActive = active;
            if (auto* old = view->item(oldActive)) {
                auto selectedFont = old->font();
                selectedFont.setBold(false);
                old->setFont(selectedFont);
            }
            active = next;
            emphasize(oldActive, next, !snap && view->isVisible());
            if (auto* current = view->item(active)) {
                auto selectedFont = current->font();
                selectedFont.setBold(true);
                current->setFont(selectedFont);
            }
            emit view->activeLineChanged(active);
        }
        progress = 0;
        if (active >= 0) {
            progress = lyrics.progressAt(active, time, delay);
            view->viewport()->update(view->visualItemRect(view->item(active)));
        }
        if (changed && playing) {
            following = true;
        }
        const QRect safe = view->viewport()->rect().adjusted(0, 12, 0, -12);
        const bool clipped = active >= 0 && !safe.contains(view->visualItemRect(view->item(active)));
        if (changed || snap || (following && clipped)) {
            follow(snap || clipped);
        }
    }

    QPointer<LyricsView> view;
    QPointer<LyricDelegate> delegate;
    Document lyrics;
    cyberlyrics::Timeline timeline;
    QTimer ticker;
    QPropertyAnimation scroll;
    QVariantAnimation lineFade, translationFade;
    std::array<RowFade, 64> emphasized, translated;
    std::function<bool()> animationPolicy;
    bool animationEnabled = true;
    int animationPercent = 100;
    double reportedPosition = -1;
    double duration = 0;
    double speed = 1;
    double delay = 0;
    double progress = 0;
    int active = -1;
    bool playing = false;
    bool following = true;
    bool translations = true;
};

LyricsView::LyricsView(QWidget* parent, std::function<bool()> animationPolicy)
    : QListWidget(parent), m_impl(std::make_unique<Impl>(this, std::move(animationPolicy))) {
    setObjectName("lyricsView");
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setSelectionMode(QAbstractItemView::NoSelection);
    m_impl->delegate = new LyricDelegate(this);
    setItemDelegate(m_impl->delegate);
}

LyricsView::~LyricsView() = default;

void LyricsView::setLyrics(const Document& lyrics) {
    if (sameOriginal(m_impl->lyrics, lyrics)) {
        // Translation and attribution may arrive after the timed original.
        // Keep the rows, playback anchor and user's browsing position intact.
        const int anchor = indexAt(QPoint(viewport()->width() / 2, 0)).row();
        const int top = anchor >= 0 ? visualItemRect(item(anchor)).top() : 0;
        m_impl->prepareTranslations(lyrics);
        m_impl->lyrics = lyrics;
        doItemsLayout();
        if (m_impl->following) {
            m_impl->follow(false);
        } else if (anchor >= 0) {
            auto* bar = verticalScrollBar();
            bar->setValue(bar->value() + visualItemRect(item(anchor)).top() - top);
        }
        viewport()->update();
        m_impl->updateTimer();
        return;
    }
    m_impl->scroll.stop();
    m_impl->finishFades();
    if (m_impl->delegate) {
        m_impl->delegate->clearLayouts();
    }
    clear();
    m_impl->lyrics = lyrics;
    m_impl->active = -1;
    m_impl->following = true;
    for (const auto& line : lyrics.lines) {
        auto* item = new QListWidgetItem(line.text, this);
        item->setData(Qt::UserRole, line.start);
        QFont lyricFont = font();
        lyricFont.setPixelSize(19);
        lyricFont.setBold(false);
        item->setFont(lyricFont);
    }
    m_impl->update(true);
    m_impl->updateTimer();
}

void LyricsView::setPlayback(double position, double duration, double speed, bool playing, bool positionValid) {
    const bool newPosition = position != m_impl->reportedPosition;
    const bool changed = newPosition || playing != m_impl->playing || speed != m_impl->speed ||
                         duration != m_impl->duration || positionValid != m_impl->timeline.valid();
    if (!changed) {
        return;
    }
    const bool snap = newPosition && std::abs(position - m_impl->position()) > .5;
    if (newPosition || playing != m_impl->playing || speed != m_impl->speed || duration != m_impl->duration ||
        positionValid != m_impl->timeline.valid()) {
        const double base = newPosition || !playing || !m_impl->timeline.valid() ? position : m_impl->position();
        m_impl->reportedPosition = position;
        m_impl->timeline.update(
            {base, duration, speed, monotonicNow(), 0, playing, positionValid && std::isfinite(base) && base >= 0});
    }
    m_impl->duration = duration;
    m_impl->speed = speed;
    m_impl->playing = playing;
    if (isVisible() && !window()->isMinimized()) {
        m_impl->update(snap);
    }
    m_impl->updateTimer();
}

void LyricsView::setDelay(double seconds) {
    if (m_impl->delay != seconds) {
        m_impl->delay = seconds;
        m_impl->update(true);
    }
}

void LyricsView::setFollowing(bool following) {
    m_impl->following = following;
    if (following) {
        m_impl->follow(false);
    } else {
        m_impl->scroll.stop();
    }
}

int LyricsView::activeLine() const {
    return m_impl->active;
}

double LyricsView::lineProgress() const {
    return m_impl->progress;
}

cyberlyrics::Line LyricsView::lyricLine(int row) const {
    return row >= 0 && row < m_impl->lyrics.lines.size() ? m_impl->lyrics.lines[row] : cyberlyrics::Line();
}

double LyricsView::lyricTime() const {
    return m_impl->position() - m_impl->delay;
}

void LyricsView::setTranslationsVisible(bool visible) {
    if (m_impl->translations != visible) {
        m_impl->translations = visible;
        if (!visible) {
            m_impl->translationFade.stop();
            m_impl->clearRows(m_impl->translated);
        }
        doItemsLayout();
        viewport()->update();
    }
}

bool LyricsView::translationsVisible() const {
    return m_impl->translations;
}

void LyricsView::setAnimationOptions(bool enabled, int speedPercent) {
    m_impl->animationEnabled = enabled;
    m_impl->animationPercent = std::clamp(speedPercent, 25, 300);
    m_impl->scroll.setDuration(kLineTransitionMilliseconds * 100 / m_impl->animationPercent);
    if (!m_impl->animations()) {
        m_impl->finishFades();
        m_impl->scroll.stop();
        m_impl->follow(true);
    }
}

bool LyricsView::animating() const {
    return m_impl->scroll.state() == QAbstractAnimation::Running ||
           m_impl->lineFade.state() == QAbstractAnimation::Running ||
           m_impl->translationFade.state() == QAbstractAnimation::Running;
}

qreal LyricsView::lineEmphasis(int row) const {
    for (const auto& entry : m_impl->emphasized) {
        if (entry.row == row) {
            return entry.value;
        }
    }
    return row == m_impl->active ? 1 : 0;
}

qreal LyricsView::translationOpacity(int row) const {
    for (const auto& entry : m_impl->translated) {
        if (entry.row == row) {
            return entry.value;
        }
    }
    return 1;
}

void LyricsView::wheelEvent(QWheelEvent* event) {
    setFollowing(false);
    QListWidget::wheelEvent(event);
}

void LyricsView::hideEvent(QHideEvent* event) {
    m_impl->ticker.stop();
    m_impl->scroll.stop();
    m_impl->finishFades();
    QListWidget::hideEvent(event);
}

void LyricsView::showEvent(QShowEvent* event) {
    QListWidget::showEvent(event);
    m_impl->update(true);
    m_impl->updateTimer();
}

void LyricsView::resizeEvent(QResizeEvent* event) {
    QListWidget::resizeEvent(event);
    m_impl->follow(true);
    QTimer::singleShot(0, this, [this] {
        m_impl->update(true);
    });
}
}  // namespace cyberlyrics
