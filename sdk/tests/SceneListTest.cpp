#include <sdk/Canvas.h>
#include <sdk/SceneList.h>

#include <iostream>

int main() {
    using namespace cybercanvas;
    try {
        cdsdk::Canvas canvas;
        cdsdk::SceneList card;
        View header;
        header.kind = ViewKind::Button;
        header.key = 17;
        header.text = L"A long header / 标题";
        header.layout.height = Length::points(28);
        header.layout.shrink = 0;
        const auto node = card.scene().add(0, header);
        card.scene().move(node, 0, card.body());
        std::vector<ListItem> items;
        for (int i = 0; i < 100; ++i) {
            items.push_back({std::to_string(i), 24});
        }
        card.list().items(items);
        card.list().actions([](const ListRow& row) {
            return std::vector<ListAction>{{1, {row.bounds.x + 5, row.bounds.y + 2, 20, 20}}};
        });
        card.paint([&](auto& draw, const auto& row) {
            draw.text(std::wstring(row.id.begin(), row.id.end()), {}, row.bounds, 0xFFFFFFFF);
        });
        for (bool rtl : {false, true}) {
            card.arrange(canvas, {0, 0, 160, 120}, 1.5, rtl);
            const auto bounds = card.scene().bounds(card.body());
            if (bounds.y < 28 || bounds.height <= 0 || card.list().viewport().y != bounds.y) {
                return 1;
            }
            std::vector<uint8_t> pixels(240 * 180 * 4);
            CdFrame frame{sizeof(frame), 240, 180, 960, pixels.size(), pixels.data(), 1.5};
            canvas.render(frame, {}, [&](auto&, auto) {
                card.draw(canvas);
            });
            CdInput press{sizeof(press), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 35, 10};
            card.input(press);
            press.kind = CD_INPUT_RELEASE;
            press.buttons = 0;
            if (!card.input(press).control.clicked) {
                return 1;
            }
            card.list().scrollTo(0, false);
            const auto row = card.list().rows().front();
            press.kind = CD_INPUT_PRESS;
            press.buttons = CD_BUTTON_LEFT;
            press.x = row.bounds.x + 10;
            press.y = row.bounds.y + 8;
            card.input(press);
            press.kind = CD_INPUT_RELEASE;
            press.buttons = 0;
            const auto clicked = card.input(press).row;
            if (!clicked.clicked || clicked.id != "0" || clicked.action != 1) {
                return 1;
            }
            press.kind = CD_INPUT_WHEEL;
            press.wheel_delta = -120;
            if (!card.input(press).row.handled || !card.active()) {
                return 1;
            }
            card.advance(1);
            if (card.list().offset() != 48) {
                return 1;
            }
            card.scene().visible(card.body(), false);
            card.arrange(canvas, {0, 0, 160, 120});
            if (card.input(press).row.handled) {
                return 1;
            }
            card.scene().visible(card.body(), true);
            card.cancel();
        }
        card.scene().remove(card.body());
        card.arrange(canvas, {0, 0, 160, 120});
        std::cout << "PASS combined geometry, RTL, row actions, wheel, hidden/removed body and old-host input\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
