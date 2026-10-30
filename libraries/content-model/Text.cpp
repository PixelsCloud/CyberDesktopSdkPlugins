#include "libraries/content-model/Text.h"

#include <Windows.h>
#include <curl/curl.h>

#include <cstring>
#include <memory>
#include <stdexcept>

namespace cdcontent {
std::wstring wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    if (value.size() > 65536) {
        throw std::runtime_error("text_limit");
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!count) {
        throw std::runtime_error("invalid_utf8");
    }
    std::wstring result(count, L' ');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), count);
    return result;
}

std::string clean(const std::string& value, size_t limit) {
    std::string out;
    bool space = false;
    for (const auto byte : value) {
        const auto ch = static_cast<unsigned char>(byte);
        if (ch <= 32 || ch == 127) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        out += byte;
        if (out.size() >= limit) {
            break;
        }
    }
    // A byte limit must not split the last UTF-8 character.
    if (out.size() < value.size() && !out.empty()) {
        size_t start = out.size() - 1;
        while (start && (static_cast<unsigned char>(out[start]) & 0xc0) == 0x80) {
            --start;
        }
        const auto lead = static_cast<unsigned char>(out[start]);
        const size_t length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (out.size() - start < length) {
            out.resize(start);
        }
    }
    wide(out);
    return out;
}

bool webUrl(const std::string& value) {
    if (value.empty() || value.size() > 2048 || value.find_first_of("\r\n\t ") != std::string::npos) {
        return false;
    }
    for (unsigned char ch : value) {
        if (ch < 32 || ch == 127) {
            return false;
        }
    }
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> uri(curl_url(), curl_url_cleanup);
    if (!uri || curl_url_set(uri.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) {
        return false;
    }
    char *scheme = nullptr, *host = nullptr, *user = nullptr;
    curl_url_get(uri.get(), CURLUPART_SCHEME, &scheme, 0);
    curl_url_get(uri.get(), CURLUPART_HOST, &host, 0);
    curl_url_get(uri.get(), CURLUPART_USER, &user, 0);
    const bool valid =
        scheme && host && *host && !user && (!std::strcmp(scheme, "https") || !std::strcmp(scheme, "http"));
    curl_free(scheme);
    curl_free(host);
    curl_free(user);
    return valid;
}

std::string resolveUrl(const std::string& base, const std::string& relative) {
    if (relative.empty() || !webUrl(base)) {
        return {};
    }
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> uri(curl_url(), curl_url_cleanup);
    if (!uri || curl_url_set(uri.get(), CURLUPART_URL, base.c_str(), 0) != CURLUE_OK ||
        curl_url_set(uri.get(), CURLUPART_URL, relative.c_str(), 0) != CURLUE_OK) {
        return {};
    }
    char* text = nullptr;
    curl_url_get(uri.get(), CURLUPART_URL, &text, 0);
    std::string result = text ? text : "";
    curl_free(text);
    return webUrl(result) ? result : "";
}

}  // namespace cdcontent
