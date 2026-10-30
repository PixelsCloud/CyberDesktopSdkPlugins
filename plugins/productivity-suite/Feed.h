#pragma once

namespace cdsdk {
struct ContentSnapshot;
}

namespace productivity {
struct Data;
bool applyFeed(Data& data, const cdsdk::ContentSnapshot& value);
}  // namespace productivity
