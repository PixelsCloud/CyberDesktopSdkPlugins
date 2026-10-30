#include <dwrite.h>
#include <wrl/client.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "sdk/WindowsFonts.h"

int main() try {
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
        throw std::runtime_error("Cannot create DirectWrite factory");
    }
    cdsdk::WindowsFonts fonts(factory.Get());
    for (const auto id : {"@siyuan-heavy", "@zqk-free"}) {
        if (!fonts.bundledAvailable(id)) {
            throw std::runtime_error(std::string("Missing bundled font: ") + id);
        }
        fonts.select(factory.Get(), id);
        if (!fonts.collection() || std::wstring_view(fonts.family()) == L"Segoe UI") {
            throw std::runtime_error("Default font silently fell back to a system font");
        }
    }
    std::cout << "PASS both default font IDs resolve to bundled DirectWrite collections\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
