#include <sdk/Canvas.h>
#include <sdk/Diagnostics.h>
#include <sdk/Scene.h>

#include <cstring>
#include <iostream>
#include <map>
#include <thread>

namespace {
std::map<uint64_t, std::pair<CdDiagnosticCallback, void*>> sources;
uint64_t next = 1;

CdResult CD_CALL attach(void*, CdDiagnosticCallback callback, void* user, uint64_t* token) {
    *token = next++;
    sources.emplace(*token, std::make_pair(callback, user));
    return CD_OK;
}

void CD_CALL detach(void*, uint64_t token) {
    sources.erase(token);
}

CdResult CD_CALL query(void*, const char* id, uint32_t version, void* output, uint32_t size) {
    if (std::strcmp(id, CD_SERVICE_DIAGNOSTICS) || version != 1 || size < sizeof(CdDiagnosticsApi)) {
        return CD_ERROR_UNSUPPORTED;
    }
    *static_cast<CdDiagnosticsApi*>(output) = {sizeof(CdDiagnosticsApi), 1, attach, detach};
    return CD_OK;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    int failed = 0;
    const auto check = [&](const char* name, bool passed) {
        failed += !passed;
        std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
    };
    try {
        cdsdk::Canvas canvas;
        CdServiceHostApi host{};
        host.input.media.base.struct_size = sizeof(host);
        host.query_service = query;
        CdCreateContext context{sizeof(context), 1, "test", "one", ".", &host.input.media.base};
        cdsdk::RedrawRequests redraw;
        redraw.bind(context);
        cdsdk::Images images;
        {
            cdsdk::Scene scene;
            scene.bind(context);
            View button;
            button.kind = ViewKind::Button;
            button.key = 1;
            button.text = L"Refresh";
            const auto first = scene.add(0, button);
            View slider;
            slider.kind = ViewKind::Slider;
            slider.key = 2;
            const auto volume = scene.add(0, slider);
            scene.arrange(canvas, {0, 0, 160, 100});
            scene.keyboard().origin({11, 7});
            std::vector<uint8_t> pixels(160 * 100 * 4);
            CdFrame frame{sizeof(frame), 160, 100, 640, pixels.size(), pixels.data(), 1};
            canvas.render(frame, {}, [&](auto&, auto) {
                scene.draw(canvas);
            });
            scene.diagnostics().extras([&](auto& value) {
                value.redraw = redraw.stats();
                value.images = images.stats();
                value.sharedImages = images.sharedStoreStats();
            });
            redraw.invalidate();
            redraw.invalidate();
            check("one source registration belongs to one scene",
                  sources.size() == 1 && scene.diagnostics().available());
            const auto source = sources.begin()->second;
            CdDiagnosticStats stats{sizeof(stats)};
            uint32_t required = 0;
            check("size query returns stats and required node count",
                  source.first(source.second, &stats, nullptr, 0, &required) == CD_ERROR_BUFFER && required == 3 &&
                      stats.values[CD_DIAG_FRAMES] == 1 && stats.values[CD_DIAG_SCENE_LAYOUTS] == 1 &&
                      stats.version == 1);
            check("redraw statistics separate invalidation and forwarded requests",
                  stats.values[CD_DIAG_INVALIDATIONS] == 2 && stats.values[CD_DIAG_FRAME_REQUESTS] == 1 &&
                      stats.values[CD_DIAG_COALESCED] == 1);
            CdDebugNode tinyNode;
            std::memset(&tinyNode, 0xDA, sizeof(tinyNode));
            const auto before = tinyNode;
            check("undersized tree buffer is left untouched",
                  source.first(source.second, &stats, &tinyNode, 1, &required) == CD_ERROR_BUFFER &&
                      std::memcmp(&tinyNode, &before, sizeof(tinyNode)) == 0);
            std::vector<CdDebugNode> tree(required);
            check("full snapshot has logical bounds and effective hit clips",
                  source.first(source.second, &stats, tree.data(), uint32_t(tree.size()), &required) == CD_OK &&
                      tree[0].bounds.x == 11 && tree[0].bounds.y == 7 && tree[2].hit.width > 0 &&
                      tree[2].hit.x >= tree[2].clip.x && tree[2].hit.y >= tree[2].clip.y);
            scene.focusGranted(true);
            scene.focus(volume);
            source.first(source.second, &stats, tree.data(), uint32_t(tree.size()), &required);
            check("diagnostics expose focus without painting",
                  (tree[2].state & CD_DEBUG_FOCUSED) && stats.values[CD_DIAG_FRAMES] == 1);
            scene.visible(first, false);
            source.first(source.second, &stats, tree.data(), uint32_t(tree.size()), &required);
            check("dirty layouts are marked rather than recalculated by sampling",
                  (tree[0].state & CD_DEBUG_LAYOUT_DIRTY) && !(tree[1].state & CD_DEBUG_VISIBLE) &&
                      scene.stats().layouts == 1);
            const auto path = std::filesystem::temp_directory_path() /
                              (L"cd-sdk-diagnostic-" + std::to_wstring(GetCurrentProcessId()) + L".png");
            if (!canvas.save(path.wstring(), frame)) {
                return 1;
            }
            images.set("first", path);
            images.set("same", path);
            for (int i = 0; i < 1000 && images.pending(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            images.poll();
            source.first(source.second, &stats, tree.data(), uint32_t(tree.size()), &required);
            check("per-instance slots deduplicate shared payload charges",
                  stats.values[CD_DIAG_IMAGE_REQUESTS] == 2 && stats.values[CD_DIAG_IMAGE_READY] == 2 &&
                      stats.values[CD_DIAG_IMAGE_REUSED] == 1 && stats.values[CD_DIAG_SLOT_BYTES] == pixels.size() &&
                      stats.values[CD_DIAG_SHARED_DECODES] == 1 && stats.values[CD_DIAG_REFERENCED_DECODE_NS] > 0);
            View broken;
            broken.kind = ViewKind::Image;
            broken.resourceImage = true;
            broken.resource = {
                ImageState::Failed, {}, "missing image", std::make_shared<const std::wstring>(L"C:/assets/不存在.png")};
            scene.add(0, broken);
            tree.resize(4);
            source.first(source.second, &stats, tree.data(), uint32_t(tree.size()), &required);
            check("resource diagnostics identify the failed node and source",
                  (tree.back().state & CD_DEBUG_ERROR) && std::strstr(tree.back().error, "missing image") &&
                      std::strstr(tree.back().resource, "C:/assets/"));
            std::filesystem::remove(path);
            scene.diagnostics().bind(context);
            check("rebinding replaces the registration", sources.size() == 1);
        }
        check("scene destruction unregisters its borrowed callbacks", sources.empty());
        const auto referenced = images.stats().referencedBytes;
        images.clear();
        check("clearing slots preserves the observed residency peak",
              images.stats().referencedBytes == 0 && images.stats().peakReferencedBytes >= referenced);
        CdHostApi old{};
        old.struct_size = sizeof(old);
        context.host = &old;
        cdsdk::DiagnosticSource legacy;
        legacy.bind(context);
        check("old hosts need no diagnostic service", !legacy.available() && sources.empty());
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
