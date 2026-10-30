#pragma once
#include <cstdint>
#include <memory>

namespace cybercanvas {
class ImageData;

// Explicit per-runtime budget; leases retain accounting independently of the owner.
class MemoryBudget {
    struct State;

public:
    class Lease {
    public:
        ~Lease();
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

    private:
        friend class MemoryBudget;
        Lease(std::shared_ptr<State> state, uint64_t bytes);
        std::shared_ptr<State> state_;
        uint64_t bytes_;
    };

    explicit MemoryBudget(uint64_t maximum = 16 * 1024 * 1024);
    std::shared_ptr<Lease> reserve(uint64_t bytes) const;
    static std::shared_ptr<const ImageData> attach(std::shared_ptr<const ImageData> image,
                                                   std::shared_ptr<Lease> lease);
    uint64_t used() const;
    uint64_t peak() const;
    uint64_t maximum() const;

private:
    std::shared_ptr<State> state_;
};
}  // namespace cybercanvas
