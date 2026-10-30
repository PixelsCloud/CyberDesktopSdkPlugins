#include <CharacterChat/Protocol.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace characterchat {
namespace {

using Json = nlohmann::json;
constexpr std::array<std::string_view, 8> kEmotions{"neutral", "happy",     "sad",     "angry",
                                                    "shy",     "surprised", "worried", "thoughtful"};

// The JSON library owns all grammar, escapes and UTF-8 decoding. This adapter
// bounds nesting and rejects duplicate keys before a DOM can overwrite them.
class BoundedSax final : public nlohmann::json_sax<Json> {
public:
    explicit BoundedSax(size_t maximumStringBytes = 32768) : maximumStringBytes_(maximumStringBytes) {}

    struct Frame {
        Json value;
        std::string member;
        std::string pendingKey;
        std::set<std::string> keys;
        bool beat = false;
    };

    bool null() override {
        return add(nullptr);
    }

    bool boolean(bool value) override {
        return add(value);
    }

    bool number_integer(number_integer_t value) override {
        return add(value);
    }

    bool number_unsigned(number_unsigned_t value) override {
        return add(value);
    }

    bool number_float(number_float_t value, const string_t&) override {
        return std::isfinite(value) ? add(value) : fail("non_finite_number");
    }

    bool string(string_t& value) override {
        return value.size() <= maximumStringBytes_ ? add(value) : fail("string_limit");
    }

    bool binary(binary_t&) override {
        return fail("binary_not_supported");
    }

    bool start_object(std::size_t) override {
        return begin(Json::object());
    }

    bool start_array(std::size_t) override {
        return begin(Json::array());
    }

    bool key(string_t& value) override {
        if (frames.empty() || value.size() > 128) {
            return fail("key_limit");
        }
        auto& frame = frames.back();
        if (!frame.keys.insert(value).second) {
            return fail("duplicate_key");
        }
        frame.pendingKey = value;
        return true;
    }

    bool end_object() override {
        return end();
    }

    bool end_array() override {
        return end();
    }

    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
        return fail("invalid_json");
    }

    bool preferencesComplete() const {
        const Json* object = root.is_object() ? &root : nullptr;
        if (!frames.empty() && frames.front().value.is_object()) {
            object = &frames.front().value;
        }
        return object && object->contains("preference_proposals") && (*object)["preference_proposals"].is_array();
    }

    Json root;
    std::vector<Frame> frames;
    size_t completedBeats = 0;
    Json completedBeatValues = Json::array();
    std::string error;

private:
    bool fail(const char* message) {
        if (error.empty()) {
            error = message;
        }
        return false;
    }

    bool begin(Json value) {
        if (frames.size() >= 32) {
            return fail("nesting_limit");
        }
        const bool beat = value.is_object() && frames.size() == 2 && frames.back().member == "beats" &&
                          frames.back().value.is_array();
        auto member = frames.empty() ? std::string{} : frames.back().pendingKey;
        frames.push_back({std::move(value), std::move(member), {}, {}, beat});
        return true;
    }

    bool end() {
        if (frames.empty()) {
            return fail("invalid_structure");
        }
        auto frame = std::move(frames.back());
        frames.pop_back();
        if (frame.beat && completedBeatValues.size() < kMaximumBeats) {
            completedBeatValues.push_back(frame.value);
        }
        completedBeats += frame.beat;
        return add(std::move(frame.value));
    }

    bool add(Json value) {
        if (++nodes_ > 32768) {
            return fail("node_limit");
        }
        if (frames.empty()) {
            root = std::move(value);
        } else if (frames.back().value.is_array()) {
            frames.back().value.push_back(std::move(value));
        } else {
            frames.back().value[frames.back().pendingKey] = std::move(value);
        }
        return true;
    }

    size_t nodes_ = 0;
    size_t maximumStringBytes_ = 32768;
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void fields(const Json& object, std::initializer_list<std::string_view> allowed) {
    require(object.is_object(), "expected_object");
    for (const auto& item : object.items()) {
        require(std::find(allowed.begin(), allowed.end(), item.key()) != allowed.end(), "unknown_field");
    }
}

std::string text(const Json& object, const char* key, size_t maximum, bool allowEmpty = false) {
    require(object.contains(key) && object[key].is_string(), "missing_or_invalid_text");
    auto value = object[key].get<std::string>();
    require(value.find('\0') == std::string::npos && unicodeLength(value) <= maximum, "text_limit");
    require(allowEmpty || value.find_first_not_of(" \t\r\n") != std::string::npos, "empty_text");
    return value;
}

std::string identifier(const Json& object, const char* key) {
    auto value = text(object, key, 64);
    require(validIdentifier(value), "invalid_identifier");
    return value;
}

void validateProposals(const Json& states, const Json& preferences) {
    require(states.is_array() && states.size() <= 4, "state_proposal_limit");
    for (const auto& state : states) {
        fields(state, {"event_id"});
        identifier(state, "event_id");
    }
    require(preferences.is_array() && preferences.size() <= 4, "preference_proposal_limit");
    for (const auto& preference : preferences) {
        fields(preference, {"key", "value", "scope", "source_text"});
        const auto key = identifier(preference, "key");
        require(text(preference, "scope", 16) == "conversation", "invalid_preference_scope");
        text(preference, "source_text", 200);
        require(preference.contains("value"), "missing_preference_value");
        if (key == "reply_length") {
            const auto value = text(preference, "value", 16);
            require(value == "short" || value == "natural" || value == "detailed", "invalid_reply_length");
        } else {
            require(key == "gestures" || key == "proactive_media" || key == "media_enabled", "unknown_preference");
            require(preference["value"].is_boolean(), "invalid_preference_value");
        }
    }
}

}  // namespace

std::string_view emotionId(Emotion value) {
    const auto index = static_cast<size_t>(value);
    return index < kEmotions.size() ? kEmotions[index] : kEmotions.front();
}

std::optional<Emotion> parseEmotion(std::string_view value) {
    const auto found = std::find(kEmotions.begin(), kEmotions.end(), value);
    return found == kEmotions.end() ? std::nullopt : std::optional(static_cast<Emotion>(found - kEmotions.begin()));
}

size_t unicodeLength(std::string_view value) {
    // dump() validates UTF-8 using the installed JSON library before counting.
    Json(std::string(value)).dump();
    return std::count_if(value.begin(), value.end(), [](unsigned char byte) {
        return (byte & 0xc0) != 0x80;
    });
}

bool validIdentifier(std::string_view value) {
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
               byte == '_' || byte == '-' || byte == '.';
    });
}

bool safeRelativeResource(std::string_view value) {
    if (value.empty() || value.size() > 1024 || value.front() == '/' || value.front() == '\\' ||
        value.find_first_of(":\0", 0, 2) != std::string_view::npos ||
        value.find_first_of("<>\"|?*") != std::string_view::npos ||
        std::any_of(value.begin(), value.end(), [](unsigned char byte) {
            return byte < 32;
        })) {
        return false;
    }
    std::string normalized(value);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    size_t start = 0;
    while (start <= normalized.size()) {
        const auto end = normalized.find('/', start);
        const auto part = normalized.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') {
            return false;
        }
        auto stem = part.substr(0, part.find('.'));
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char byte) {
            return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte + 'a' - 'A') : static_cast<char>(byte);
        });
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' &&
             stem[3] <= '9')) {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return true;
}

PrefixResult inspectPrefix(std::string_view bytes) {
    if (bytes.size() > kMaximumReplyBytes) {
        return {0, false, false, "reply_limit"};
    }
    BoundedSax sax;
    const bool complete = Json::sax_parse(bytes.begin(), bytes.end(), &sax);
    return {sax.completedBeats, sax.preferencesComplete(), complete, sax.error};
}

Json parseBoundedDocument(std::string_view bytes, size_t maximumBytes, size_t maximumStringBytes) {
    require(maximumBytes <= 256 * 1024 && bytes.size() <= maximumBytes && maximumStringBytes <= 256 * 1024,
            "document_limit");
    BoundedSax sax(maximumStringBytes);
    const bool parsed = Json::sax_parse(bytes.begin(), bytes.end(), &sax);
    require(parsed, sax.error.empty() ? "invalid_json" : sax.error.c_str());
    return std::move(sax.root);
}

Json parseBoundedCheckpoint(std::string_view bytes) {
    require(bytes.size() <= kMaximumCheckpointBytes, "checkpoint_limit");
    BoundedSax sax(kMaximumReplyBytes);
    const auto parsed = Json::sax_parse(bytes.begin(), bytes.end(), &sax);
    require(parsed, sax.error.empty() ? "invalid_json" : sax.error.c_str());
    return std::move(sax.root);
}

ValidationResult parseTurn(std::string_view bytes, const Capabilities& capabilities) {
    ValidationResult result;
    try {
        require(bytes.size() <= kMaximumReplyBytes, "reply_limit");
        BoundedSax sax;
        if (!Json::sax_parse(bytes.begin(), bytes.end(), &sax)) {
            result.error = sax.error;
            return result;
        }
        const auto& root = sax.root;
        fields(root, {"schema_version", "scene_id", "beats", "choices", "state_proposals", "preference_proposals",
                      "memory_summary_proposal"});
        require(root.contains("schema_version") && root["schema_version"].is_number_integer() &&
                    root["schema_version"] == 1,
                "unsupported_version");
        Turn turn;
        turn.sceneId = identifier(root, "scene_id");
        if (!capabilities.scenes.contains(turn.sceneId)) {
            turn.sceneId = capabilities.currentScene;
            result.warnings.push_back("unknown_scene_kept_current");
        }
        require(root.contains("beats") && root["beats"].is_array() && !root["beats"].empty() &&
                    root["beats"].size() <= kMaximumBeats,
                "beat_limit");
        size_t totalText = 0;
        unsigned attachments = 0;
        std::set<std::string> mediaRequestIds;
        for (const auto& node : root["beats"]) {
            Beat beat;
            const auto kind = text(node, "kind", 32);
            beat.speakerId = identifier(node, "speaker_id");
            if (kind == "media_request") {
                fields(node, {"kind", "speaker_id", "request_id", "collection_id", "media_type", "required_tags",
                              "count", "caption"});
                require(beat.speakerId == capabilities.characterId, "unknown_speaker");
                beat.kind = BeatKind::MediaRequest;
                MediaRequest request;
                request.requestId = identifier(node, "request_id");
                require(mediaRequestIds.insert(request.requestId).second, "duplicate_media_request");
                request.collectionId = identifier(node, "collection_id");
                request.type = text(node, "media_type", 8);
                require(request.type == "image" || request.type == "video", "invalid_media_type");
                require(node.contains("count") && node["count"].is_number_integer() && node["count"] >= 1 &&
                            node["count"] <= 2,
                        "attachment_limit");
                request.count = node["count"].get<unsigned>();
                attachments += request.count;
                require(attachments <= 2, "attachment_limit");
                require(node.contains("required_tags") && node["required_tags"].is_array() &&
                            node["required_tags"].size() <= 16,
                        "tag_limit");
                for (const auto& tag : node["required_tags"]) {
                    require(tag.is_string() && validIdentifier(tag.get_ref<const std::string&>()), "invalid_tag");
                    require(request.requiredTags.insert(tag.get<std::string>()).second, "duplicate_tag");
                }
                request.caption = text(node, "caption", 120, true);
                totalText += unicodeLength(request.caption);
                beat.media = std::move(request);
            } else {
                fields(node, {"kind", "speaker_id", "text", "emotion", "intensity", "expression", "gesture"});
                require(kind == "dialogue" || kind == "narration", "unknown_beat_kind");
                beat.kind = kind == "dialogue" ? BeatKind::Dialogue : BeatKind::Narration;
                require(beat.speakerId == (kind == "narration" ? "narrator" : capabilities.characterId),
                        "unknown_speaker");
                beat.text = text(node, "text", 300);
                totalText += unicodeLength(beat.text);
                if (node.contains("emotion")) {
                    beat.emotion = parseEmotion(text(node, "emotion", 16));
                    require(beat.emotion.has_value(), "unknown_emotion");
                }
                if (node.contains("intensity")) {
                    require(node["intensity"].is_number(), "invalid_intensity");
                    beat.intensity = node["intensity"].get<double>();
                    require(std::isfinite(beat.intensity) && beat.intensity >= 0 && beat.intensity <= 1,
                            "invalid_intensity");
                }
                if (node.contains("expression")) {
                    beat.expression = identifier(node, "expression");
                    if (!capabilities.expressions.contains(beat.expression)) {
                        beat.expression = "neutral";
                        result.warnings.push_back("unknown_expression_neutral");
                    }
                }
                if (node.contains("gesture")) {
                    beat.gesture = identifier(node, "gesture");
                    if (beat.gesture == "none") {
                        beat.gesture.clear();
                    } else if (!capabilities.gestures.contains(beat.gesture)) {
                        beat.gesture.clear();
                        result.warnings.push_back("unknown_gesture_idle");
                    }
                }
            }
            turn.beats.push_back(std::move(beat));
        }
        require((totalText > 0 || attachments > 0) && totalText <= 2000, "total_text_limit");
        require(root.contains("choices") && root["choices"].is_array() && root["choices"].size() <= 4, "choice_limit");
        std::set<std::string> choiceIds;
        for (const auto& node : root["choices"]) {
            fields(node, {"id", "text"});
            Choice choice{identifier(node, "id"), text(node, "text", 60)};
            require(choiceIds.insert(choice.id).second, "duplicate_choice");
            turn.choices.push_back(std::move(choice));
        }
        require(root.contains("state_proposals") && root.contains("preference_proposals"), "missing_proposals");
        validateProposals(root["state_proposals"], root["preference_proposals"]);
        turn.stateProposals = root["state_proposals"];
        turn.preferenceProposals = root["preference_proposals"];
        if (root.contains("memory_summary_proposal")) {
            turn.summaryProposal = text(root, "memory_summary_proposal", 600, true);
        }
        result.turn = std::move(turn);
    } catch (const std::invalid_argument& error) {
        result.error = error.what();
    } catch (const std::exception&) {
        // Public errors describe the protocol class, never echo response content.
        result.error = "invalid_story_protocol";
    }
    return result;
}

std::optional<Turn> provisionalTurn(std::string_view bytes, const Capabilities& capabilities) {
    if (bytes.empty() || bytes.size() > kMaximumReplyBytes) {
        return std::nullopt;
    }
    BoundedSax parser;
    Json::sax_parse(bytes.begin(), bytes.end(), &parser);
    if (!parser.error.empty() && parser.error != "invalid_json") {
        return std::nullopt;
    }
    Json beats = Json::array();
    for (const auto& value : parser.completedBeatValues) {
        if (value.value("kind", "") == "dialogue" || value.value("kind", "") == "narration") {
            beats.push_back(value);
        }
    }
    if (beats.empty()) {
        return std::nullopt;
    }
    Json view{{"schema_version", 1},
              {"scene_id", capabilities.currentScene},
              {"state_proposals", Json::array()},
              {"preference_proposals", Json::array()},
              {"choices", Json::array()},
              {"beats", std::move(beats)}};
    return parseTurn(view.dump(), capabilities).turn;
}

Json turnJson(const Turn& turn) {
    Json root{{"schema_version", 1},
              {"scene_id", turn.sceneId},
              {"state_proposals", turn.stateProposals},
              {"preference_proposals", turn.preferenceProposals},
              {"beats", Json::array()},
              {"choices", Json::array()}};
    for (const auto& beat : turn.beats) {
        Json value{{"speaker_id", beat.speakerId}};
        if (beat.media) {
            const auto& media = *beat.media;
            value.update({{"kind", "media_request"},
                          {"request_id", media.requestId},
                          {"collection_id", media.collectionId},
                          {"media_type", media.type},
                          {"required_tags", media.requiredTags},
                          {"count", media.count},
                          {"caption", media.caption}});
        } else {
            value["kind"] = beat.kind == BeatKind::Narration ? "narration" : "dialogue";
            value["text"] = beat.text;
            if (beat.emotion) {
                value["emotion"] = emotionId(*beat.emotion);
                value["intensity"] = beat.intensity;
            }
            if (!beat.expression.empty()) {
                value["expression"] = beat.expression;
            }
            if (!beat.gesture.empty()) {
                value["gesture"] = beat.gesture;
            }
        }
        root["beats"].push_back(std::move(value));
    }
    for (const auto& choice : turn.choices) {
        root["choices"].push_back({{"id", choice.id}, {"text", choice.text}});
    }
    if (turn.summaryProposal) {
        root["memory_summary_proposal"] = *turn.summaryProposal;
    }
    return root;
}

std::vector<CharacterProfile> parseProfiles(std::string_view bytes) {
    require(bytes.size() <= kMaximumReplyBytes, "profile_limit");
    const auto root = parseBoundedDocument(bytes);
    fields(root, {"schema_version", "characters"});
    require(
        root.contains("schema_version") && root["schema_version"].is_number_integer() && root["schema_version"] == 1,
        "unsupported_profile_version");
    require(root.contains("characters") && root["characters"].is_array() && !root["characters"].empty() &&
                root["characters"].size() <= 32,
            "profile_limit");
    std::vector<CharacterProfile> profiles;
    std::set<std::string> ids;
    for (const auto& node : root["characters"]) {
        fields(node, {"id", "name", "age", "prompt", "greeting"});
        require(node.contains("age") && node["age"].is_number_integer() && node["age"] >= 18 && node["age"] <= 120,
                "invalid_adult_age");
        CharacterProfile profile{identifier(node, "id"), text(node, "name", 40), node["age"].get<unsigned>(),
                                 text(node, "prompt", 8000), text(node, "greeting", 300)};
        require(ids.insert(profile.id).second, "duplicate_profile");
        profiles.push_back(std::move(profile));
    }
    return profiles;
}

std::vector<SceneProfile> parseScenes(std::string_view bytes) {
    const auto root = parseBoundedDocument(bytes);
    fields(root, {"schema_version", "scenes"});
    require(
        root.contains("schema_version") && root["schema_version"].is_number_integer() && root["schema_version"] == 1,
        "unsupported_scene_version");
    require(
        root.contains("scenes") && root["scenes"].is_array() && !root["scenes"].empty() && root["scenes"].size() <= 32,
        "scene_limit");
    std::vector<SceneProfile> scenes;
    std::set<std::string> ids;
    for (const auto& node : root["scenes"]) {
        fields(node, {"id", "name", "file", "description"});
        SceneProfile scene{identifier(node, "id"), text(node, "name", 40), text(node, "file", 1024),
                           text(node, "description", 500)};
        require(ids.insert(scene.id).second && safeRelativeResource(scene.relativePath), "invalid_scene_resource");
        scenes.push_back(std::move(scene));
    }
    return scenes;
}

bool validSha256(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char byte) {
               return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
           });
}

bool validResourcePackId(std::string_view value) {
    return value == "builtin-xiaqing-v1" || (value.starts_with("pack-") && validSha256(value.substr(5)));
}

ResourceManifest parseResources(std::string_view bytes) {
    const auto root = parseBoundedDocument(bytes, 256 * 1024);
    fields(root, {"schema_version", "character_id", "assets", "portrait_layers"});
    require(
        root.contains("schema_version") && root["schema_version"].is_number_integer() && root["schema_version"] == 1,
        "unsupported_resource_version");
    ResourceManifest manifest;
    manifest.characterId = identifier(root, "character_id");
    require(root.contains("assets") && root["assets"].is_array() && root["assets"].size() <= 256, "resource_limit");
    std::set<std::string> ids;
    std::set<std::string> paths;
    uint64_t totalBytes = 0;
    bool hasVideo = false;
    for (const auto& node : root["assets"]) {
        fields(node,
               {"id", "file", "usages", "type", "sha256", "width", "height", "bytes", "emotion", "duration_ms", "fps"});
        ResourceAsset asset;
        asset.id = identifier(node, "id");
        asset.relativePath = text(node, "file", 1024);
        require(safeRelativeResource(asset.relativePath), "unsafe_resource_path");
        auto normalized = asset.relativePath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char byte) {
            return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte + 'a' - 'A') : static_cast<char>(byte);
        });
        require(ids.insert(asset.id).second && paths.insert(normalized).second, "duplicate_resource");
        require(node.contains("usages") && node["usages"].is_array() && !node["usages"].empty() &&
                    node["usages"].size() <= 4,
                "invalid_resource_usage");
        for (const auto& usage : node["usages"]) {
            require(usage.is_string(), "invalid_resource_usage");
            const auto value = usage.get<std::string>();
            require(value == "expression" || value == "pose" || value == "background" || value == "media",
                    "invalid_resource_usage");
            require(asset.usages.insert(value).second, "duplicate_resource_usage");
        }
        asset.type = text(node, "type", 8);
        require(asset.type == "image" || asset.type == "video", "invalid_resource_type");
        asset.sha256 = text(node, "sha256", 64);
        require(asset.sha256.size() == 64 && std::all_of(asset.sha256.begin(), asset.sha256.end(),
                                                         [](char value) {
                                                             return (value >= '0' && value <= '9') ||
                                                                    (value >= 'a' && value <= 'f');
                                                         }),
                "invalid_resource_hash");
        for (const auto* key : {"width", "height"}) {
            require(node.contains(key) && node[key].is_number_integer() && node[key] >= 1 && node[key] <= 4096,
                    "invalid_resource_dimensions");
        }
        asset.width = node["width"].get<uint32_t>();
        asset.height = node["height"].get<uint32_t>();
        require(node.contains("bytes") && node["bytes"].is_number_integer() && node["bytes"] >= 1 &&
                    node["bytes"] <= (asset.type == "image" ? 32 : 128) * 1024 * 1024,
                "invalid_resource_size");
        asset.bytes = node["bytes"].get<uint64_t>();
        totalBytes += asset.bytes;
        hasVideo |= asset.type == "video";
        if (asset.type == "video") {
            require(asset.usages == std::set<std::string>{"media"} && std::max(asset.width, asset.height) <= 1920 &&
                        std::min(asset.width, asset.height) <= 1080,
                    "unsupported_video_usage_or_dimensions");
            require(node.contains("duration_ms") && node["duration_ms"].is_number_integer() &&
                        node["duration_ms"] >= 1 && node["duration_ms"] <= 120000 && node.contains("fps") &&
                        node["fps"].is_number_integer() && node["fps"] >= 1 && node["fps"] <= 30,
                    "invalid_video_limits");
            asset.durationMilliseconds = node["duration_ms"].get<uint64_t>();
            asset.fps = node["fps"].get<uint32_t>();
        } else {
            require(!node.contains("duration_ms") && !node.contains("fps"), "image_has_video_fields");
        }
        if (node.contains("emotion")) {
            asset.emotion = parseEmotion(text(node, "emotion", 16));
            require(asset.emotion.has_value() && asset.usages.contains("expression") && asset.type == "image",
                    "invalid_resource_emotion");
        }
        manifest.assets.push_back(std::move(asset));
    }
    require(totalBytes <= (hasVideo ? uint64_t{1024} : uint64_t{256}) * 1024 * 1024, "resource_package_limit");
    if (root.contains("portrait_layers")) {
        const auto& value = root.at("portrait_layers");
        fields(value, {"base_asset", "faces"});
        PortraitLayers layers;
        layers.baseAsset = identifier(value, "base_asset");
        const auto find = [&](const std::string& id) {
            return std::find_if(manifest.assets.begin(), manifest.assets.end(), [&](const auto& asset) {
                return asset.id == id;
            });
        };
        const auto base = find(layers.baseAsset);
        require(base != manifest.assets.end() && base->type == "image" && base->usages.contains("pose"),
                "invalid_portrait_base");
        for (const auto& pose : manifest.assets) {
            if (pose.usages.contains("pose") && (pose.id == "pose_wave" || pose.id == "pose_folded")) {
                require(pose.width == base->width && pose.height == base->height, "invalid_portrait_pose_canvas");
            }
        }
        require(value.contains("faces") && value.at("faces").is_object() && !value.at("faces").empty() &&
                    value.at("faces").size() <= 8,
                "invalid_portrait_layers");
        for (const auto& [id, rect] : value.at("faces").items()) {
            const auto face = find(id);
            require(face != manifest.assets.end() && face != base && face->type == "image" &&
                        face->usages.contains("expression") && rect.is_array() && rect.size() == 4,
                    "invalid_portrait_face");
            for (const auto& coordinate : rect) {
                require(coordinate.is_number() && std::isfinite(coordinate.get<double>()), "invalid_portrait_anchor");
            }
            const auto anchor = rect.get<std::array<double, 4>>();
            require(anchor[0] >= 0 && anchor[1] >= 0 && anchor[2] > 0 && anchor[3] > 0 && anchor[0] + anchor[2] <= 1 &&
                        anchor[1] + anchor[3] <= 1,
                    "invalid_portrait_anchor");
            layers.faces.emplace(id, anchor);
        }
        manifest.portraitLayers = std::move(layers);
    }
    return manifest;
}

}  // namespace characterchat
