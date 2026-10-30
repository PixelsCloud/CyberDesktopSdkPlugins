#include <sdk/Canvas.h>
#include <sdk/Controls.h>
#include <sdk/Scene.h>
#include <sdk/SceneList.h>

#include <cstring>
#include <iostream>
#include <map>

namespace {
CdUiCallback callback = nullptr;
void* callbackUser = nullptr;
int publishes = 0;
bool hostFocused = false;
std::vector<uint32_t> ids;
std::map<uint32_t, std::string> names;

CdResult CD_CALL connect(void*, CdUiCallback value, void* user) {
    callback = value;
    callbackUser = user;
    hostFocused = false;
    return CD_OK;
}

CdResult CD_CALL focus(void*, uint32_t value) {
    if (hostFocused == bool(value)) {
        return CD_OK;
    }
    hostFocused = bool(value);
    CdUiEvent event{sizeof(event), CD_UI_FOCUS, 0, 0, 0, 0, double(value)};
    return callback ? callback(callbackUser, &event) : CD_ERROR_UNSUPPORTED;
}

CdResult CD_CALL publish(void*, const CdSemanticNode* nodes, uint32_t count) {
    ++publishes;
    ids.clear();
    names.clear();
    for (uint32_t i = 0; i < count; ++i) {
        if (!nodes[i].name || !nodes[i].value) {
            return CD_ERROR_ARGUMENT;
        }
        ids.push_back(nodes[i].id);
        names.emplace(nodes[i].id, nodes[i].name);
    }
    return CD_OK;
}

CdResult CD_CALL query(void*, const char* name, uint32_t version, void* output, uint32_t capacity) {
    if (std::strcmp(name, CD_SERVICE_KEYBOARD) || version != 1 || capacity < sizeof(CdKeyboardApi)) {
        return CD_ERROR_UNSUPPORTED;
    }
    *static_cast<CdKeyboardApi*>(output) = {sizeof(CdKeyboardApi), 1, connect, focus, publish};
    return CD_OK;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    int failures = 0;
    const auto check = [&](const char* name, bool value) {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        failures += !value;
    };
    try {
        cdsdk::Canvas canvas;
        CdServiceHostApi host{};
        host.input.media.base.struct_size = sizeof(host);
        host.query_service = query;
        CdCreateContext context{sizeof(context), 1, "test", "test", ".", &host.input.media.base};
        {
            cdsdk::Controls controls;
            uint32_t activated = 0;
            controls.bind(context, [&](uint32_t id) {
                activated = id;
            });
            const std::vector<cdsdk::Controls::Item> items{{1, {0, 0, 30, 20}, "Start"},
                                                           {2, {35, 0, 30, 20}, "Disabled", false},
                                                           {3, {70, 0, 30, 20}, "Task", true, true, true}};
            controls.items(items, {10, 20});
            const auto before = publishes;
            controls.items(items, {10, 20});
            check("immediate controls do not republish unchanged accessibility", publishes == before);
            CdUiEvent key{sizeof(key), CD_UI_KEY, CD_KEY_ENTER};
            check("immediate controls reject keys before focus grant",
                  callback(callbackUser, &key) == CD_ERROR_UNSUPPORTED);
            focus(nullptr, 1);
            key.key = CD_KEY_TAB;
            callback(callbackUser, &key);
            key.key = CD_KEY_ENTER;
            callback(callbackUser, &key);
            check("immediate controls support keyboard activation", activated == 1);
            key.key = CD_KEY_TAB;
            callback(callbackUser, &key);
            key.key = CD_KEY_SPACE;
            callback(callbackUser, &key);
            check("focus navigation skips disabled controls", activated == 3);
            activated = 0;
            CdInput press{};
            press.struct_size = sizeof(press);
            press.kind = CD_INPUT_PRESS;
            press.buttons = CD_BUTTON_LEFT;
            press.x = 15;
            press.y = 25;
            controls.input(press);
            auto renamed = items;
            renamed[0].name = "Different item";
            controls.items(renamed, {10, 20});
            press.kind = CD_INPUT_RELEASE;
            press.buttons = 0;
            controls.input(press);
            check("data replacement cancels pressed identity", activated == 0);
            press.kind = CD_INPUT_PRESS;
            press.buttons = CD_BUTTON_LEFT;
            controls.input(press);
            press.kind = CD_INPUT_RELEASE;
            press.buttons = 0;
            controls.input(press);
            check("pointer uses translated control geometry before first frame", activated == 1);
            controls.cancel();
            check("hidden controls clear accessibility and keyboard focus",
                  ids.empty() && !hostFocused && callback(callbackUser, &key) == CD_ERROR_UNSUPPORTED);
            controls.items(items);
            focus(nullptr, 1);
            key.key = CD_KEY_TAB;
            callback(callbackUser, &key);
            activated = 0;
            key.key = CD_KEY_ENTER;
            callback(callbackUser, &key);
            check("controls reacquire host focus after cancellation", activated == 1);
        }
        check("immediate control destruction unregisters keyboard callback", callback == nullptr);
        {
            cdsdk::Scene scene;
            View view;
            view.kind = ViewKind::Button;
            view.key = 10;
            view.text = L"Refresh";
            const auto button = scene.add(0, view);
            view.kind = ViewKind::Slider;
            view.key = 20;
            view.accessibleName = L"Volume";
            view.maximum = 100;
            view.step = 5;
            const auto slider = scene.add(0, view);
            view.kind = ViewKind::Toggle;
            view.key = 30;
            view.maximum = 1;
            const auto toggle = scene.add(0, view);
            scene.bind(context);
            scene.arrange(canvas, {0, 0, 200, 200}, 2);
            auto event = CdUiEvent{sizeof(CdUiEvent), CD_UI_KEY, 1};
            check("keys before host grant are ignored",
                  callback(callbackUser, &event) == CD_ERROR_UNSUPPORTED && !scene.focused());
            check("focus request uses optional host grant", scene.keyboard().request());
            int actions = 0;
            scene.onAction([&](const auto& result) {
                actions += result.clicked;
            });
            callback(callbackUser, &event);
            check("Tab enters first control", scene.focused() == button);
            event.key = 6;
            callback(callbackUser, &event);
            check("Enter activates a button", actions == 1);
            event.key = 1;
            callback(callbackUser, &event);
            check("Tab follows current scene order", scene.focused() == slider);
            event.key = 3;
            callback(callbackUser, &event);
            check("slider arrow adjusts controlled value", scene.view(slider).value == 5);
            event.key = 10;
            callback(callbackUser, &event);
            check("End clamps to slider maximum", scene.view(slider).value == 100);
            const auto semantic = scene.semantics();
            check("slider exposes name role and value", semantic[1].name == L"Volume" &&
                                                            semantic[1].role == SemanticRole::Slider &&
                                                            semantic[1].number == 100);
            scene.move(slider, 0, button);
            scene.arrange(canvas, {0, 0, 200, 200});
            check("moving a node preserves valid focus", scene.focused() == slider);
            scene.invalidate(true);
            const auto pendingSemantics = scene.semantics();
            check("pending layout exposes no stale focus or focusable semantic bounds",
                  !scene.focused() &&
                      std::none_of(pendingSemantics.begin(), pendingSemantics.end(), [](const auto& node) {
                          return node.focusable;
                      }));
            scene.arrange(canvas, {0, 0, 200, 200});
            check("rearranging valid bounds retains the logical focus", scene.focused() == slider);
            scene.arrange(canvas, {0, 0, 200, 0});
            check("fully clipped controls cannot retain or acquire focus", !scene.focused() && !scene.focus(slider));
            scene.arrange(canvas, {0, 0, 200, 200});
            check("restoring size does not resurrect focus from a hidden control", !scene.focused());
            scene.focus(slider);
            scene.accessibleName(button, std::wstring(10000, L'汉'));
            check("long accessible names are bounded at a valid UTF-8 boundary",
                  !names[button].empty() && names[button].size() <= 4096 &&
                      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, names[button].data(),
                                          int(names[button].size()), nullptr, 0) > 0);
            scene.enabled(slider, false);
            check("disabling clears focus immediately", !scene.focused());
            event.key = 1;
            event.modifiers = CD_MOD_SHIFT;
            callback(callbackUser, &event);
            check("Shift Tab skips disabled controls", scene.focused() == toggle);
            event.key = 7;
            event.modifiers = 0;
            callback(callbackUser, &event);
            check("Space toggles checked semantic state",
                  scene.view(toggle).value == 1 && scene.semantics().back().checked);
            scene.visible(toggle, false);
            check("hidden node loses focus and semantic object",
                  !scene.focused() && std::find(ids.begin(), ids.end(), toggle) == ids.end());
            scene.arrange(canvas, {0, 0, 200, 200});
            scene.focus(button);
            scene.remove(button);
            check("removed node loses focus and semantic object",
                  !scene.focused() && std::find(ids.begin(), ids.end(), button) == ids.end());
            focus(nullptr, 0);
            check("focus loss revokes keyboard delivery", callback(callbackUser, &event) == CD_ERROR_UNSUPPORTED);
            const int previous = publishes;
            scene.keyboard().sync();
            check("unchanged semantics do not republish", previous == publishes);
        }
        check("destruction unregisters callback", !callback && !callbackUser);
        {
            cdsdk::SceneList card;
            card.bind(context);
            card.list().items({{"first", 40}, {"second", 40}, {"third", 40}});
            card.list().actions([](const auto& row) {
                return std::vector<ListAction>{{7, {row.bounds.x, row.bounds.y, 20, 20}}};
            });
            card.arrange(canvas, {0, 0, 160, 50});
            card.scene().keyboard().request();
            card.scene().focus(card.body());
            ListEvent row;
            card.onRowAction([&](const auto& value) {
                row = value;
            });
            auto event = CdUiEvent{sizeof(CdUiEvent), CD_UI_KEY, 5};
            callback(callbackUser, &event);
            check("focused list Down selects a stable ID", row.id == "first" && !row.clicked);
            callback(callbackUser, &event);
            check("list navigation scrolls selected row into view", row.id == "second" && card.list().offset() > 0);
            event.key = 3;
            callback(callbackUser, &event);
            event.key = 6;
            callback(callbackUser, &event);
            check("list row actions can be selected and activated",
                  row.clicked && row.id == "second" && row.action == 7);
            check("list semantics expose the current selection",
                  card.scene().semantics().front().role == SemanticRole::List &&
                      card.scene().semantics().front().value == L"second");
            card.list().items({{"new", 40}, {"second", 40}});
            check("list reordering preserves the selected business ID", card.list().selected() == "second");
        }
        CdHostApi old{};
        old.struct_size = sizeof(old);
        context.host = &old;
        cdsdk::Scene legacy;
        legacy.bind(context);
        check("old hosts keep pointer-only operation", !legacy.keyboard().available());
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
