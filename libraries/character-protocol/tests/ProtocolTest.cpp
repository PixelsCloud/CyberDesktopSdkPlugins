#include <CharacterChat/Protocol.h>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void rejects(const std::function<void()>& operation, const char* message) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}
}  // namespace

int main() {
    try {
        const auto state = characterchat::parseBoundedCheckpoint(R"({"draft":"你好","emotion":"happy"})");
        require(state.at("draft") == "你好", "checkpoint preserves UTF-8 content");
        require(characterchat::parseEmotion(state.at("emotion").get<std::string>()) == characterchat::Emotion::Happy,
                "snapshot emotion maps to the shared enum");
        require(!characterchat::parseEmotion("unknown"), "unknown emotions remain unsupported");
        rejects(
            [] {
                characterchat::parseBoundedDocument(R"({"draft":1,"draft":2})");
            },
            "duplicate keys accepted");
        rejects(
            [] {
                characterchat::parseBoundedDocument("{\"draft\":");
            },
            "partial JSON accepted as committed state");
        rejects(
            [] {
                characterchat::parseBoundedDocument(R"({"draft":"abcdef"})", 64, 3);
            },
            "string budget ignored");
        rejects(
            [] {
                characterchat::parseBoundedCheckpoint(std::string(characterchat::kMaximumCheckpointBytes + 1, ' '));
            },
            "checkpoint budget ignored");
        require(characterchat::validIdentifier("cat-01") && !characterchat::validIdentifier("../cat"),
                "identifiers must not escape their namespace");
        require(characterchat::safeRelativeResource("portraits/cat.png") &&
                    !characterchat::safeRelativeResource("../private.png"),
                "resource references must stay inside the package");
        std::cout << "Public protocol: UTF-8, bounded JSON, duplicate keys, emotion and resource contracts PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
