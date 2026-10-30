#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "plugins/mechanical-pixels/Face.h"

struct CdCreateContext;

namespace streetlink {
class MatrixFace final : public mechanicalpixels::Face {
public:
    explicit MatrixFace(const CdCreateContext& context);
    ~MatrixFace() override;
    void onChange(std::function<void()> callback) override;
    void pause(bool paused) override;
    bool render(mechanicalpixels::Kind kind, const cdsdk::Settings& settings, std::string_view text, bool colonVisible,
                const widgetplugin::Spectrum& spectrum, const CdFrame& frame, CdRect safe) override;
    bool ready() const;
    uint64_t cacheBuilds() const;
    uint64_t referencedBytes() const;
    bool save(const std::filesystem::path& path, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace streetlink
