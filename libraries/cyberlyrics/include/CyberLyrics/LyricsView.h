#pragma once

#include <QListWidget>
#include <functional>
#include <memory>

class QHideEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

namespace cyberlyrics {
struct Document;
struct Line;
}  // namespace cyberlyrics

namespace cyberlyrics {

class LyricsView final : public QListWidget {
    Q_OBJECT
public:
    explicit LyricsView(QWidget* parent = nullptr, std::function<bool()> animationPolicy = {});
    ~LyricsView() override;
    void setLyrics(const cyberlyrics::Document& lyrics);
    void setPlayback(double position, double duration, double speed, bool playing, bool positionValid = true);
    void setDelay(double seconds);
    void setFollowing(bool following);
    int activeLine() const;
    double lineProgress() const;
    cyberlyrics::Line lyricLine(int row) const;
    double lyricTime() const;
    void setTranslationsVisible(bool visible);
    bool translationsVisible() const;
    void setAnimationOptions(bool enabled, int speedPercent = 100);
    bool animating() const;
    qreal lineEmphasis(int row) const;
    qreal translationOpacity(int row) const;

signals:
    void activeLineChanged(int row);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace cyberlyrics
