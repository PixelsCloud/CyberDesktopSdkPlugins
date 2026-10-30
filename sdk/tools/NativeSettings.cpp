#include "tools/NativeSettings.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <type_traits>
#include <vector>

#include "support/Package.h"
#include "tools/PreviewServices.h"
#include "tools/Runtime.h"

namespace cdsdk {
struct FontDeleter {
    void operator()(HFONT font) const {
        DeleteObject(font);
    }
};

using Font = std::unique_ptr<std::remove_pointer_t<HFONT>, FontDeleter>;

struct WindowDeleter {
    void operator()(HWND window) const {
        if (IsWindow(window))
            DestroyWindow(window);
    }
};

using Window = std::unique_ptr<std::remove_pointer_t<HWND>, WindowDeleter>;

class ModalOwner {
public:
    explicit ModalOwner(HWND owner) : owner_(owner) {
        EnableWindow(owner_, FALSE);
    }

    ~ModalOwner() {
        if (IsWindow(owner_)) {
            EnableWindow(owner_, TRUE);
            SetActiveWindow(owner_);
        }
    }

private:
    HWND owner_;
};

int CALLBACK collectFont(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM combo);

struct SettingsWindow {
    NativeInstance& instance;
    pb::SettingsPage& page;

    struct Control {
        std::reference_wrapper<const pb::SettingField> field;
        HWND editor;
        HWND label;
        HWND browse = nullptr;
        std::string context;
    };

    std::vector<Control> controls;
    bool closed = false;
    int offset = 0, contentHeight = 0;
    Font font;
    std::string language = "zh-CN";

    void translate(HWND window);

    void defaults();

    void read(const std::string& only = {});
    void refresh(HWND window);
    ~SettingsWindow();

    bool apply(HWND window);

    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l);
};

SettingsWindow::~SettingsWindow() {
    instance.host.preview->settingsChanged({});
}

std::wstring settingsWide(const std::string& text) {
    const auto path = utf8Path(text);
    return path.wstring();
}

auto SettingsWindow::translate(HWND window) -> void {
    const bool english = language == "en";
    const auto pageText = page.localized().find(language);
    SetWindowTextW(window,
                   settingsWide(pageText == page.localized().end() ? page.title() : pageText->second.title()).c_str());
    for (const auto& control : controls) {
        const auto& field = control.field.get();
        const auto local = field.localized().find(language);
        SetWindowTextW(control.label,
                       settingsWide(local == field.localized().end() ? field.label() : local->second.label()).c_str());
        if (control.browse)
            SetWindowTextW(control.browse, english ? L"Browse…" : language == "zh-TW" ? L"瀏覽…" : L"浏览…");
        if (field.type() == pb::CHOICE) {
            const auto current = SendMessageW(control.editor, CB_GETCURSEL, 0, 0);
            SendMessageW(control.editor, CB_RESETCONTENT, 0, 0);
            for (const auto& choice : field.choices()) {
                const auto translated = choice.localized_labels().find(language);
                const auto value = translated == choice.localized_labels().end() ? choice.label() : translated->second;
                SendMessageW(control.editor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(settingsWide(value).c_str()));
            }
            SendMessageW(control.editor, CB_SETCURSEL, current, 0);
        }
    }
    SetWindowTextW(GetDlgItem(window, 1), english ? L"Save" : L"保存");
    SetWindowTextW(GetDlgItem(window, 2), english ? L"Cancel" : L"取消");
    SetWindowTextW(GetDlgItem(window, 3), english ? L"Defaults" : language == "zh-TW" ? L"還原預設值" : L"恢复默认");
    SetWindowTextW(GetDlgItem(window, 4), english ? L"Apply" : language == "zh-TW" ? L"套用" : L"应用");
    SetWindowTextW(GetDlgItem(window, 5), english               ? L"Settings affect this preview instance only."
                                          : language == "zh-TW" ? L"僅設定目前預覽實例。"
                                                                : L"仅设置当前预览实例。");
}

auto SettingsWindow::defaults() -> void {
    for (const auto& control : controls) {
        const auto& field = control.field.get();
        std::wstring text;
        if (field.type() == pb::TOGGLE) {
            SendMessageW(control.editor, BM_SETCHECK, field.bool_default() ? BST_CHECKED : BST_UNCHECKED, 0);
            continue;
        }
        if (field.type() == pb::CHOICE) {
            for (int i = 0; i < field.choices_size(); ++i)
                if (field.choices(i).value() == field.text_default())
                    SendMessageW(control.editor, CB_SETCURSEL, i, 0);
            continue;
        }
        if (field.type() == pb::INTEGER)
            text = std::to_wstring(field.integer_default());
        else if (field.type() == pb::NUMBER)
            text = std::to_wstring(field.number_default());
        else if (field.type() == pb::COLOR) {
            wchar_t rgba[16]{};
            swprintf_s(rgba, L"%08X", field.color_default());
            text = rgba;
        } else
            text = settingsWide(field.text_default());
        SetWindowTextW(control.editor, text.c_str());
    }
}

auto SettingsWindow::read(const std::string& only) -> void {
    for (const auto& control : controls) {
        const auto& field = control.field.get();
        if (!only.empty() && field.id() != only)
            continue;
        CdSettingValue value{};
        if (instance.api.get_setting(instance.api.instance, field.id().c_str(), &value, sizeof(value)) != CD_OK)
            continue;
        if (field.type() == pb::TOGGLE) {
            SendMessageW(control.editor, BM_SETCHECK, value.integer ? BST_CHECKED : BST_UNCHECKED, 0);
            continue;
        }
        if (field.type() == pb::CHOICE) {
            for (int i = 0; i < field.choices_size(); ++i)
                if (value.text && field.choices(i).value() == value.text)
                    SendMessageW(control.editor, CB_SETCURSEL, i, 0);
            continue;
        }
        std::wstring text;
        if (field.type() == pb::INTEGER)
            text = std::to_wstring(value.integer);
        else if (field.type() == pb::NUMBER)
            text = std::to_wstring(value.number);
        else if (field.type() == pb::COLOR) {
            wchar_t rgba[16]{};
            swprintf_s(rgba, L"%08X", uint32_t(value.integer));
            text = rgba;
        } else
            text = value.text ? settingsWide(value.text) : L"";
        SetWindowTextW(control.editor, text.c_str());
    }
}

void SettingsWindow::refresh(HWND window) {
    std::map<std::string, std::string> selected;
    for (const auto& control : controls) {
        const auto& field = control.field.get();
        if (field.type() != pb::CHOICE)
            continue;
        const auto index = int(SendMessageW(control.editor, CB_GETCURSEL, 0, 0));
        if (index >= 0 && index < field.choices_size())
            selected[field.id()] = field.choices(index).value();
    }
    instance.host.preview->applySettingsChoices(page);
    translate(window);
    for (auto& control : controls) {
        const auto& field = control.field.get();
        const auto context = instance.host.preview->settingsContext(field.id());
        if (context != control.context) {
            control.context = context;
            read(field.id());
        } else if (field.type() == pb::CHOICE) {
            bool found = false;
            for (int index = 0; index < field.choices_size(); ++index) {
                if (selected[field.id()] == field.choices(index).value()) {
                    SendMessageW(control.editor, CB_SETCURSEL, index, 0);
                    found = true;
                    break;
                }
            }
            if (!found)
                read(field.id());
        }
    }
}

auto SettingsWindow::apply(HWND window) -> bool {
    try {
        std::vector<CdSettingValue> values;
        std::vector<std::string> text;
        values.reserve(controls.size());
        text.reserve(controls.size());
        for (const auto& control : controls) {
            const auto& field = control.field.get();
            CdSettingValue value{sizeof(value), uint32_t(field.type()), field.id().c_str(), 0, 0, nullptr};
            wchar_t buffer[4097]{};
            GetWindowTextW(control.editor, buffer, 4097);
            text.push_back(utf8Argument(buffer));
            value.text = text.back().c_str();
            size_t end = 0;
            if (field.type() == pb::TOGGLE)
                value.integer = SendMessageW(control.editor, BM_GETCHECK, 0, 0) == BST_CHECKED;
            else if (field.type() == pb::CHOICE) {
                const auto index = int(SendMessageW(control.editor, CB_GETCURSEL, 0, 0));
                if (index < 0 || index >= field.choices_size())
                    throw std::runtime_error("Select a choice");
                value.text = field.choices(index).value().c_str();
            } else if (field.type() == pb::INTEGER) {
                value.integer = std::stoll(text.back(), &end);
                if (end != text.back().size() || value.integer < field.minimum() || value.integer > field.maximum())
                    throw std::runtime_error("Integer outside declared range");
            } else if (field.type() == pb::NUMBER) {
                value.number = std::stod(text.back(), &end);
                if (end != text.back().size() || !std::isfinite(value.number) || value.number < field.minimum() ||
                    value.number > field.maximum())
                    throw std::runtime_error("Number outside declared range");
            } else if (field.type() == pb::COLOR) {
                value.integer = std::stoll(text.back(), &end, 16);
                if (end != text.back().size() || value.integer < 0 || uint64_t(value.integer) > UINT32_MAX)
                    throw std::runtime_error("Use RGBA hex: RRGGBBAA");
            } else if (text.back().size() > field.max_length())
                throw std::runtime_error("Text exceeds declared UTF-8 limit");
            values.push_back(value);
        }
        if (instance.api.apply_settings(instance.api.instance, values.data(), uint32_t(values.size())) != CD_OK)
            throw std::runtime_error("Plugin rejected settings; previous values retained");
        instance.host.dirty = true;
        return true;
    } catch (const std::exception& error) {
        MessageBoxW(window, settingsWide(error.what()).c_str(), L"Cannot apply settings", MB_OK | MB_ICONWARNING);
        return false;
    }
}

void showSettings(HWND owner, NativeInstance& instance, const pb::SettingsPage& declaredPage) {
    pb::SettingsPage page = declaredPage;
    instance.host.preview->applySettingsChoices(page);
    SettingsWindow state{instance, page};
    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsWindow::proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CyberDesktopSdkSettings";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassW(&wc);
    Window ownedWindow(CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, settingsWide(page.title()).c_str(),
                                       WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL, CW_USEDEFAULT,
                                       CW_USEDEFAULT, 540, 540, owner, nullptr, wc.hInstance, &state));
    const HWND window = ownedWindow.get();
    if (!window)
        return;
    state.font.reset(CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
    auto create = [&](const wchar_t* kind, const std::wstring& text, DWORD style, int x, int y, int width, int height,
                      int id = 0) {
        auto child = CreateWindowExW(0, kind, text.c_str(), WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
                                     reinterpret_cast<HMENU>(INT_PTR(id)), wc.hInstance, nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.font.get()), TRUE);
        return child;
    };
    create(L"STATIC", L"Settings affect this preview instance only.", 0, 14, 10, 480, 24, 5);
    int y = 44;
    for (const auto& field : page.fields()) {
        HWND label = create(L"STATIC", settingsWide(field.label()) + (field.type() == pb::COLOR ? L" (RRGGBBAA)" : L""),
                            0, 14, y, 250, 26);
        HWND editor = nullptr;
        if (field.type() == pb::TOGGLE)
            editor = create(L"BUTTON", L"Enabled", BS_AUTOCHECKBOX | WS_TABSTOP, 270, y, 220, 26);
        else if (field.type() == pb::CHOICE) {
            editor = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 270, y, 220, 240);
            for (const auto& option : field.choices())
                SendMessageW(editor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(settingsWide(option.label()).c_str()));
        } else if (field.type() == pb::FONT) {
            editor = create(L"COMBOBOX", L"", CBS_DROPDOWN | WS_TABSTOP | WS_VSCROLL, 270, y, 220, 240);
            SendMessageW(editor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"@siyuan-heavy"));
            SendMessageW(editor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"@zqk-free"));
            LOGFONTW query{};
            query.lfCharSet = DEFAULT_CHARSET;
            HDC dc = GetDC(window);
            EnumFontFamiliesExW(dc, &query, collectFont, reinterpret_cast<LPARAM>(editor), 0);
            ReleaseDC(window, dc);
        } else if (field.type() == pb::TEXT && field.text_editor() == pb::SettingField::MULTILINE) {
            editor = create(L"EDIT", L"",
                            WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP, 270, y,
                            220, 110);
            SendMessageW(editor, EM_SETLIMITTEXT, field.max_length(), 0);
        } else
            editor = create(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 270, y, 220, 26);
        HWND browse = nullptr;
        if (field.type() == pb::TEXT && field.text_editor() == pb::SettingField::DIRECTORY) {
            SetWindowPos(editor, nullptr, 270, y, 140, 26, SWP_NOZORDER);
            browse = create(L"BUTTON", L"Browse…", BS_PUSHBUTTON | WS_TABSTOP, 415, y, 75, 26);
        }
        state.controls.push_back(
            {std::cref(field), editor, label, browse, instance.host.preview->settingsContext(field.id())});
        y += field.text_editor() == pb::SettingField::MULTILINE ? 132 : 52;
    }
    create(L"BUTTON", L"Defaults", BS_PUSHBUTTON | WS_TABSTOP, 14, y + 10, 100, 30, 3);
    create(L"BUTTON", L"Apply", BS_PUSHBUTTON | WS_TABSTOP, 170, y + 10, 100, 30, 4);
    create(L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 280, y + 10, 100, 30, 1);
    create(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 390, y + 10, 100, 30, 2);
    state.contentHeight = y + 10;
    SetScrollRange(window, SB_VERT, 0, std::max(0, state.contentHeight - 360), TRUE);
    state.read();
    CdSettingValue language{};
    if (instance.api.get_setting(instance.api.instance, "language", &language, sizeof(language)) == CD_OK &&
        language.text)
        state.language = language.text;
    state.translate(window);
    instance.host.preview->settingsChanged([window] {
        PostMessageW(window, WM_APP + 19, 0, 0);
    });
    ModalOwner modal(owner);
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (!state.closed) {
        const auto status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            if (status == 0)
                PostQuitMessage(int(message.wParam));
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    instance.host.preview->settingsChanged({});
    ownedWindow.reset();
}

int CALLBACK collectFont(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM combo) {
    if (font->lfFaceName[0] == L'@')
        return 1;
    const auto window = reinterpret_cast<HWND>(combo);
    if (SendMessageW(window, CB_FINDSTRINGEXACT, WPARAM(-1), reinterpret_cast<LPARAM>(font->lfFaceName)) == CB_ERR)
        SendMessageW(window, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(font->lfFaceName));
    return 1;
}

LRESULT CALLBACK SettingsWindow::proc(HWND window, UINT message, WPARAM w, LPARAM l) try {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(window, message, w, l);
    if (message == WM_APP + 19) {
        // Only choices change: field addresses retained by controls remain stable.
        self->refresh(window);
        return 0;
    }
    if (message == WM_COMMAND) {
        for (const auto& control : self->controls)
            if (control.browse && control.browse == reinterpret_cast<HWND>(l) && HIWORD(w) == BN_CLICKED) {
                const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                Microsoft::WRL::ComPtr<IFileDialog> dialog;
                if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(dialog.GetAddressOf())))) {
                    DWORD options = 0;
                    dialog->GetOptions(&options);
                    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                    if (SUCCEEDED(dialog->Show(window))) {
                        Microsoft::WRL::ComPtr<IShellItem> folder;
                        PWSTR path = nullptr;
                        if (SUCCEEDED(dialog->GetResult(folder.GetAddressOf())) &&
                            SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            SetWindowTextW(control.editor, path);
                            CoTaskMemFree(path);
                        }
                    }
                }
                dialog.Reset();
                if (SUCCEEDED(initialized))
                    CoUninitialize();
                return 0;
            }
        if (HIWORD(w) == CBN_SELCHANGE)
            for (const auto& control : self->controls)
                if (control.field.get().id() == "language" && control.editor == reinterpret_cast<HWND>(l)) {
                    const auto index = int(SendMessageW(control.editor, CB_GETCURSEL, 0, 0));
                    if (index >= 0 && index < control.field.get().choices_size()) {
                        self->language = control.field.get().choices(index).value();
                        self->translate(window);
                    }
                    return 0;
                }
        const auto id = LOWORD(w);
        if (id == 1) {
            if (self->apply(window))
                DestroyWindow(window);
            return 0;
        }
        if (id == 2) {
            DestroyWindow(window);
            return 0;
        }
        if (id == 3) {
            self->defaults();
            for (const auto& control : self->controls)
                if (control.field.get().id() == "language")
                    self->language = control.field.get().text_default();
            self->translate(window);
            return 0;
        }
        if (id == 4) {
            self->apply(window);
            return 0;
        }
    }
    if (message == WM_VSCROLL) {
        const int old = self->offset;
        const int code = LOWORD(w);
        if (code == SB_LINEUP)
            self->offset -= 30;
        if (code == SB_LINEDOWN)
            self->offset += 30;
        if (code == SB_PAGEUP)
            self->offset -= 300;
        if (code == SB_PAGEDOWN)
            self->offset += 300;
        if (code == SB_THUMBTRACK)
            self->offset = HIWORD(w);
        self->offset = std::clamp(self->offset, 0, std::max(0, self->contentHeight - 360));
        ScrollWindowEx(window, 0, old - self->offset, nullptr, nullptr, nullptr, nullptr,
                       SW_SCROLLCHILDREN | SW_INVALIDATE);
        SetScrollPos(window, SB_VERT, self->offset, TRUE);
        return 0;
    }
    if (message == WM_DESTROY) {
        self->instance.host.preview->settingsChanged({});
        self->closed = true;
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
} catch (...) {
    return DefWindowProcW(window, message, w, l);
}
}  // namespace cdsdk
