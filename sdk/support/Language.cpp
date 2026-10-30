#include <sdk/Language.h>
#include <sdk/UiTranslations.h>

#include <algorithm>
#include <iterator>

namespace cdsdk {
thread_local UiLanguage currentUiLanguage = UiLanguage::Simplified;

LanguageScope::LanguageScope(std::string_view language) : previous_(currentUiLanguage) {
    currentUiLanguage = language == "en"      ? UiLanguage::English
                        : language == "zh-TW" ? UiLanguage::Traditional
                                              : UiLanguage::Simplified;
}

LanguageScope::~LanguageScope() {
    currentUiLanguage = previous_;
}

std::wstring displayText(std::wstring_view source) {
    if (currentUiLanguage != UiLanguage::Simplified) {
        const auto found = std::lower_bound(std::begin(kUiPhrases), std::end(kUiPhrases), source,
                                            [](const UiPhrase& phrase, std::wstring_view value) {
                                                return phrase.source < value;
                                            });
        if (found != std::end(kUiPhrases) && found->source == source) {
            return std::wstring(currentUiLanguage == UiLanguage::English ? found->english : found->traditional);
        }
    }
    return std::wstring(source);
}

const wchar_t* drawingLocale() {
    return currentUiLanguage == UiLanguage::English       ? L"en-US"
           : currentUiLanguage == UiLanguage::Traditional ? L"zh-TW"
                                                          : L"zh-CN";
}
}  // namespace cdsdk
