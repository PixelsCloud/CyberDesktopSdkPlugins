#pragma once

#include <filesystem>
#include <functional>
#include <memory>

#include "plugins/productivity-suite/Productivity.h"
#include "plugins/productivity-suite/ThemedController.h"

namespace aerisskin {
class Skin;
}

namespace aeris_productivity {
using namespace productivity;

class Renderer final : public productivity::themed::View {
public:
    Renderer();
    ~Renderer() override;
    void bind(const CdCreateContext&, std::function<void(uint32_t)>) override;
    void input(const CdInput&) override;
    void cancelInput() override;
    void pause(bool paused) override;
    void syncInput(Spec spec, const cdsdk::Settings& settings, const Data& data, CdRect safe) override;
    bool render(Spec, const cdsdk::Settings&, const Data&, const CdFrame&, CdRect) override;
    bool savePng(const std::filesystem::path&, const CdFrame&);

private:
    std::unique_ptr<aerisskin::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace aeris_productivity
