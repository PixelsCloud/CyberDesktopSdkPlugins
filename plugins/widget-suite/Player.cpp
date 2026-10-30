#include <algorithm>
#include <cmath>
#include <cwctype>
#include <numbers>

#include "plugins/widget-suite/PlayerArtwork.h"
#include "plugins/widget-suite/Widget.h"

namespace widgetplugin {
TonearmPose tonearmPose(double progress) {
    constexpr cybercanvas::Point pivot{553, 130}, originalContact{411, 389};
    const double fraction = std::isfinite(progress) ? std::clamp(progress, 0., 1.) : 0;
    const double rotation = std::lerp(-1.5, 1.5, fraction);
    const double radians = rotation * std::numbers::pi / 180;
    const double dx = originalContact.x - pivot.x, dy = originalContact.y - pivot.y;
    return {pivot,
            {pivot.x + dx * std::cos(radians) - dy * std::sin(radians),
             pivot.y + dx * std::sin(radians) + dy * std::cos(radians)},
            std::hypot(dx, dy),
            rotation};
}

void advanceTonearm(Media& media, double seconds, double position) {
    if (media.status != CD_MEDIA_PLAYING || media.source.empty() || !std::isfinite(seconds) || seconds <= 0) {
        return;
    }
    media.tonearmSeconds += seconds;
    // A live stream has no duration: use a very slow, continuous sweep rather
    // than a looping sawtooth that would snap the cartridge across the record.
    const double target = media.duration > 0 && std::isfinite(position)
                              ? std::clamp(position / media.duration, 0., 1.)
                              : .1 + .4 * (1 - std::cos(media.tonearmSeconds * std::numbers::pi / 120));
    const double travel = seconds * .035;
    media.tonearmProgress += std::clamp(target - media.tonearmProgress, -travel, travel);
}

PlayerLayout arrangePlayer(Spec spec, float width, float height) {
    PlayerLayout layout;
    const double w = std::max(32.f, width), h = std::max(32.f, height);
    if (spec.mechanical) {
        const auto stage = playerArtworkStage(spec, w, h);
        const auto rect = [&](cybercanvas::Rect r) {
            const auto box = stage.map(r);
            return CdRect{box.x, box.y, box.width, box.height};
        };
        const auto pivot = [&](double x, double y) {
            const auto point = rect({x, y, 0, 0});
            return cybercanvas::Point{point.x, point.y};
        };
        if (spec.rows == 1) {
            layout.cover = rect({54, 28, 264, 264});
            layout.tonearmPivot = pivot(364, 49);
            layout.title = rect({425, 87, 480, 45});
            layout.timeline = rect({345, 261, 579, 11});
            for (unsigned i = 0; i < 3; ++i) {
                constexpr double centers[]{473.3, 656.3, 843.7};
                layout.buttons[i] = rect({centers[i] - 45, 150, 90, 90});
            }
            layout.titleSize = float(std::clamp(32 * stage.scale, 9.5, 16.));
            layout.detailSize = float(std::clamp(21 * stage.scale, 6., 9.5));
            return layout;
        }
        if (spec.columns == 2) {
            layout.cover = rect({202, 81.6, 556, 556});
            layout.tonearmPivot = pivot(832, 136);
            layout.title = rect({203, 753, 594, 72});
            layout.timeline = rect({160, 1087, 680, 9});
            for (unsigned i = 0; i < 3; ++i) {
                constexpr double centers[]{258.9, 496.9, 735.7};
                layout.buttons[i] = rect({centers[i] - 82, 887, 164, 164});
            }
            layout.titleSize = float(std::clamp(46 * stage.scale, 9.5, 17.));
            layout.detailSize = float(std::clamp(42 * stage.scale, 8., 12.));
            return layout;
        }
        layout.cover = rect({78, 99, 402, 402});
        layout.tonearmPivot = pivot(553, 130);
        layout.title = rect({558, 228, 334, 65});
        layout.timeline = rect({448, 459, 455, 12});
        layout.elapsed = rect({441, 479, 115, 36});
        layout.duration = rect({799, 479, 115, 36});
        layout.titleSize = float(std::clamp(43 * stage.scale, 10., 18.));
        layout.detailSize = float(std::clamp(32 * stage.scale, 9., 13.));
        for (unsigned i = 0; i < 3; ++i) {
            layout.buttons[i] = rect(mechanicalart::playerButton(i));
        }
        return layout;
    }
    double buttonSize = 0, centerX = 0, centerY = 0;
    if (spec.rows == 1) {
        const double cover = std::min(h - 24, w * .26), left = cover + 24;
        layout.cover = {10, (h - cover - 3) * .5, cover, cover};
        layout.title = {left, 8, w - left - 12, h * .30};
        layout.artist = {left, h * .34, w - left - 12, h * .20};
        layout.titleSize = float(std::clamp(h * .20, 12.0, 20.0));
        layout.detailSize = float(std::clamp(h * .125, 9.0, 12.0));
        buttonSize = std::min({30.0, h * .28, (w - left - 12) / 3.6});
        centerX = (left + w - 12) * .5;
        centerY = h * .70;
        layout.timeline = {left, h - 12, w - left - 12, 3};
    } else if (spec.columns == 2) {
        buttonSize = std::min({32.0, h * .17, (w - 26) / 3.6});
        const double titleHeight = h < 170 ? 22 : h * .12, artistHeight = h < 170 ? 16 : h * .10;
        const double cover =
                         std::max(4.0, std::min({w - 28, h * .43, h - 37 - buttonSize - titleHeight - artistHeight})),
                     top = 9;
        layout.cover = {(w - cover - 2) * .5, top, cover, cover};
        layout.title = {10, top + cover + 3, w - 22, titleHeight};
        layout.artist = {10, top + cover + 5 + titleHeight, w - 22, artistHeight};
        layout.titleSize = float(std::clamp(w * (h < 170 ? .10 : .115), 12.0, 18.0));
        layout.detailSize = float(std::clamp(w * (h < 170 ? .07 : .08), 9.0, 12.0));
        centerX = (w - 2) * .5;
        centerY = h - 18 - buttonSize * .5;
        layout.timeline = {12, h - 12, w - 26, 3};
    } else {
        const double cover = std::max(4.0, std::min(h - 58, w * .43)), left = cover + 28;
        layout.cover = {12, 14, cover, cover};
        layout.title = {left, 13, w - left - 14, h * .16};
        layout.artist = {left, h * .23, w - left - 14, h * .12};
        layout.album = {left, h * .37, w - left - 14, h * .12};
        layout.source = {left, h - 64, w - left - 14, 17};
        layout.titleSize = float(std::clamp(w * .064, 14.0, 21.0));
        layout.detailSize = float(std::clamp(w * .04, 10.0, 13.0));
        buttonSize = std::min({36.0, h * .17, (w - left - 14) / 3.6});
        centerX = (left + w - 14) * .5;
        centerY = h * .58;
        layout.timeline = {12, h - 34, w - 26, 3};
        layout.elapsed = {12, h - 28, 60, 18};
        layout.duration = {w - 74, h - 28, 60, 18};
    }

    for (int i = 0; i < 3; ++i)
        layout.buttons[i] = {centerX + (i - 1) * buttonSize * 1.25 - buttonSize * .5, centerY - buttonSize * .5,
                             buttonSize, buttonSize};
    if (spec.rows > 1 && spec.columns > 2) {
        layout.source.y = layout.buttons[1].y + layout.buttons[1].height + 3;
        layout.source.height = std::min(17., layout.timeline.y - layout.source.y - 4);
        if (layout.source.height < 10) {
            layout.source = {};
        }
    }
    return layout;
}

int playerHit(const PlayerLayout& layout, double x, double y, uint32_t controls) {
    constexpr uint32_t flags[]{CD_MEDIA_CAN_PREVIOUS, CD_MEDIA_CAN_TOGGLE, CD_MEDIA_CAN_NEXT};
    for (int i = 0; i < 3; ++i) {
        const auto& r = layout.buttons[i];
        if ((controls & flags[i]) && x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height)
            return i + 1;
    }
    const auto& r = layout.timeline;
    if ((controls & CD_MEDIA_CAN_SEEK) && x >= r.x && x <= r.x + r.width &&
        std::abs(y - r.y - r.height / 2) <= std::max(8., r.height / 2))
        return 4;
    return 0;
}

double mediaPosition(const Media& media, uint64_t now) {
    double position = media.position;
    if (media.status == CD_MEDIA_PLAYING && media.sampledAt && now >= media.sampledAt)
        position += std::min<uint64_t>(now - media.sampledAt, 3000) * .001 * media.rate;
    return std::clamp(position, 0.0, std::max(0.0, media.duration));
}

std::wstring playerName(const std::wstring& source, bool english) {
    std::wstring lower = source;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) {
        return wchar_t(std::towlower(c));
    });
    if (lower.find(L"cyber_player") != std::wstring::npos || lower.find(L"cyberplayer") != std::wstring::npos) {
        return L"CyberPlayer";
    }
    if (lower.find(L"qqmusic") != std::wstring::npos)
        return english ? L"QQ Music" : L"QQ 音乐";
    if (lower.find(L"cloudmusic") != std::wstring::npos)
        return english ? L"NetEase Music" : L"网易云音乐";
    if (lower.find(L"kugou") != std::wstring::npos)
        return english ? L"Kugou Music" : L"酷狗音乐";
    if (lower.find(L"spotify") != std::wstring::npos)
        return L"Spotify";
    return source;
}
}  // namespace widgetplugin
