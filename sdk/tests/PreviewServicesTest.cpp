#include <Windows.h>
#include <unknwn.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "sdk/cpp/Services.h"
#include "sdk/services/Icons.h"
#include "sdk/services/ImageSkins.h"
#include "sdk/services/ImageViewer.h"
#include "sdk/services/RenderImages.h"
#include "sdk/services/RenderMemory.h"
#include "sdk/services/SettingsChoices.h"
#include "sdk/services/TextInput.h"
#include "tools/PreviewOutput.h"
#include "tools/PreviewServices.h"
#include "tools/Runtime.h"

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

struct WindowDeleter {
    void operator()(HWND window) const {
        DestroyWindow(window);
    }
};

struct TextEvents {
    unsigned changed = 0, submitted = 0, lostFocus = 0;
    std::string text;

    static void CD_CALL receive(void* user, uint32_t event, const char* bytes, uint32_t length) {
        auto& self = *static_cast<TextEvents*>(user);
        self.changed += event == CD_TEXT_CHANGED;
        self.submitted += event == CD_TEXT_SUBMIT;
        self.lostFocus += event == CD_TEXT_FOCUS_LOST;
        self.text.assign(bytes, length);
    }
};
}  // namespace

int main(int argc, char** argv) try {
    check(argc == 2, "fixed fixture directory required");
    const auto root = std::filesystem::absolute(cdsdk::utf8Path(argv[1]));
    std::filesystem::create_directories(root / "assets");
    std::ofstream(root / "assets" / "owned.png") << "controlled path fixture";
    std::ofstream(root / "outside.png") << "controlled external fixture";
    cdsdk::MockHost host;
    host.preview->configure(root, {{"choice", true}, {"text", false}});
    cdsdk::Services services(&host.api, &host);

    CdIconsApi icons{};
    check(services.query(CD_SERVICE_ICONS, icons) == CD_OK, "bundled Lucide service available");
    for (const uint32_t edge : {16u, 24u, 48u}) {
        std::vector<uint8_t> pixels(size_t(edge) * edge * 4);
        const CdFrame frame{sizeof(CdFrame), edge, edge, edge * 4, pixels.size(), pixels.data(), edge / 16.};
        check(icons.raster(&host, "volume-2", 0xff000080, &frame) == CD_OK, "Lucide icon rendered at actual DPI");
        unsigned visible = 0, antialiased = 0;
        for (size_t offset = 0; offset < pixels.size(); offset += 4) {
            const auto alpha = pixels[offset + 3];
            visible += alpha != 0;
            antialiased += alpha != 0 && alpha != 128;
            check(alpha <= 128 && pixels[offset] == alpha && pixels[offset + 1] == 0 && pixels[offset + 2] == 0,
                  "icon color is premultiplied RGBA");
        }
        check(visible && antialiased, "actual Lucide shape and antialiased coverage present");
        check(icons.raster(&host, "../volume-2", 0xffffffff, &frame) == CD_ERROR_ARGUMENT, "icon traversal rejected");
        check(icons.raster(&host, "not-an-icon", 0xffffffff, &frame) == CD_ERROR_UNSUPPORTED, "unknown icon explicit");
    }

    CdImageSkinsApi skins{};
    check(services.query(CD_SERVICE_IMAGE_SKINS, skins) == CD_ERROR_UNSUPPORTED, "skin capability enforced");
    host.capabilities.insert("IMAGE_SKINS");
    check(services.query(CD_SERVICE_IMAGE_SKINS, skins) == CD_OK, "non-brand skin query available");
    std::array<char, 65537> output{};
    uint64_t revision = 999;
    const auto owned = utf8(root / "assets" / "owned.png");
    const auto external = utf8(root / "outside.png");
    check(skins.resolve(&host, owned.c_str(), output.data(), uint32_t(output.size()), &revision) == CD_OK &&
              owned == output.data() && revision == 0,
          "preview resolves original package skin");
    check(
        skins.resolve(&host, external.c_str(), output.data(), uint32_t(output.size()), &revision) == CD_ERROR_ARGUMENT,
        "external source rejected");
    check(skins.resolve(&host, "assets/owned.png", output.data(), uint32_t(output.size()), &revision) ==
              CD_ERROR_ARGUMENT,
          "relative source rejected");
    check(skins.resolve(&host, owned.c_str(), output.data(), 1, &revision) == CD_ERROR_BUFFER,
          "small output buffer rejected");
    uint64_t token = 0;
    check(skins.subscribe(
              &host,
              [](void*, uint64_t) {
              },
              nullptr, &token) == CD_OK,
          "skin subscription accepted");
    skins.unsubscribe(&host, token);
    host.api.unsubscribe(&host, token);

    CdImageViewerApi viewer{};
    check(services.query(CD_SERVICE_IMAGE_VIEWER, viewer) == CD_OK, "viewer command adapter available");
    for (int index = 0; index < 70; ++index) {
        check(viewer.open(&host, owned.data(), uint32_t(owned.size())) == CD_OK, "viewer request recorded");
    }
    check(host.preview->openedImages().size() == 64, "external command history bounded");
    check(viewer.open(&host, "relative.png", 12) == CD_ERROR_ARGUMENT, "relative viewer argument rejected");
    check(viewer.open(&host, "\xff", 1) == CD_ERROR_ARGUMENT, "invalid UTF-8 rejected");
    check(viewer.open(&host, "x\0y", 3) == CD_ERROR_ARGUMENT, "embedded NUL rejected");

    CdRenderImagesApi images{};
    check(services.query(CD_SERVICE_RENDER_IMAGES, images) == CD_OK, "image diagnostics available");
    for (uint64_t source = 1; source <= 256; ++source) {
        images.update(&host, source, 1, 0, 0);
    }
    check(host.preview->imageSourceCount() == 128, "diagnostic sources bounded");
    images.update(&host, 1, 0, 0, 0);
    check(host.preview->imageSourceCount() == 127, "destroyed image source unregistered");

    CdSettingsChoicesApi settings{};
    check(services.query(CD_SERVICE_SETTINGS_CHOICES, settings) == CD_OK, "settings choices available");
    unsigned publications = 0;
    host.preview->settingsChanged([&] {
        ++publications;
    });
    const std::string valid =
        R"({"fields":{"choice":{"choices":[{"value":"auto","label":"Automatic"}],"context":"one"}}})";
    check(settings.publish(&host, valid.data(), uint32_t(valid.size())) == CD_OK, "declared choice updated");
    check(publications == 1, "dynamic editor receives one committed change");
    check(settings.publish(&host, valid.data(), uint32_t(valid.size())) == CD_OK && publications == 1,
          "unchanged choices do not trigger editor refresh");
    cdsdk::pb::SettingsPage page;
    auto* field = page.add_fields();
    field->set_id("choice");
    field->set_type(cdsdk::pb::CHOICE);
    host.preview->applySettingsChoices(page);
    check(field->choices_size() == 1 && field->choices(0).value() == "auto" && field->choices(0).label() == "Automatic",
          "editor consumes published values and labels");
    host.preview->settingsChanged({});
    const auto snapshot = host.preview->settingsChoices();
    const std::array invalid{
        R"({"fields":{"unknown":{"context":"x"}}})", R"({"fields":{"text":{"choices":[{"value":"a","label":"A"}]}}})",
        R"({"fields":{"choice":{"choices":[{"value":"a","label":"A"},{"value":"a","label":"B"}]}}})",
        R"({"fields":{"choice":{"choices":[]}}})", R"({"fields":{"choice":{"unsupported":1}}})"};
    for (const auto* value : invalid) {
        check(settings.publish(&host, value, uint32_t(std::strlen(value))) == CD_ERROR_ARGUMENT,
              "invalid dynamic settings rejected");
        check(host.preview->settingsChoices() == snapshot, "failed publication is atomic");
    }

    cdsdk::PreviewOutput cpuOutput;
    std::array<uint8_t, 64> pixels{};
    CdFrame mainFrame{sizeof(CdFrame), 4, 4, 16, pixels.size(), pixels.data(), 1};
    CdRenderSession session{sizeof(session)};
    check(cpuOutput.configure() == CD_OK, "CPU output opt-in");
    check(cpuOutput.session(&mainFrame, &session) == CD_OK && !session.output_epoch,
          "outside render has no writable output session");
    cpuOutput.begin(mainFrame);
    check(cpuOutput.session(&mainFrame, &session) == CD_OK && session.backend == CD_OUTPUT_CPU && session.output_epoch,
          "CPU render reports actual output allocation");
    const auto epoch = session.output_epoch;
    const auto layoutVersion = session.layout_version;
    CdFrame nested = mainFrame;
    nested.width = 2;
    CdRenderSession local{sizeof(local)};
    check(cpuOutput.session(&nested, &local) == CD_OK && !local.output_epoch,
          "nested CPU frame cannot report main output");
    CdRenderReport report{sizeof(report), 1, CD_OUTPUT_PAINTED, CD_OUTPUT_GPU, epoch, 64};
    check(cpuOutput.complete(&report) == CD_ERROR_ARGUMENT, "CPU output rejects GPU report");
    report.backend = CD_OUTPUT_CPU;
    check(cpuOutput.complete(&report) == CD_OK && cpuOutput.complete(&report) == CD_ERROR_ARGUMENT,
          "render output completes once per active callback");
    cpuOutput.end();
    check(cpuOutput.complete(&report) == CD_ERROR_ARGUMENT, "late output report rejected");
    mainFrame.device_pixel_ratio = 2;
    cpuOutput.begin(mainFrame);
    cpuOutput.session(&mainFrame, &session);
    check(session.output_epoch == epoch && session.layout_version > layoutVersion,
          "DPI changes layout without inventing a new output allocation");
    cpuOutput.end();

    CdTextInputApi input{};
    check(services.query(CD_SERVICE_TEXT_INPUT, input) == CD_OK, "native text input service available");
    TextEvents events;
    check(input.connect(&host, TextEvents::receive, &events) == CD_OK, "text callback connected");
    const CdRect bounds{4, 4, 200, 60};
    check(input.configure(&host, &bounds, "Type here", 9) == CD_OK, "logical editor bounds configured");
    check(input.set_text(&host, "initial", 7) == CD_OK && events.changed == 0, "set_text emits no change");
    check(input.focus(&host, 1) == CD_ERROR_UNSUPPORTED, "headless focus explicitly unavailable");
    std::unique_ptr<HWND__, WindowDeleter> parent(
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"SDK input contract", WS_POPUP, -32000, -32000,
                        300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr));
    check(bool(parent), "native test owner created");
    ShowWindow(parent.get(), SW_SHOWNOACTIVATE);
    host.preview->placement(parent.get(), 1.5);
    check(input.focus(&host, 1) == CD_OK, "native editor focused");
    const auto editor = FindWindowExW(parent.get(), nullptr, L"EDIT", nullptr);
    check(editor != nullptr, "real EDIT child exists");
    RECT editorRect{};
    GetWindowRect(editor, &editorRect);
    check(editorRect.right - editorRect.left == 300 && editorRect.bottom - editorRect.top == 90,
          "editor follows actual DPI");
    SendMessageW(editor, EM_SETSEL, 0, -1);
    SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Japanese 日本語 and Korean 한국어"));
    std::cout << "text changes=" << events.changed << " bytes=" << events.text.size() << "\n";
    check(events.changed >= 1 && !events.text.empty(), "native Unicode edit publishes copied text");
    SendMessageW(editor, WM_KEYDOWN, VK_RETURN, 0);
    check(events.submitted == 1, "Enter submits");
    SendMessageW(editor, WM_IME_STARTCOMPOSITION, 0, 0);
    SendMessageW(editor, WM_KEYDOWN, VK_RETURN, 0);
    check(events.submitted == 1, "IME confirmation does not submit");
    SendMessageW(editor, WM_IME_ENDCOMPOSITION, 0, 0);
    const std::string tooLong(2001, 'a');
    check(input.set_text(&host, tooLong.data(), uint32_t(tooLong.size())) == CD_ERROR_ARGUMENT,
          "code point bound enforced");
    host.preview->pause(true);
    check(!IsWindowVisible(editor) && input.focus(&host, 1) == CD_ERROR_UNSUPPORTED, "pause revokes editor focus");
    host.preview->pause(false);
    check(input.connect(&host, nullptr, nullptr) == CD_OK, "text callback synchronously revoked");
    const auto changes = events.changed;
    SetWindowTextW(editor, L"After disconnect");
    check(events.changed == changes, "disconnected editor cannot call old user context");
    parent.reset();
    check(input.focus(&host, 1) == CD_ERROR_UNSUPPORTED, "destroyed presentation owner unavailable");

    CdRenderMemoryApi memory{};
    std::array<Microsoft::WRL::ComPtr<IUnknown>, 4> leases;
    {
        cdsdk::MockHost temporary;
        cdsdk::Services temporaryServices(&temporary.api, &temporary);
        check(temporaryServices.query(CD_SERVICE_RENDER_MEMORY, memory) == CD_OK, "memory service available");
        for (auto& lease : leases) {
            lease.Attach(static_cast<IUnknown*>(memory.reserve(memory.resource_context, 16 * 1024 * 1024)));
            check(bool(lease), "bounded lease allocated");
        }
    }
    check(!memory.reserve(memory.resource_context, 1), "leases remain charged after host destruction");
    leases[0].Reset();
    Microsoft::WRL::ComPtr<IUnknown> recovered;
    recovered.Attach(static_cast<IUnknown*>(memory.reserve(memory.resource_context, 16 * 1024 * 1024)));
    check(bool(recovered), "release returns budget to process-lifetime allocator");
    check(!memory.reserve(memory.resource_context, 0) && !memory.reserve(nullptr, 1),
          "invalid lease requests rejected");
    check(host.extended.query_service(&host, CD_SERVICE_IMAGE_VIEWER, 2, &viewer, sizeof(viewer)) == CD_ERROR_VERSION,
          "version mismatch rejected");
    check(host.extended.query_service(&host, CD_SERVICE_IMAGE_VIEWER, 1, &viewer, 4) == CD_ERROR_BUFFER,
          "undersized table rejected");
    std::cout << "PASS preview services ownership, capabilities, bounds, atomic publication and lease lifetime\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
