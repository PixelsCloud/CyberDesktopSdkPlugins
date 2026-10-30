#include <CyberCanvas/Components.h>
#include <windowsx.h>

#include <cstring>
#include <iostream>

namespace {
using namespace cybercanvas;

struct Demo {
    Canvas canvas;
    InputRouter input;
    WidgetState feedback;
    AnimationClock clock;
    double value = 42, lastWidth = 0, lastHeight = 0, lastDpr = 0;
    bool dark = true;
    Button reset{1, {}, L"Reset", ButtonStyle::Filled};
    Button disabled{2, {}, L"Disabled", ButtonStyle::Outline};
    IconButton icon{3, {}, "info", L"Help", ButtonStyle::Outline};
    Slider slider{4, {0, 0, 0, 6}, 0, 100, 1};
    Toggle toggle{5, {}};
    ProgressBar progress{{}, 0, 100};
    TitleActions header{{}, L"CyberCanvas 0.6 — responsive controls", {icon}, 40, 8};
    Rect volumeArea{}, progressLabel{}, messageArea{};
    std::wstring message = L"Drag beyond the slider bounds. Resize the window to wrap the controls.";
    std::vector<uint8_t> pixels, bgra;

    Demo() {
        disabled.enabled = false;
        arrange(560, 340, 1);
    }

    void arrange(double width, double height, double dpr) {
        if (lastWidth == width && lastHeight == height && lastDpr == dpr) {
            return;
        }
        input.dispatch({PointerKind::Cancel});
        feedback = {};
        FlexStyle column;
        column.padding = {24, 24, 24, 24};
        column.gap = 12;
        FlexLayout tree(column);
        FlexStyle title;
        title.height = Length::points(44);
        title.shrink = 0;
        const auto head = tree.add(0, title);
        const auto buttons =
            wrappingRow({0, 0, std::max(0., width - 48), 100}, {{88, 32}, {104, 32}, {44, 32}}, 8, false, dpr);
        FlexStyle toolbar;
        toolbar.height = Length::points(buttons.height);
        toolbar.shrink = 0;
        const auto bar = tree.add(0, toolbar);
        FlexStyle volume;
        volume.height = Length::points(78);
        volume.shrink = 0;
        const auto track = tree.add(0, volume);
        FlexStyle progressStyle;
        progressStyle.height = Length::points(48);
        progressStyle.shrink = 0;
        const auto meter = tree.add(0, progressStyle);
        FlexStyle messageStyle;
        messageStyle.grow = 1;
        messageStyle.basis = Length::points(0);
        const auto note = tree.add(0, messageStyle);
        tree.calculate({0, 0, width, height}, dpr);
        header.bounds = tree.clippedBounds(head);
        icon.bounds = header.layout(dpr).actions.front();
        const auto toolbarArea = tree.clippedBounds(bar);
        const auto flow = wrappingRow(toolbarArea, {{88, 32}, {104, 32}, {44, 32}}, 8, false, dpr);
        reset.bounds = flow.items[0];
        disabled.bounds = flow.items[1];
        toggle.bounds = flow.items[2];
        volumeArea = tree.clippedBounds(track);
        auto theme = WidgetTheme::dark();
        theme.font.size = 12;
        slider.bounds = LabelledSlider{volumeArea, L"Volume", slider}.layout(canvas, theme).slider.bounds;
        const auto parts =
            LabelledSlider{tree.clippedBounds(meter), L"Progress", Slider(0, {0, 0, 0, 8})}.layout(canvas, theme);
        this->progress.bounds = parts.slider.bounds;
        progressLabel = parts.label;
        messageArea = tree.clippedBounds(note);
        input.regions({reset.region(), disabled.region(), icon.region(), slider.region(), toggle.region()});
        lastWidth = width;
        lastHeight = height;
        lastDpr = dpr;
    }

    void event(PointerEvent event) {
        const auto result = input.dispatch(event);
        if (reset.clicked(result)) {
            value = 0;
        }
        if (icon.clicked(result)) {
            message = L"TitleActions, LabelledSlider and wrapping rows share one layout engine.";
        }
        if (const auto next = slider.change(result)) {
            value = *next;
        }
        if (const auto next = toggle.change(result, dark)) {
            dark = *next;
        }
        feedback.sync(input);
    }

    FrameBuffer render(uint32_t width, uint32_t height, double dpr) {
        arrange(width / dpr, height / dpr, dpr);
        pixels.resize(size_t(width) * height * 4);
        FrameBuffer frame{width, height, width * 4, pixels.size(), pixels.data(), dpr};
        auto theme = dark ? WidgetTheme::dark() : WidgetTheme::light();
        theme.font.size = 12;
        canvas.render(frame, {}, [&](auto& draw, auto area) {
            draw.box(0, 0, area.width, area.height, 0, dark ? 0x151C28FF : 0xF9FBFDFF);
            auto titleTheme = theme;
            titleTheme.font.size = 18;
            titleTheme.font.weight = FontWeight::SemiBold;
            header.draw(draw, titleTheme, feedback, {});
            reset.draw(draw, theme, feedback.visual(reset.id));
            disabled.draw(draw, theme, feedback.visual(disabled.id));
            toggle.draw(draw, theme, dark, feedback.visual(toggle.id));
            LabelledSlider{volumeArea, L"Volume   " + std::to_wstring(int(value)) + L"%", slider}.draw(
                draw, theme, value, feedback.visual(slider.id));
            progress.draw(draw, theme, value);
            draw.text(L"Progress", theme.font, progressLabel, theme.muted);
            auto label = theme.font;
            label.wrap = label.ellipsis = true;
            label.vertical = VerticalAlign::Top;
            draw.clipped(messageArea, [&] {
                draw.text(message, label, messageArea, theme.muted);
            });
        });
        return frame;
    }
};

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* demo = reinterpret_cast<Demo*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        demo = static_cast<Demo*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(demo));
    }
    if (!demo) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    const auto send = [&](PointerKind kind, bool left = false) {
        const double dpr = GetDpiForWindow(window) / 96.;
        demo->event({kind, logicalPointer({double(GET_X_LPARAM(lparam)), double(GET_Y_LPARAM(lparam))}, dpr), left});
        if (demo->feedback.active()) {
            SetTimer(window, 1, 16, nullptr);
        }
        InvalidateRect(window, nullptr, FALSE);
    };
    switch (message) {
        case WM_SIZE: {
            demo->event({PointerKind::Cancel});
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            RECT area{};
            GetClientRect(window, &area);
            const double dpr = GetDpiForWindow(window) / 96.;
            demo->arrange(std::max(1L, area.right) / dpr, std::max(1L, area.bottom) / dpr, dpr);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDOWN:
            send(PointerKind::Press, true);
            SetCapture(window);
            return 0;
        case WM_LBUTTONUP:
            send(PointerKind::Release);
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            send(PointerKind::Move, (wparam & MK_LBUTTON) != 0);
            return 0;
        }
        case WM_MOUSELEAVE:
            send(PointerKind::Leave);
            return 0;
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            send(PointerKind::Cancel);
            return 0;
        case WM_DPICHANGED: {
            demo->event({PointerKind::Cancel});
            const auto* rect = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER);
            return 0;
        }
        case WM_TIMER:
            if (demo->feedback.advance(demo->clock.tick(GetTickCount64()).unscaledDelta)) {
                InvalidateRect(window, nullptr, FALSE);
            }
            if (!demo->feedback.active()) {
                KillTimer(window, 1);
                demo->clock.resetAnchor();
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(window, &paint);
            RECT area{};
            GetClientRect(window, &area);
            try {
                const auto frame = demo->render(uint32_t(std::max(1L, area.right)), uint32_t(std::max(1L, area.bottom)),
                                                GetDpiForWindow(window) / 96.);
                demo->bgra = demo->pixels;
                for (size_t i = 0; i < demo->bgra.size(); i += 4) {
                    std::swap(demo->bgra[i], demo->bgra[i + 2]);
                }
                BITMAPINFO bitmap{};
                bitmap.bmiHeader = {sizeof(BITMAPINFOHEADER), LONG(frame.width), -LONG(frame.height), 1, 32, BI_RGB};
                SetDIBitsToDevice(dc, 0, 0, frame.width, frame.height, 0, 0, 0, frame.height, demo->bgra.data(),
                                  &bitmap, DIB_RGB_COLORS);
            } catch (const std::exception&) {
                PostQuitMessage(1);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}  // namespace

int main(int argc, char** argv) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        Demo demo;
        const bool verify = argc > 2 && std::string_view(argv[1]) == "--verify";
        if (argc > 1 && !verify) {
            demo.dark = argc < 3 || std::string_view(argv[2]) != "light";
            demo.event(
                {PointerKind::Move, {demo.slider.bounds.x + demo.slider.bounds.width * .5, demo.slider.bounds.y}});
            demo.feedback.advance(.14);
            const bool narrow = argc > 3 && std::string_view(argv[3]) == "narrow";
            const auto frame = demo.render(narrow ? 560 : 1120, narrow ? 1000 : 680, 2);
            return demo.canvas.save(std::wstring(argv[1], argv[1] + std::strlen(argv[1])), frame) ? 0 : 1;
        }
        WNDCLASSW type{};
        type.lpfnWndProc = windowProc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"CyberCanvasWidgets";
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&type);
        const auto window =
            CreateWindowExW(0, type.lpszClassName, L"CyberCanvas controls", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, 600, 410, nullptr, nullptr, type.hInstance, &demo);
        if (!window) {
            return 2;
        }
        const UINT dpi = GetDpiForWindow(window);
        RECT desired{0, 0, int(std::lround(560 * dpi / 96.)), int(std::lround(340 * dpi / 96.))};
        AdjustWindowRectExForDpi(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
        SetWindowPos(window, nullptr, 0, 0, desired.right - desired.left, desired.bottom - desired.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (verify) {
            const auto send = [&](UINT kind, double x, double y, bool left = false) {
                const double dpr = GetDpiForWindow(window) / 96.;
                SendMessageW(window, kind, left ? MK_LBUTTON : 0,
                             MAKELPARAM(int(std::lround(x * dpr)), int(std::lround(y * dpr))));
            };
            demo.arrange(560, 340, dpi / 96.);
            const auto center = [](Rect r) {
                return Point{r.x + r.width * .5, r.y + r.height * .5};
            };
            const auto sliderPoint = center(demo.slider.bounds), disabledPoint = center(demo.disabled.bounds),
                       resetPoint = center(demo.reset.bounds), togglePoint = center(demo.toggle.bounds);
            bool passed = true;
            send(WM_LBUTTONDOWN, sliderPoint.x, sliderPoint.y, true);
            passed &= demo.input.captured();
            send(WM_MOUSEMOVE, 700, sliderPoint.y, true);
            passed &= demo.value == 100;
            send(WM_LBUTTONUP, 700, sliderPoint.y);
            passed &= !demo.input.captured();
            send(WM_LBUTTONDOWN, disabledPoint.x, disabledPoint.y, true);
            send(WM_LBUTTONUP, disabledPoint.x, disabledPoint.y);
            passed &= demo.value == 100;
            send(WM_LBUTTONDOWN, resetPoint.x, resetPoint.y, true);
            send(WM_LBUTTONUP, resetPoint.x, resetPoint.y);
            passed &= demo.value == 0;
            send(WM_LBUTTONDOWN, togglePoint.x, togglePoint.y, true);
            send(WM_LBUTTONUP, togglePoint.x, togglePoint.y);
            passed &= !demo.dark;
            send(WM_CANCELMODE, 0, 0);
            demo.feedback.advance(.14);
            passed &= !demo.feedback.active();
            const auto frame = demo.render(1120, 680, 2);
            passed &= demo.canvas.save(std::wstring(argv[2], argv[2] + std::strlen(argv[2])), frame);
            DestroyWindow(window);
            std::cout << (passed ? "PASS" : "FAIL")
                      << " native window messages: capture, outside drag, disabled button, reset, theme toggle and "
                         "idle feedback\n";
            return passed ? 0 : 1;
        }
        ShowWindow(window, SW_SHOWDEFAULT);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return int(message.wParam);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
