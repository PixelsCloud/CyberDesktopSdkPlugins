#include "Model.h"

#include "ShaderCache.h"
#undef D3DCompile

#include <CubismDefaultParameterId.hpp>
#include <ICubismAllocator.hpp>
#include <Id/CubismIdManager.hpp>
#include <Math/CubismMath.hpp>
#include <Motion/CubismMotion.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <Rendering/D3D11/CubismRenderer_D3D11.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace cd::live2d {
namespace {
constexpr float kMotionBlendSeconds = .25f;

class Allocator : public Csm::ICubismAllocator {
public:
    void* Allocate(Csm::csmSizeType size) override {
        return std::malloc(size);
    }

    void Deallocate(void* memory) override {
        std::free(memory);
    }

    void* AllocateAligned(Csm::csmSizeType size, Csm::csmUint32 alignment) override {
        return _aligned_malloc(size, alignment);
    }

    void DeallocateAligned(void* memory) override {
        _aligned_free(memory);
    }
} allocator;

Csm::csmByte* shaderFile(const std::string path, Csm::csmSizeInt* size) {
    *size = 0;
    if (path != "FrameworkShaders/CubismEffect.fx" && path != "FrameworkShaders/CubismBlendMode.fx") {
        return nullptr;
    }
    QFile file(QCoreApplication::applicationDirPath() + "/live2d/" + QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) {
        return nullptr;
    }
    const auto data = file.readAll();
    auto* bytes = static_cast<Csm::csmByte*>(std::malloc(size_t(data.size())));
    if (bytes) {
        std::memcpy(bytes, data.constData(), size_t(data.size()));
        *size = int(data.size());
    }
    return bytes;
}
}  // namespace

void initializeCubism() {
    static Csm::CubismFramework::Option option{};
    option.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Error;
    option.LoadFileFunction = shaderFile;
    option.ReleaseBytesFunction = [](Csm::csmByte* data) {
        std::free(data);
    };
    if (!Csm::CubismFramework::StartUp(&allocator, &option)) {
        throw Failure("Cubism initialization failed");
    }
    Csm::CubismFramework::Initialize();
}

void disposeCubism() {
    Csm::CubismFramework::Dispose();
    Csm::CubismFramework::CleanUp();
}

Model::Model(const Assets& assets, ID3D11Device* device, int width, int height,
             const std::function<void(const QString&)>& progress)
    : width_(width), height_(height) {
    settings_ = std::make_unique<Csm::CubismModelSettingJson>(
        reinterpret_cast<const Csm::csmByte*>(assets.descriptor.constData()), int(assets.descriptor.size()));
    auto resource = [&](const char* path) -> const QByteArray& {
        const auto it = assets.files.constFind(QString::fromUtf8(path));
        if (it == assets.files.cend()) {
            throw Failure("Missing validated resource");
        }
        return it.value();
    };
    const auto& moc = resource(settings_->GetModelFileName());
    LoadModel(reinterpret_cast<const Csm::csmByte*>(moc.constData()), int(moc.size()), true);
    if (!GetModel()) {
        throw Failure("MOC3 consistency or version check failed");
    }
    progress("model initialized");
    if (GetModel()->GetDrawableCount() > 4096 || GetModel()->GetParameterCount() > 4096) {
        throw Failure("Model complexity exceeds limit");
    }
    const std::array<std::array<const char*, 2>, 4> aliases{{{"ParamAngleX", "PARAM_ANGLE_X"},
                                                             {"ParamAngleY", "PARAM_ANGLE_Y"},
                                                             {"ParamEyeBallX", "PARAM_EYE_BALL_X"},
                                                             {"ParamEyeBallY", "PARAM_EYE_BALL_Y"}}};
    Csm::CubismIdHandle breathId = nullptr;
    QHash<QString, int> parameterIndices;
    for (int i = 0; i < GetModel()->GetParameterCount(); ++i) {
        initialParameters_.push_back(GetModel()->GetParameterValue(i));
        const auto* id = GetModel()->GetParameterId(i)->GetString().GetRawString();
        parameterIndices.insert(QString::fromUtf8(id), i);
        if (std::strcmp(id, "ParamBreath") == 0 || std::strcmp(id, "PARAM_BREATH") == 0) {
            breathId = GetModel()->GetParameterId(i);
        }
        for (size_t axis = 0; axis < aliases.size(); ++axis) {
            if (std::strcmp(id, aliases[axis][0]) == 0 || std::strcmp(id, aliases[axis][1]) == 0) {
                followParameters_[axis] = i;
            }
        }
    }
    quint64 vertices = 0, indices = 0;
    for (int i = 0; i < GetModel()->GetDrawableCount(); ++i) {
        vertices += GetModel()->GetDrawableVertexCount(i);
        indices += GetModel()->GetDrawableVertexIndexCount(i);
    }
    // Offscreen blend surfaces scale with output area and are GPU allocations,
    // so the process private-commit limit alone cannot bound them.
    for (const auto& image : assets.textures) {
        int w = image.width(), h = image.height(), levels = 0;
        while (true) {
            textureGpuBytes_ += quint64(w) * h * 4;
            ++levels;
            if (w == 1 && h == 1) {
                break;
            }
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
        textureMipLevels_.append(levels);
    }
    const quint64 estimatedGpuBytes = textureGpuBytes_ +
                                      quint64(width) * height * 4 * (8 + quint64(GetModel()->GetOffscreenCount()) * 2) +
                                      vertices * 32 + indices * 2;
    if (vertices > 1000000 || indices > 6000000 || GetModel()->GetOffscreenCount() > 256 ||
        estimatedGpuBytes > 512ull * 1024 * 1024) {
        throw Failure("Model GPU budget exceeded; reduce output size or model complexity");
    }
    auto optional = [&](const char* name, auto load) {
        if (name && *name) {
            const auto& data = resource(name);
            load(reinterpret_cast<const Csm::csmByte*>(data.constData()), int(data.size()));
        }
    };
    optional(settings_->GetPhysicsFileName(), [this](auto data, auto size) {
        LoadPhysics(data, size);
    });
    optional(settings_->GetPoseFileName(), [this](auto data, auto size) {
        LoadPose(data, size);
    });
    progress("physics and pose initialized");
    _eyeBlink = Csm::CubismEyeBlink::Create(settings_.get());
    _breath = Csm::CubismBreath::Create();
    Csm::csmVector<Csm::CubismBreath::BreathParameterData> breath;
    if (breathId) {
        breath.PushBack({breathId, .5f, .5f, 3.2f, 1.f});
    }
    _breath->SetParameters(breath);
    Csm::csmVector<Csm::CubismIdHandle> blinkIds, lipIds;
    for (int i = 0; i < settings_->GetEyeBlinkParameterCount(); ++i) {
        blinkIds.PushBack(settings_->GetEyeBlinkParameterId(i));
    }
    for (int i = 0; i < settings_->GetLipSyncParameterCount(); ++i) {
        lipIds.PushBack(settings_->GetLipSyncParameterId(i));
    }
    for (int group = 0; group < settings_->GetMotionGroupCount(); ++group) {
        const auto* name = settings_->GetMotionGroupName(group);
        for (int i = 0; i < settings_->GetMotionCount(name); ++i) {
            const auto& data = resource(settings_->GetMotionFileName(name, i));
            auto* motion = LoadMotion(reinterpret_cast<const Csm::csmByte*>(data.constData()), int(data.size()), name,
                                      nullptr, nullptr, settings_.get(), name, i, true);
            if (!motion) {
                throw Failure("Invalid motion data");
            }
            static_cast<Csm::CubismMotion*>(motion)->SetEffectIds(blinkIds, lipIds);
            // The host repeats idle clips and returns from authored actions explicitly.
            // Exported Loop=true must not trap a click action forever.
            motion->SetLoop(false);
            motion->SetFadeInTime(kMotionBlendSeconds);
            motion->SetFadeOutTime(kMotionBlendSeconds);
            std::vector<bool> controlled(initialParameters_.size(), false);
            bool opacity = false;
            for (const auto& value : checkedJson(data).value("Curves").toArray()) {
                const auto curve = value.toObject();
                const auto id = curve.value("Id").toString();
                if (curve.value("Target") == "Parameter" && parameterIndices.contains(id)) {
                    const auto index = parameterIndices.value(id);
                    controlled[index] = true;
                    auto* cubismMotion = static_cast<Csm::CubismMotion*>(motion);
                    cubismMotion->SetParameterFadeInTime(GetModel()->GetParameterId(index), kMotionBlendSeconds);
                    cubismMotion->SetParameterFadeOutTime(GetModel()->GetParameterId(index), kMotionBlendSeconds);
                } else if (curve.value("Target") == "Model") {
                    opacity |= id == "Opacity";
                    const auto& ids = id == "EyeBlink" ? blinkIds : lipIds;
                    if (id == "EyeBlink" || id == "LipSync") {
                        for (Csm::csmUint32 n = 0; n < ids.GetSize(); ++n) {
                            const auto key = QString::fromUtf8(ids[n]->GetString().GetRawString());
                            if (parameterIndices.contains(key)) {
                                controlled[parameterIndices.value(key)] = true;
                            }
                        }
                    }
                }
            }
            const auto key = QString::fromUtf8(name) + '/' + QString::number(i);
            motions_.insert(key, motion);
            motionParameters_.insert(key, std::move(controlled));
            motionOpacity_.insert(key, opacity);
            progress("motion initialized: " + key);
        }
    }
    auto names = motions_.keys();
    names.sort();
    for (const auto& name : names) {
        if (name.startsWith("Idle/", Qt::CaseInsensitive)) {
            idleMotions_.append(name);
        } else if (name.startsWith("Tap", Qt::CaseInsensitive)) {
            tapMotions_.append(name);
        }
    }
    if (tapMotions_.isEmpty()) {
        for (const auto& name : names) {
            if (!name.startsWith("Idle/", Qt::CaseInsensitive)) {
                tapMotions_.append(name);
            }
        }
    }
    for (int i = 0; i < settings_->GetExpressionCount(); ++i) {
        const auto& data = resource(settings_->GetExpressionFileName(i));
        auto* expression = LoadExpression(reinterpret_cast<const Csm::csmByte*>(data.constData()), int(data.size()),
                                          settings_->GetExpressionName(i));
        if (!expression) {
            throw Failure("Invalid expression data");
        }
        expressions_.insert(QString::fromUtf8(settings_->GetExpressionName(i)), expression);
        progress("expression initialized");
    }
    Csm::csmMap<Csm::csmString, float> layout;
    settings_->GetLayoutMap(layout);
    GetModelMatrix()->SetupFromLayout(layout);
    GetModel()->SaveParameters();
    using Renderer = Csm::Rendering::CubismRenderer_D3D11;
    Renderer::SetConstantSettings(2, device);
    CreateRenderer(width, height);
    progress("renderer initialized");
    if (!live2dShaderCacheHealthy()) {
        throw Failure("Live2D shader resources are missing or invalid");
    }
    auto* renderer = GetRenderer<Renderer>();
    // MSAA handles mesh edges; alpha-correct mipmaps handle shrinking the
    // character's texture atlas. Masks also need enough pixels at the actual DPI.
    renderer->IsPremultipliedAlpha(true);
    renderer->SetAnisotropy(4);
    while (maskEdge_ < std::max(width, height) && maskEdge_ < 1024) {
        maskEdge_ *= 2;
    }
    // Cubism's count getters dereference the corresponding mask manager.
    // Models without that kind of masking never create the manager.
    const int drawableMaskCount = GetModel()->IsUsingMasking() ? renderer->GetDrawableRenderTextureCount() : 0;
    const int offscreenMaskCount =
        GetModel()->IsUsingMaskingForOffscreen() ? renderer->GetOffscreenRenderTextureCount() : 0;
    const quint64 maskCount = quint64(drawableMaskCount) + offscreenMaskCount;
    const auto maskBytes = [&] {
        return maskCount * 2 * quint64(maskEdge_) * maskEdge_ * 4;
    };
    while (maskEdge_ > 256 && estimatedGpuBytes + maskBytes() > 512ull * 1024 * 1024) {
        maskEdge_ /= 2;
    }
    if (estimatedGpuBytes + maskBytes() > 512ull * 1024 * 1024) {
        throw Failure("Model clipping masks exceed the GPU budget");
    }
    if (drawableMaskCount > 0) {
        renderer->SetDrawableClippingMaskBufferSize(float(maskEdge_), float(maskEdge_));
    }
    if (offscreenMaskCount > 0) {
        renderer->SetOffscreenClippingMaskBufferSize(float(maskEdge_), float(maskEdge_));
    }
    for (int i = 0; i < assets.textures.size(); ++i) {
        const auto& image = assets.textures[i];
        std::vector<QImage> levels{image};
        while (levels.back().width() > 1 || levels.back().height() > 1) {
            const auto& previous = levels.back();
            auto next = previous.scaled(std::max(1, previous.width() / 2), std::max(1, previous.height() / 2),
                                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (next.isNull()) {
                throw Failure("Texture mipmap allocation failed");
            }
            levels.push_back(std::move(next));
        }
        std::vector<D3D11_SUBRESOURCE_DATA> input;
        for (const auto& level : levels) {
            input.push_back({level.constBits(), UINT(level.bytesPerLine()), 0});
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = image.width();
        desc.Height = image.height();
        desc.MipLevels = UINT(levels.size());
        desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device->CreateTexture2D(&desc, input.data(), &texture)) ||
            FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
            throw Failure("Texture upload failed");
        }
        renderer->BindTexture(i, view.Get());
        textures_.push_back(std::move(view));
        progress(QString("texture initialized: %1").arg(i));
    }
}

Model::~Model() {
    _motionManager->StopAllMotions();
    _expressionManager->StopAllMotions();
    for (auto* motion : motions_) {
        Csm::ACubismMotion::Delete(motion);
    }
    for (auto* expression : expressions_) {
        Csm::ACubismMotion::Delete(expression);
    }
}

void Model::configure(const QJsonObject& options) {
    if (options_.value("motion") != options.value("motion")) {
        _motionManager->StopAllMotions();
    }
    options_ = options;
    const auto expression = options.value("expression").toString();
    if (expression != activeExpression_) {
        _expressionManager->StopAllMotions();
        activeExpression_ = expression;
        if (expressions_.contains(expression)) {
            _expressionManager->StartMotion(expressions_.value(expression), false);
        }
    }
}

void Model::pointer(float x, float y, bool tap) {
    SetDragging(std::clamp(x, -1.f, 1.f), std::clamp(y, -1.f, 1.f));
    if (tap && !tapMotions_.isEmpty()) {
        ++tapCount_;
        startMotion(tapMotions_[nextTap_], 3);
        nextTap_ = (nextTap_ + 1) % tapMotions_.size();
    }
}

void Model::startMotion(const QString& name, int priority) {
    if (!motions_.contains(name)) {
        return;
    }
    auto* model = GetModel();
    model->LoadParameters();
    transitionParameters_.resize(initialParameters_.size());
    for (int i = 0; i < int(initialParameters_.size()); ++i) {
        transitionParameters_[i] = model->GetParameterValue(i);
    }
    controlledParameters_ = motionParameters_.value(name);
    transitionElapsed_ = 0;
    transitionOpacity_ = model->GetModelOpacity();
    // Cubism crossfades outgoing entries. Bound rapid-click work without
    // resetting the pose, so even a saturated queue keeps visual continuity.
    if (_motionManager->GetCubismMotionQueueEntries()->GetSize() >= 8) {
        _motionManager->StopAllMotions();
    }
    activeMotion_ = name;
    ++motionGeneration_;
    _motionManager->StartMotionPriority(motions_.value(name), false, priority);
}

bool Model::performMotion(const QString& name, unsigned priority) {
    if (!motions_.contains(name) || priority < 1 || priority > 3 ||
        (!_motionManager->IsFinished() && actionPriority_ > priority)) {
        return false;
    }
    actionPriority_ = priority;
    startMotion(name, int(priority));
    return true;
}

void Model::draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, float delta) {
    auto* model = GetModel();
    model->LoadParameters();
    if (_motionManager->IsFinished()) {
        actionPriority_ = 0;
        auto name = options_.value("motion").toString("auto");
        if ((name.isEmpty() || name == "auto") && !idleMotions_.isEmpty()) {
            name = idleMotions_[nextIdle_];
            nextIdle_ = (nextIdle_ + 1) % idleMotions_.size();
        }
        startMotion(name, 1);
    }
    const bool motion = _motionManager->UpdateMotion(model, delta);
    if (!transitionParameters_.empty()) {
        transitionElapsed_ = std::min(kMotionBlendSeconds, transitionElapsed_ + delta);
        const float blend = Csm::CubismMath::GetEasingSine(transitionElapsed_ / kMotionBlendSeconds);
        // The incoming motion may never write old prop switches. Retire those
        // parameters to the initial pose while Cubism blends the shared curves.
        for (int i = 0; i < int(initialParameters_.size()); ++i) {
            if (!controlledParameters_[i]) {
                model->SetParameterValue(
                    i, transitionParameters_[i] + (initialParameters_[i] - transitionParameters_[i]) * blend);
            }
        }
        const float opacity = motionOpacity_.value(activeMotion_) ? model->GetModelOpacity() : 1.f;
        model->SetModelOpacity(transitionOpacity_ + (opacity - transitionOpacity_) * blend);
    }
    model->SaveParameters();
    if (!motion && _eyeBlink) {
        _eyeBlink->UpdateParameters(model, delta);
    }
    _expressionManager->UpdateMotion(model, delta);
    // CubismTargetPoint limits velocity and acceleration per update. Use the
    // same bounded simulation cadence at both render rates, retaining its state
    // across pointer entry/leave, motion changes and pause/resume. Still frames
    // have zero delta and must not advance tracking.
    constexpr double followStep = 1. / 60.;
    followRemainder_ += std::clamp(double(delta), 0., .05);
    while (followRemainder_ >= followStep) {
        _dragManager->Update(float(followStep));
        followRemainder_ -= followStep;
    }
    if (options_.value("follow").toBool(true)) {
        const float x = _dragManager->GetX(), y = _dragManager->GetY();
        const std::array<float, 4> values{x * 30.f, y * 30.f, x, y};
        for (size_t i = 0; i < followParameters_.size(); ++i) {
            if (followParameters_[i] >= 0) {
                model->AddParameterValue(followParameters_[i], values[i]);
            }
        }
    }
    if (_breath) {
        _breath->UpdateParameters(model, delta);
    }
    if (_physics && options_.value("physics").toBool(true)) {
        _physics->Evaluate(model, delta);
    }
    if (_pose) {
        _pose->UpdateParameters(model, delta);
    }
    model->Update();

    float clear[4]{};
    context->OMSetRenderTargets(1, &target, nullptr);
    context->ClearRenderTargetView(target, clear);
    D3D11_VIEWPORT viewport{0, 0, float(width_), float(height_), 0, 1};
    context->RSSetViewports(1, &viewport);
    Csm::CubismMatrix44 matrix;
    const float aspect = float(width_) / float(height_);
    const float extentX = std::max(.001f, model->GetCanvasWidth() * GetModelMatrix()->GetScaleX());
    const float extentY = std::max(.001f, model->GetCanvasHeight() * GetModelMatrix()->GetScaleY());
    const float fit = std::min(2.f / extentY, 2.f * aspect / extentX);
    matrix.Scale(fit / aspect, fit);
    const float scale = float(options_.value("scale").toDouble(1));
    matrix.ScaleRelative(scale, scale);
    matrix.TranslateRelative(float(options_.value("x").toDouble()), float(options_.value("y").toDouble()));
    matrix.MultiplyByMatrix(GetModelMatrix());
    auto* renderer = GetRenderer<Csm::Rendering::CubismRenderer_D3D11>();
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    auto* offscreens = Csm::Rendering::CubismDeviceInfo_D3D11::GetDeviceInfo(device.Get())->GetOffscreenManager();
    offscreens->BeginFrameProcess();
    renderer->StartFrame(context);
    renderer->SetMvpMatrix(&matrix);
    renderer->DrawModel();
    renderer->EndFrame();
    offscreens->EndFrameProcess();
    offscreens->ReleaseStaleRenderTextures();
    context->OMSetRenderTargets(0, nullptr, nullptr);
}

QJsonObject Model::info() const {
    auto motions = motions_.keys();
    auto expressions = expressions_.keys();
    motions.sort();
    expressions.sort();
    QJsonObject durations;
    for (const auto& name : motions) {
        durations[name] = motions_.value(name)->GetDuration();
    }
    return {{"motions", QJsonArray::fromStringList(motions)},
            {"expressions", QJsonArray::fromStringList(expressions)},
            {"tapMotions", QJsonArray::fromStringList(tapMotions_)},
            {"durations", durations},
            {"textureMipLevels", textureMipLevels_},
            {"textureGpuBytes", double(textureGpuBytes_)},
            {"premultipliedTextures", true},
            {"maskEdge", maskEdge_},
            {"drawables", GetModel()->GetDrawableCount()},
            {"parameters", GetModel()->GetParameterCount()}};
}

QJsonObject Model::animation() const {
    QJsonArray following;
    for (const auto index : followParameters_) {
        following.append(index >= 0 ? QJsonValue(GetModel()->GetParameterValue(index)) : QJsonValue());
    }
    return {{"motion", activeMotion_},
            {"expression", activeExpression_},
            {"generation", double(motionGeneration_)},
            {"taps", double(tapCount_)},
            {"blendProgress", std::min(1.f, transitionElapsed_ / kMotionBlendSeconds)},
            {"activeMotions", int(_motionManager->GetCubismMotionQueueEntries()->GetSize())},
            {"tracking", QJsonArray{_dragManager->GetX(), _dragManager->GetY()}},
            {"following", following}};
}
}  // namespace cd::live2d
