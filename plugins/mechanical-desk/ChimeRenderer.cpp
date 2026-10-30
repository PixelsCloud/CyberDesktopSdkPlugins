#include <numbers>

#include "controllers/toys/Toys.h"

namespace mechanicaldesk_toys {
MechanicalRenderer::MechanicalRenderer() : skin_(std::make_unique<mechanicaldesk::Skin>()) {}

MechanicalRenderer::~MechanicalRenderer() = default;

void MechanicalRenderer::bind(const CdCreateContext& context, std::function<void()> invalidate) {
    skin_->bind(context);
    skin_->onChange(std::move(invalidate));
}

void MechanicalRenderer::pause(bool paused) {
    skin_->pause(paused);
}

bool MechanicalRenderer::pending() const {
    return skin_->pending();
}

mechanicaldesk::View MechanicalRenderer::view(CdRect safe, cybercanvas::Size) {
    return skin_->view(safe);
}

void MechanicalRenderer::draw(cybercanvas::Canvas& canvas, const Model& model, const Appearance&, double scale) {
    skin_->backdrop(canvas, scale);
    for (size_t i = 0; i < model.pieces().size(); ++i) {
        const auto& piece = model.pieces()[i];
        const auto from = model.anchor(piece), to = model.attachment(piece);
        const auto state = model.state(i);
        canvas.line(from.x * 100, from.y * 100, to.x * 100, to.y * 100, 0x222a2bff, 4.5);
        canvas.line(from.x * 100 - 1, from.y * 100, to.x * 100 - 1, to.y * 100, 0xbac4c2ff, 1.8);
        canvas.saved([&] {
            canvas.translate(state.position.x * 100, state.position.y * 100);
            canvas.rotate(state.angle * 180 / std::numbers::pi);
            const auto name = std::string(i % 2 ? "tail" : "body") + std::to_string(i / 2);
            skin_->part(
                canvas, name,
                {-piece.half.width * 100, -piece.half.height * 100, piece.half.width * 200, piece.half.height * 200});
        });
    }
}
}  // namespace mechanicaldesk_toys
