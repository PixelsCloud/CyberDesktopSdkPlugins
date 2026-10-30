#include <sdk/Canvas.h>
#include <sdk/List.h>

#include <cstring>
#include <iostream>

namespace {
bool captured = false, allowed = true;
int redraws = 0;

void CD_CALL redraw(void*) {
    ++redraws;
}

CdResult CD_CALL enable(void*, uint32_t) {
    return CD_OK;
}

CdResult CD_CALL capture(void*, uint32_t next) {
    captured = next && allowed;
    return next && !allowed ? CD_ERROR_UNSUPPORTED : CD_OK;
}

CdResult CD_CALL query(void*, const char* name, uint32_t version, void* out, uint32_t size) {
    if (std::strcmp(name, CD_SERVICE_INTERACTION) || version != 1 || size < sizeof(CdInteractionApi)) {
        return CD_ERROR_UNSUPPORTED;
    }
    *static_cast<CdInteractionApi*>(out) = {sizeof(CdInteractionApi), 1, enable, capture};
    return CD_OK;
}
}  // namespace

int main() {
    try {
        CdServiceHostApi host{};
        host.input.media.base.struct_size = sizeof(host);
        host.input.media.base.request_redraw = redraw;
        host.query_service = query;
        CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, "test", "test", ".", &host.input.media.base};
        cdsdk::ListView list;
        list.bind(context);
        std::vector<cybercanvas::ListItem> items;
        for (int i = 0; i < 100; ++i) {
            items.push_back({std::to_string(i), 24});
        }
        list.items(items);
        list.arrange({5, 5, 90, 90}, 2);
        cdsdk::Canvas canvas;
        std::vector<uint8_t> pixels(200 * 200 * 4);
        CdFrame frame{sizeof(frame), 200, 200, 800, pixels.size(), pixels.data(), 2};
        canvas.render(frame, {}, [&](auto&, auto) {
            list.draw(canvas, cybercanvas::WidgetTheme::dark(), {});
        });
        list.update("90", 1);
        if (redraws || list.needsPaint()) {
            return 1;
        }
        CdInput wheel{sizeof(wheel), CD_INPUT_WHEEL, 0, 0, 30, 30, -120};
        if (!list.input(wheel).handled || !list.active() || redraws != 1) {
            return 1;
        }
        list.advance(1);
        if (list.offset() != 48) {
            return 1;
        }
        const auto bar = list.thumb();
        CdInput press{sizeof(press), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, bar.x + 2 + 7, bar.y + bar.height * .5 + 9};
        list.input(press, {7, 9});
        if (!captured || !list.router().captured()) {
            return 1;
        }
        auto move = press;
        move.kind = CD_INPUT_MOVE;
        move.y = 1000;
        list.input(move, {7, 9});
        if (list.offset() != list.maximum()) {
            return 1;
        }
        move.kind = CD_INPUT_RELEASE;
        move.buttons = 0;
        list.input(move, {7, 9});
        if (captured) {
            return 1;
        }
        cdsdk::ListView outer;
        outer.items({{"one", 300}, {"two", 300}});
        outer.arrange({0, 0, 100, 100});
        CdInput boundary{sizeof(boundary), CD_INPUT_WHEEL, 0, 0, 30, 30, -120};
        if (list.input(boundary).handled || !outer.input(boundary).handled) {
            return 1;
        }
        outer.advance(1);
        if (outer.offset() != 48) {
            return 1;
        }
        list.scrollTo(0, false);
        const auto top = list.thumb();
        press.y = top.y + top.height * .5 + 9;
        list.input(press, {7, 9});
        list.arrange({5, 5, 90, 80}, 2);
        if (captured || list.router().captured()) {
            return 1;
        }
        allowed = false;
        list.input(press, {7, 9});
        if (captured || list.router().captured()) {
            return 1;
        }
        allowed = true;
        list.cancel();
        CdInput outside{sizeof(outside), CD_INPUT_WHEEL, 0, 0, -100, -100, -120};
        if (list.input(outside).handled || list.active()) {
            return 1;
        }
        std::cout << "PASS SDK list: wheel units, translated DPI capture, outside drag, resize/rejection cancellation, "
                     "scoped redraw and offscreen updates\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
