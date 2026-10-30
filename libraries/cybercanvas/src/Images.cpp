#include <CyberCanvas/Diagnostics.h>
#include <CyberCanvas/Images.h>
#include <CyberCanvas/Native.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>

namespace cybercanvas {
namespace {
struct Budget {
    std::atomic<uint64_t> bytes = 0, peak = 0;
};

struct Record {
    std::mutex mutex;
    ImageSnapshot result{ImageState::Loading};
    std::atomic<size_t> users = 0;
    std::filesystem::path path;
    std::shared_ptr<const std::wstring> source;
    uint32_t edge = 4096;
    ImageRegion region;
    std::vector<uint32_t> palette;
    ImageSampling sampling = ImageSampling::Smooth;
    bool oriented = false;
    uint64_t age = 0, decodeNanoseconds = 0;
};
}  // namespace

struct ImageHandle::Interest {
    std::shared_ptr<Record> record;
    bool reused = false;

    explicit Interest(std::shared_ptr<Record> value) : record(std::move(value)) {
        ++record->users;
    }

    ~Interest() {
        --record->users;
    }
};

ImageSnapshot ImageHandle::snapshot() const {
    if (!interest_) {
        return {cancelled_ ? ImageState::Cancelled : ImageState::Empty};
    }
    const auto& record = interest_->record;
    std::lock_guard lock(record->mutex);
    return record->result;
}

ImageRequestStats ImageHandle::stats() const {
    if (!interest_) {
        return {cancelled_ ? ImageState::Cancelled : ImageState::Empty};
    }
    const auto& record = interest_->record;
    std::lock_guard lock(record->mutex);
    return {record->result.state, record->decodeNanoseconds,
            record->result.image ? record->result.image->pixels().size() : 0, record->users.load(), interest_->reused};
}

void ImageHandle::cancel() {
    interest_.reset();
    cancelled_ = true;
}

struct ImageStore::Impl {
    using Key = std::tuple<std::wstring, uint64_t, uint32_t, ImageRegion, std::vector<uint32_t>, ImageSampling, bool>;
    ImageLimits limits;
    std::shared_ptr<Budget> budget = std::make_shared<Budget>();
    std::shared_ptr<MemoryBudget> sharedBudget;
    ResourceAccount externalAccount;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::map<Key, std::shared_ptr<Record>> cache;
    std::deque<std::shared_ptr<Record>> queue;
    uint64_t clock = 0, decodes = 0, hits = 0, failures = 0, decodeNanoseconds = 0;
    bool stopping = false;
    // Declared last so the worker joins before any state is destroyed.
    std::jthread worker;

    explicit Impl(ImageLimits value, std::shared_ptr<MemoryBudget> shared)
        : limits(value), sharedBudget(std::move(shared)) {
        if (limits.decodedBytes < 4 || limits.decodedBytes > (1ULL << 32) || !limits.fileBytes || !limits.entries ||
            limits.entries > 4096) {
            throw std::invalid_argument("Invalid image resource limits");
        }
        worker = std::jthread([this] {
            run();
        });
    }

    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            queue.clear();
        }
        wake.notify_all();
        worker.join();
        for (auto& [key, record] : cache) {
            std::lock_guard lock(record->mutex);
            if (record->result.state == ImageState::Loading) {
                record->result = {ImageState::Cancelled};
            }
        }
    }

    // Called with mutex held. External snapshots retain accounted pixel memory,
    // but must not pin obsolete cache keys during simultaneous DPI changes.
    bool evict() {
        auto oldest = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            const auto& record = it->second;
            std::lock_guard lock(record->mutex);
            if (!record->users && (oldest == cache.end() || record->age < oldest->second->age)) {
                oldest = it;
            }
        }
        if (oldest == cache.end()) {
            return false;
        }
        cache.erase(oldest);
        return true;
    }

    std::shared_ptr<ImageData> allocate(uint32_t width, uint32_t height) {
        const uint64_t bytes = uint64_t(width) * height * 4;
        auto lease = sharedBudget ? sharedBudget->reserve(bytes) : std::shared_ptr<MemoryBudget::Lease>{};
        auto allocation = std::unique_ptr<ImageData>(new ImageData);
        ComPtr<IUnknown> externalLease;
        {
            std::lock_guard lock(mutex);
            while (budget->bytes + bytes > limits.decodedBytes && evict()) {
            }
            if (stopping || bytes > limits.decodedBytes || budget->bytes + bytes > limits.decodedBytes) {
                throw std::runtime_error("Image decoded-memory budget exhausted");
            }
            try {
                externalLease = externalAccount.acquire(bytes);
            } catch (const std::length_error&) {
                while (evict()) {
                }
                externalLease = externalAccount.acquire(bytes);
            }
            const auto used = budget->bytes.fetch_add(bytes) + bytes;
            budget->peak = std::max(budget->peak.load(), used);
        }
        // Allocate/zero the potentially large pixel buffer without blocking request().
        auto image = std::shared_ptr<ImageData>(
            allocation.release(), [account = budget, bytes, lease = std::move(lease), externalLease](ImageData* value) {
                auto owned = std::unique_ptr<ImageData>(value);
                // Release pixel storage before making its budget available again.
                owned.reset();
                account->bytes -= bytes;
            });
        image->width_ = width;
        image->height_ = height;
        image->pixels_.resize(size_t(bytes));
        return image;
    }

    std::shared_ptr<const ImageData> decode(const Record& record, IWICImagingFactory* factory) {
        if (std::filesystem::file_size(record.path) > limits.fileBytes) {
            throw std::runtime_error("Image file exceeds the size limit");
        }
        ComPtr<IWICBitmapDecoder> decoder;
        require(factory->CreateDecoderFromFilename(record.path.c_str(), nullptr, GENERIC_READ,
                                                   WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
        ComPtr<IWICBitmapFrameDecode> frame;
        require(decoder->GetFrame(0, frame.GetAddressOf()));
        UINT width = 0, height = 0;
        require(frame->GetSize(&width, &height));
        if (!width || !height || width > 100000 || height > 100000) {
            throw std::runtime_error("Invalid image source dimensions");
        }
        ComPtr<IWICBitmapSource> source = frame;
        if (record.oriented) {
            if (uint64_t(width) * height > 100000000) {
                throw std::runtime_error("Photo exceeds the source pixel limit");
            }
            ComPtr<IWICMetadataQueryReader> metadata;
            PROPVARIANT orientation{};
            PropVariantInit(&orientation);
            unsigned value = 1;
            if (SUCCEEDED(frame->GetMetadataQueryReader(&metadata))) {
                if (FAILED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &orientation))) {
                    PropVariantClear(&orientation);
                    metadata->GetMetadataByName(L"/ifd/{ushort=274}", &orientation);
                }
                if (orientation.vt == VT_UI2) {
                    value = orientation.uiVal;
                }
            }
            PropVariantClear(&orientation);
            constexpr WICBitmapTransformOptions transforms[]{
                WICBitmapTransformRotate0,
                WICBitmapTransformRotate0,
                WICBitmapTransformFlipHorizontal,
                WICBitmapTransformRotate180,
                WICBitmapTransformFlipVertical,
                WICBitmapTransformOptions(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal),
                WICBitmapTransformRotate90,
                WICBitmapTransformOptions(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal),
                WICBitmapTransformRotate270};
            if (value >= 2 && value <= 8) {
                ComPtr<IWICBitmapFlipRotator> rotated;
                require(factory->CreateBitmapFlipRotator(&rotated));
                require(rotated->Initialize(source.Get(), transforms[value]));
                source = rotated;
                require(source->GetSize(&width, &height));
            }
        }
        ComPtr<IWICBitmapClipper> clipper;
        const auto region = record.region;
        if (region.width) {
            if (uint64_t(region.x) + region.width > width || uint64_t(region.y) + region.height > height) {
                throw std::invalid_argument("Image region exceeds source bounds");
            }
            require(factory->CreateBitmapClipper(&clipper));
            const WICRect bounds{int(region.x), int(region.y), int(region.width), int(region.height)};
            require(clipper->Initialize(frame.Get(), &bounds));
            source = clipper;
            width = region.width;
            height = region.height;
        }
        const double scale = std::min(1., double(record.edge) / std::max(width, height));
        width = std::max(1u, UINT(std::lround(width * scale)));
        height = std::max(1u, UINT(std::lround(height * scale)));
        auto result = allocate(width, height);
        ComPtr<IWICBitmapScaler> scaler;
        require(factory->CreateBitmapScaler(scaler.GetAddressOf()));
        require(scaler->Initialize(source.Get(), width, height,
                                   record.palette.empty() && record.sampling == ImageSampling::Smooth
                                       ? WICBitmapInterpolationModeFant
                                       : WICBitmapInterpolationModeNearestNeighbor));
        ComPtr<IWICPalette> palette;
        ComPtr<IWICFormatConverter> indexed;
        ComPtr<IWICBitmapSource> sampled = scaler;
        if (!record.palette.empty()) {
            require(factory->CreatePalette(&palette));
            auto colors = record.palette;
            require(palette->InitializeCustom(colors.data(), UINT(colors.size())));
            require(factory->CreateFormatConverter(&indexed));
            require(indexed->Initialize(scaler.Get(), GUID_WICPixelFormat8bppIndexed, WICBitmapDitherTypeNone,
                                        palette.Get(), 50, WICBitmapPaletteTypeCustom));
            sampled = indexed;
        }
        ComPtr<IWICFormatConverter> converter;
        require(factory->CreateFormatConverter(converter.GetAddressOf()));
        require(converter->Initialize(sampled.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                      WICBitmapPaletteTypeCustom));
        require(converter->CopyPixels(nullptr, width * 4, UINT(result->pixels_.size()), result->pixels_.data()));
        return result;
    }

    void run() noexcept {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ComPtr<IWICImagingFactory> factory;
        const auto created = SUCCEEDED(initialized)
                                 ? CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                    IID_PPV_ARGS(factory.GetAddressOf()))
                                 : initialized;
        for (;;) {
            std::shared_ptr<Record> record;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] {
                    return stopping || !queue.empty();
                });
                if (stopping) {
                    break;
                }
                record = std::move(queue.front());
                queue.pop_front();
                if (!record->users) {
                    std::lock_guard recordLock(record->mutex);
                    record->result = {ImageState::Cancelled};
                    continue;
                }
            }
            ImageSnapshot result;
            result.source = record->source;
            const auto started = std::chrono::steady_clock::now();
            try {
                require(created);
                result.image = decode(*record, factory.Get());
                result.state = ImageState::Ready;
            } catch (const std::exception& error) {
                result.state = ImageState::Failed;
                result.error = error.what();
            } catch (...) {
                result.state = ImageState::Failed;
            }
            const auto elapsed = nanosecondsSince(started);
            {
                std::lock_guard lock(mutex);
                std::lock_guard recordLock(record->mutex);
                record->decodeNanoseconds = elapsed;
                decodeNanoseconds += elapsed;
                decodes += result.state != ImageState::Cancelled;
                failures += result.state == ImageState::Failed;
                record->result = stopping ? ImageSnapshot{ImageState::Cancelled} : std::move(result);
            }
        }
        factory.Reset();
        if (SUCCEEDED(initialized)) {
            CoUninitialize();
        }
    }
};

ImageStore::ImageStore(ImageLimits limits, std::shared_ptr<MemoryBudget> sharedBudget)
    : impl_(std::make_unique<Impl>(limits, std::move(sharedBudget))) {}

ImageStore::~ImageStore() = default;

ImageHandle ImageStore::request(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge) {
    return request(path, revision, maximumEdge, {});
}

ImageHandle ImageStore::request(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge,
                                ImageRegion region) {
    return request(path, revision, maximumEdge, region, {});
}

ImageHandle ImageStore::request(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge,
                                ImageRegion region, const std::vector<uint32_t>& palette) {
    return request(path, revision, maximumEdge, region, palette, ImageSampling::Smooth);
}

ImageHandle ImageStore::request(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge,
                                ImageRegion region, const std::vector<uint32_t>& palette, ImageSampling sampling) {
    return requestInternal(path, revision, maximumEdge, region, palette, sampling, false);
}

ImageHandle ImageStore::requestOriented(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge) {
    return requestInternal(path, revision, maximumEdge, {}, {}, ImageSampling::Smooth, true);
}

ImageHandle ImageStore::requestInternal(const std::filesystem::path& path, uint64_t revision, uint32_t maximumEdge,
                                        ImageRegion region, const std::vector<uint32_t>& palette,
                                        ImageSampling sampling, bool oriented) {
    if (palette.size() > 256) {
        throw std::invalid_argument("Image palette exceeds 256 entries");
    }
    if (static_cast<unsigned>(sampling) > static_cast<unsigned>(ImageSampling::Nearest)) {
        throw std::invalid_argument("Invalid image decode sampling");
    }
    if (path.empty() || path.native().find(L'\0') != std::wstring::npos || !maximumEdge || maximumEdge > 4096) {
        throw std::invalid_argument("Invalid image request");
    }
    if ((region.width == 0) != (region.height == 0) || (!region.width && (region.x || region.y)) ||
        uint64_t(region.x) + region.width > 100000 || uint64_t(region.y) + region.height > 100000) {
        throw std::invalid_argument("Invalid image source region");
    }
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    const Impl::Key key{absolute.wstring(), revision, maximumEdge, region, palette, sampling, oriented};
    std::lock_guard lock(impl_->mutex);
    for (auto it = impl_->queue.begin(); it != impl_->queue.end();) {
        const auto record = *it;
        if (!record->users) {
            std::lock_guard recordLock(record->mutex);
            record->result = {ImageState::Cancelled};
            it = impl_->queue.erase(it);
        } else {
            ++it;
        }
    }
    auto found = impl_->cache.find(key);
    if (found != impl_->cache.end()) {
        const auto record = found->second;
        bool cancelled = false;
        {
            std::lock_guard recordLock(record->mutex);
            cancelled = record->result.state == ImageState::Cancelled ||
                        (record->result.state == ImageState::Failed && !record->users);
        }
        if (cancelled) {
            impl_->cache.erase(found);
            found = impl_->cache.end();
        }
    }
    std::shared_ptr<Record> record;
    if (found != impl_->cache.end()) {
        record = found->second;
        ++impl_->hits;
    } else {
        while (impl_->cache.size() >= impl_->limits.entries && impl_->evict()) {
        }
        record = std::make_shared<Record>();
        record->path = absolute;
        record->edge = maximumEdge;
        record->region = region;
        record->palette = palette;
        record->sampling = sampling;
        record->oriented = oriented;
        record->source = std::make_shared<const std::wstring>(absolute.wstring());
        if (impl_->cache.size() >= impl_->limits.entries || impl_->queue.size() >= impl_->limits.entries) {
            record->result = {ImageState::Failed, {}, "Image request capacity exhausted"};
            ++impl_->failures;
        } else {
            impl_->cache.emplace(key, record);
            impl_->queue.push_back(record);
        }
        record->result.source = record->source;
    }
    record->age = ++impl_->clock;
    ImageHandle handle;
    handle.interest_ = std::make_shared<ImageHandle::Interest>(record);
    handle.interest_->reused = found != impl_->cache.end();
    impl_->wake.notify_one();
    return handle;
}

void ImageStore::trim() {
    std::lock_guard lock(impl_->mutex);
    while (impl_->evict()) {
    }
}

void ImageStore::resourceAccount(ResourceAccount account) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->externalAccount.reserve &&
        (impl_->externalAccount.reserve != account.reserve || impl_->externalAccount.context != account.context)) {
        throw std::logic_error("Image store already belongs to another resource account");
    }
    impl_->externalAccount = account;
}

bool ImageStore::retry(const ImageHandle& handle) {
    if (!handle.interest_ || handle.cancelled_) {
        return false;
    }
    std::lock_guard lock(impl_->mutex);
    const auto record = handle.interest_->record;
    if (std::none_of(impl_->cache.begin(), impl_->cache.end(), [&](const auto& entry) {
            return entry.second == record;
        })) {
        return false;
    }
    std::lock_guard recordLock(record->mutex);
    if (impl_->stopping || record->result.state != ImageState::Failed) {
        return false;
    }
    record->result = {ImageState::Loading};
    record->result.source = record->source;
    impl_->queue.push_back(record);
    impl_->wake.notify_one();
    return true;
}

ImageStats ImageStore::stats() const {
    std::lock_guard lock(impl_->mutex);
    size_t pending = 0;
    for (const auto& [key, record] : impl_->cache) {
        std::lock_guard recordLock(record->mutex);
        pending += record->result.state == ImageState::Loading;
    }
    return {impl_->budget->bytes.load(),
            impl_->budget->peak.load(),
            impl_->decodes,
            impl_->hits,
            impl_->failures,
            impl_->cache.size(),
            pending,
            impl_->decodeNanoseconds};
}

std::shared_ptr<ImageStore> ImageStore::shared() {
    static std::mutex mutex;
    static std::weak_ptr<ImageStore> instance;
    std::lock_guard lock(mutex);
    auto value = instance.lock();
    if (!value) {
        value = std::make_shared<ImageStore>();
        instance = value;
    }
    return value;
}
}  // namespace cybercanvas
