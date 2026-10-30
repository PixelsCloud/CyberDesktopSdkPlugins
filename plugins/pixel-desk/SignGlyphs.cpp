#include "plugins/pixel-desk/SignGlyphs.h"

namespace pixeldesk {
bool signGlyph(cybercanvas::Canvas& canvas, std::wstring_view text, cybercanvas::Rect bounds) {
    if (text.empty() || text == L" ") {
        return true;
    }
    if (text.size() != 1) {
        return false;
    }
    constexpr std::wstring_view alphabet = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    constexpr uint8_t letters[][7]{
        {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
        {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
        {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14},
        {7, 2, 2, 2, 18, 18, 12},     {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
        {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
        {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
        {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
        {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
        {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},     {0, 0, 14, 1, 15, 17, 15},
        {16, 16, 22, 25, 17, 17, 30}, {0, 0, 14, 16, 16, 17, 14},   {1, 1, 13, 19, 17, 17, 15},
        {0, 0, 14, 17, 31, 16, 14},   {6, 9, 8, 28, 8, 8, 8},       {0, 15, 17, 17, 15, 1, 14},
        {16, 16, 22, 25, 17, 17, 17}, {4, 0, 12, 4, 4, 4, 14},      {2, 0, 6, 2, 2, 18, 12},
        {16, 16, 18, 20, 24, 20, 18}, {12, 4, 4, 4, 4, 4, 14},      {0, 0, 26, 21, 21, 17, 17},
        {0, 0, 22, 25, 17, 17, 17},   {0, 0, 14, 17, 17, 17, 14},   {0, 0, 30, 17, 30, 16, 16},
        {0, 0, 15, 17, 15, 1, 1},     {0, 0, 22, 25, 16, 16, 16},   {0, 0, 15, 16, 14, 1, 30},
        {4, 4, 14, 4, 4, 5, 2},       {0, 0, 17, 17, 17, 19, 13},   {0, 0, 17, 17, 17, 10, 4},
        {0, 0, 17, 17, 21, 21, 10},   {0, 0, 17, 10, 4, 10, 17},    {0, 0, 17, 17, 15, 1, 14},
        {0, 0, 31, 2, 4, 8, 31},
    };
    const auto index = alphabet.find(text.front());
    const bool number = text.front() >= L'0' && text.front() <= L'9';
    if (index == alphabet.npos && !number && text.front() != L'-' && text.front() != L'.') {
        return false;
    }
    std::vector<cybercanvas::Point> cells;
    unsigned left = 5, right = 0;
    // Capitals and normal lowercase letters share baseline row six. The two
    // extra rows belong to descenders; shrinking p/y into seven rows would lift
    // their bowls above the other lowercase letters in CyberDesktop.
    const std::array<uint8_t, 9>* descender = nullptr;
    constexpr std::array<uint8_t, 9> g{0, 0, 15, 17, 17, 17, 15, 1, 14};
    constexpr std::array<uint8_t, 9> j{4, 0, 12, 4, 4, 4, 4, 20, 8};
    constexpr std::array<uint8_t, 9> p{0, 0, 30, 17, 17, 17, 30, 16, 16};
    constexpr std::array<uint8_t, 9> q{0, 0, 13, 19, 17, 17, 15, 1, 1};
    constexpr std::array<uint8_t, 9> y{0, 0, 17, 17, 17, 17, 15, 1, 14};
    switch (text.front()) {
        case L'g':
            descender = &g;
            break;
        case L'j':
            descender = &j;
            break;
        case L'p':
            descender = &p;
            break;
        case L'q':
            descender = &q;
            break;
        case L'y':
            descender = &y;
            break;
        default:
            break;
    }
    for (unsigned row = 0; row < 9; ++row) {
        for (unsigned column = 0; column < 5; ++column) {
            const bool lit = descender                ? ((*descender)[row] & (1u << (4 - column))) != 0
                             : row >= 7               ? false
                             : index != alphabet.npos ? (letters[index][row] & (1u << (4 - column))) != 0
                             : number
                                 ? mechanicalpixels::clockCell(std::string(1, char(text.front())), column + 1, row + 1)
                             : text.front() == L'-' ? row == 3
                                                    : row == 6 && column == 2;
            if (lit) {
                cells.push_back({double(column), double(row)});
                left = std::min(left, column);
                right = std::max(right, column + 1);
            }
        }
    }
    if (cells.empty()) {
        return true;
    }
    // Fixed glyph height avoids enlarging narrow letters; center actual ink.
    const double unit = std::min(bounds.width * .72 / 5, bounds.height * .78 / 9),
                 x = bounds.x + (bounds.width - (right - left) * unit) / 2 - left * unit,
                 top = bounds.y + (bounds.height - 9 * unit) / 2;
    cybercanvas::Path glyph(canvas.factory.Get(), cybercanvas::FillRule::Winding);
    for (const auto p : cells) {
        const double px = x + p.x * unit, py = top + p.y * unit;
        glyph.moveTo({px, py}).lineTo({px + unit, py}).lineTo({px + unit, py + unit}).lineTo({px, py + unit}).close();
    }
    canvas.fill(glyph, kCream);
    return true;
}
}  // namespace pixeldesk
