#include "tools/PreviewChat.h"

#include <CharacterChat/Protocol.h>

#include <QString>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "tools/Runtime.h"

namespace cdsdk {
namespace {
using Json = nlohmann::json;

std::vector<std::string> graphemes(const std::string& bytes) {
    const auto text = QString::fromUtf8(bytes.data(), qsizetype(bytes.size()));
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    std::vector<std::string> result;
    qsizetype previous = 0;
    for (auto end = finder.toNextBoundary(); end >= 0; end = finder.toNextBoundary()) {
        result.push_back(text.mid(previous, end - previous).toUtf8().toStdString());
        previous = end;
    }
    return result;
}
}  // namespace

struct PreviewChat::Impl {
    CdCharacterChatCallback callback = nullptr;
    void* user = nullptr;
    std::function<void()> redraw;
    Json characters = Json::array();
    Json visual = Json::object();
    Json history = Json::array(), provisional = nullptr;
    std::string pack, error, draft, input, scene = "cafe";
    size_t selected = 0;
    uint64_t generation = 0, turns = 0;
    double time = 0, due = 0;
    unsigned phase = 0;
    bool loaded = false, pending = true, story = true, offline = true, live = false;
    MockScenario scenario = MockScenario::Normal;

    Json reply(const std::string& text) const {
        characterchat::Turn turn;
        turn.sceneId = scene;
        characterchat::Beat beat;
        beat.speakerId = characters.at(selected).at("id").get<std::string>();
        beat.text = text;
        beat.emotion = characterchat::Emotion::Happy;
        turn.beats.push_back(std::move(beat));
        turn.choices.push_back({"continue", "继续预览 / Continue preview"});
        return characterchat::turnJson(turn);
    }

    Json snapshot() const {
        const auto id =
            characters.empty() ? std::string("loading") : characters.at(selected).at("id").get<std::string>();
        const bool failed = scenario == MockScenario::Error;
        const bool loading = !loaded || scenario == MockScenario::Loading;
        Json session{{"schema_version", 2},      {"session_id", "sdk-preview-" + id},
                     {"branch_id", "preview"},   {"instance_id", "preview"},
                     {"character_version", 1},   {"state_version", turns},
                     {"character_id", id},       {"scene_id", scene},
                     {"generation", generation}, {"status", phase ? "generating" : "idle"},
                     {"committed_turns", turns}, {"mood_weights", Json::array({1., 0., 0., 0., 0., 0., 0., 0.})},
                     {"media_enabled", false},   {"unlocked_flags", Json::array()}};
        return {{"characters", characters},
                {"selected", selected},
                {"active_character", characters.empty() ? Json::object() : characters.at(selected)},
                {"status", loading ? "loading_assets"
                           : phase ? "generating"
                                   : "idle"},
                {"error", failed ? "Deterministic preview error" : error},
                {"offline", offline},
                {"history", scenario == MockScenario::Empty ? Json::array() : history},
                {"provisional", provisional},
                {"session", session},
                {"draft", draft},
                {"resource_pack", pack},
                {"resource_epoch", loaded ? 1 : 0},
                {"visual", visual},
                {"visual_name", "SDK offline preview"},
                {"renderer_live2d", live},
                {"controls", {{"story", story}, {"gestures", true}}}};
    }

    void changed() {
        pending = true;
        if (redraw) {
            redraw();
        }
    }
};

PreviewChat::PreviewChat(std::function<void()> redraw) : impl_(std::make_unique<Impl>()) {
    impl_->redraw = std::move(redraw);
}

PreviewChat::~PreviewChat() = default;

CdResult PreviewChat::connect(CdCharacterChatCallback callback, void* user) {
    impl_->callback = callback;
    impl_->user = callback ? user : nullptr;
    if (!callback) {
        impl_->phase = 0;
        impl_->provisional = nullptr;
        impl_->pending = false;
        ++impl_->generation;
    } else {
        impl_->changed();
    }
    return CD_OK;
}

CdResult PreviewChat::command(const char* bytes, uint32_t length) try {
    if (!bytes || !length || length > 32768) {
        return CD_ERROR_ARGUMENT;
    }
    const auto document = characterchat::parseBoundedDocument({bytes, length}, 32768, 16000);
    if (!document.is_object() || document.value("schema_version", 0) != 1 || !document.contains("op") ||
        !document["op"].is_string()) {
        return CD_ERROR_ARGUMENT;
    }
    const auto op = document["op"].get<std::string>();
    static const std::map<std::string, std::set<std::string>> fields{{"send", {"text"}},
                                                                     {"draft", {"text"}},
                                                                     {"stop", {}},
                                                                     {"character", {"index"}},
                                                                     {"mode", {"offline"}},
                                                                     {"story", {"enabled"}},
                                                                     {"renderer", {"kind"}},
                                                                     {"manage", {}},
                                                                     {"connection_settings", {}},
                                                                     {"playback", {"identity", "cursor"}},
                                                                     {"choices", {"identity"}},
                                                                     {"open_attachment", {"identity", "ordinal"}}};
    const auto allowed = fields.find(op);
    if (allowed == fields.end()) {
        return CD_ERROR_UNSUPPORTED;
    }
    for (const auto& [key, value] : document.items()) {
        if (key != "schema_version" && key != "op" && !allowed->second.contains(key)) {
            return CD_ERROR_ARGUMENT;
        }
    }
    if (op == "send" || op == "draft") {
        const auto text = document.at("text").get<std::string>();
        if (characterchat::unicodeLength(text) > 2000 || (op == "send" && text.empty())) {
            return CD_ERROR_ARGUMENT;
        }
        if (op == "send") {
            if (!impl_->loaded || impl_->phase) {
                return CD_ERROR_BUSY;
            }
            impl_->input = text;
            impl_->phase = 1;
            impl_->due = impl_->time + .15;
            ++impl_->generation;
            impl_->error.clear();
        }
        impl_->draft = text;
    } else if (op == "stop") {
        impl_->phase = 0;
        impl_->provisional = nullptr;
        ++impl_->generation;
    } else if (op == "character") {
        const auto index = document.at("index").get<size_t>();
        if (index >= impl_->characters.size()) {
            return CD_ERROR_ARGUMENT;
        }
        impl_->selected = index;
        impl_->history.clear();
        impl_->draft.clear();
        impl_->turns = 0;
        impl_->phase = 0;
        impl_->provisional = nullptr;
        ++impl_->generation;
    } else if (op == "story") {
        impl_->story = document.at("enabled").get<bool>();
    } else if (op == "mode") {
        impl_->offline = document.at("offline").get<bool>();
        impl_->error =
            impl_->offline ? "" : "Public preview uses simulated replies; no network or credentials are accessed.";
    } else if (op == "renderer") {
        const auto kind = document.at("kind").get<std::string>();
        if (kind != "image" && kind != "live2d") {
            return CD_ERROR_ARGUMENT;
        }
        if (impl_->live == (kind == "live2d")) {
            return CD_OK;
        }
        impl_->live = kind == "live2d";
    } else if (op == "manage" || op == "connection_settings" || op == "open_attachment") {
        impl_->error = "Preview command recorded: " + op + ". External applications are not opened.";
    } else {
        return CD_OK;
    }
    impl_->changed();
    return CD_OK;
} catch (const std::exception&) {
    return CD_ERROR_ARGUMENT;
}

void PreviewChat::metadata(const std::string& bytes, const std::string& error) {
    if (impl_->loaded) {
        return;
    }
    if (!error.empty()) {
        if (impl_->error != error) {
            impl_->error = error;
            impl_->changed();
        }
        return;
    }
    if (bytes.empty()) {
        return;
    }
    const auto document = characterchat::parseBoundedDocument(bytes, 256 * 1024);
    const auto characters = document.at("characters");
    if (!characters.is_array() || characters.empty() || characters.size() > 32) {
        throw std::runtime_error("Invalid preview character inventory");
    }
    impl_->characters = characters;
    impl_->pack = document.at("pack").get<std::string>();
    Json expressions = Json::array(), emotions = Json::object(), gestures = Json::object(), scenes = Json::object();
    for (const auto& asset : document.at("resources").at("assets")) {
        const auto id = asset.at("id").get<std::string>();
        if (asset.contains("emotion")) {
            emotions[asset["emotion"].get<std::string>()] = id;
            expressions.push_back(id);
        }
        if (id.starts_with("pose_"))
            gestures[id.substr(5)] = id;
        if (id.starts_with("scene_"))
            scenes[id.substr(6)] = id;
    }
    impl_->visual = {{"expressions", expressions}, {"emotions", emotions}, {"gestures", gestures}, {"scenes", scenes}};
    if (document["resources"].contains("portrait_layers")) {
        impl_->visual["portrait_layers"] = document["resources"]["portrait_layers"];
    }
    impl_->loaded = true;
    impl_->changed();
}

void PreviewChat::tick(double seconds, MockScenario scenario, bool paused) {
    impl_->time = seconds;
    if (impl_->scenario != scenario) {
        impl_->scenario = scenario;
        impl_->changed();
    }
    if (paused || !impl_->callback) {
        return;
    }
    if (impl_->phase && seconds >= impl_->due && scenario != MockScenario::Loading) {
        if (scenario == MockScenario::Error) {
            impl_->phase = 0;
            impl_->provisional = nullptr;
        } else if (impl_->phase == 1) {
            impl_->provisional = impl_->reply("这是公开 SDK 的离线预览。");
            impl_->phase = 2;
            impl_->due = seconds + .3;
        } else {
            const std::string text = "这是公开 SDK 的离线预览。你好！こんにちは！안녕하세요! Bonjour! ¡Hola!";
            ++impl_->turns;
            impl_->history.push_back(
                {{"reply", impl_->reply(text)},
                 {"attachments", Json::array()},
                 {"identity",
                  {{"session_id", "sdk-preview"}, {"generation", impl_->generation}, {"turn", impl_->turns}}},
                 {"graphemes", Json::array({graphemes(text)})},
                 {"user_text", impl_->input}});
            if (impl_->history.size() > 16)
                impl_->history.erase(impl_->history.begin());
            impl_->phase = 0;
            impl_->provisional = nullptr;
        }
        impl_->changed();
    }
    if (impl_->pending) {
        impl_->pending = false;
        const auto bytes = impl_->snapshot().dump();
        const auto callback = impl_->callback;
        auto* user = impl_->user;
        // Nothing may access the instance after a callback that can disconnect or destroy it.
        callback(user, bytes.data(), uint32_t(bytes.size()));
    }
}
}  // namespace cdsdk
