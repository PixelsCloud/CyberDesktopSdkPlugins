#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Icons.h>
#include <sdk/Images.h>
#include <sdk/Scene.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/cpp/Plugin.h>

#include <iomanip>
#include <sstream>

#include "templates/components/Card.h"

namespace {
class Workbench {
public:
    cdsdk::Settings settings{
        {"animate", CD_SETTING_TOGGLE, 0},
        {"level", CD_SETTING_NUMBER, 0, .45, {}, 0, 1},
        {"language", CD_SETTING_TEXT, 0, 0, "auto", 0, 31},
        {"data_source", CD_SETTING_CHOICE, 0, 0, "host", 0, 0, {"host", "demo"}},
        {"page", CD_SETTING_CHOICE, 0, 0, "canvas", 0, 0, {"canvas", "info", "tasks", "media", "calendar"}}};

    explicit Workbench(const CdCreateContext& context) {
        context_ = context;
        std::memcpy(&services_, context.host, std::min<size_t>(context.host->struct_size, sizeof(services_)));
        services_.input.media.base.struct_size =
            uint32_t(std::min<size_t>(context.host->struct_size, sizeof(services_)));
        context_.host = &services_.input.media.base;
        using namespace cybercanvas;
        environment_.bind(context);
        updates_.bind(context);
        clockTimer_.bind(context);
        redraw_.bind(context);
        updates_.onInvalidate([this] {
            invalidate();
        });
        images_.bind(context);
        icons_.bind(context);
        scene_.bind(context);
        View title;
        title.kind = ViewKind::Text;
        title.layout.height = Length::points(28);
        title.layout.shrink = 0;
        title.theme.fontSize = 20;
        title_ = scene_.add(0, title);
        View art;
        art.kind = ViewKind::Image;
        art.layout.grow = 1;
        art.layout.minimum.height = 16;
        art.accessibleName = L"Resource illustration";
        image_ = scene_.add(0, art);
        View value;
        value.kind = ViewKind::Text;
        value.layout.height = Length::points(23);
        value.layout.shrink = 0;
        count_ = scene_.add(0, value);
        clock_ = scene_.add(0, value);
        View row;
        row.kind = ViewKind::Row;
        row.layout.height = Length::points(34);
        row.layout.shrink = 0;
        const auto buttons = scene_.add(0, row);
        View button;
        button.kind = ViewKind::Button;
        button.key = 1;
        button.text = L"Add";
        button.accessibleName = L"Add one";
        button.icon = "plus";
        button.layout.grow = 1;
        button.appearance = ButtonStyle::Filled;
        add_ = scene_.add(buttons, button);
        button.key = 2;
        button.text = L"Reset";
        button.accessibleName = L"Reset count";
        button.icon = "rotate-ccw";
        button.appearance = ButtonStyle::Outline;
        reset_ = scene_.add(buttons, button);
        View slider;
        slider.kind = ViewKind::Slider;
        slider.key = 3;
        slider.minimum = 0;
        slider.maximum = 1;
        slider.step = .05;
        slider.accessibleName = L"Level";
        slider.layout.height = Length::points(26);
        slider.layout.shrink = 0;
        level_ = scene_.add(0, slider);
        scene_.onInvalidate([this] {
            invalidate();
        });
        scene_.onAction([this](SceneEvent event) {
            action(event);
        });
        images_.onChange([this] {
            scene_.imageResource(image_, images_.get("art"));
        });
        scene_.diagnostics().extras([this](cdsdk::InstanceStats& stats) {
            stats.images = images_.stats();
            stats.sharedImages = images_.sharedStoreStats();
            stats.redraw = redraw_.stats();
        });
        images_.set("art", std::filesystem::u8path(context.plugin_directory) / "art.png", 0, 512);
        updates_.onUpdate([this](AnimationTick tick) {
            const bool feedback = scene_.advance(tick.delta);
            const bool animated = settings.at("animate").integer != 0;
            if (animated) {
                phase_ += tick.delta;
                scene_.pose(image_, {.translation = {0, std::sin(phase_ * 2) * 4}, .rotation = std::sin(phase_) * 3});
            }
            return cdsdk::UpdateStep{feedback || animated, scene_.active() || animated};
        });
        clockTimer_.replace(1000, clockTick, this);
    }

    ~Workbench() {
        updates_.stop();
        clockTimer_.stop();
        scene_.onInvalidate({});
        images_.onChange({});
    }

    void invalidate() {
        redraw_.invalidate();
    }

    CdResult render(const CdFrame& frame) {
        if (!canvas_.valid(frame)) {
            return CD_ERROR_ARGUMENT;
        }
        selectPage();
        if (card_) {
            return card_->render(frame);
        }
        using namespace cybercanvas;
        updates_.beginFrame();
        redraw_.beginFrame();
        const auto snapshot = environment_.snapshot();
        lastSecond_ = snapshot.unix_ms / 1000;
        const auto& settingLanguage = settings.at("language").text;
        const std::string language = settingLanguage == "auto" ? snapshot.locale : settingLanguage;
        const bool chinese = language.starts_with("zh"), rtl = language.starts_with("ar") || language.starts_with("he");
        auto theme = WidgetTheme::desktop(snapshot.theme == CD_ENV_THEME_LIGHT);
        theme.font.size = 12;
        theme.font.direction = rtl ? TextDirection::RightToLeft : TextDirection::LeftToRight;
        scene_.theme(theme);
        scene_.text(title_, chinese ? L"画布工作台" : rtl ? L"لوحة الرسم" : L"Canvas Workbench");
        scene_.text(add_, chinese ? L"增加" : rtl ? L"أضف" : L"Add");
        scene_.text(reset_, chinese ? L"重置" : rtl ? L"إعادة" : L"Reset");
        scene_.text(count_, (chinese ? L"计数  " : L"Count  ") + std::to_wstring(countValue_));
        const auto time = cdsdk::Environment::localTime(snapshot);
        std::wostringstream clock;
        clock << std::put_time(&time, L"%Y-%m-%d  %H:%M:%S");
        scene_.text(clock_, clock.str());
        scene_.value(level_, settings.at("level").number);
        const Rect area{0, 0, frame.width / frame.device_pixel_ratio, frame.height / frame.device_pixel_ratio};
        scene_.arrange(canvas_, inset(area, {12, 12, 12, 12}), frame.device_pixel_ratio, rtl);
        canvas_.render(frame, {}, [&](auto& draw, auto) {
            draw.box(0, 0, area.width, area.height, 12, theme.surface);
            scene_.draw(draw, icons_.painter());
        });
        if (scene_.active() || settings.at("animate").integer) {
            updates_.start(16);
        }
        return CD_OK;
    }

    void input(const CdInput& event) {
        selectPage();
        if (card_) {
            card_->input(event);
            return;
        }
        action(scene_.input(event));
    }

    void layoutChanged(const CdLayout& layout) {
        layout_ = layout;
        if (card_) {
            card_->layoutChanged(layout);
        }
    }

    void setPaused(bool paused) {
        paused_ = paused;
        if (card_) {
            card_->setPaused(paused);
            return;
        }
        scene_.cancel();
        images_.pause(paused);
        updates_.pause(paused);
        redraw_.pause(paused);
        if (paused) {
            clockTimer_.stop();
        } else {
            clockTimer_.replace(1000, clockTick, this);
        }
    }

    CdResult applySettings(const CdSettingValue* values, uint32_t count) {
        const auto result = settings.apply(values, count);
        if (result == CD_OK) {
            selectPage();
            if (card_) {
                const CdSettingValue values[]{
                    {sizeof(CdSettingValue), CD_SETTING_TEXT, "language", 0, 0, settings.at("language").text.c_str()},
                    {sizeof(CdSettingValue), CD_SETTING_CHOICE, "data_source", 0, 0,
                     settings.at("data_source").text.c_str()}};
                card_->applySettings(values, 2);
                return CD_OK;
            }
            if (settings.at("animate").integer) {
                updates_.start(16);
            } else {
                scene_.pose(image_, {});
                if (!scene_.active()) {
                    updates_.stop();
                }
            }
        }
        return result;
    }

    std::vector<uint8_t> saveState() const {
        std::vector<uint8_t> result{'W',
                                    'B',
                                    2,
                                    0,
                                    uint8_t(countValue_),
                                    uint8_t(countValue_ >> 8),
                                    uint8_t(countValue_ >> 16),
                                    uint8_t(countValue_ >> 24)};
        for (int i = 0; i < 4; ++i) {
            const auto bytes = card_ && page_ == i + 1 ? card_->saveState() : cardStates_[i];
            result.push_back(uint8_t(bytes.size()));
            result.insert(result.end(), bytes.begin(), bytes.end());
            result.resize(result.size() + 12 - bytes.size(), 0);
        }
        return result;
    }

    CdResult restoreState(std::span<const uint8_t> state) {
        if (state.empty()) {
            countValue_ = 0;
            cardStates_ = {};
            if (card_) {
                card_->restoreState({});
            }
            return CD_OK;
        }
        if ((state.size() != 8 && state.size() != 60) || state[0] != 'W' || state[1] != 'B' ||
            state[2] != (state.size() == 8 ? 1 : 2) || state[3] != 0) {
            return CD_ERROR_ARGUMENT;
        }
        std::array<std::vector<uint8_t>, 4> restored;
        if (state.size() == 60) {
            for (int i = 0; i < 4; ++i) {
                const size_t offset = 8 + i * 13;
                const auto size = state[offset];
                if ((size != 0 && size != 12) || !componentdemo::Card::validState(state.subspan(offset + 1, size), i)) {
                    return CD_ERROR_ARGUMENT;
                }
                restored[i] = {state.begin() + offset + 1, state.begin() + offset + 1 + size};
            }
        }
        countValue_ =
            uint32_t(state[4]) | uint32_t(state[5]) << 8 | uint32_t(state[6]) << 16 | uint32_t(state[7]) << 24;
        cardStates_ = std::move(restored);
        if (card_) {
            card_->restoreState(cardStates_[page_ - 1]);
        }
        return CD_OK;
    }

private:
    CdServiceHostApi services_{};
    CdCreateContext context_{};
    CdLayout layout_{};
    std::array<std::vector<uint8_t>, 4> cardStates_;
    std::unique_ptr<componentdemo::Card> card_;
    int page_ = 0;
    bool paused_ = false;
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::Icons icons_;
    cdsdk::Images images_;
    cdsdk::Scene scene_;
    cdsdk::UpdateLoop updates_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::Timer clockTimer_;
    uint64_t lastSecond_ = 0;
    uint32_t title_ = 0, image_ = 0, count_ = 0, clock_ = 0, add_ = 0, reset_ = 0, level_ = 0, countValue_ = 0;
    double phase_ = 0;

    void selectPage() {
        const std::string pages[]{"canvas", "info", "tasks", "media", "calendar"};
        const auto found = std::find(std::begin(pages), std::end(pages), settings.at("page").text);
        const int next = int(found - std::begin(pages));
        if (next == page_) {
            return;
        }
        if (card_) {
            cardStates_[page_ - 1] = card_->saveState();
            card_.reset();
        } else {
            updates_.pause(true);
            clockTimer_.stop();
            images_.pause(true);
            scene_.cancel();
            scene_.keyboard().unbind();
        }
        page_ = next;
        if (page_) {
            card_ = std::make_unique<componentdemo::Card>(context_, page_ - 1);
            card_->restoreState(cardStates_[page_ - 1]);
            card_->layoutChanged(layout_);
            card_->setPaused(paused_);
        } else {
            scene_.bind(context_);
            setPaused(paused_);
        }
    }

    static void CD_CALL clockTick(void* user, uint64_t) {
        auto& self = *static_cast<Workbench*>(user);
        if (self.environment_.snapshot().unix_ms / 1000 != self.lastSecond_) {
            self.invalidate();
        }
    }

    void action(cybercanvas::SceneEvent event) {
        if (event.clicked) {
            if (event.key == 1 && countValue_ < UINT32_MAX) {
                ++countValue_;
            }
            if (event.key == 2) {
                countValue_ = 0;
            }
            invalidate();
        }
        if (event.key == 3 && event.value) {
            const CdSettingValue value{sizeof(value), CD_SETTING_NUMBER, "level", 0, *event.value, ""};
            settings.apply(&value, 1);
            invalidate();
        }
        if (scene_.active()) {
            updates_.start(16);
        }
    }
};
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<Workbench>::query(abi, output, size, "org.cyberdesktop.sdk.workbench", "1.0.0");
}
