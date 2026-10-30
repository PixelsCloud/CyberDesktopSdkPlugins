#include "tools/Diagnostics.h"

namespace cdsdk {
bool sampleDiagnostics(MockHost& host, std::vector<DiagnosticSample>* output, Error* error) {
    DiagnosticError diagnosticError;
    if (!host.diagnostics.read(output, &diagnosticError)) {
        if (error) {
            *error = {diagnosticError.code, diagnosticError.message};
        }
        return false;
    }
    return true;
}

bool writeDiagnostics(NativeInstance& instance, const std::filesystem::path& path, Error* error) {
    std::vector<DiagnosticSample> samples;
    if (!sampleDiagnostics(instance.host, &samples, error)) {
        return false;
    }
    google::protobuf::Struct root;
    auto& fields = *root.mutable_fields();
    fields["instance"].set_string_value(instance.uuid);
    fields["plugin"].set_string_value(instance.id);
    fields["available"].set_bool_value(!samples.empty());
    fields["units"].set_string_value("counts, nanoseconds, known pixel bytes; uint64 values are decimal strings");
    fields["scope"].set_string_value("Per instance. Shared decoder totals must not be summed across sources.");
    fields["sources"].mutable_list_value();
    for (const auto& sample : samples) {
        auto& item = *fields["sources"].mutable_list_value()->add_values()->mutable_struct_value()->mutable_fields();
        item["token"].set_string_value(std::to_string(sample.token));
        auto& counters = *item["metrics"].mutable_struct_value()->mutable_fields();
        for (size_t i = 0; i < std::size(diagnosticNames); ++i) {
            counters[diagnosticNames[i]].set_string_value(std::to_string(sample.stats.values[i]));
        }
        item["nodes"].mutable_list_value();
        for (const auto& node : sample.nodes) {
            auto& value = *item["nodes"].mutable_list_value()->add_values()->mutable_struct_value()->mutable_fields();
            value["id"].set_number_value(node.id);
            value["name"].set_string_value(node.name);
            value["resource"].set_string_value(node.resource);
            value["error"].set_string_value(node.error);
            value["kind"].set_number_value(node.kind);
            value["state"].set_number_value(node.state);
            const auto area = [&](const char* key, CdRect bounds) {
                auto* list = value[key].mutable_list_value();
                for (double coordinate : {bounds.x, bounds.y, bounds.width, bounds.height}) {
                    list->add_values()->set_number_value(coordinate);
                }
            };
            area("bounds", node.bounds);
            area("clip", node.clip);
            area("hit", node.hit);
        }
    }
    std::string json;
    const auto status = google::protobuf::util::MessageToJsonString(root, &json);
    if (!status.ok()) {
        return false;
    }
    std::ofstream output(path, std::ios::binary);
    output << json << '\n';
    return bool(output);
}

bool diagnosticDirectory(const std::filesystem::path& root, const std::filesystem::path& output, Error* error) {
    pb::PluginDescriptor descriptor;
    NativeModule module;
    NativeInstance instance;
    if (!loadDirectoryDescriptor(root, &descriptor, error) || !module.open(root, descriptor, error) ||
        !instance.create(module, root, "diagnostics-1", error)) {
        return false;
    }
    const auto& footprint = descriptor.layouts(0);
    std::vector<CdCell> cells;
    int minA = INT_MAX, minB = INT_MAX, maxA = INT_MIN, maxB = INT_MIN;
    for (const auto& cell : footprint.cells()) {
        cells.push_back({cell.a(), cell.b()});
        minA = std::min(minA, cell.a());
        maxA = std::max(maxA, cell.a());
        minB = std::min(minB, cell.b());
        maxB = std::max(maxB, cell.b());
    }
    const uint32_t width = uint32_t(std::clamp((maxA - minA + 1) * 96, 48, 1024));
    const uint32_t height = uint32_t(std::clamp((maxB - minB + 1) * 96, 48, 1024));
    CdLayout layout{sizeof(layout),
                    uint32_t(footprint.topology()),
                    0,
                    uint32_t(cells.size()),
                    cells.data(),
                    double(width),
                    double(height),
                    2,
                    {0, 0, double(width), double(height)},
                    {235, 240, 245, 255},
                    {70, 200, 170, 255}};
    instance.api.layout_changed(instance.api.instance, &layout);
    std::vector<uint8_t> pixels;
    for (int i = 0; i < 60; ++i) {
        instance.host.tick(.1 + i / 60.);
        if (instance.host.dirty) {
            if (!instance.render(width * 2, height * 2, 2, &pixels, error)) {
                return false;
            }
            instance.host.dirty = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return writeDiagnostics(instance, output, error);
}
}  // namespace cdsdk
