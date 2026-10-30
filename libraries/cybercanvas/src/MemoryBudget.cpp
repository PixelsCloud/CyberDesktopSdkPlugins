#include <CyberCanvas/Image.h>
#include <CyberCanvas/MemoryBudget.h>

#include <atomic>
#include <stdexcept>
#include <utility>

namespace cybercanvas {
struct MemoryBudget::State {
    uint64_t maximum;
    std::atomic<uint64_t> used = 0, peak = 0;

    explicit State(uint64_t value) : maximum(value) {}
};

MemoryBudget::Lease::Lease(std::shared_ptr<State> state, uint64_t bytes) : state_(std::move(state)), bytes_(bytes) {}

MemoryBudget::Lease::~Lease() {
    state_->used.fetch_sub(bytes_, std::memory_order_relaxed);
}

MemoryBudget::MemoryBudget(uint64_t maximum) : state_(std::make_shared<State>(maximum)) {
    if (maximum < 4 || maximum > (1ULL << 32)) {
        throw std::invalid_argument("Invalid rendering memory budget");
    }
}

std::shared_ptr<MemoryBudget::Lease> MemoryBudget::reserve(uint64_t bytes) const {
    auto lease = std::shared_ptr<Lease>(new Lease(state_, 0));
    auto used = state_->used.load(std::memory_order_relaxed);
    do {
        if (bytes > state_->maximum - used) {
            throw std::length_error("Rendering memory budget exhausted");
        }
    } while (!state_->used.compare_exchange_weak(used, used + bytes, std::memory_order_relaxed));
    lease->bytes_ = bytes;
    auto peak = state_->peak.load(std::memory_order_relaxed);
    while (peak < used + bytes && !state_->peak.compare_exchange_weak(peak, used + bytes, std::memory_order_relaxed)) {
    }
    return lease;
}

std::shared_ptr<const ImageData> MemoryBudget::attach(std::shared_ptr<const ImageData> image,
                                                      std::shared_ptr<Lease> lease) {
    struct Held {
        std::shared_ptr<Lease> lease;
        std::shared_ptr<const ImageData> image;
    };

    const auto address = image.get();
    auto held = std::make_shared<Held>(Held{std::move(lease), std::move(image)});
    return {std::move(held), address};
}

uint64_t MemoryBudget::used() const {
    return state_->used.load(std::memory_order_relaxed);
}

uint64_t MemoryBudget::peak() const {
    return state_->peak.load(std::memory_order_relaxed);
}

uint64_t MemoryBudget::maximum() const {
    return state_->maximum;
}

}  // namespace cybercanvas
