#include "tools/PreviewOutput.h"

namespace cdsdk {
CdResult PreviewOutput::configure() {
    configured_ = true;
    return CD_OK;
}

void PreviewOutput::begin(const CdFrame& frame) {
    const auto allocation = reinterpret_cast<uintptr_t>(frame.rgba);
    if (allocation != allocation_ || width_ != frame.width || height_ != frame.height) {
        ++epoch_;
        allocation_ = allocation;
    }
    if (width_ != frame.width || height_ != frame.height || dpr_ != frame.device_pixel_ratio) {
        ++layout_;
    }
    width_ = frame.width;
    height_ = frame.height;
    dpr_ = frame.device_pixel_ratio;
    frame_ = frame;
    ++content_;
    active_ = true;
    completed_ = false;
}

void PreviewOutput::end() {
    active_ = false;
    frame_ = {};
}

CdResult PreviewOutput::session(const CdFrame* frame, CdRenderSession* output) const {
    if (!frame || frame->struct_size < sizeof(*frame) || !output || output->struct_size < sizeof(*output)) {
        return CD_ERROR_ARGUMENT;
    }
    *output = {sizeof(*output), 1, CD_OUTPUT_CPU};
    if (configured_ && active_ && frame->rgba == frame_.rgba && frame->width == frame_.width &&
        frame->height == frame_.height && frame->stride == frame_.stride && frame->capacity == frame_.capacity &&
        frame->device_pixel_ratio == frame_.device_pixel_ratio) {
        output->output_epoch = epoch_;
        output->content_version = content_;
        output->layout_version = layout_;
    }
    return CD_OK;
}

CdResult PreviewOutput::complete(const CdRenderReport* report) {
    if (!report || report->struct_size < sizeof(*report) || report->version != 1 || !configured_ || !active_ ||
        completed_ || report->backend != CD_OUTPUT_CPU || !report->output_epoch || report->output_epoch != epoch_ ||
        report->result < CD_OUTPUT_PAINTED || report->result > CD_OUTPUT_FAILED ||
        report->pixel_copy_bytes > uint64_t(frame_.width) * frame_.height * 4) {
        return CD_ERROR_ARGUMENT;
    }
    completed_ = true;
    return CD_OK;
}
}  // namespace cdsdk
