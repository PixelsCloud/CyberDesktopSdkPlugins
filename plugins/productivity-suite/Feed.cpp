#include "plugins/productivity-suite/Feed.h"

#include <algorithm>

#include "plugins/productivity-suite/Productivity.h"
#include "sdk/cpp/Snapshots.h"

namespace productivity {
bool applyFeed(Data& data, const cdsdk::ContentSnapshot& value) {
    const bool loading = value.status == CD_DATA_LOADING;
    const bool stale = value.status == CD_DATA_STALE || (!value.valid() && data.valid && !loading);
    bool changed = data.loading != loading || data.stale != stale || data.error != value.errorCode;
    data.loading = loading;
    data.stale = stale;
    data.error = value.errorCode;
    if (value.valid()) {
        std::vector<Article> articles;
        articles.reserve(value.items.size());
        for (const auto& item : value.items) {
            articles.push_back({item.title, item.url, item.date});
        }
        changed |= !data.valid || data.articles != articles || data.source != value.title;
        const auto anchor =
            data.scroll >= 0 && size_t(data.scroll) < data.articles.size() ? data.articles[data.scroll].url : "";
        data.valid = true;
        data.source = value.title;
        data.articles = std::move(articles);
        data.scroll = std::clamp(data.scroll, 0, std::max(0, int(data.articles.size()) - 1));
        const auto same = std::find_if(data.articles.begin(), data.articles.end(), [&](const auto& item) {
            return item.url == anchor;
        });
        if (same != data.articles.end()) {
            data.scroll = int(same - data.articles.begin());
        }
    }
    return changed;
}
}  // namespace productivity
