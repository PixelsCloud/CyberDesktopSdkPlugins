#pragma once

#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Interaction.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <wrl/client.h>

#include <array>
#include <string>
#include <vector>

namespace mechanicalsign {
inline constexpr const char* kId = "com.cyberdesktop.sign.mechanical.6x2";
inline constexpr double kWidth = 1200, kHeight = 400;
inline constexpr size_t kSlots = 12;
using Page = std::array<std::wstring, kSlots>;
std::vector<Page> pages(std::string_view text);

enum class Format { Standard, Strip, Compact };

struct Geometry {
    double width = kWidth, height = kHeight;
    bool slim = false;
    cybercanvas::Rect slot(size_t index) const;
    double mechanismY() const;
    double mechanismScale() const;
};

Geometry geometry(Format format, double aspect = 0);
Format formatFor(const CdLayout& layout);

class Model {
public:
    Model();
    void text(std::vector<Page> value, bool animate);
    void request();
    void finish();
    bool advance(double delta);
    bool active() const;
    bool queued() const;
    double progress(size_t index) const;
    double angle() const;
    double power() const;
    double duration() const;
    size_t page() const;
    size_t pageCount() const;
    const Page& before() const;
    const Page& after() const;

private:
    void begin(const Page& target, bool force);
    std::vector<Page> pages_;
    Page before_{}, after_{};
    std::array<double, kSlots> starts_{};
    std::array<bool, kSlots> changed_{};
    size_t page_ = 0;
    double elapsed_ = 0, duration_ = 0, baseAngle_ = 0;
    bool active_ = false, queued_ = false, pendingText_ = false;
};

class Renderer {
public:
    explicit Renderer(const std::filesystem::path& assets = {});
    void bind(const CdCreateContext& context);
    bool poll();
    bool pending() const;
    void pause(bool paused);
    cdsdk::ImageSlotStats assets() const;
    void draw(cybercanvas::Canvas& canvas, const Model& model, bool background, double scale,
              Format format = Format::Standard, double aspect = 0);

private:
    void stationary(cybercanvas::Canvas& canvas, bool background, Geometry geometry);
    void mechanism(cybercanvas::Canvas& canvas, const Model& model, Geometry geometry);
    void tile(cybercanvas::Canvas& canvas, cybercanvas::Rect bounds, const std::wstring& before,
              const std::wstring& after, double progress, const cybercanvas::TextStyle& style);
    cdsdk::Images images_;
    std::filesystem::path assetsRoot_;
    std::shared_ptr<const cybercanvas::ImageData> chassis_;
    cybercanvas::OffscreenSurface backdrop_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> owner_;
    uint32_t width_ = 0, height_ = 0;
    uint64_t revision_ = 0, cachedRevision_ = 0;
    double dpi_ = 0;
    bool background_ = true;
    Format format_ = Format::Standard;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    ~Instance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void input(const CdInput& event);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);

private:
    bool enabled(const cdsdk::Settings& value) const;
    CdResult schedule(const Model& model, const cdsdk::Settings& value);
    void arrange();
    void trigger();
    void invalidate();
    static void CD_CALL wake(void* user, uint64_t now) noexcept;
    Model model_;
    Renderer renderer_;
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::Interaction interaction_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    cdsdk::Timer wake_;
    CdRect safe_{0, 0, 600, 200};
    cybercanvas::Point origin_{};
    double scale_ = .5;
    bool hidden_ = false, arranged_ = false;
    Format format_ = Format::Standard;
    bool layoutKnown_ = false;
};
}  // namespace mechanicalsign
