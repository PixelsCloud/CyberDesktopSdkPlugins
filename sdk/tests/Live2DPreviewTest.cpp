#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "sdk/cpp/Services.h"
#include "sdk/services/Live2D.h"
#include "sdk/services/Live2DActions.h"
#include "tools/PreviewServices.h"
#include "tools/Runtime.h"

namespace {
void require(bool success, const char* message) {
    if (!success) {
        throw std::runtime_error(message);
    }
}
}  // namespace

int main(int argc, char** argv) try {
    require(argc == 2, "package directory required");
    cdsdk::MockHost host;
    const auto root = std::filesystem::absolute(cdsdk::utf8Path(argv[1]));
    host.preview->configure(root, {});
    cdsdk::Services services(&host.api, &host);
    CdLive2DApi api{};
    CdLive2DActionsApi actions{};
    require(services.query(CD_SERVICE_LIVE2D, api) == CD_OK, "Live2D service missing");
    require(services.query(CD_SERVICE_LIVE2D_ACTIONS, actions) == CD_OK, "actions missing");
    const auto bytes = (root / "models" / "wanko").generic_u8string();
    const std::string path(bytes.begin(), bytes.end());
    CdLive2DOptions options{sizeof(options), 30, path.c_str(), "auto", "", 1., 0, 0, 0, 1, 1, 0};
    require(api.configure(&host, &options) == CD_OK, "model configure rejected");
    for (unsigned edge : {256u, 384u, 512u}) {
        std::vector<uint8_t> pixels(size_t(edge) * edge * 4);
        CdFrame frame{sizeof(frame), edge, edge, edge * 4, pixels.size(), pixels.data(), double(edge) / 256};
        const auto start = std::chrono::steady_clock::now();
        CdLive2DStatus state{sizeof(state)};
        do {
            host.tick(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            host.preview->render(frame);
            require(api.snapshot(&host, &state) == CD_OK, "snapshot failed");
            if (state.state == 3) {
                throw std::runtime_error(state.message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        } while ((state.state != 2 || state.frames < 3) &&
                 std::chrono::steady_clock::now() - start < std::chrono::seconds(15));
        require(state.state == 2 && state.frames >= 3, "model never produced real frames");
        size_t visible = 0;
        for (size_t index = 3; index < pixels.size(); index += 4) {
            visible += pixels[index] != 0;
        }
        require(visible > size_t(edge) * edge / 100, "ready model image is blank");
        std::cout << "PASS edge=" << edge << " frames=" << state.frames << " visible=" << visible << '\n';
        const std::string motions = state.motions;
        require(!motions.empty(), "real model motion list empty");
        const auto motion = motions.substr(0, motions.find('\n'));
        require(actions.perform(&host, motion.c_str(), 3) == CD_OK, "real motion refused");
        host.preview->pause(true);
        require(actions.perform(&host, motion.c_str(), 3) == CD_ERROR_BUSY, "paused model accepted motion");
        host.preview->pause(false);
    }
    options.directory = "C:/Windows";
    require(api.configure(&host, &options) == CD_ERROR_ARGUMENT, "unowned model directory accepted");
    options.directory = "";
    require(api.configure(&host, &options) == CD_OK, "model clear failed");
    std::cout << "PASS public model worker, images, DPI, actions, pause and resource bounds\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
