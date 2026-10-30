#pragma once

#include <CyberCanvas/Components.h>
#include <sdk/Diagnostics.h>
#include <sdk/List.h>
#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "libraries/content-model/Text.h"
#include "sdk/cpp/Snapshots.h"

namespace news {
enum class Kind { Reader, Github };

struct Spec {
    const char* id;
    Kind kind;
    int rows, columns;
};

using Item = cdsdk::ContentItem;
using cdcontent::clean;
using cdcontent::resolveUrl;
using cdcontent::webUrl;
using cdcontent::wide;

struct Source {
    const char* id;
    const char* name;
    const char* url;
};

const std::vector<Source>& sources();
int64_t wallSeconds();
std::string compactNumber(int64_t);

struct Query {
    Kind kind = Kind::Reader;
    std::string source, period, language, hnOrder;
    std::vector<std::string> urls;
    std::string key() const;
    std::string address() const;
};

cdsdk::Settings defaults(Kind);
Query query(Kind, const cdsdk::Settings&);
bool chinese(const cdsdk::Settings&);

struct Result {
    std::string key, error;
    std::vector<Item> items;
    int64_t fetched = 0;
    bool valid = false, partial = false;
};

struct Model {
    std::string key, error;
    std::vector<Item> items, saved;
    std::vector<std::string> read;
    int64_t fetched = 0;
    int page = 0;
    cybercanvas::WidgetState widgets;
    bool loading = true, partial = false, savedOnly = false;
    bool light = false;
};

const std::vector<Item>& visible(const Model&);
void accept(Model&, Result);
void toggleSaved(Model&, const Item&);
void markRead(Model&, const std::string&);
std::string stateBytes(const Model&);
bool restoreBytes(Model&, const std::string&);

struct Hit {
    CdRect area;
    int action;
};

struct Geometry {
    double width, height, top, bottom, rowHeight;
    int capacity;
};

Geometry geometry(Spec, CdRect, double dpr = 1);
std::vector<Hit> hitRegions(Spec, const Model&, CdRect, double dpr = 1, bool scrolling = false);

class Renderer {
public:
    Renderer();
    ~Renderer();
    void bindHost(const CdCreateContext&);
    void onInvalidate(std::function<void()> callback);
    void onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback);
    cdsdk::ListView& listView(Spec, const cdsdk::Settings&, const Model&, CdRect safe, double dpr = 1);
    void cancelList();
    void resetList();
    bool advanceList(double seconds);
    bool listActive() const;
    bool render(Spec, const cdsdk::Settings&, const Model&, const CdFrame&, CdRect);
    bool savePng(const std::filesystem::path&, const CdFrame&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);
}  // namespace news
