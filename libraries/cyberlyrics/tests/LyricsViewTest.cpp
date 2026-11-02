#include <QApplication>
#include <QEventLoop>
#include <QPoint>
#include <QScrollBar>
#include <QString>
#include <QTimer>
#include <iostream>

#include "CyberLyrics/Document.h"
#include "CyberLyrics/LyricsView.h"

namespace {
void events(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, Qt::PreciseTimer, &loop, &QEventLoop::quit);
    loop.exec();
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](const char* name, bool passed) {
        std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
        failures += !passed;
    };
    cyberlyrics::Document original;
    original.track = {"fixture", "Recording", "Artist", "Album", "", 120};
    for (int row = 0; row < 30; ++row) {
        const double start = row * 4.;
        original.lines.append({start,
                               start + 4,
                               QString("Timed original %1").arg(row),
                               {},
                               {{QString("Timed original %1").arg(row), start, start + 2}}});
    }
    original.normalize();
    cyberlyrics::LyricsView view;
    view.resize(440, 180);
    view.setLyrics(original);
    view.show();
    view.setPlayback(61, 120, 1, false);
    events(60);
    int changes = 0;
    QObject::connect(&view, &cyberlyrics::LyricsView::activeLineChanged, &view, [&](int) {
        ++changes;
    });
    const auto* active = view.item(view.activeLine());
    const double progress = view.lineProgress();
    view.setFollowing(false);
    view.verticalScrollBar()->setValue(170);
    const auto anchor = view.indexAt(QPoint(view.viewport()->width() / 2, 0));
    const int top = view.visualItemRect(view.item(anchor.row())).top();
    auto translated = original;
    ++translated.revision;
    for (auto& line : translated.lines) {
        line.translation = QString(180, QChar('x'));
    }
    translated.lines[15].translation = "Late translation";
    view.setLyrics(translated);
    events(60);
    check("late translation preserves the active item and genuine word progress",
          view.activeLine() == 15 && view.item(15) == active && view.lineProgress() == progress && changes == 0 &&
              view.lyricLine(15).translation == "Late translation");
    check("late translation preserves the browsed row and its pixel offset",
          anchor.isValid() && view.indexAt(QPoint(view.viewport()->width() / 2, 0)).row() == anchor.row() &&
              view.visualItemRect(view.item(anchor.row())).top() == top);
    view.setFollowing(true);
    events(420);
    check("following can resume after a translated row changes height",
          view.visualItemRect(view.item(15)).intersects(view.viewport()->rect()));
    view.setLyrics(original);
    check("removing translation clears the previous text without restarting the original",
          view.item(15) == active && view.lyricLine(15).translation.isEmpty() && view.lineProgress() == progress &&
              changes == 0);
    auto different = original;
    different.track.version = "live";
    view.setFollowing(false);
    view.verticalScrollBar()->setValue(0);
    view.setLyrics(different);
    check("a different recording resets the old browsing state", changes == 1);
    view.setPlayback(67.95, 120, 1, true);
    view.hide();
    const int hiddenLine = view.activeLine();
    const int hiddenChanges = changes;
    events(150);
    check("hidden lyrics suspend updates after document replacement",
          view.activeLine() == hiddenLine && changes == hiddenChanges);
    cyberlyrics::Document dense;
    dense.track = {"fixture", "Dense recording", "Artist", "Album", "", 12};
    for (int row = 0; row < 40; ++row) {
        dense.lines.append({row * .3, (row + 1) * .3, QString("Animated row %1").arg(row), {}, {}});
    }
    cyberlyrics::LyricsView animated(nullptr, [] {
        return true;
    });
    animated.resize(440, 400);
    animated.setLyrics(dense);
    animated.show();
    animated.setPlayback(3.29, 12, 1, false);
    events(30);
    animated.setPlayback(3.31, 12, 1, false);
    check("line emphasis crossfades from the previous active row",
          animated.activeLine() == 11 && animated.lineEmphasis(10) == 1 && animated.lineEmphasis(11) == 0 &&
              animated.animating());
    events(80);
    const auto intermediate = animated.lineEmphasis(11);
    animated.setPlayback(3.61, 12, 1, false);
    check("rapid line changes preserve the current emphasis", intermediate > 0 && intermediate < 1 &&
                                                                  animated.lineEmphasis(11) == intermediate &&
                                                                  animated.lineEmphasis(12) == 0);
    events(420);
    check("line and scroll animations reach a finite endpoint",
          !animated.animating() && animated.lineEmphasis(12) == 1 && animated.lineEmphasis(11) == 0);
    auto late = dense;
    late.lines[12].translation = "Independent translated text";
    const auto* timedItem = animated.item(12);
    const auto timedProgress = animated.lineProgress();
    const auto originalTop = animated.visualItemRect(animated.item(12)).top();
    animated.setLyrics(late);
    check("late translation starts independently with the original untouched",
          animated.translationOpacity(12) == 0 && animated.lineEmphasis(12) == 1 && animated.item(12) == timedItem &&
              animated.lineProgress() == timedProgress &&
              animated.visualItemRect(animated.item(12)).top() == originalTop);
    events(80);
    check("translation has real intermediate opacity",
          animated.translationOpacity(12) > 0 && animated.translationOpacity(12) < 1);
    animated.setAnimationOptions(false);
    check("disabling animation settles lyrics and translation immediately",
          !animated.animating() && animated.translationOpacity(12) == 1 && animated.lineEmphasis(12) == 1);
    animated.setAnimationOptions(true);
    animated.setPlayback(3.91, 12, 1, false);
    animated.hide();
    check("hiding a view cancels every finite lyric animation", !animated.animating());
    cyberlyrics::LyricsView reduced(nullptr, [] {
        return false;
    });
    reduced.resize(440, 400);
    reduced.setLyrics(dense);
    reduced.show();
    reduced.setPlayback(3.29, 12, 1, false);
    reduced.setPlayback(3.31, 12, 1, false);
    check("reduced motion policy skips emphasis and scrolling animations",
          !reduced.animating() && reduced.lineEmphasis(11) == 1 && reduced.lineEmphasis(10) == 0);
    return failures ? 1 : 0;
}
