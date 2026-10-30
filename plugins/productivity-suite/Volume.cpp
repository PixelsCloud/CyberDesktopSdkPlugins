#include "plugins/productivity-suite/Volume.h"

#include <sdk/cpp/Snapshots.h>
#include <sdk/services/Storage.h>

#include <algorithm>
#include <string>

#include "plugins/productivity-suite/Productivity.h"

namespace productivity {
VolumeChange applyVolume(Data& data, const cdsdk::VolumeSnapshot& value) {
    const bool valid = value.valid();
    const bool loading = value.status == CD_DATA_LOADING;
    const bool stale = value.status == CD_DATA_STALE;
    const std::string error = value.error == CD_VOLUME_REMOTE ? "remote_drive" : value.error ? "drive_unavailable" : "";
    VolumeChange change;
    change.changed = data.valid != valid || data.loading != loading || data.stale != stale || data.error != error ||
                     data.available != value.available || data.total != value.total || data.free != value.free;
    change.wasValid = data.valid;
    change.capacityChanged = valid && (!data.valid || data.free != value.free || data.total != value.total);
    change.used = value.total ? double(value.total - std::min(value.free, value.total)) / value.total : 0;
    data.valid = valid;
    data.loading = loading;
    data.stale = stale;
    data.error = error;
    data.available = value.available;
    data.total = value.total;
    data.free = value.free;
    return change;
}
}  // namespace productivity
