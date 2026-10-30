#include "templates/components/ServiceModel.h"

namespace componentdemo {
ServiceModel::ServiceModel(const CdCreateContext& context, int kind, std::function<void()> changed)
    : services_(context.host, context.host_context), kind_(kind), changed_(std::move(changed)) {
    environment_.bind(context);
    deadline_.bind(context);
}

ServiceModel::~ServiceModel() {
    stop();
}

auto ServiceModel::view() const -> const View& {
    return view_;
}

auto ServiceModel::active(bool active) -> void {
    if (active_ == active) {
        return;
    }
    active_ = active;
    if (active) {
        retry();
    } else {
        stop();
    }
}

auto ServiceModel::retry() -> void {
    if (!active_) {
        return;
    }
    stop();
    publish({});
    began_ = now();
    CdResult result = CD_ERROR_UNSUPPORTED;
    if (kind_ == 0) {
        system_ = services_.system(1000, [this](const auto& snapshot) {
            receive(snapshot);
        });
        result = system_.result();
    } else if (kind_ == 2) {
        media_ = services_.media("auto", [this](const auto& snapshot) {
            receive(snapshot);
        });
        result = media_.result();
    }
    if (result != CD_OK) {
        auto value = View{};
        value.state = result == CD_ERROR_UNSUPPORTED ? State::Unavailable : State::Error;
        publish(std::move(value));
    }
    schedule();
}

auto ServiceModel::can(uint32_t command) const -> bool {
    const auto flag = command == CD_MEDIA_PREVIOUS ? CD_MEDIA_CAN_PREVIOUS
                      : command == CD_MEDIA_NEXT   ? CD_MEDIA_CAN_NEXT
                      : command == CD_MEDIA_TOGGLE ? CD_MEDIA_CAN_TOGGLE
                                                   : 0;
    return active_ && view_.state == State::Ready && !view_.pending && (view_.controls & flag);
}

auto ServiceModel::command(uint32_t command) -> void {
    if (!can(command)) {
        return;
    }
    auto value = view_;
    uint64_t request = 0;
    const auto result = services_.command(media_.token(), command, 0, &request);
    request_ = result == CD_OK ? request : 0;
    value.pending = request_ != 0;
    value.commandError = !value.pending;
    began_ = now();
    publish(std::move(value));
    schedule();
}

auto ServiceModel::now() const -> uint64_t {
    return environment_.snapshot().monotonic_ms;
}

auto ServiceModel::stop() -> void {
    deadline_.stop();
    system_.reset();
    media_.reset();
    request_ = generation_ = 0;
}

auto ServiceModel::publish(View value) -> void {
    if (value != view_) {
        view_ = std::move(value);
        changed_();
    }
}

auto ServiceModel::receive(const cdsdk::SystemSnapshot& snapshot) -> void {
    if (!active_) {
        return;
    }
    const auto& data = snapshot.data.base;
    View value;
    constexpr auto required = CD_SYSTEM_CPU_VALID | CD_SYSTEM_MEMORY_VALID;
    const bool missing = data.memory_total_bytes == 0 || (data.struct_size >= sizeof(CdSystemInfoEx) &&
                                                          (snapshot.data.valid_fields & required) != required);
    const bool invalid = !std::isfinite(data.cpu_percent) || data.memory_used_bytes > data.memory_total_bytes;
    value.state = invalid ? State::Error : missing ? State::Empty : State::Ready;
    if (value.state == State::Ready) {
        value.cpu = int(std::lround(std::clamp(data.cpu_percent, 0., 100.)));
        value.memory = int(std::lround(100. * double(data.memory_used_bytes) / double(data.memory_total_bytes)));
    }
    publish(std::move(value));
    schedule();
}

auto ServiceModel::receive(const cdsdk::MediaSnapshot& snapshot) -> void {
    if (!active_) {
        return;
    }
    auto value = view_;
    if (snapshot.sourceId != view_.source || snapshot.sessionGeneration != generation_) {
        request_ = 0;
        value.pending = value.commandError = false;
    }
    generation_ = snapshot.sessionGeneration;
    value.state = snapshot.sourceId.empty() ? State::Empty : State::Ready;
    value.source = snapshot.sourceId;
    value.title = snapshot.title;
    value.artist = snapshot.artist;
    value.controls = snapshot.controls;
    value.playing = snapshot.status == CD_MEDIA_PLAYING;
    const auto seconds = [](double number) {
        return std::isfinite(number) ? int(std::clamp(number, 0., 2147483647.)) : 0;
    };
    value.duration = seconds(snapshot.durationSeconds);
    value.position = std::min(seconds(snapshot.positionSeconds), value.duration);
    if (request_ && snapshot.commandRequestId == request_ && snapshot.commandResult != CD_ERROR_BUSY) {
        value.pending = false;
        value.commandError = snapshot.commandResult != CD_OK;
        request_ = 0;
    }
    publish(std::move(value));
    schedule();
}

auto ServiceModel::schedule() -> void {
    if (!active_ || (view_.state != State::Loading && !view_.pending)) {
        deadline_.stop();
        return;
    }
    if (deadline_.replace(1000, tick, this) != CD_OK) {
        auto value = view_;
        if (value.state == State::Loading) {
            value.state = State::Error;
        }
        value.commandError = value.pending;
        value.pending = false;
        request_ = 0;
        publish(std::move(value));
    }
}

void CD_CALL ServiceModel::tick(void* user, uint64_t) noexcept {
    try {
        auto& self = *static_cast<ServiceModel*>(user);
        const auto time = self.now();
        if (time < self.began_ || time - self.began_ < 15000) {
            return;
        }
        auto value = self.view_;
        if (value.state == State::Loading) {
            value.state = State::Timeout;
        }
        value.commandError = value.pending;
        value.pending = false;
        self.request_ = 0;
        self.deadline_.stop();
        self.publish(std::move(value));
    } catch (...) {
    }
}
}  // namespace componentdemo
