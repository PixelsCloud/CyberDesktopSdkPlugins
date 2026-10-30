#include <CharacterChat/Protocol.h>
#include <CyberCanvas/List.h>
#include <CyberCanvas/Text.h>
#include <sdk/Canvas.h>
#include <sdk/Interaction.h>
#include <sdk/Settings.h>
#include <sdk/WindowsFonts.h>
#include <sdk/cpp/Plugin.h>
#include <sdk/services/CharacterChat.h>
#include <sdk/services/CharacterImages.h>
#include <sdk/services/Composition.h>
#include <sdk/services/Icons.h>
#include <sdk/services/Interaction.h>
#include <sdk/services/Live2D.h>
#include <sdk/services/Live2DActions.h>
#include <sdk/services/SettingsChoices.h>
#include <sdk/services/TextInput.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <sstream>

#include "plugins/live2d-suite/StatusPresentation.h"

namespace {

using Json = nlohmann::json;

bool contains(const CdRect& rect, double x, double y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

class CharacterChatPlugin {
public:
    cdsdk::Settings settings{{"offline", CD_SETTING_TOGGLE, 1},
                             {"story", CD_SETTING_TOGGLE, 1},
                             {"expression", CD_SETTING_CHOICE, 0, 0, "auto", 0, 0, {"auto"}},
                             {"pose", CD_SETTING_CHOICE, 0, 0, "auto", 0, 0, {"auto"}},
                             {"renderer", CD_SETTING_CHOICE, 0, 0, "image", 0, 0, {"image", "mao", "haru"}},
                             {"fps", CD_SETTING_CHOICE, 0, 0, "30", 0, 0, {"30", "60"}},
                             {"model_directory", CD_SETTING_TEXT, 0, 0, "", 0, 4096}};

    explicit CharacterChatPlugin(const CdCreateContext& create) : host_(*create.host), context_(create.host_context) {
        canvas_.bindGpu(create);
        directory_ = std::filesystem::path(cdsdk::fontWide(create.plugin_directory));
        if (create.host->struct_size >= sizeof(CdServiceHostApi)) {
            const auto* services = reinterpret_cast<const CdServiceHostApi*>(create.host);
            if (services->query_service) {
                services->query_service(context_, CD_SERVICE_CHARACTER_CHAT, 1, &chat_, sizeof(chat_));
                services->query_service(context_, CD_SERVICE_CHARACTER_IMAGES, 1, &images_, sizeof(images_));
                services->query_service(context_, CD_SERVICE_CHARACTER_VERSIONED_IMAGES, 1, &versionedImages_,
                                        sizeof(versionedImages_));
                services->query_service(context_, CD_SERVICE_COMPOSITION, 1, &composition_, sizeof(composition_));
                services->query_service(context_, CD_SERVICE_LIVE2D, 1, &live2d_, sizeof(live2d_));
                services->query_service(context_, CD_SERVICE_LIVE2D_ACTIONS, 1, &actions_, sizeof(actions_));
                services->query_service(context_, CD_SERVICE_TEXT_INPUT, 1, &input_, sizeof(input_));
                services->query_service(context_, CD_SERVICE_ICONS, 1, &icons_, sizeof(icons_));
                services->query_service(context_, CD_SERVICE_INTERACTION, 1, &interaction_, sizeof(interaction_));
                services->query_service(context_, CD_SERVICE_SETTINGS_CHOICES, 1, &settingsChoices_,
                                        sizeof(settingsChoices_));
            }
        }
        if (interaction_.enable) {
            interaction_.enable(context_, 1);
        }
        dialogueInteraction_.bind(create);
        if (input_.connect) {
            input_.connect(context_, onText, this);
        }
        if (chat_.connect && chat_.connect(context_, onSnapshot, this) != CD_OK) {
            error_ = "聊天服务繁忙，请稍后重试。";
        }
        syncPresentation();
    }

    ~CharacterChatPlugin() {
        persistPlayback();
        dialogueInteraction_.cancel();
        stopPresentationTimer();
        if (input_.connect) {
            input_.connect(context_, nullptr, nullptr);
        }
        if (chat_.connect) {
            chat_.connect(context_, nullptr, nullptr);
        }
    }

    void layoutChanged(const CdLayout& layout) {
        width_ = std::max(160., layout.logical_width);
        height_ = std::max(160., layout.logical_height);
        dpr_ = layout.device_pixel_ratio;
        dialogueInteraction_.cancel();
        dialogueScroll_.cancel();
        editor_ = {12, height_ - 60, std::max(24., width_ - 76), 40};
        send_ = {width_ - 56, height_ - 60, 44, 40};
        role_ = {12, 10, std::max(70., std::min(150., width_ - 178)), 30};
        mode_ = {width_ - 152, 10, 100, 30};
        connection_ = {width_ - 44, 10, 32, 30};
        model_ = {width_ * .35, 34, width_ * .65 - 8, std::max(24., stageHeight() - 34)};
        action1_ = {14, stageHeight() - 58, 76, 28};
        action2_ = {98, stageHeight() - 58, 76, 28};
        if (composition_.configure) {
            composition_.configure(context_, &model_, liveMode());
        }
        if (input_.configure) {
            constexpr char hint[] = "输入消息… Enter 发送";
            input_.configure(context_, &editor_, hint, sizeof(hint) - 1);
        }
        iconCache_.clear();
    }

    CdResult applySettings(const CdSettingValue* values, uint32_t count) {
        auto next = settings;
        const auto result = next.apply(values, count);
        if (result != CD_OK) {
            return result;
        }
        if (next.at("offline").integer != settings.at("offline").integer) {
            command({{"op", "mode"}, {"offline", bool(next.at("offline").integer)}});
        }
        if (next.at("story").integer != settings.at("story").integer) {
            command({{"op", "story"}, {"enabled", bool(next.at("story").integer)}});
        }
        const bool modelChanged = next.at("renderer").text != settings.at("renderer").text ||
                                  next.at("model_directory").text != settings.at("model_directory").text;
        const bool presentationChanged =
            modelChanged || (next.at("renderer").text != "image" && next.at("fps").text != settings.at("fps").text);
        if (presentationChanged && next.at("renderer").text != "image" &&
            (!composition_.configure || !live2d_.configure)) {
            return CD_ERROR_UNSUPPORTED;
        }
        if (modelChanged) {
            rememberPreview();
        }
        settings = std::move(next);
        if (presentationChanged) {
            if (modelChanged) {
                // The settings dialog submits the old inventory together with the new
                // model. Those old preview values must never target the new model.
                clearLiveInventory();
                previewScope_.clear();
                resetPreviews();
            }
            configureModel(modelChanged);
        }
        syncPresentation();
        rememberPresentation();
        if (liveMode() && !modelChanged && actions_.perform && settings.at("pose").text != "auto") {
            for (uint32_t index = 0; index < count; ++index) {
                if (std::string_view(values[index].id) == "pose") {
                    actions_.perform(context_, settings.at("pose").text.c_str(), CD_LIVE2D_ACTION_PREVIEW);
                    break;
                }
            }
        }
        applyLiveExpression();
        redraw(CD_COMPOSITION_BACKGROUND | CD_COMPOSITION_FOREGROUND);
        return CD_OK;
    }

    std::vector<uint8_t> saveState() {
        rememberPresentation();
        auto encode = [&] {
            return Json{{"version", 1}, {"roles", roleSettings_}, {"previews", previewSettings_}}.dump();
        };
        auto bytes = encode();
        // Bound escaped paths as well as decoded values. Keep the active role and
        // illustration even after many imports and externally edited state files.
        for (auto* values : {&previewSettings_, &roleSettings_}) {
            const auto& active = values == &previewSettings_ ? previewScope_ : roleId_;
            while (bytes.size() > characterchat::kMaximumCheckpointBytes && values->size() > 1) {
                auto oldest = values->begin();
                if (oldest.key() == active) {
                    ++oldest;
                }
                values->erase(oldest);
                bytes = encode();
            }
        }
        return {bytes.begin(), bytes.end()};
    }

    CdResult restoreState(std::span<const uint8_t> bytes) {
        if (bytes.empty()) {
            return CD_OK;
        }
        try {
            const auto state = characterchat::parseBoundedCheckpoint(
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            if (state.value("version", 0) != 1 || !state.at("roles").is_object() || !state.at("previews").is_object() ||
                state.at("roles").size() > 128 || state.at("previews").size() > 256) {
                return CD_ERROR_ARGUMENT;
            }
            for (const auto& [id, fields] : state.at("roles").items()) {
                auto validate = settings;
                if (!characterchat::validIdentifier(id) || !fields.is_object() || fields.size() != 3) {
                    return CD_ERROR_ARGUMENT;
                }
                for (const auto* key : {"renderer", "model_directory", "fps"}) {
                    const auto text = fields.at(key).get<std::string>();
                    const CdSettingValue value{sizeof(value), validate.at(key).type, key, 0, 0, text.c_str()};
                    if (text.find('\0') != std::string::npos || validate.apply(&value, 1) != CD_OK) {
                        return CD_ERROR_ARGUMENT;
                    }
                }
            }
            for (const auto& [id, fields] : state.at("previews").items()) {
                if (id.empty() || id.size() > 256 || !fields.is_object() || fields.size() != 2) {
                    return CD_ERROR_ARGUMENT;
                }
                for (const auto* key : {"expression", "pose"}) {
                    const auto text = fields.at(key).get<std::string>();
                    if (text.empty() || text.size() > 256 || text.find('\0') != std::string::npos) {
                        return CD_ERROR_ARGUMENT;
                    }
                }
            }
            roleSettings_ = state.at("roles");
            previewSettings_ = state.at("previews");
            if (!roleId_.empty()) {
                activateRole(roleId_, true);
                syncPresentation();
            }
            return CD_OK;
        } catch (...) {
            return CD_ERROR_ARGUMENT;
        }
    }

    void setPaused(bool paused) {
        paused_ = paused;
        if (paused) {
            persistPlayback();
            dialogueInteraction_.cancel();
            stopPresentationTimer();
            activePose_.clear();
            previousPortrait_.clear();
            previousBody_.clear();
            moodReturnDue_ = 0;
            idleMoodActive_ = true;
        } else {
            applyLiveExpression();
        }
        if (paused && input_.focus) {
            input_.focus(context_, 0);
        }
    }

    void input(const CdInput& event) {
        const auto viewport = dialogueScroll_.viewport();
        if (event.kind == CD_INPUT_WHEEL && std::isfinite(event.wheel_delta) &&
            contains({viewport.x, viewport.y, viewport.width, viewport.height}, event.x, event.y)) {
            if (dialogueScroll_.scrollBy(-event.wheel_delta / 120 * 48, false)) {
                followDialogue_ = dialogueScroll_.offset() >= dialogueScroll_.maximum() - 1;
                redraw();
            }
            return;
        }
        const auto track = dialogueScroll_.track();
        // Route only gestures beginning on the scrollbar: the other controls use
        // the same host capture service and must retain their own active press.
        if (dialogueInteraction_.router.captured() ||
            (!pressed_ && event.kind == CD_INPUT_PRESS && dialogueScroll_.maximum() > 0 &&
             contains({track.x - 3, track.y, track.width + 6, track.height}, event.x, event.y))) {
            dialogueInteraction_.regions({dialogueScroll_.region(1)});
            const auto result = dialogueInteraction_.input(event);
            if (dialogueScroll_.drag(result, 1)) {
                followDialogue_ = dialogueScroll_.offset() >= dialogueScroll_.maximum() - 1;
                redraw();
            }
            if (result.target && event.kind != CD_INPUT_CANCEL) {
                return;
            }
        }
        if (liveMode() && live2d_.pointer && (event.kind == CD_INPUT_MOVE || event.kind == CD_INPUT_LEAVE)) {
            if (event.kind == CD_INPUT_MOVE && hit(event.x, event.y) == 8) {
                live2d_.pointer(context_, (event.x - model_.x) / model_.width * 2 - 1,
                                1 - (event.y - model_.y) / model_.height * 2, 0);
            } else {
                live2d_.pointer(context_, 0, 0, 0);
            }
        }
        if (event.kind == CD_INPUT_CANCEL) {
            if (input_.focus) {
                input_.focus(context_, 0);
            }
            pressed_ = false;
            return;
        }
        if (event.kind == CD_INPUT_PRESS && (event.buttons & CD_BUTTON_LEFT)) {
            pressTarget_ = hit(event.x, event.y);
            pressed_ = contains(editor_, event.x, event.y) || contains(send_, event.x, event.y) ||
                       contains(role_, event.x, event.y) || contains(mode_, event.x, event.y) ||
                       contains(connection_, event.x, event.y) || contains(action1_, event.x, event.y) ||
                       contains(action2_, event.x, event.y) ||
                       (liveMode() && (contains(model_, event.x, event.y) || contains(action1_, event.x, event.y) ||
                                       contains(action2_, event.x, event.y)));
            for (const auto& choice : choices_) {
                pressed_ |= contains(choice.first, event.x, event.y);
            }
            for (const auto& rect : attachmentRects_) {
                pressed_ |= contains(rect, event.x, event.y);
            }
            if (pressed_ && interaction_.capture) {
                interaction_.capture(context_, 1);
            }
            return;
        }
        if (event.kind != CD_INPUT_RELEASE || !pressed_) {
            return;
        }
        pressed_ = false;
        if (interaction_.capture) {
            interaction_.capture(context_, 0);
        }
        if (hit(event.x, event.y) != pressTarget_) {
            return;
        }
        if (contains(editor_, event.x, event.y)) {
            if (input_.focus) {
                input_.focus(context_, 1);
            }
        } else if (contains(send_, event.x, event.y)) {
            if (status_ == "generating") {
                command({{"op", "stop"}});
            } else {
                send();
            }
            if (input_.focus) {
                input_.focus(context_, 1);
            }
        } else if (contains(role_, event.x, event.y)) {
            command({{"op", "character"}, {"index", (selected_ + 1) % std::max(size_t(1), roleCount_)}});
        } else if (contains(mode_, event.x, event.y) && status_ != "generating") {
            command({{"op", "mode"}, {"offline", !offline_}});
        } else if (contains(connection_, event.x, event.y)) {
            if (input_.focus) {
                input_.focus(context_, 0);
            }
            command({{"op", "manage"}});
        } else if (liveMode() && contains(action1_, event.x, event.y)) {
            perform(0, CD_LIVE2D_ACTION_PREVIEW);
        } else if (liveMode() && contains(action2_, event.x, event.y)) {
            perform(1, CD_LIVE2D_ACTION_PREVIEW);
        } else if (!liveMode() && contains(action1_, event.x, event.y)) {
            const auto actions = availableActions();
            if (!actions.empty()) {
                previewPose(actions[0], true);
            }
        } else if (!liveMode() && contains(action2_, event.x, event.y)) {
            const auto actions = availableActions();
            if (actions.size() > 1) {
                previewPose(actions[1], true);
            }
        } else if (liveMode() && contains(model_, event.x, event.y)) {
            perform(nextTap_++, CD_LIVE2D_ACTION_POINTER);
        } else {
            for (size_t i = 0; i < attachmentRects_.size(); ++i) {
                if (contains(attachmentRects_[i], event.x, event.y)) {
                    command({{"op", "open_attachment"}, {"identity", attachmentIdentity_}, {"ordinal", i}});
                    return;
                }
            }
            for (const auto& choice : choices_) {
                if (contains(choice.first, event.x, event.y)) {
                    persistPlayback();
                    command({{"op", "choices"}, {"identity", attachmentIdentity_}});
                    break;
                }
            }
        }
    }

    CdResult render(const CdFrame& frame) {
        if (!canvas_.valid(frame)) {
            return CD_ERROR_ARGUMENT;
        }
        canvas_.render(frame, {0, 0, width_, height_}, [&](auto& draw, auto area) {
            const auto pass = composition_.current_pass ? composition_.current_pass(context_) : CD_COMPOSITION_ALL;
            const double stageHeight = this->stageHeight();
            if (pass != CD_COMPOSITION_FOREGROUND) {
                draw.box(0, 0, area.width, area.height, 10, 0x101C2AFF);
                draw.clipped({0, 0, area.width, stageHeight}, [&] {
                    asset(visual_.value("scenes", Json::object()).value(scene_, "scene_" + scene_),
                          {0, 0, area.width, stageHeight}, true);
                    const double portraitWidth = std::min(area.width * .70, stageHeight * .95);
                    if (!liveMode()) {
                        drawPortrait({area.width - portraitWidth - 4, 30, portraitWidth, stageHeight * 1.25});
                    }
                });
            }
            if (pass == CD_COMPOSITION_BACKGROUND) {
                return;
            }
            draw.box(0, stageHeight, area.width, area.height - stageHeight, 0, 0x101C2AFF);
            if (liveMode()) {
                CdLive2DStatus status{};
                status.struct_size = sizeof(status);
                if (live2d_.snapshot) {
                    live2d_.snapshot(context_, &status);
                }
                if (status.state == 3) {
                    live2dplugin::drawFailure(draw, {model_.x, model_.y, model_.width, model_.height}, status.message,
                                              0xE9F2FFFF);
                } else if (status.state != 2) {
                    draw.text(L"正在加载 Live2D…", 12, {14, 50, area.width - 28, 70}, 0xEDB9A9FF);
                }
            }
            for (unsigned index = 0; index < 2; ++index) {
                if (!actionAvailable(index)) {
                    continue;
                }
                const auto button = index ? action2_ : action1_;
                draw.box(button.x, button.y, button.width, button.height, 6, 0x122436D8);
                icon("play", {button.x + 2, button.y, 24, button.height});
                const auto action = availableActions().at(index);
                const auto label = action == "wave" ? "挥手" : action == "folded" ? "抱臂" : action;
                draw.text(cdsdk::fontWide(label), 11, {button.x + 26, button.y, button.width - 28, button.height},
                          0xE7F0F5FF);
            }
            const double textY = stageHeight + 10;
            draw.text(cdsdk::fontWide(name_), 14, {14, textY, area.width - 28, 24}, 0xA5DBD3FF, false, true);
            cybercanvas::TextStyle style;
            style.family = L"Microsoft YaHei";
            style.size = area.width < 350 ? 12 : 14;
            style.align = cybercanvas::TextAlign::Start;
            style.vertical = cybercanvas::VerticalAlign::Top;
            style.wrap = true;
            attachmentRects_.clear();
            const double thumbnailWidth = attachments_.empty() ? 0 : std::min(170., area.width * .4);
            const double dialogueBottom = choiceTexts_.empty() ? editor_.y - 10 : height_ - 108;
            const CdRect dialogueRect{14, textY + 28, std::max(80., area.width - 28 - thumbnailWidth),
                                      std::max(24., dialogueBottom - textY - 28)};
            drawDialogue(style, dialogueRect);
            for (size_t i = 0; i < attachments_.size(); ++i) {
                const double each = thumbnailWidth / attachments_.size();
                CdRect rect{area.width - thumbnailWidth - 10 + i * each, textY + 30, each - 5,
                            std::max(40., std::min(100., height_ - 110 - textY - 30))};
                draw.box(rect.x, rect.y, rect.width, rect.height, 6, 0x294052FF);
                const auto id = attachments_[i].value("resource_id", "");
                if (attachments_[i].value("sha256", "").empty()) {
                    draw.text(L"旧素材\n未绑定版本", 11, rect, 0xEDB9A9FF);
                } else if (!id.empty()) {
                    asset(id, rect, false, attachments_[i].value("resource_pack", ""));
                }
                if (attachments_[i].value("type", "image") == "video") {
                    icon("play", rect);
                }
                attachmentRects_.push_back(rect);
            }
            choices_.clear();
            if (!choiceTexts_.empty() && status_ != "generating") {
                CdRect button{14, height_ - 100, area.width - 28, 28};
                draw.box(button.x, button.y, button.width, button.height, 6, 0x294052FF);
                draw.text(L"选择接下来的剧情…", 11, button, 0xD9EAF2FF);
                choices_.push_back({button, {}});
            }
            draw.box(editor_.x, editor_.y, editor_.width, editor_.height, 7, 0x1A2B3DFF);
            draw.text(cdsdk::fontWide(draft_.empty() ? "输入消息… Enter 发送" : draft_), 12,
                      {editor_.x + 9, editor_.y + 8, editor_.width - 18, editor_.height - 16},
                      draft_.empty() ? 0x7E95ADFF : 0xDBE6F2FF, false);
            draw.box(send_.x, send_.y, send_.width, send_.height, 7, 0x327F85FF);
            icon(status_ == "generating" ? "square" : "arrow-up", send_);
            if (!chat_.connect || !input_.connect) {
                draw.text(L"请更新 Desktop 以启用聊天和中文输入", 12, {10, 45, area.width - 20, 50}, 0xFFCC99FF);
            }
        });
        return CD_OK;
    }

private:
    uint32_t hit(double x, double y) const {
        if (contains(editor_, x, y)) {
            return 1;
        }
        if (contains(send_, x, y)) {
            return 2;
        }
        if (contains(role_, x, y)) {
            return 3;
        }
        if (contains(mode_, x, y)) {
            return 4;
        }
        if (contains(connection_, x, y)) {
            return 5;
        }
        if (actionAvailable(0) && contains(action1_, x, y)) {
            return 6;
        }
        if (actionAvailable(1) && contains(action2_, x, y)) {
            return 7;
        }
        if (liveMode() && contains(model_, x, y)) {
            return 8;
        }
        for (size_t index = 0; index < choices_.size(); ++index) {
            if (contains(choices_[index].first, x, y)) {
                return uint32_t(index) + 100;
            }
        }
        for (size_t index = 0; index < attachmentRects_.size(); ++index) {
            if (contains(attachmentRects_[index], x, y)) {
                return uint32_t(index) + 200;
            }
        }
        return 0;
    }

    void rememberPreview() {
        if (!previewScope_.empty()) {
            if (previewSettings_.size() >= 256 && !previewSettings_.contains(previewScope_)) {
                previewSettings_.erase(previewSettings_.begin());
            }
            previewSettings_[previewScope_] = {{"expression", settings.at("expression").text},
                                               {"pose", settings.at("pose").text}};
        }
    }

    void rememberPresentation() {
        if (!roleId_.empty()) {
            if (roleSettings_.size() >= 128 && !roleSettings_.contains(roleId_)) {
                roleSettings_.erase(roleSettings_.begin());
            }
            roleSettings_[roleId_] = {{"renderer", settings.at("renderer").text},
                                      {"model_directory", settings.at("model_directory").text},
                                      {"fps", settings.at("fps").text}};
        }
        rememberPreview();
    }

    void clearLiveInventory() {
        liveMotions_.clear();
        liveExpressions_.clear();
        liveFingerprint_.clear();
        liveBindings_ = Json::object();
        appliedLiveExpression_.clear();
        nextTap_ = 0;
    }

    void resetPreviews() {
        settings.setChoices("expression", {"auto"}, "auto");
        settings.setChoices("pose", {"auto"}, "auto");
        activePose_.clear();
        manualPose_ = false;
    }

    void activateRole(const std::string& id, bool restore = false) {
        const bool firstRole = roleId_.empty();
        if (!restore) {
            rememberPresentation();
        }
        roleId_ = id;
        previewScope_.clear();
        resetPreviews();
        auto selected = Json{{"renderer", "image"}, {"model_directory", ""}, {"fps", "30"}};
        if (roleSettings_.contains(id)) {
            selected = roleSettings_.at(id);
        } else if (firstRole) {
            // Upgrade existing instances whose state only contained global settings.
            selected = {{"renderer", settings.at("renderer").text},
                        {"model_directory", settings.at("model_directory").text},
                        {"fps", settings.at("fps").text}};
        }
        bool changed = restore;
        for (const auto* key : {"renderer", "model_directory", "fps"}) {
            const auto text = selected.at(key).get<std::string>();
            changed |= settings.at(key).text != text;
            const CdSettingValue value{sizeof(value), settings.at(key).type, key, 0, 0, text.c_str()};
            settings.apply(&value, 1);
        }
        if (changed) {
            clearLiveInventory();
            configureModel(true);
        }
    }

    void syncPresentation() {
        const auto scope = roleId_.empty() ? std::string{}
                           : liveMode()
                               ? (liveFingerprint_.empty() ? std::string{} : roleId_ + ":live:" + liveFingerprint_)
                           : resourcePack_.empty() ? std::string{}
                                                   : roleId_ + ":image:" + resourcePack_;
        const bool changed = scope != previewScope_;
        if (changed) {
            rememberPreview();
            previewScope_ = scope;
            resetPreviews();
        }
        Json expressionChoices = Json::array({{{"value", "auto"}, {"label", "跟随对话"}}});
        Json poseChoices = expressionChoices;
        const auto append = [](Json& choices, const std::string& value, const std::string& label) {
            if (std::none_of(choices.begin(), choices.end(), [&](const auto& choice) {
                    return choice.at("value") == value;
                })) {
                choices.push_back({{"value", value}, {"label", label}});
            }
        };
        if (liveMode()) {
            for (const auto& id : liveExpressions_) {
                append(expressionChoices, id, id);
            }
            for (const auto& id : liveMotions_) {
                append(poseChoices, id, id);
            }
        } else {
            constexpr const char* labels[] = {"平静", "开心", "难过", "生气", "害羞", "惊讶", "担心", "思考"};
            const auto emotions = visual_.value("emotions", Json::object());
            for (unsigned index = 0; index < 8; ++index) {
                const auto id = std::string(characterchat::emotionId(static_cast<characterchat::Emotion>(index)));
                if (emotions.contains(id)) {
                    append(expressionChoices, id, labels[index]);
                }
            }
            for (const auto& id : visual_.value("expressions", std::vector<std::string>{})) {
                const bool mapped = std::any_of(emotions.begin(), emotions.end(), [&](const auto& asset) {
                    return asset == id;
                });
                if (!mapped) {
                    append(expressionChoices, "asset:" + id, id);
                }
            }
            if (!emotions.empty()) {
                append(poseChoices, "neutral", "默认站姿");
            }
            const auto gestures = visual_.value("gestures", Json::object());
            for (const auto& [id, asset] : gestures.items()) {
                append(poseChoices, id, id == "wave" ? "挥手" : id == "folded" ? "抱臂" : id);
            }
        }
        for (const auto& [key, choices] :
             {std::pair{"expression", expressionChoices}, std::pair{"pose", poseChoices}}) {
            std::vector<std::string> ids;
            for (const auto& choice : choices) {
                ids.push_back(choice.at("value").get<std::string>());
            }
            settings.setChoices(key, std::move(ids), "auto");
            if (changed && previewSettings_.contains(scope)) {
                const auto text = previewSettings_.at(scope).at(key).get<std::string>();
                const CdSettingValue value{sizeof(value), CD_SETTING_CHOICE, key, 0, 0, text.c_str()};
                settings.apply(&value, 1);  // Missing options keep the valid automatic fallback.
            }
        }
        if (settingsChoices_.publish) {
            Json fields{{"expression", {{"context", scope}, {"choices", expressionChoices}}},
                        {"pose", {{"context", scope}, {"choices", poseChoices}}}};
            for (const auto* key : {"renderer", "model_directory", "fps", "story"}) {
                fields[key] = {{"context", roleId_}};
            }
            const auto bytes = Json{{"fields", fields}}.dump();
            if (bytes != publishedChoices_ &&
                settingsChoices_.publish(context_, bytes.data(), uint32_t(bytes.size())) == CD_OK) {
                publishedChoices_ = bytes;
            }
        }
    }

    bool liveMode() const {
        return settings.at("renderer").text != "image";
    }

    uint32_t liveFramesPerSecond() const {
        return settings.at("fps").text == "60" ? 60 : 30;
    }

    std::vector<std::string> availableActions() const {
        std::vector<std::string> actions;
        if (liveMode()) {
            for (const auto& id : liveMotions_) {
                if (id.starts_with("TapBody/")) {
                    actions.push_back(id);
                }
            }
            if (actions.empty()) {
                for (const auto& id : liveMotions_) {
                    if (!id.starts_with("Idle/")) {
                        actions.push_back(id);
                    }
                }
            }
        } else {
            const auto gestures = visual_.value("gestures", Json::object());
            for (const auto* id : {"wave", "folded"}) {
                if (gestures.contains(id)) {
                    actions.emplace_back(id);
                }
            }
            for (const auto& [id, asset] : gestures.items()) {
                if (id != "wave" && id != "folded") {
                    actions.push_back(id);
                }
            }
        }
        return actions;
    }

    bool actionAvailable(unsigned index) const {
        return availableActions().size() > index;
    }

    double stageHeight() const {
        return std::max(60., std::min(height_ * .54, height_ - 190));
    }

    void drawDialogue(const cybercanvas::TextStyle& style, CdRect area) {
        const auto& text = error_.empty() ? dialogue_ : error_;
        const auto wide = cdsdk::fontWide(text);
        const double textWidth = std::max(1., area.width - 12);
        if (text != measuredDialogue_ || textWidth != dialogueWidth_ || style.size != dialogueFont_) {
            dialogueHeight_ = 0;
            for (const auto& line : canvas_.textLines(wide, style, textWidth)) {
                dialogueHeight_ += line.height;
            }
            measuredDialogue_ = text;
            dialogueWidth_ = textWidth;
            dialogueFont_ = style.size;
        }
        dialogueScroll_.arrange({area.x, area.y, area.width, area.height}, dialogueHeight_, dpr_);
        if (followDialogue_) {
            dialogueScroll_.scrollTo(dialogueScroll_.maximum(), false);
        }
        canvas_.clipped(area, [&] {
            canvas_.text(wide, style,
                         {area.x, area.y - dialogueScroll_.offset(), textWidth, std::max(area.height, dialogueHeight_)},
                         error_.empty() ? 0xE2EAF4FF : 0xF1AE9FFF);
        });
        dialogueScroll_.draw(canvas_, cybercanvas::WidgetTheme::dark());
    }

    void persistPlayback() {
        if (playbackBeats_.empty() || attachmentIdentity_.is_null() || !playbackTurn_) {
            return;
        }
        // A complete reply is available together; old manual reading cursors no
        // longer gate delivery. Keep the durable cursor compatible with old saves.
        command({{"op", "playback"},
                 {"identity", attachmentIdentity_},
                 {"cursor",
                  {{"turn", playbackTurn_},
                   {"beat", playbackBeats_.size() - 1},
                   {"glyph", graphemes_.empty() ? size_t(0) : graphemes_.back().size()}}}});
    }

    void configureModel(bool updateCapabilities) {
        if (updateCapabilities) {
            command({{"op", "renderer"}, {"kind", liveMode() ? "live2d" : "image"}});
        }
        stopPresentationTimer();
        activePose_.clear();
        previousPortrait_.clear();
        if (composition_.configure) {
            composition_.configure(context_, &model_, liveMode());
        }
        if (!live2d_.configure) {
            return;
        }
        auto path = settings.at("model_directory").text;
        if (path.empty() && liveMode()) {
            const auto value = (directory_ / "models" / settings.at("renderer").text).u8string();
            path.assign(value.begin(), value.end());
        }
        if (!liveMode()) {
            path.clear();
        }
        modelDirectory_ = path;
        appliedLiveExpression_.clear();
        CdLive2DOptions options{sizeof(options),
                                liveFramesPerSecond(),
                                path.c_str(),
                                "auto",
                                "",
                                1.2,
                                0,
                                0,
                                uint32_t(!liveMode()),
                                1,
                                1,
                                0};
        live2d_.configure(context_, &options);
        redraw(CD_COMPOSITION_BACKGROUND | CD_COMPOSITION_FOREGROUND);
    }

    void perform(unsigned index, uint32_t priority) {
        if (!actions_.perform || !live2d_.snapshot) {
            return;
        }
        CdLive2DStatus status{};
        status.struct_size = sizeof(status);
        live2d_.snapshot(context_, &status);
        const auto taps = availableActions();
        if (status.state != 2 || taps.empty()) {
            return;
        }
        actions_.perform(context_, taps[index % taps.size()].c_str(), priority);
    }

    static uint64_t presentationNow() {
        return uint64_t(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    void stopPresentationTimer() {
        if (presentationTimer_ && host_.unsubscribe) {
            host_.unsubscribe(context_, presentationTimer_);
        }
        presentationTimer_ = 0;
    }

    void startPresentationTimer() {
        if (!paused_ && !presentationTimer_ && host_.subscribe_timer) {
            host_.subscribe_timer(context_, 50, onPresentationTick, this, &presentationTimer_);
        }
    }

    void armMoodReturn() {
        if (!paused_ && status_ == "idle" && !idleMoodActive_ && !moodReturnDue_) {
            moodReturnDue_ = presentationNow() + 2000;
            startPresentationTimer();
        }
    }

    static void CD_CALL onPresentationTick(void* context, uint64_t) noexcept {
        try {
            auto& self = *static_cast<CharacterChatPlugin*>(context);
            const auto now = presentationNow();
            bool changed = false;
            if (!self.activePose_.empty() && now >= self.poseUntil_) {
                self.activePose_.clear();
                self.manualPose_ = false;
                changed = true;
            }
            if (self.moodReturnDue_ && now >= self.moodReturnDue_) {
                self.moodReturnDue_ = 0;
                self.idleMoodActive_ = true;
                self.applyLiveExpression();
                changed = true;
            }
            const bool imageAnimation = !self.liveMode() && ((!self.previousPortrait_.empty() && self.fadeStarted_) ||
                                                             (!self.previousBody_.empty() && self.bodyFadeStarted_));
            if (self.paused_ || (!imageAnimation && self.activePose_.empty() && !self.moodReturnDue_)) {
                self.stopPresentationTimer();
            }
            if (!self.paused_ && (changed || imageAnimation)) {
                self.redraw(CD_COMPOSITION_BACKGROUND | CD_COMPOSITION_FOREGROUND);
            }
        } catch (...) {
        }
    }

    void previewPose(const std::string& gesture, bool manual) {
        if (gesture.empty()) {
            return;
        }
        const auto now = presentationNow();
        if (!manual && ((manualPose_ && now < poseUntil_) || now < nextStoryPose_)) {
            return;
        }
        if (liveMode()) {
            const auto mapping = liveBindings_.value("gestures", Json::object());
            if (!manual && gesturesEnabled_ && mapping.contains(gesture) && actions_.perform) {
                const auto motion = mapping.at(gesture).get<std::string>();
                if (actions_.perform(context_, motion.c_str(), CD_LIVE2D_ACTION_STORY) == CD_OK) {
                    nextStoryPose_ = now + 4000;
                }
            }
            return;
        }
        if (!visual_.value("gestures", Json::object()).contains(gesture)) {
            return;
        }
        activePose_ = gesture;
        manualPose_ = manual;
        poseUntil_ = now + 2000;
        if (!manual) {
            nextStoryPose_ = now + 4000;
        }
        startPresentationTimer();
        redraw(CD_COMPOSITION_BACKGROUND | CD_COMPOSITION_FOREGROUND);
    }

    void applyLiveExpression() {
        if (!liveMode() || paused_ || !live2d_.configure || modelDirectory_.empty()) {
            return;
        }
        const auto mapping = liveBindings_.value("expressions", Json::object());
        const auto selected = settings.at("expression").text;
        const auto emotion = selected == "auto" ? (idleMoodActive_ ? idleEmotion_ : emotion_) : selected;
        const auto expression = selected != "auto" ? selected
                                : !idleMoodActive_ ? mapping.value(expression_, mapping.value(emotion, ""))
                                                   : mapping.value(emotion, "");
        if (expression == appliedLiveExpression_) {
            return;
        }
        CdLive2DOptions options{sizeof(options),
                                liveFramesPerSecond(),
                                modelDirectory_.c_str(),
                                "auto",
                                expression.c_str(),
                                1.2,
                                0,
                                0,
                                0,
                                1,
                                1,
                                0};
        if (live2d_.configure(context_, &options) == CD_OK) {
            appliedLiveExpression_ = expression;
        }
    }

    std::string desiredPortrait() const {
        const auto pose = settings.at("pose").text;
        const auto gesture = pose == "auto" ? activePose_ : pose;
        const auto gestures = visual_.value("gestures", Json::object());
        if (!visual_.contains("layers") && gestures.contains(gesture)) {
            return gestures.at(gesture).get<std::string>();
        }
        const auto emotions = visual_.value("emotions", Json::object());
        const auto selected = settings.at("expression").text;
        if (selected.starts_with("asset:")) {
            return selected.substr(6);
        }
        const auto expression = selected == "auto" && !idleMoodActive_ ? expression_ : std::string{};
        const auto expressions = visual_.value("expressions", std::vector<std::string>{});
        if (pose != "neutral" && !expression.empty() &&
            std::find(expressions.begin(), expressions.end(), expression) != expressions.end()) {
            return expression;
        }
        const auto emotion = pose == "neutral"    ? "neutral"
                             : selected == "auto" ? (idleMoodActive_ ? idleEmotion_ : emotion_)
                                                  : selected;
        return emotions.value(emotion, emotions.value("neutral", "neutral"));
    }

    bool layeredPortrait(const std::string& id) const {
        return visual_.contains("layers") && visual_.at("layers").at("faces").contains(id);
    }

    bool drawLayerBody(CdRect area, double opacity = 1) {
        const auto& layers = visual_.at("layers");
        const auto pose = settings.at("pose").text;
        const auto gesture = pose == "auto" ? activePose_ : pose;
        const auto target =
            visual_.value("gestures", Json::object()).value(gesture, layers.at("base_asset").get<std::string>());
        if (target != body_) {
            previousBody_ = body_;
            body_ = target;
            bodyFadeStarted_ = 0;
        }
        if (previousBody_.empty()) {
            return asset(target, area, false, {}, opacity);
        }
        if (!asset(target, area, false, {}, 0)) {
            return asset(previousBody_, area, false, {}, opacity);
        }
        if (!bodyFadeStarted_) {
            bodyFadeStarted_ = presentationNow();
            startPresentationTimer();
        }
        const auto alpha = std::min(1., double(presentationNow() - bodyFadeStarted_) / 200);
        if (alpha < 1) {
            asset(previousBody_, area, false, {}, opacity * (1 - alpha));
        }
        asset(target, area, false, {}, opacity * alpha);
        if (alpha >= 1) {
            previousBody_.clear();
            bodyFadeStarted_ = 0;
        }
        return true;
    }

    bool portraitAsset(const std::string& id, CdRect area, double opacity = 1, bool includeBody = true) {
        if (!layeredPortrait(id)) {
            return asset(id, area, false, {}, opacity);
        }
        const auto& layers = visual_.at("layers");
        const auto width = layers.at("width").get<double>(), height = layers.at("height").get<double>();
        const auto scale = std::min(area.width / width, area.height / height);
        const CdRect body{area.x + (area.width - width * scale) / 2, area.y + (area.height - height * scale) / 2,
                          width * scale, height * scale};
        const auto anchor = layers.at("faces").at(id).get<std::array<double, 4>>();
        const CdRect face{body.x + anchor[0] * body.width, body.y + anchor[1] * body.height, anchor[2] * body.width,
                          anchor[3] * body.height};
        const bool ready = !includeBody || drawLayerBody(area, opacity);
        return asset(id, face, false, {}, opacity, true) && ready;
    }

    void drawPortrait(CdRect area) {
        const auto target = desiredPortrait();
        if (target != portrait_) {
            previousPortrait_ = portrait_;
            portrait_ = target;
            fadeStarted_ = 0;
        }
        if (previousPortrait_.empty()) {
            portraitAsset(target, area);
            return;
        }
        const auto sharedBody = layeredPortrait(target) && layeredPortrait(previousPortrait_);
        if (sharedBody) {
            drawLayerBody(area);
        }
        // Wait for verified pixels before beginning the transition. Image-service
        // completion requests the next local redraw; no loading poll is needed.
        if (!portraitAsset(target, area, 0, !sharedBody)) {
            portraitAsset(previousPortrait_, area, 1, !sharedBody);
            portraitAsset(target, area, 0, !sharedBody);
            return;
        }
        if (!fadeStarted_) {
            fadeStarted_ = presentationNow();
            startPresentationTimer();
        }
        const auto alpha = std::min(1., double(presentationNow() - fadeStarted_) / 200);
        if (alpha < 1) {
            portraitAsset(previousPortrait_, area, 1 - alpha, !sharedBody);
        }
        portraitAsset(target, area, alpha, !sharedBody);
        if (alpha >= 1) {
            previousPortrait_.clear();
            fadeStarted_ = 0;
        }
    }

    bool asset(const std::string& id, CdRect area, bool cover = false, std::string pack = {}, double opacity = 1,
               bool stretch = false) {
        if (status_ == "loading_assets" || status_ == "loading") {
            return false;
        }
        if (!images_.request) {
            return false;
        }
        CdCharacterImage snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        const auto edge = uint32_t(std::clamp(std::ceil(std::max(area.width, area.height) * dpr_), 64., 2048.));
        if (pack.empty()) {
            pack = resourcePack_;
        }
        const auto result = versionedImages_.request && !pack.empty() && pack != "builtin-xiaqing-v1"
                                ? versionedImages_.request(context_, pack.c_str(), id.c_str(), edge, &snapshot)
                                : images_.request(context_, id.c_str(), edge, &snapshot);
        if (result != CD_OK || snapshot.state == CD_CHARACTER_IMAGE_FAILED) {
            canvas_.text(snapshot.error ? cdsdk::fontWide(snapshot.error) : L"素材服务暂不可用", 12,
                         {area.x + 10, area.y + area.height * .45, std::max(1., area.width - 20), 70}, 0xEDB9A9FF);
            return false;
        }
        if (snapshot.state != CD_CHARACTER_IMAGE_READY) {
            return false;
        }
        if (!snapshot.bgra || !snapshot.width || !snapshot.height || snapshot.width > 2048 || snapshot.height > 2048 ||
            snapshot.stride != snapshot.width * 4) {
            return false;
        }
        const auto cacheId = pack + '/' + id;
        auto& entry = assetCache_[cacheId];
        entry.use = ++assetClock_;
        if (!entry.image || entry.generation != snapshot.generation) {
            const auto count = size_t(snapshot.stride) * snapshot.height;
            entry.image = cybercanvas::ImageData::fromPremultipliedBgra(
                snapshot.width, snapshot.height, std::vector<uint8_t>(snapshot.bgra, snapshot.bgra + count));
            entry.generation = snapshot.generation;
        }
        while (assetCache_.size() > 4) {
            auto oldest = assetCache_.end();
            for (auto it = assetCache_.begin(); it != assetCache_.end(); ++it) {
                if (it->first != cacheId && (oldest == assetCache_.end() || it->second.use < oldest->second.use)) {
                    oldest = it;
                }
            }
            if (oldest == assetCache_.end()) {
                break;
            }
            assetCache_.erase(oldest);
        }
        cybercanvas::ImagePlacement placement;
        placement.fit = stretch ? cybercanvas::ImageFit::Stretch
                        : cover ? cybercanvas::ImageFit::Cover
                                : cybercanvas::ImageFit::Contain;
        placement.opacity = opacity;
        canvas_.image(*entry.image, area, placement);
        return true;
    }

    bool command(Json command) {
        if (!chat_.command) {
            return false;
        }
        command["schema_version"] = 1;
        const auto bytes = command.dump();
        if (chat_.command(context_, bytes.data(), uint32_t(bytes.size())) != CD_OK) {
            error_ = "操作未接受，请稍后重试。";
            redraw();
            return false;
        }
        return true;
    }

    void send() {
        if (draft_.empty() || status_ == "generating" || status_ == "loading_assets" || status_ == "loading") {
            return;
        }
        persistPlayback();
        if (command({{"op", "send"}, {"text", draft_}})) {
            pendingDraft_ = draft_;
            error_.clear();
            status_ = "generating";
        }
        redraw();
    }

    void redraw(uint32_t layers = CD_COMPOSITION_FOREGROUND) {
        if (liveMode() && composition_.invalidate) {
            composition_.invalidate(context_, layers);
            return;
        }
        if (host_.request_redraw) {
            host_.request_redraw(context_);
        }
    }

    static void CD_CALL onText(void* context, uint32_t event, const char* bytes, uint32_t length) noexcept {
        try {
            auto& self = *static_cast<CharacterChatPlugin*>(context);
            if (length <= 16000) {
                self.draft_.assign(bytes, length);
            }
            if (event == CD_TEXT_SUBMIT) {
                self.send();
            } else if (event == CD_TEXT_CHANGED || event == CD_TEXT_FOCUS_LOST) {
                self.command({{"op", "draft"}, {"text", self.draft_}});
            }
            self.redraw();
        } catch (...) {
        }
    }

    static void CD_CALL onSnapshot(void* context, const char* bytes, uint32_t length) noexcept {
        try {
            auto& self = *static_cast<CharacterChatPlugin*>(context);
            auto view = characterchat::parseBoundedDocument(std::string_view(bytes, length), 256 * 1024, 65536);
            const auto& roles = view.at("characters");
            const auto selectedRole = view.at("selected").get<size_t>();
            if (selectedRole < roles.size()) {
                const auto roleId = roles[selectedRole].at("id").get<std::string>();
                if (roleId != self.roleId_) {
                    self.activateRole(roleId);
                }
            }
            const auto previousScene = self.scene_;
            const auto previousPack = self.resourcePack_;
            const auto previousResourceEpoch = self.resourceEpoch_;
            self.resourceEpoch_ = view.value("resource_epoch", uint64_t(0));
            if (previousResourceEpoch != self.resourceEpoch_) {
                self.assetCache_.clear();
            }
            self.emotion_ = "neutral";
            self.expression_.clear();
            self.visual_ = view.value("visual", Json::object());
            self.visualName_ = view.value("visual_name", "夏晴示范包");
            self.liveBindings_ = view.value("live_bindings", Json::object());
            self.liveMotions_.clear();
            self.liveExpressions_.clear();
            self.liveFingerprint_.clear();
            if (self.liveMode() && view.value("live_model_source", "") == self.modelDirectory_ &&
                view.contains("live_model") && view["live_model"].contains("motions")) {
                self.liveMotions_ = view["live_model"]["motions"].get<std::vector<std::string>>();
                self.liveExpressions_ = view["live_model"].value("expressions", std::vector<std::string>{});
                self.liveFingerprint_ = view["live_model"].value("fingerprint", "");
            } else if (self.liveMode()) {
                self.liveBindings_ = Json::object();
            }
            const bool wasGenerating = self.status_ == "generating";
            self.status_ = view.at("status").get<std::string>();
            self.error_ = view.at("error").get<std::string>();
            self.resourcePack_ = view.value("resource_pack", "");
            self.syncPresentation();
            self.offline_ = view.at("offline").get<bool>();
            self.story_ = view.contains("controls") ? view["controls"].value("story", true) : true;
            self.gesturesEnabled_ = view.contains("controls") ? view["controls"].value("gestures", true) : true;
            if (!self.gesturesEnabled_ && !self.manualPose_) {
                self.activePose_.clear();
            }
            const CdSettingValue toggles[] = {
                {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "story", int64_t(self.story_)},
                {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "offline", int64_t(self.offline_)}};
            self.settings.apply(toggles, 2);
            const auto nextSelected = view.at("selected").get<size_t>();
            const auto nextKey = view.contains("session") ? view["session"].at("session_id").get<std::string>() + "/" +
                                                                view["session"].at("branch_id").get<std::string>()
                                                          : "";
            const bool changedRole = nextKey != self.sessionKey_;
            if (changedRole) {
                self.stopPresentationTimer();
                self.activePose_.clear();
                self.previousPortrait_.clear();
                self.portrait_.clear();
                self.body_.clear();
                self.previousBody_.clear();
                self.bodyFadeStarted_ = 0;
                self.idleMoodActive_ = false;
                self.moodReturnDue_ = 0;
                self.historyInitialized_ = false;
                self.playbackBeats_ = Json::array();
                self.playbackTurn_ = 0;
                self.followDialogue_ = true;
                self.dialogueInteraction_.cancel();
                self.drafts_[self.selected_] = self.draft_;
                self.selected_ = nextSelected;
                self.draft_ = view.value("draft", "");
                self.sessionKey_ = nextKey;
                self.pendingDraft_.clear();
                self.lastCommitted_.clear();
                if (self.input_.set_text) {
                    self.input_.set_text(self.context_, self.draft_.data(), uint32_t(self.draft_.size()));
                }
            }
            self.selected_ = nextSelected;
            self.choiceTexts_.clear();
            const auto& characters = view.at("characters");
            self.roleCount_ = characters.size();
            if (self.selected_ < characters.size()) {
                self.name_ = characters[self.selected_].at("name").get<std::string>();
                self.dialogue_ = characters[self.selected_].value("greeting", "");
            }
            if (view.contains("active_character")) {
                self.name_ = view["active_character"].value("name", self.name_);
                self.dialogue_ = view["active_character"].value("greeting", self.dialogue_);
            }
            const auto& history = view.at("history");
            self.attachments_ = Json::array();
            if (!history.empty()) {
                const auto& latest = history.back();
                self.attachments_ = latest.at("attachments");
                self.attachmentIdentity_ = latest.at("identity");
                const auto& reply = latest.at("reply");
                self.scene_ = reply.at("scene_id").get<std::string>();
                self.dialogue_.clear();
                for (const auto& beat : reply.at("beats")) {
                    const auto text = beat.value("text", "");
                    if (!text.empty()) {
                        if (!self.dialogue_.empty()) {
                            self.dialogue_ += "\n\n";
                        }
                        self.dialogue_ += text;
                    }
                    if (beat.contains("emotion") && beat["emotion"].is_string()) {
                        self.emotion_ = beat["emotion"].get<std::string>();
                    }
                    if (beat.contains("expression") && beat["expression"].is_string()) {
                        self.expression_ = beat["expression"].get<std::string>();
                    }
                }
                for (const auto& choice : reply.at("choices")) {
                    self.choiceTexts_.push_back(choice.at("text").get<std::string>());
                }
                const auto committed = latest.at("identity").dump();
                if (committed != self.lastCommitted_) {
                    self.idleMoodActive_ = false;
                    self.moodReturnDue_ = 0;
                    if (self.historyInitialized_ && self.gesturesEnabled_) {
                        for (const auto& beat : reply.at("beats")) {
                            if (beat.contains("gesture")) {
                                self.previewPose(beat.at("gesture").get<std::string>(), false);
                                break;
                            }
                        }
                    }
                    self.playbackBeats_ = reply.at("beats");
                    self.graphemes_ = latest.at("graphemes").get<std::vector<std::vector<std::string>>>();
                    self.playbackTurn_ = view.at("session").at("committed_turns").get<uint64_t>();
                    if (!wasGenerating) {
                        self.followDialogue_ = true;
                    }
                    self.persistPlayback();
                }
                if (self.status_ == "idle" && committed != self.lastCommitted_ && !self.pendingDraft_.empty() &&
                    latest.at("user_text") == self.pendingDraft_ && self.draft_ == self.pendingDraft_) {
                    self.draft_.clear();
                    self.pendingDraft_.clear();
                    if (self.input_.set_text) {
                        self.input_.set_text(self.context_, "", 0);
                    }
                }
                self.lastCommitted_ = committed;
            }
            if (view.contains("provisional") && view["provisional"].is_object()) {
                self.dialogue_.clear();
                for (const auto& beat : view["provisional"].at("beats")) {
                    if (!self.dialogue_.empty()) {
                        self.dialogue_ += "\n\n";
                    }
                    self.dialogue_ += beat.at("text").get<std::string>();
                    if (beat.contains("emotion") && beat["emotion"].is_string()) {
                        self.emotion_ = beat["emotion"].get<std::string>();
                    }
                }
            }
            if (view.contains("session")) {
                self.scene_ = view["session"].at("scene_id").get<std::string>();
                const auto weights = view["session"].at("mood_weights").get<std::array<double, 8>>();
                const auto dominant = size_t(std::max_element(weights.begin(), weights.end()) - weights.begin());
                self.idleEmotion_ = characterchat::emotionId(static_cast<characterchat::Emotion>(dominant));
            }
            if (!characterchat::validIdentifier(self.scene_)) {
                self.scene_ = "cafe";
            }
            if (!characterchat::parseEmotion(self.emotion_)) {
                self.emotion_ = "neutral";
            }
            if (self.status_ == "idle") {
                self.historyInitialized_ = true;
            }
            self.applyLiveExpression();
            if (view.contains("session") && view.value("renderer_live2d", false) != self.liveMode()) {
                self.command({{"op", "renderer"}, {"kind", self.liveMode() ? "live2d" : "image"}});
            }
            self.armMoodReturn();
            self.redraw(previousScene == self.scene_ && previousPack == self.resourcePack_ &&
                                previousResourceEpoch == self.resourceEpoch_
                            ? CD_COMPOSITION_FOREGROUND
                            : CD_COMPOSITION_BACKGROUND | CD_COMPOSITION_FOREGROUND);
        } catch (...) {
        }
    }

    void icon(const std::string& name, CdRect area) {
        if (!icons_.raster) {
            return;
        }
        const unsigned size = unsigned(std::clamp(20. * dpr_, 12., 128.));
        auto& image = iconCache_[name];
        if (!image) {
            std::vector<uint8_t> pixels(size * size * 4);
            CdFrame buffer{sizeof(buffer), size, size, size * 4, pixels.size(), pixels.data(), dpr_};
            if (icons_.raster(context_, name.c_str(), 0xF2FBFFFF, &buffer) != CD_OK) {
                return;
            }
            for (size_t index = 0; index < pixels.size(); index += 4) {
                std::swap(pixels[index], pixels[index + 2]);
            }
            image = cybercanvas::ImageData::fromPremultipliedBgra(size, size, std::move(pixels));
        }
        canvas_.image(*image, {area.x + (area.width - 20) / 2, area.y + (area.height - 20) / 2, 20, 20});
    }

    cdsdk::Canvas canvas_;
    CdHostApi host_{};
    void* context_;
    CdCharacterChatApi chat_{};
    CdCharacterImagesApi images_{};
    CdCharacterVersionedImagesApi versionedImages_{};
    CdCompositionApi composition_{};
    CdLive2DApi live2d_{};
    CdLive2DActionsApi actions_{};
    std::filesystem::path directory_;
    unsigned nextTap_ = 0;
    uint32_t pressTarget_ = 0;
    CdTextInputApi input_{};
    CdIconsApi icons_{};
    CdInteractionApi interaction_{};
    CdSettingsChoicesApi settingsChoices_{};
    std::string publishedChoices_, roleId_, previewScope_, liveFingerprint_;
    Json roleSettings_ = Json::object(), previewSettings_ = Json::object();
    double width_ = 600, height_ = 400, dpr_ = 1;
    CdRect editor_{}, send_{}, role_{}, mode_{}, connection_{}, model_{}, action1_{}, action2_{};
    bool pressed_ = false, offline_ = true;
    size_t selected_ = 1;
    size_t roleCount_ = 4;
    std::string draft_, error_, status_ = "loading", name_ = "夏晴", dialogue_ = "正在读取人物…";
    std::string pendingDraft_, lastCommitted_;
    std::string sessionKey_;
    std::string resourcePack_;
    std::string visualName_ = "夏晴示范包";
    uint64_t resourceEpoch_ = 0;
    std::map<size_t, std::string> drafts_;
    std::string emotion_ = "neutral", scene_ = "cafe";
    Json visual_ = Json::object();
    Json liveBindings_ = Json::object();
    std::vector<std::string> liveMotions_, liveExpressions_;
    std::string idleEmotion_ = "neutral";
    bool idleMoodActive_ = false;
    uint64_t moodReturnDue_ = 0;
    std::string modelDirectory_, appliedLiveExpression_;
    std::string expression_, activePose_, portrait_, previousPortrait_;
    std::string body_, previousBody_;
    uint64_t bodyFadeStarted_ = 0;
    uint64_t presentationTimer_ = 0, poseUntil_ = 0, nextStoryPose_ = 0, fadeStarted_ = 0;
    bool manualPose_ = false, historyInitialized_ = false, gesturesEnabled_ = true;
    std::vector<std::string> choiceTexts_;
    std::vector<std::pair<CdRect, std::string>> choices_;
    Json attachments_ = Json::array(), attachmentIdentity_;
    Json playbackBeats_ = Json::array();
    std::vector<std::vector<std::string>> graphemes_;
    uint64_t playbackTurn_ = 0;
    bool story_ = true, paused_ = false;
    cybercanvas::ScrollView dialogueScroll_;
    cdsdk::Interaction dialogueInteraction_;
    bool followDialogue_ = true;
    std::string measuredDialogue_;
    double dialogueWidth_ = 0, dialogueHeight_ = 0, dialogueFont_ = 0;
    std::vector<CdRect> attachmentRects_;
    std::map<std::string, std::shared_ptr<const cybercanvas::ImageData>> iconCache_;

    struct CachedImage {
        uint64_t generation = 0, use = 0;
        std::shared_ptr<const cybercanvas::ImageData> image;
    };

    std::map<std::string, CachedImage> assetCache_;
    uint64_t assetClock_ = 0;
};

}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<CharacterChatPlugin>::query(abi, output, size, "com.cyberdesktop.character-chat",
                                                            "0.1.0");
}
