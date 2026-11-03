#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using Microsoft::WRL::ComPtr;

void compile(const std::string& source, const char* entry, std::ostream& output) {
    ComPtr<ID3DBlob> bytecode, errors;
    const auto result = D3DCompile(source.data(), source.size(), "LiquidGlass.React", nullptr, nullptr, entry, "cs_5_0",
                                   D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
    if (FAILED(result)) {
        const auto message =
            errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
                   : "D3DCompile failed";
        throw std::runtime_error(std::string(entry) + ": " + message);
    }
    output << "constexpr unsigned char kReact" << entry << "[] = {\n";
    const auto* bytes = static_cast<const uint8_t*>(bytecode->GetBufferPointer());
    for (size_t index = 0; index < bytecode->GetBufferSize(); ++index) {
        output << "0x" << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[index]) << ',';
        if (index % 20 == 19) {
            output << '\n';
        }
    }
    output << "\n};\n";
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        return 2;
    }
    try {
        std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
        if (!input) {
            throw std::runtime_error("Cannot open material shader input");
        }
        const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto destination = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Cannot open material bytecode output");
        }
        output << "// Generated GPU bytecode. Do not edit.\n";
        for (const auto* entry : std::array{"Underlay", "RasterSource", "Warp", "Composite", "MaskImage"}) {
            compile(source, entry, output);
        }
        output.close();
        if (!output) {
            throw std::runtime_error("Cannot finish material bytecode output");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
