#include "tools/PreviewTextInput.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace cdsdk {
namespace {

struct WindowDeleter {
    void operator()(HWND window) const noexcept {
        if (IsWindow(window)) {
            DestroyWindow(window);
        }
    }
};

std::optional<std::wstring> decode(const char* bytes, uint32_t length) {
    if (length > 16000 || (!bytes && length) ||
        (length && std::string_view(bytes, length).find('\0') != std::string_view::npos)) {
        return std::nullopt;
    }
    if (!length) {
        return std::wstring{};
    }
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, int(length), nullptr, 0);
    if (!count) {
        return std::nullopt;
    }
    std::wstring result(size_t(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, int(length), result.data(), count)) {
        return std::nullopt;
    }
    unsigned points = 0;
    for (const auto value : result) {
        points += value < 0xdc00 || value > 0xdfff;
    }
    return points <= 2000 ? std::optional(std::move(result)) : std::nullopt;
}

std::optional<std::string> encode(std::wstring_view text) {
    if (text.empty()) {
        return std::string{};
    }
    const auto count =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count || count > 16000) {
        return std::nullopt;
    }
    std::string result(size_t(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), count,
                             nullptr, nullptr)) {
        return std::nullopt;
    }
    return decode(result.data(), uint32_t(result.size())) ? std::optional(std::move(result)) : std::nullopt;
}

}  // namespace

struct PreviewTextInput::Impl {
    std::unique_ptr<HWND__, WindowDeleter> editor;
    HWND parent = nullptr;  // Observed native window; subclass revokes it on WM_NCDESTROY.
    CdTextInputCallback callback = nullptr;
    void* user = nullptr;
    CdRect bounds{};
    double dpr = 1;
    std::string text;
    std::wstring placeholder;
    bool updating = false, composing = false, paused = false, configured = false;

    ~Impl() {
        callback = nullptr;
        detach();
    }

    void detach() {
        if (parent && IsWindow(parent)) {
            RemoveWindowSubclass(parent, parentProc, UINT_PTR(this));
        }
        if (editor && IsWindow(editor.get())) {
            RemoveWindowSubclass(editor.get(), editorProc, UINT_PTR(this));
        }
        editor.reset();
        parent = nullptr;
    }

    void notify(uint32_t event) {
        // Copy before entering plugin code: disconnect/destruction may be reentrant.
        const auto receive = callback;
        const auto context = user;
        const auto value = text;
        if (receive) {
            receive(context, event, value.data(), uint32_t(value.size()));
        }
    }

    void updateText() {
        if (updating || !editor) {
            return;
        }
        const auto length = GetWindowTextLengthW(editor.get());
        std::wstring wide(size_t(std::max(0, length)) + 1, L'\0');
        wide.resize(size_t(GetWindowTextW(editor.get(), wide.data(), int(wide.size()))));
        const auto value = encode(wide);
        if (!value) {
            const auto previous = decode(text.data(), uint32_t(text.size()));
            updating = true;
            SetWindowTextW(editor.get(), previous->c_str());
            SendMessageW(editor.get(), EM_SETSEL, previous->size(), previous->size());
            updating = false;
            return;
        }
        if (*value != text) {
            text = *value;
            notify(CD_TEXT_CHANGED);
        }
    }

    void position() {
        if (editor) {
            SetWindowPos(editor.get(), HWND_TOP, int(std::lround(bounds.x * dpr)), int(std::lround(bounds.y * dpr)),
                         int(std::lround(bounds.width * dpr)), int(std::lround(bounds.height * dpr)), SWP_NOACTIVATE);
        }
    }

    static LRESULT CALLBACK editorProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) try {
        auto& self = *reinterpret_cast<Impl*>(data);
        if (message == WM_IME_STARTCOMPOSITION) {
            self.composing = true;
        } else if (message == WM_IME_ENDCOMPOSITION) {
            self.composing = false;
        } else if (message == WM_KEYDOWN && w == VK_RETURN && !self.composing && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            self.notify(CD_TEXT_SUBMIT);
            return 0;
        } else if (message == WM_CHAR && w == VK_RETURN && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            return 0;
        } else if (message == WM_KEYDOWN && w == VK_ESCAPE && !self.composing) {
            SetFocus(self.parent);
            return 0;
        } else if (message == WM_KILLFOCUS) {
            ShowWindow(window, SW_HIDE);
            self.notify(CD_TEXT_FOCUS_LOST);
            return 0;
        }
        return DefSubclassProc(window, message, w, l);
    } catch (...) {
        return 0;
    }

    static LRESULT CALLBACK parentProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) try {
        auto& self = *reinterpret_cast<Impl*>(data);
        if (message == WM_COMMAND && reinterpret_cast<HWND>(l) == self.editor.get() && HIWORD(w) == EN_CHANGE) {
            self.updateText();
            return 0;
        }
        if (message == WM_NCDESTROY) {
            self.parent = nullptr;
            self.editor.release();  // Windows has destroyed all child windows.
            RemoveWindowSubclass(window, parentProc, id);
        }
        return DefSubclassProc(window, message, w, l);
    } catch (...) {
        return 0;
    }
};

PreviewTextInput::PreviewTextInput() : impl_(std::make_unique<Impl>()) {}

PreviewTextInput::~PreviewTextInput() = default;

CdResult PreviewTextInput::connect(CdTextInputCallback callback, void* user) {
    impl_->callback = callback;
    impl_->user = callback ? user : nullptr;
    if (!callback) {
        focus(false);
    }
    return CD_OK;
}

CdResult PreviewTextInput::configure(const CdRect* bounds, const char* placeholder, uint32_t length) {
    const auto decoded = decode(placeholder, length);
    if (!bounds || !decoded || !std::isfinite(bounds->x) || !std::isfinite(bounds->y) ||
        !std::isfinite(bounds->width) || !std::isfinite(bounds->height) || bounds->width <= 0 || bounds->height <= 0 ||
        std::abs(bounds->x) > 32768 || std::abs(bounds->y) > 32768 || bounds->width > 32768 || bounds->height > 32768) {
        return CD_ERROR_ARGUMENT;
    }
    impl_->bounds = *bounds;
    impl_->configured = true;
    impl_->placeholder = *decoded;
    if (impl_->editor) {
        SendMessageW(impl_->editor.get(), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(impl_->placeholder.c_str()));
        impl_->position();
    }
    return CD_OK;
}

CdResult PreviewTextInput::setText(const char* bytes, uint32_t length) {
    const auto decoded = decode(bytes, length);
    if (!decoded) {
        return CD_ERROR_ARGUMENT;
    }
    impl_->text = length ? std::string(bytes, length) : std::string{};
    if (impl_->editor) {
        impl_->updating = true;
        SetWindowTextW(impl_->editor.get(), decoded->c_str());
        impl_->updating = false;
    }
    return CD_OK;
}

CdResult PreviewTextInput::focus(bool focused) {
    auto& self = *impl_;
    if (!focused) {
        const auto editor = self.editor.get();
        const auto parent = self.parent;
        if (editor) {
            ShowWindow(editor, SW_HIDE);
            if (GetFocus() == editor) {
                SetFocus(parent);
            }
        }
        return CD_OK;
    }
    if (!self.callback || self.paused || !self.parent || !IsWindow(self.parent) || !self.configured) {
        return CD_ERROR_UNSUPPORTED;
    }
    if (!self.editor) {
        self.editor.reset(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 1, 1,
                                          self.parent, nullptr, GetModuleHandleW(nullptr), nullptr));
        if (!self.editor ||
            !SetWindowSubclass(self.editor.get(), Impl::editorProc, UINT_PTR(&self), DWORD_PTR(&self))) {
            self.editor.reset();
            return CD_ERROR_INTERNAL;
        }
        SendMessageW(self.editor.get(), WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
        SendMessageW(self.editor.get(), EM_LIMITTEXT, 4000, 0);
        SendMessageW(self.editor.get(), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(self.placeholder.c_str()));
        setText(self.text.data(), uint32_t(self.text.size()));
    }
    self.position();
    const auto editor = self.editor.get();
    ShowWindow(editor, SW_SHOW);
    SetFocus(editor);
    return GetFocus() == editor ? CD_OK : CD_ERROR_UNSUPPORTED;
}

void PreviewTextInput::placement(HWND parent, double dpr) {
    if (!std::isfinite(dpr) || dpr <= 0 || dpr > 8) {
        return;
    }
    if (parent != impl_->parent) {
        impl_->detach();
        if (parent && IsWindow(parent) &&
            SetWindowSubclass(parent, Impl::parentProc, UINT_PTR(impl_.get()), DWORD_PTR(impl_.get()))) {
            impl_->parent = parent;
        }
    }
    impl_->dpr = dpr;
    impl_->position();
}

void PreviewTextInput::pause(bool paused) {
    impl_->paused = paused;
    if (paused) {
        focus(false);
    }
}
}  // namespace cdsdk
