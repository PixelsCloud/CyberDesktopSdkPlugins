#include <sdk/Interaction.h>

#include <cstring>
#include <iostream>

namespace {
struct Host {
    bool enabled = false, captured = false, inPress = false;
};

CdResult CD_CALL enable(void* pointer, uint32_t value) {
    static_cast<Host*>(pointer)->enabled = value != 0;
    return CD_OK;
}

CdResult CD_CALL capture(void* pointer, uint32_t value) {
    auto& host = *static_cast<Host*>(pointer);
    if (value && !host.inPress) {
        return CD_ERROR_ARGUMENT;
    }
    host.captured = value != 0;
    return CD_OK;
}

CdResult CD_CALL query(void*, const char* id, uint32_t version, void* output, uint32_t size) {
    if (std::strcmp(id, CD_SERVICE_INTERACTION) || version != 1 || size < sizeof(CdInteractionApi)) {
        return CD_ERROR_UNSUPPORTED;
    }
    *static_cast<CdInteractionApi*>(output) = {sizeof(CdInteractionApi), 1, enable, capture};
    return CD_OK;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value;
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    };
    Host host;
    CdServiceHostApi api{};
    api.input.media.base.struct_size = sizeof(api);
    api.query_service = query;
    CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, "test", "test", ".", &api.input.media.base, &host};
    cdsdk::Interaction input;
    input.bind(context);
    check("optional service enables pointer streaming", input.streaming() && host.enabled);
    input.router.regions({{1, HitShape::rectangle({0, 0, 100, 10}), PointerBehavior::Drag}});
    CdInput event{sizeof(event), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 5, 5};
    host.inPress = true;
    check("drag begins with synchronous host capture", input.input(event).drag == DragPhase::Begin && host.captured);
    host.inPress = false;
    event.kind = CD_INPUT_MOVE;
    event.x = 200;
    check("captured moves survive outside bounds", input.input(event).drag == DragPhase::Update && host.captured);
    event.kind = CD_INPUT_RELEASE;
    event.buttons = 0;
    check("release clears host capture", input.input(event).drag == DragPhase::End && !host.captured);
    event.kind = CD_INPUT_PRESS;
    event.buttons = CD_BUTTON_LEFT;
    event.x = 5;
    check("rejected capture cancels the local gesture",
          input.input(event).drag == DragPhase::Cancel && !input.router.captured());
    host.inPress = true;
    input.input(event);
    host.inPress = false;
    event.kind = CD_INPUT_CANCEL;
    check("host cancellation clears both states", input.input(event).drag == DragPhase::Cancel && !host.captured);
    host.inPress = true;
    event.kind = CD_INPUT_PRESS;
    input.input(event);
    host.inPress = false;
    check("removing a captured target releases the host claim",
          input.regions({}).drag == DragPhase::Cancel && !host.captured);
    CdHostApi legacy{sizeof(legacy), CD_PLUGIN_ABI_VERSION};
    context.host = &legacy;
    cdsdk::Interaction fallback;
    fallback.bind(context);
    fallback.router.regions({{3, HitShape::rectangle({0, 0, 10, 10})}});
    event.kind = CD_INPUT_PRESS;
    fallback.input(event);
    event.kind = CD_INPUT_RELEASE;
    event.buttons = 0;
    check("old hosts retain click support", !fallback.streaming() && fallback.input(event).clicked == 3);
    check("public ABI sizes remain stable",
          sizeof(CdHostApi) == 56 && sizeof(CdInstanceApi) == 88 && sizeof(CdInteractionApi) == 24);
    return passed ? 0 : 1;
}
