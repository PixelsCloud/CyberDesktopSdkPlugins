#pragma once

#include <cstdint>

// Private ABI: fixed scalar layout, no C++/Rust allocations cross ownership.
struct CfVec2 {
    float x, y;
};

struct CfSample {
    CfVec2 position, velocity;
};

struct CfReaction {
    CfVec2 impulse;
    float moment;
};

struct CfSettings {
    float radius, density, viscosity, tension, adhesion;
    uint32_t substeps;
};

static_assert(sizeof(CfVec2) == 8 && sizeof(CfSample) == 16 && sizeof(CfReaction) == 12 && sizeof(CfSettings) == 24);

extern "C" {
void* cc_fluid_create(const CfSettings* settings);
void cc_fluid_destroy(void* backend);
int32_t cc_fluid_add(void* backend, const CfSample* samples, uint32_t count);
int32_t cc_fluid_boundary(void* backend, uint64_t id, const CfSample* samples, uint32_t count);
int32_t cc_fluid_remove_boundary(void* backend, uint64_t id);
int32_t cc_fluid_step(void* backend, float seconds, CfVec2 gravity);
int32_t cc_fluid_copy(void* backend, CfSample* samples, uint32_t count);
int32_t cc_fluid_reaction(void* backend, uint64_t id, CfReaction* out);
}
