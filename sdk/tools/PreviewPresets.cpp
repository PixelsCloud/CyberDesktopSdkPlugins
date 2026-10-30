#include "tools/PreviewPresets.h"

namespace cdsdk {
size_t previewScenarioIndex(const std::string& id) {
    for (size_t i = 0; i < std::size(previewScenarios); ++i) {
        if (id == previewScenarios[i].id) {
            return i;
        }
    }
    throw std::invalid_argument("Scenario must be normal, loading, empty or error");
}
}  // namespace cdsdk
