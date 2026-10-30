#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace characterchat {

inline constexpr size_t kMaximumReplyBytes = 64 * 1024;
inline constexpr size_t kMaximumBeats = 8;
inline constexpr size_t kMaximumCheckpointBytes = 768 * 1024;

enum class Emotion { Neutral, Happy, Sad, Angry, Shy, Surprised, Worried, Thoughtful };
enum class BeatKind { Dialogue, Narration, MediaRequest };

std::string_view emotionId(Emotion value);
std::optional<Emotion> parseEmotion(std::string_view value);
size_t unicodeLength(std::string_view value);
bool validIdentifier(std::string_view value);
bool validSha256(std::string_view value);
bool validResourcePackId(std::string_view value);
// Lexical gate only. External imports must also resolve their real root and
// reject symlinks/reparse points in the isolated resource worker before opening.
bool safeRelativeResource(std::string_view value);

struct Capabilities {
    std::string characterId;
    std::string currentScene = "cafe";
    std::set<std::string> scenes{"cafe", "home", "beach"};
    std::set<std::string> expressions;
    std::set<std::string> gestures;
    std::map<std::string, std::string> sceneDescriptions;
};

struct MediaRequest {
    std::string requestId;
    std::string collectionId;
    std::string type = "image";
    std::set<std::string> requiredTags;
    unsigned count = 1;
    std::string caption;
};

struct Beat {
    BeatKind kind = BeatKind::Dialogue;
    std::string speakerId;
    std::string text;
    std::optional<Emotion> emotion;
    double intensity = .5;
    std::string expression;
    std::string gesture;
    std::optional<MediaRequest> media;
};

struct Choice {
    std::string id;
    std::string text;
};

struct Turn {
    std::string sceneId;
    std::vector<Beat> beats;
    std::vector<Choice> choices;
    nlohmann::json stateProposals = nlohmann::json::array();
    nlohmann::json preferenceProposals = nlohmann::json::array();
    std::optional<std::string> summaryProposal;
};

struct ValidationResult {
    std::optional<Turn> turn;
    std::string error;
    std::vector<std::string> warnings;
};

// Diagnostic spike: reparses a bounded prefix through the installed JSON SAX parser.
// Completed beats are provisional. It does not commit or execute any of them.
struct PrefixResult {
    size_t completedBeats = 0;
    bool preferencesComplete = false;
    bool completeDocument = false;
    std::string error;
};

ValidationResult parseTurn(std::string_view bytes, const Capabilities& capabilities);
// A provisional view can contain completed dialogue/narration only. It never
// carries choices, attachments, preferences, state proposals or a summary.
std::optional<Turn> provisionalTurn(std::string_view prefix, const Capabilities& capabilities);
PrefixResult inspectPrefix(std::string_view bytes);
nlohmann::json parseBoundedDocument(std::string_view bytes, size_t maximumBytes = kMaximumReplyBytes,
                                    size_t maximumStringBytes = 32768);
nlohmann::json parseBoundedCheckpoint(std::string_view bytes);
nlohmann::json turnJson(const Turn& turn);

struct CharacterProfile {
    std::string id;
    std::string name;
    unsigned age = 0;
    std::string prompt;
    std::string greeting;
};

std::vector<CharacterProfile> parseProfiles(std::string_view bytes);

struct SceneProfile {
    std::string id;
    std::string name;
    std::string relativePath;
    std::string description;
};

struct ResourceAsset {
    std::string id;
    std::string relativePath;
    std::set<std::string> usages;
    std::string type;
    std::string sha256;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t bytes = 0;
    uint64_t durationMilliseconds = 0;
    uint32_t fps = 0;
    std::optional<Emotion> emotion;
};

struct PortraitLayers {
    std::string baseAsset;
    std::map<std::string, std::array<double, 4>> faces;
};

struct ResourceManifest {
    std::string characterId;
    std::vector<ResourceAsset> assets;
    std::optional<PortraitLayers> portraitLayers;
};

std::vector<SceneProfile> parseScenes(std::string_view bytes);
ResourceManifest parseResources(std::string_view bytes);

}  // namespace characterchat
