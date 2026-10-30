#include <Windows.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "sdk/cpp/Services.h"
#include "sdk/services/SettingsChoices.h"
#include "support/Package.h"
#include "tools/NativeSettings.h"
#include "tools/PreviewServices.h"
#include "tools/Runtime.h"

namespace {
void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct WindowDeleter {
    void operator()(HWND window) const {
        DestroyWindow(window);
    }
};

struct Probe {
    cdsdk::NativeInstance instance;
    CdSettingsChoicesApi choices{};
    unsigned phase = 0;
    unsigned attempts = 0;
    std::string error;

    void publish(const char* json) {
        check(choices.publish(&instance.host, json, uint32_t(std::strlen(json))) == CD_OK,
              "dynamic choice update failed");
    }

    void step(HWND owner) {
        const auto window = FindWindowW(L"CyberDesktopSdkSettings", L"Settings contract");
        if (!window || GetWindow(window, GW_OWNER) != owner) {
            check(++attempts < 100, "settings window did not open");
            return;
        }
        const auto combo = FindWindowExW(window, nullptr, L"COMBOBOX", nullptr);
        const auto edit = FindWindowExW(window, nullptr, L"EDIT", nullptr);
        check(combo && edit, "settings controls missing");
        check(!IsWindowEnabled(owner), "modal owner remained enabled");
        if (phase == 0) {
            SendMessageW(combo, CB_SETCURSEL, 1, 0);
            SetWindowTextW(edit, L"unsaved 日本語 한국어");
            publish(
                R"({"fields":{"choice":{"context":"first","choices":[{"value":"manual","label":"Manual updated"},{"value":"auto","label":"Automatic updated"}]}}})");
        } else if (phase == 1) {
            check(SendMessageW(combo, CB_GETCURSEL, 0, 0) == 0, "option reorder changed the pending selected value");
            publish(
                R"({"fields":{"choice":{"context":"second","choices":[{"value":"manual","label":"Manual updated"},{"value":"auto","label":"Automatic updated"}]}}})");
        } else {
            check(SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1, "context change did not reread the plugin setting");
            wchar_t text[64]{};
            GetWindowTextW(edit, text, 64);
            check(std::wstring(text) == L"unsaved 日本語 한국어", "refresh discarded unrelated unsaved text");
            KillTimer(owner, 1);
            PostMessageW(window, WM_COMMAND, 2, 0);
        }
        ++phase;
    }

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* probe = reinterpret_cast<Probe*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            probe = static_cast<Probe*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(probe));
        }
        if (message == WM_TIMER && probe) {
            try {
                probe->step(window);
            } catch (const std::exception& error) {
                probe->error = error.what();
                KillTimer(window, 1);
                const auto settings = FindWindowW(L"CyberDesktopSdkSettings", L"Settings contract");
                if (settings && GetWindow(settings, GW_OWNER) == window) {
                    PostMessageW(settings, WM_CLOSE, 0, 0);
                } else {
                    PostQuitMessage(1);
                }
            }
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
};
}  // namespace

int main() try {
    Probe probe;
    probe.instance.host.preview->configure({}, {{"choice", true}, {"text", false}});
    probe.instance.api.get_setting = [](void*, const char* id, CdSettingValue* value, uint32_t) -> CdResult {
        *value = {};
        value->text = std::strcmp(id, "language") == 0 ? "en" : std::strcmp(id, "choice") == 0 ? "auto" : "stored";
        return CD_OK;
    };
    cdsdk::Services services(&probe.instance.host.api, &probe.instance.host);
    check(services.query(CD_SERVICE_SETTINGS_CHOICES, probe.choices) == CD_OK, "settings service missing");
    probe.publish(
        R"({"fields":{"choice":{"context":"first","choices":[{"value":"auto","label":"Automatic"},{"value":"manual","label":"Manual"}]}}})");
    cdsdk::pb::SettingsPage page;
    page.set_title("Settings contract");
    auto* field = page.add_fields();
    field->set_id("choice");
    field->set_type(cdsdk::pb::CHOICE);
    field->set_label("Choice");
    field = page.add_fields();
    field->set_id("text");
    field->set_type(cdsdk::pb::TEXT);
    field->set_label("Text");
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"CyberDesktopSdkSettingsContract";
    type.lpfnWndProc = Probe::windowProc;
    check(RegisterClassW(&type) != 0, "owner class failed");
    std::unique_ptr<std::remove_pointer_t<HWND>, WindowDeleter> owner(
        CreateWindowExW(0, type.lpszClassName, L"Contract owner", WS_OVERLAPPED, 0, 0, 20, 20, nullptr, nullptr,
                        type.hInstance, &probe));
    check(bool(owner) && SetTimer(owner.get(), 1, 25, nullptr), "owner/timer creation failed");
    cdsdk::showSettings(owner.get(), probe.instance, page);
    check(probe.error.empty(), probe.error.c_str());
    check(probe.phase == 3 && IsWindowEnabled(owner.get()), "settings did not finish or restore its owner");
    probe.publish(R"({"fields":{"choice":{"context":"closed"}}})");
    std::cout << "PASS native settings: pending edits, reordered choices, context refresh, modal cleanup\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
