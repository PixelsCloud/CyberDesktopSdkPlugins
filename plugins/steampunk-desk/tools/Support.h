#pragma once

#include <sdk/Interaction.h>

#include <filesystem>
#include <iostream>

#include "plugins/mechanical-pixels/tools/TestHost.h"

namespace steamdesk::testing {
void check(bool result, const char* message);

std::string utf8(const std::filesystem::path& path);

struct Host : mechanicalpixels::testing::Host {
    decltype(api.query_service) fallback = api.query_service;
    decltype(api.input.media.base.subscribe_timer) timerFallback = api.input.media.base.subscribe_timer;
    bool captured = false;

    Host();
};

struct Frame {
    std::vector<uint8_t> bytes;
    CdFrame value;

    Frame(unsigned width, unsigned height, double dpi);

    bool valid(CdRect safe) const;
};

CdSettingValue toggle(const char* id, bool value);
}  // namespace steamdesk::testing
