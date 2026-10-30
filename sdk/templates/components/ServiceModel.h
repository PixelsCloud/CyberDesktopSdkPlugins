#pragma once

#include <sdk/Environment.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include <cmath>

namespace componentdemo {
// Host callbacks are delivered on the UI thread. Own subscriptions and copied
// values here; rendering never performs collection or sends media commands.
class ServiceModel {
public:
    enum class State { Loading, Ready, Empty, Unavailable, Error, Timeout };

    struct View {
        State state = State::Loading;
        int cpu = 0, memory = 0;
        std::string source, title, artist;
        uint32_t controls = 0;
        bool playing = false, pending = false, commandError = false;
        int position = 0, duration = 0;
        bool operator==(const View&) const = default;
    };

    ServiceModel(const CdCreateContext& context, int kind, std::function<void()> changed);

    ~ServiceModel();

    const View& view() const;

    void active(bool active);

    void retry();

    bool can(uint32_t command) const;

    void command(uint32_t command);

private:
    cdsdk::Services services_;
    cdsdk::Environment environment_;
    cdsdk::SystemWatch system_;
    cdsdk::MediaWatch media_;
    cdsdk::Timer deadline_;
    int kind_;
    std::function<void()> changed_;
    View view_;
    bool active_ = false;
    uint64_t began_ = 0, request_ = 0, generation_ = 0;

    uint64_t now() const;

    void stop();

    void publish(View value);

    void receive(const cdsdk::SystemSnapshot& snapshot);

    void receive(const cdsdk::MediaSnapshot& snapshot);

    void schedule();

    static void CD_CALL tick(void* user, uint64_t) noexcept;
};
}  // namespace componentdemo
