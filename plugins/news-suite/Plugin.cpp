#include "plugins/news-suite/News.h"

namespace {
CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return news::create({CD_NEWS_ID, news::Kind(CD_NEWS_KIND), CD_NEWS_ROWS, 4}, context, out, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* out, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!out || size < sizeof(*out)) {
        return CD_ERROR_BUFFER;
    }
    *out = {sizeof(*out), CD_PLUGIN_ABI_VERSION, CD_NEWS_ID, CD_NEWS_VERSION, create};
    return CD_OK;
}
