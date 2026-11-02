#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace cyberlyrics::windowsmedia {
enum MediaStatus : uint32_t { MediaStopped = 0, MediaPlaying = 1, MediaPaused = 2 };

enum MediaControl : uint32_t { CanToggle = 1, CanPrevious = 2, CanNext = 4, CanSeek = 8 };

enum MediaCommand : uint32_t { Toggle = 1, Previous = 2, Next = 3, Seek = 4 };

enum MediaResult : int32_t {
    MediaOk = 0,
    MediaArgument = -1,
    MediaInternal = -4,
    MediaUnsupported = -5,
    MediaBusy = -6
};

struct MediaSnapshot {
    QString source, title, artist, album;
    QImage cover;
    uint32_t status = 0, controls = 0;
    uint64_t coverGeneration = 0, sampledAt = 0, sessionGeneration = 0, commandRequestId = 0;
    double position = 0, duration = 0, rate = 1;
    bool positionValid = false;
    int32_t commandResult = MediaOk;
};

struct MediaSessionInfo {
    QString source, name;
    uint32_t controls = 0;
    bool operator==(const MediaSessionInfo&) const = default;
};

enum class SourceMatch { Contains, Exact };

// Empty/auto follows the system's current session. Exact selection never falls
// through to a similarly named application when the chosen session disappears.
std::optional<std::size_t> selectMediaSessionIndex(std::span<const MediaSnapshot> snapshots, const QString& current,
                                                   const QString& filter, SourceMatch match);

// One process-wide C++/WinRT reader. All OS calls and cover decoding run on
// its worker; subscribers and deliveries remain on the host UI thread.
class MediaService : public QObject {
public:
    explicit MediaService(QObject* parent = nullptr);
    ~MediaService() override;
    uint64_t subscribe(QString filter, std::function<void(const MediaSnapshot&)> callback,
                       SourceMatch match = SourceMatch::Contains);
    uint64_t subscribeSessions(std::function<void(const std::vector<MediaSessionInfo>&)> callback);
    void unsubscribe(uint64_t token);
    int32_t command(uint64_t token, uint32_t command, double value, uint64_t* requestId = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace cyberlyrics::windowsmedia
