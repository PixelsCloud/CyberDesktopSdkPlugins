#include "sdk/WindowsFonts.h"

#include <dwrite_3.h>
#include <wrl/client.h>

#include <iterator>
#include <stdexcept>
#include <vector>

namespace cdsdk {
struct WindowsFonts::Impl {
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct ExternalFont {
        ComPtr<IDWriteFontCollection> collection;
        std::wstring family;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    };

    ExternalFont bundled, zqk, external;
    std::filesystem::path externalPath;
    std::wstring currentFamily = L"Segoe UI";
    std::string selected;
    enum class Selected { None, Bundled, Zqk, External };
    Selected custom = Selected::None;

    const ExternalFont* current() const {
        switch (custom) {
            case Selected::Bundled:
                return &bundled;
            case Selected::Zqk:
                return &zqk;
            case Selected::External:
                return &external;
            default:
                return nullptr;
        }
    }

    static void check(HRESULT result);

    static void load(IDWriteFactory* factory, const std::filesystem::path& path, ExternalFont& output);
};

std::wstring fontWide(const std::string& value) {
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (size <= 0)
        throw std::runtime_error("Invalid UTF-8 font name");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), size);
    return result;
}

std::filesystem::path bundledFontPath(const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute()) {
        return {};
    }
    for (const auto& component : relative) {
        if (component == L"..") {
            return {};
        }
    }
    HMODULE owner = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&bundledFontPath), &owner);
    std::vector<std::filesystem::path> directories;
    for (const auto module : {owner, HMODULE(nullptr)}) {
        wchar_t path[32768]{};
        const auto size = GetModuleFileNameW(module, path, uint32_t(std::size(path)));
        if (!size || size == std::size(path)) {
            continue;
        }
        const auto directory = std::filesystem::path(path).parent_path();
        directories.push_back(directory);
        if (directory.parent_path().filename() == L"plugins") {
            directories.push_back(directory.parent_path().parent_path());
        } else if (directory.filename() == L"bin" && directory.parent_path().filename() == L"sdk") {
            directories.push_back(directory.parent_path().parent_path());
        }
    }
    for (const auto& directory : directories) {
        const auto file = directory / relative;
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error)) {
            return file;
        }
    }
    return {};
}

auto WindowsFonts::Impl::check(HRESULT result) -> void {
    if (FAILED(result))
        throw std::runtime_error("Cannot load external font");
}

auto WindowsFonts::Impl::load(IDWriteFactory* factory, const std::filesystem::path& path, ExternalFont& output)
    -> void {
    if (path.empty())
        return;
    ComPtr<IDWriteFactory3> modern;
    check(factory->QueryInterface(IID_PPV_ARGS(modern.GetAddressOf())));
    ComPtr<IDWriteFontFile> file;
    check(factory->CreateFontFileReference(path.c_str(), nullptr, file.GetAddressOf()));
    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType{};
    DWRITE_FONT_FACE_TYPE faceType{};
    UINT32 faces = 0;
    check(file->Analyze(&supported, &fileType, &faceType, &faces));
    if (!supported || !faces)
        throw std::runtime_error("Invalid bundled font");
    ComPtr<IDWriteFontSetBuilder> builder;
    check(modern->CreateFontSetBuilder(builder.GetAddressOf()));
    for (UINT32 face = 0; face < faces; ++face) {
        ComPtr<IDWriteFontFaceReference> reference;
        check(
            modern->CreateFontFaceReference(file.Get(), face, DWRITE_FONT_SIMULATIONS_NONE, reference.GetAddressOf()));
        check(builder->AddFontFaceReference(reference.Get()));
    }
    ComPtr<IDWriteFontSet> set;
    check(builder->CreateFontSet(set.GetAddressOf()));
    ComPtr<IDWriteFontCollection1> collection;
    check(modern->CreateFontCollectionFromFontSet(set.Get(), collection.GetAddressOf()));
    check(collection.As(&output.collection));
    ComPtr<IDWriteFontFamily> family;
    check(output.collection->GetFontFamily(0, family.GetAddressOf()));
    ComPtr<IDWriteFont> font;
    check(family->GetFont(0, font.GetAddressOf()));
    output.weight = font->GetWeight();
    ComPtr<IDWriteLocalizedStrings> names;
    check(family->GetFamilyNames(names.GetAddressOf()));
    UINT32 index = 0, length = 0;
    BOOL exists = FALSE;
    check(names->FindLocaleName(L"en-us", &index, &exists));
    if (!exists)
        index = 0;
    check(names->GetStringLength(index, &length));
    std::vector<wchar_t> text(length + 1);
    check(names->GetString(index, text.data(), UINT32(text.size())));
    output.family = text.data();
}

WindowsFonts::~WindowsFonts() = default;
WindowsFonts::WindowsFonts(WindowsFonts&&) noexcept = default;
WindowsFonts& WindowsFonts::operator=(WindowsFonts&&) noexcept = default;

WindowsFonts::WindowsFonts(const WindowsFonts& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : std::make_unique<Impl>()) {}

WindowsFonts& WindowsFonts::operator=(const WindowsFonts& other) {
    if (this != &other) {
        auto copy = other.impl_ ? std::make_unique<Impl>(*other.impl_) : std::make_unique<Impl>();
        impl_ = std::move(copy);
    }
    return *this;
}

WindowsFonts::WindowsFonts(IDWriteFactory* factory) : impl_(std::make_unique<Impl>()) {
    Impl::load(factory, bundledFontPath(), impl_->bundled);
    Impl::load(factory, bundledFontPath(L"assets/fonts/ZQKFreeFont/ZQKfreefont-2.ttf"), impl_->zqk);
}

WindowsFonts::WindowsFonts(IDWriteFactory* factory, const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>()) {
    selectFile(factory, path);
}

auto WindowsFonts::selectFile(IDWriteFactory* factory, const std::filesystem::path& path) -> bool {
    if (path == impl_->externalPath && impl_->custom == Impl::Selected::External) {
        return false;
    }
    if (path.empty()) {
        throw std::invalid_argument("Empty external font path");
    }
    Impl::ExternalFont next;
    Impl::load(factory, path, next);
    impl_->external = std::move(next);
    impl_->externalPath = path;
    impl_->custom = Impl::Selected::External;
    impl_->currentFamily = impl_->external.family;
    impl_->selected = "@external-file";
    return true;
}

auto WindowsFonts::select(IDWriteFactory* factory, const std::string& value) -> bool {
    if (value == impl_->selected)
        return false;
    impl_->selected = value;
    impl_->custom = Impl::Selected::None;
    if (value == "@siyuan-heavy" || value == "@zqk-free") {
        const auto& font = value == "@zqk-free" ? impl_->zqk : impl_->bundled;
        if (font.collection) {
            impl_->currentFamily = font.family;
            impl_->custom = value == "@zqk-free" ? Impl::Selected::Zqk : Impl::Selected::Bundled;
        } else
            impl_->currentFamily = L"Segoe UI";
    } else {
        impl_->currentFamily = fontWide(value);
        Impl::ComPtr<IDWriteFontCollection> system;
        Impl::check(factory->GetSystemFontCollection(system.GetAddressOf()));
        UINT32 index = 0;
        BOOL exists = FALSE;
        Impl::check(system->FindFamilyName(impl_->currentFamily.c_str(), &index, &exists));
        if (!exists) {
            impl_->currentFamily = impl_->bundled.family.empty() ? L"Segoe UI" : impl_->bundled.family;
            if (impl_->bundled.collection)
                impl_->custom = Impl::Selected::Bundled;
        }
    }
    return true;
}

auto WindowsFonts::family() const -> const wchar_t* {
    return impl_->currentFamily.c_str();
}

auto WindowsFonts::collection() const -> IDWriteFontCollection* {
    const auto* current = impl_->current();
    return current ? current->collection.Get() : nullptr;
}

auto WindowsFonts::weight() const -> DWRITE_FONT_WEIGHT {
    const auto* current = impl_->current();
    return current ? current->weight : DWRITE_FONT_WEIGHT_NORMAL;
}

auto WindowsFonts::bundledAvailable(const std::string& id) const -> bool {
    return id == "@zqk-free" ? bool(impl_->zqk.collection) : bool(impl_->bundled.collection);
}
}  // namespace cdsdk
