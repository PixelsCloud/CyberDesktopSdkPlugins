#include <CyberCanvas/Canvas.h>
#include <CyberCanvas/Text.h>

#include <array>
#include <iostream>
#include <thread>

namespace {
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}

template <class F>
bool rejects(F action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        Canvas canvas;
        const std::wstring paragraph =
            L"你好，这是一段需要分页的中文。🙂\nA long wrapped paragraph with an authored newline.";
        const TextStyle lineStyle{.size = 16, .family = L"Microsoft YaHei", .wrap = true};
        const auto lines = canvas.textLines(paragraph, lineStyle, 110);
        uint32_t consumed = 0;
        bool contiguous = !lines.empty();
        for (const auto& line : lines) {
            contiguous &= line.offset == consumed && line.height > 0;
            consumed += line.length;
        }
        check("DirectWrite pagination exposes contiguous UTF-16 lines without losing newlines or surrogate pairs",
              contiguous && consumed == paragraph.size() &&
                  lines.size() == canvas.measureText(paragraph, lineStyle, {110, 16384}).lineCount);
        TextSpan hiddenEmoji;
        hiddenEmoji.text = L"🙂🙂";
        hiddenEmoji.style = {.size = 32, .family = L"Segoe UI Emoji"};
        hiddenEmoji.ink.color = 0xffffff00;
        StyledText hidden(canvas, std::array{hiddenEmoji}, {.available = {180, 70}});
        const auto hiddenPixels = hidden.snapshot();
        bool transparent = true;
        for (size_t i = 3; i < hiddenPixels->pixels().size(); i += 4) {
            transparent &= hiddenPixels->pixels()[i] == 0;
        }
        check("transparent text ink also hides color emoji palette layers", transparent);
        TextSpan heading;
        heading.text = L"福禄寿 ";
        heading.style = {.size = 36, .family = L"Microsoft YaHei", .bold = true};
        heading.ink = {0xffdd55ff, 0xf07828ff, 1.5, 0x442211ff};
        TextSpan link;
        link.text = L"Hello 世界";
        link.link = "example";
        link.style.size = 24;
        link.ink.color = 0x44ccffff;
        link.underline = true;
        std::array spans{heading, link};
        StyledText text(canvas, spans, {.available = {400, 100}});
        check("UTF-16 text retained", text.text() == L"福禄寿 Hello 世界");
        check("mixed sizes use DirectWrite line metrics", text.metrics().height > 36 && text.metrics().lineCount == 1);
        const auto bounds = text.linkBounds("example");
        check("link has layout bounds", !bounds.empty());
        if (!bounds.empty()) {
            const auto hit = text.hit({bounds[0].x + 2, bounds[0].y + bounds[0].height / 2});
            check("link hit follows shaped text", hit && hit->link == "example");
        }
        check("outside text is not a link", !text.hit({-1, 0}) && !text.hit({399, 99}));
        auto image = text.snapshot();
        check("snapshot cache reuses immutable pixels", text.snapshot() == image);
        size_t colored = 0, shaded = 0;
        for (size_t i = 0; i < image->pixels().size(); i += 4) {
            const auto& p = image->pixels();
            colored += p[i + 3] > 200 && p[i + 2] > 180 && p[i + 1] > 100;
            shaded += p[i + 3] > 200 && p[i + 2] < 100 && p[i + 1] < 60;
            if (p[i] > p[i + 3] || p[i + 1] > p[i + 3] || p[i + 2] > p[i + 3]) {
                check("premultiplied text output", false);
                break;
            }
        }
        check("gradient fills and geometry outlines both rendered", colored > 100 && shaded > 100);
        const auto doubled = text.snapshot(2);
        check("snapshot generated at actual DPI", doubled->width() == image->width() * 2);

        link.text = L"one two three four five six seven eight nine ten eleven twelve thirteen fourteen";
        StyledText wrapped(canvas, std::array{link},
                           {.available = {110, 180}, .paragraph = {.wrap = true, .ellipsis = true}, .maximumLines = 2});
        check("maximum lines clips and trims paragraph",
              wrapped.metrics().lineCount == 2 && wrapped.metrics().height < 80);
        for (const auto& rect : wrapped.linkBounds("example")) {
            check("link excludes hidden lines", rect.y + rect.height <= wrapped.metrics().height + .01);
        }
        check("hidden text cannot be clicked", !wrapped.hit({10, 100}));
        wrapped.snapshot();
        TextSpan rtl;
        rtl.text = L"مرحبا بالعالم 123";
        rtl.link = "rtl";
        rtl.style.size = 26;
        StyledText bidi(canvas, std::array{rtl}, {.available = {400, 80}});
        check("bidi and fallback produce glyphs", bidi.metrics().width > 100 && !bidi.linkBounds("rtl").empty());
        bidi.snapshot();
        TextSpan icon;
        icon.icon = ImageData::fromPremultipliedBgra(4, 4, std::vector<uint8_t>(4 * 4 * 4, 255));
        icon.iconSize = {20, 20};
        icon.iconBaseline = 16;
        StyledText withIcon(canvas, std::array{link, icon}, {.available = {400, 100}});
        check("inline icon occupies one character", withIcon.text().back() == 0xfffc);
        withIcon.snapshot();
        TextSpan emoji;
        emoji.text = L"😀";
        emoji.style = {.size = 48, .family = L"Segoe UI Emoji"};
        emoji.ink = {0xffffffff, 0xff0000ff, 2};
        StyledText colorful(canvas, std::array{emoji});
        const auto emojiImage = colorful.snapshot();
        bool hasColor = false;
        for (size_t i = 0; i < emojiImage->pixels().size(); i += 4) {
            const auto& p = emojiImage->pixels();
            hasColor |= p[i + 3] > 200 && p[i + 2] > p[i] + 80 && p[i + 1] > p[i] + 80;
        }
        check("color emoji keeps palette instead of gradient", hasColor);
        StyledText empty(canvas, {});
        empty.snapshot();
        TextSpan outlined;
        outlined.text = L"g";
        outlined.style.size = 32;
        outlined.ink = {0xffffffff, {}, 16, 0xff0000ff};
        StyledText thick(canvas, std::array{outlined}, {.available = {100, 80}});
        const auto thickImage = thick.snapshot(1, 20);
        bool bottomOutline = false;
        for (uint32_t y = uint32_t(std::ceil(thick.metrics().height)) + 21; y < thickImage->height(); ++y) {
            for (uint32_t x = 0; x < thickImage->width(); ++x) {
                bottomOutline |= thickImage->pixels()[(size_t(y) * thickImage->width() + x) * 4 + 3] != 0;
            }
        }
        check("snapshot padding preserves descender outline below line metrics", bottomOutline);
        TextSpan firstLine = outlined;
        firstLine.text = L"g\n";
        firstLine.ink.color = 0xff0000ff;
        TextSpan hiddenLine;
        hiddenLine.text = L"MMMM";
        hiddenLine.style.size = 32;
        hiddenLine.ink.color = 0x00ff00ff;
        StyledText clippedLines(canvas, std::array{firstLine, hiddenLine},
                                {.available = {200, 120}, .maximumLines = 1});
        const auto clippedImage = clippedLines.snapshot(1, 20);
        bool hiddenGreen = false;
        for (size_t i = 0; i < clippedImage->pixels().size(); i += 4) {
            const auto& p = clippedImage->pixels();
            hiddenGreen |= p[i + 1] > p[i + 2] + 50;
        }
        check("expanded outline padding does not reveal hidden lines", !hiddenGreen);
        TextSpan spaced = link;
        spaced.text = L"1234";
        spaced.style.characterSpacing = 0;
        StyledText unspaced(canvas, std::array{spaced});
        spaced.style.characterSpacing = 2;
        spaced.style.tabularFigures = true;
        StyledText spacedText(canvas, std::array{spaced});
        check("span character spacing is applied", spacedText.metrics().width > unspaced.metrics().width + 10);
        bool rejected = false;
        std::thread foreign([&] {
            rejected = rejects([&] {
                text.metrics();
            });
        });
        foreign.join();
        check("foreign-thread layout access rejected", rejected);
        check("invalid snapshot DPI rejected", rejects([&] {
                  text.snapshot(0);
              }));
        check("snapshot pixel budget enforced", rejects([&] {
                  StyledText large(canvas, {}, {.available = {4096, 4096}});
                  large.snapshot(4);
              }));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
