#pragma once
#include <string>
#include <string_view>

namespace cdsdk {
enum class UiLanguage { Simplified, Traditional, English };
extern thread_local UiLanguage currentUiLanguage;

class LanguageScope {
public:
    explicit LanguageScope(std::string_view language);
    ~LanguageScope();
    LanguageScope(const LanguageScope&) = delete;
    LanguageScope& operator=(const LanguageScope&) = delete;

private:
    UiLanguage previous_;
};

// Translates only known interface phrases, never user content.
std::wstring displayText(std::wstring_view source);
const wchar_t* drawingLocale();
}  // namespace cdsdk
