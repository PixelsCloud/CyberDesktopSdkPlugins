#include <CharacterChat/Protocol.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "sdk/cpp/Services.h"
#include "sdk/services/CharacterChat.h"
#include "sdk/services/CharacterImages.h"
#include "sdk/services/Live2D.h"
#include "tools/PreviewImages.h"
#include "tools/PreviewServices.h"
#include "tools/Runtime.h"

namespace {
void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Observer {
    nlohmann::json snapshot;
    unsigned calls = 0;
    std::reference_wrapper<cdsdk::MockHost> host;

    explicit Observer(cdsdk::MockHost& owner) : host(owner) {}

    CdCharacterChatApi api{};
    bool disconnect = false;

    static void CD_CALL changed(void* user, const char* bytes, uint32_t length) {
        auto& self = *static_cast<Observer*>(user);
        self.snapshot = characterchat::parseBoundedDocument({bytes, length}, 256 * 1024);
        ++self.calls;
        if (self.disconnect) {
            self.api.connect(&self.host.get(), nullptr, nullptr);
        }
    }
};

void command(cdsdk::MockHost& host, const CdCharacterChatApi& api, const nlohmann::json& value) {
    const auto bytes = value.dump();
    check(api.command(&host, bytes.data(), uint32_t(bytes.size())) == CD_OK, "preview command rejected");
}
}  // namespace

int main(int argc, char** argv) try {
    check(argc == 2, "character package directory required");
    const auto root = std::filesystem::absolute(cdsdk::utf8Path(argv[1]));
    cdsdk::MockHost host;
    host.preview->configure(root, {});
    cdsdk::Services services(&host.api, &host);
    CdCharacterChatApi chat{};
    CdCharacterImagesApi images{};
    CdCharacterVersionedImagesApi versioned{};
    check(services.query(CD_SERVICE_CHARACTER_CHAT, chat) == CD_OK, "chat service missing");
    check(services.query(CD_SERVICE_CHARACTER_IMAGES, images) == CD_OK, "image service missing");
    check(services.query(CD_SERVICE_CHARACTER_VERSIONED_IMAGES, versioned) == CD_OK, "versioned service missing");
    Observer observer(host);
    observer.api = chat;
    check(chat.connect(&host, Observer::changed, &observer) == CD_OK, "connect failed");
    const auto start = std::chrono::steady_clock::now();
    CdCharacterImage image{sizeof(image)};
    do {
        host.tick(0);
        check(images.request(&host, "neutral", 256, &image) == CD_OK, "image request failed");
        if (image.state == CD_CHARACTER_IMAGE_FAILED)
            throw std::runtime_error(image.error);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while ((image.state != CD_CHARACTER_IMAGE_READY || observer.snapshot.value("status", "") != "idle") &&
             std::chrono::steady_clock::now() - start < std::chrono::seconds(15));
    check(image.state == CD_CHARACTER_IMAGE_READY && image.bgra && image.width <= 256 && image.height <= 256,
          "actual bounded character pixels did not become ready");
    const auto pack = observer.snapshot.at("resource_pack").get<std::string>();
    check(versioned.request(&host, pack.c_str(), "neutral", 256, &image) == CD_OK &&
              image.state == CD_CHARACTER_IMAGE_READY,
          "versioned image did not share decoded result");
    check(versioned.request(&host, "pack-missing", "neutral", 256, &image) == CD_OK &&
              image.state == CD_CHARACTER_IMAGE_FAILED,
          "stale resource revision accepted");
    check(images.request(&host, "../outside", 256, &image) == CD_ERROR_ARGUMENT, "asset path accepted as ID");
    check(images.request(&host, "neutral", 4096, &image) == CD_ERROR_ARGUMENT, "image bound ignored");
    check(chat.command(&host, "{", 1) == CD_ERROR_ARGUMENT, "malformed command accepted");
    command(host, chat, {{"schema_version", 1}, {"op", "send"}, {"text", "你好 日本語 한국어 Français Español"}});
    host.tick(.2);
    check(observer.snapshot.at("status") == "generating" && observer.snapshot.at("provisional").is_object(),
          "deterministic provisional stream missing");
    host.tick(.7);
    check(observer.snapshot.at("status") == "idle" && observer.snapshot.at("history").size() == 1,
          "stream did not commit exactly once");
    const auto calls = observer.calls;
    for (unsigned count = 0; count < 60; ++count)
        host.tick(1 + count / 60.);
    check(observer.calls == calls, "idle preview emits periodic snapshots");
    command(host, chat, {{"schema_version", 1}, {"op", "send"}, {"text", "cancel this reply"}});
    command(host, chat, {{"schema_version", 1}, {"op", "stop"}});
    host.tick(3);
    check(observer.snapshot.at("history").size() == 1 && observer.snapshot.at("provisional").is_null(),
          "cancelled generation committed a late reply");
    observer.disconnect = true;
    command(host, chat, {{"schema_version", 1}, {"op", "story"}, {"enabled", false}});
    host.tick(4);
    const auto disconnectedCalls = observer.calls;
    host.tick(5);
    check(observer.calls == disconnectedCalls, "callback self-cancellation was not synchronous");
    host.scenario = cdsdk::MockScenario::Error;
    observer.disconnect = false;
    chat.connect(&host, Observer::changed, &observer);
    host.tick(6);
    check(observer.snapshot.at("error") == "Deterministic preview error", "error scenario missing");
    chat.connect(&host, nullptr, nullptr);

    cdsdk::pb::PluginDescriptor descriptor;
    cdsdk::Error error;
    check(cdsdk::loadDirectoryDescriptor(root, &descriptor, &error), "character descriptor unavailable");
    cdsdk::NativeModule module;
    check(module.open(root, descriptor, &error), "character module unavailable");
    cdsdk::NativeInstance instance;
    check(instance.create(module, root, "composition-test", &error), "character instance unavailable");
    CdCell cell{0, 0};
    CdLayout layout{sizeof(layout),     0, 0, 1, &cell, 768, 512, 1, {0, 0, 768, 512}, {235, 240, 245, 255},
                    {70, 200, 170, 255}};
    instance.api.layout_changed(instance.api.instance, &layout);
    CdSettingValue renderer{sizeof(renderer), CD_SETTING_CHOICE, "renderer", 0, 0, "mao"};
    check(instance.api.apply_settings(instance.api.instance, &renderer, 1) == CD_OK,
          "Live2D renderer selection failed");
    cdsdk::Services instanceServices(&instance.host.api, &instance.host);
    CdLive2DApi live{};
    check(instanceServices.query(CD_SERVICE_LIVE2D, live) == CD_OK, "composition model service missing");
    CdLive2DStatus status{sizeof(status)};
    std::vector<uint8_t> composite;
    const auto compositionStart = std::chrono::steady_clock::now();
    do {
        instance.host.tick(std::chrono::duration<double>(std::chrono::steady_clock::now() - compositionStart).count());
        check(instance.render(768, 512, 1, &composite, &error), "layered render failed");
        live.snapshot(&instance.host, &status);
        if (status.state == 3)
            throw std::runtime_error(status.message);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    } while ((status.state != 2 || status.frames < 3) &&
             std::chrono::steady_clock::now() - compositionStart < std::chrono::seconds(15));
    if (status.state != 2 || status.frames < 3) {
        CdSettingValue selected{};
        instance.api.get_setting(instance.api.instance, "renderer", &selected, sizeof(selected));
        throw std::runtime_error("layered preview state=" + std::to_string(status.state) +
                                 " frames=" + std::to_string(status.frames) +
                                 " renderer=" + (selected.text ? selected.text : "") + " " + status.message);
    }
    const auto bottom = (size_t(470) * 768 + 500) * 4;
    check(composite[bottom + 3] == 255 && composite[bottom + 2] > composite[bottom],
          "model rendering erased the chat input foreground");
    cdsdk::PreviewImages capture;
    capture.save(
        std::filesystem::path(argv[1]).parent_path().parent_path().parent_path() / "character-live2d-preview.png", 768,
        512, composite);

    auto doomed = std::make_unique<cdsdk::MockHost>();
    doomed->preview->configure(root, {});
    cdsdk::Services doomedServices(&doomed->api, doomed.get());
    CdCharacterChatApi doomedChat{};
    check(doomedServices.query(CD_SERVICE_CHARACTER_CHAT, doomedChat) == CD_OK, "destruction fixture missing service");
    doomedChat.connect(
        doomed.get(),
        [](void* user, const char*, uint32_t) {
            static_cast<std::unique_ptr<cdsdk::MockHost>*>(user)->reset();
        },
        &doomed);
    doomed->tick(0);
    check(!doomed, "host was not destroyed inside callback");
    std::cout << "PASS real isolated images, revisions, UTF-8 streaming, idle, cancellation and callback destruction\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
