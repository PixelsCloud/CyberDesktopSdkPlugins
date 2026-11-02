#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>

#include "CyberLyrics/Codec.h"
#include "CyberLyrics/Document.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name) {
        if (!condition) {
            std::cerr << name << '\n';
            ++failures;
        }
    };
    cyberlyrics::Document document;
    document.track.duration = 20;
    document.lines = {{10, 0, "second", {}, {}}, {2, 0, "first", {}, {}}};
    document.normalize();
    check(document.indexAt(1) == -1, "prelude has no current line");
    check(document.indexAt(10, 1) == 0, "positive offset delays lyrics exactly once");
    check(document.indexAt(11, 1) == 1, "exact boundary uses following line");
    check(document.indexAt(3) == 0, "backward seek selects preceding line");
    check(document.indexAt(std::numeric_limits<double>::quiet_NaN()) == -1, "reject invalid position");
    document.lines[0].words = {{"fi", 2, 3}, {"rst", 4, 5}};
    document.normalize();
    check(document.quality == cyberlyrics::TimingQuality::Word, "genuine word timing preserved");
    check(std::abs(document.progressAt(0, 3.5) - .5) < .001, "inter-word gap freezes progress");
    document.lines[0].words[1].text = "wrong";
    document.normalize();
    check(document.lines[0].words.isEmpty(), "mismatched words rejected");
    cyberlyrics::Document combined;
    combined.lines = {{0, 3, "é x", {}, {{"e", 0, 1}, {"́", 1, 2}, {" x", 2, 3}}}};
    combined.normalize();
    check(combined.lines[0].words.size() == 2 && combined.lines[0].words[0].text == "é" &&
              combined.lines[0].words[0].end == 2,
          "word highlighting never splits a combining grapheme");
    auto restored = cyberlyrics::decode(cyberlyrics::encode(document));
    check(restored.state == cyberlyrics::LoadState::Ready && restored.lines.size() == document.lines.size(),
          "persistent document round trip preserves usable lyric lines");
    cyberlyrics::Document empty;
    empty.state = cyberlyrics::LoadState::Ready;
    check(cyberlyrics::decode(cyberlyrics::encode(empty)).state == cyberlyrics::LoadState::Failed,
          "empty ready cache cannot suppress network fallback permanently");
    empty.state = cyberlyrics::LoadState::Instrumental;
    check(cyberlyrics::decode(cyberlyrics::encode(empty)).state == cyberlyrics::LoadState::Instrumental,
          "instrumental cache legitimately has no lyric lines");
    cyberlyrics::Timeline clock;
    clock.update({60, 0, 2, 1000, 1, true, true});
    check(clock.positionAt(1500) == 61, "unknown duration retains position and rate applies to delta only");
    clock.update({61, 0, 2, 1500, 1, false, true});
    check(clock.positionAt(5000) == 61, "paused clock remains frozen");
    clock.update({12, 20, 1, 5000, 2, true, true});
    check(clock.positionAt(6000) == 13, "new generation resets authoritative anchor");
    check(clock.positionAt(99000) == 20, "known end bounds extrapolation");
    clock.update({0, 0, 1, 0, 3, true, false});
    check(!clock.valid(), "missing position never fabricates synchronization");
    cyberlyrics::Track track{"qq", "Song", "Artist", "Album", {}, 200};
    auto candidate = track;
    candidate.source = "network";
    check(cyberlyrics::matches(track, candidate), "matching recording accepted");
    candidate.duration = 210;
    check(!cyberlyrics::matches(track, candidate), "different duration rejected");
    candidate = track;
    candidate.version = "live";
    check(!cyberlyrics::matches(track, candidate), "different version rejected");
    candidate = track;
    candidate.artist.clear();
    check(!cyberlyrics::matches(track, candidate), "title-only match rejected");
    std::cout << "CyberLyrics core failures: " << failures << '\n';
    return failures ? 1 : 0;
}
