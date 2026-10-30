#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <CubismModelSettingJson.hpp>
#include <Model/CubismUserModel.hpp>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "Assets.h"

namespace cd::live2d {
void initializeCubism();
void disposeCubism();

class Model : public Csm::CubismUserModel {
public:
    Model(const Assets& assets, ID3D11Device* device, int width, int height,
          const std::function<void(const QString&)>& progress);
    ~Model() override;
    void configure(const QJsonObject& options);
    void pointer(float x, float y, bool tap);
    bool performMotion(const QString& name, unsigned priority);
    void draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, float delta);
    QJsonObject info() const;
    QJsonObject animation() const;

private:
    void startMotion(const QString& name, int priority);
    std::unique_ptr<Csm::CubismModelSettingJson> settings_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> textures_;
    QHash<QString, Csm::ACubismMotion*> motions_;
    QHash<QString, Csm::ACubismMotion*> expressions_;
    QHash<QString, std::vector<bool>> motionParameters_;
    QHash<QString, bool> motionOpacity_;
    QJsonObject options_;
    QString activeExpression_;
    QString activeMotion_;
    QStringList idleMotions_, tapMotions_;
    std::vector<float> initialParameters_;
    std::vector<float> transitionParameters_;
    std::vector<bool> controlledParameters_;
    QJsonArray textureMipLevels_;
    std::array<int, 4> followParameters_{-1, -1, -1, -1};
    quint64 motionGeneration_ = 0, tapCount_ = 0;
    int nextIdle_ = 0, nextTap_ = 0;
    unsigned actionPriority_ = 0;
    int width_, height_;
    double followRemainder_ = 0;
    float transitionElapsed_ = 0, transitionOpacity_ = 1;
    int maskEdge_ = 256;
    quint64 textureGpuBytes_ = 0;
};
}  // namespace cd::live2d
