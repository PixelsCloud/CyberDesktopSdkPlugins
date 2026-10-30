#include "tools/PreviewAssets.h"

#include <CharacterChat/Protocol.h>
#include <CyberCanvas/MemoryBudget.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QProcess>
#include <QTimer>
#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <tuple>

#include "tools/PreviewEvents.h"

namespace cdsdk {
namespace {
cybercanvas::MemoryBudget& imageBudget() {
    static cybercanvas::MemoryBudget budget(48 * 1024 * 1024);
    return budget;
}
}  // namespace

struct PreviewAssets::Impl {
    using Key = std::pair<std::string, uint32_t>;

    struct Entry {
        uint32_t state = CD_CHARACTER_IMAGE_LOADING, width = 0, height = 0;
        uint64_t used = 0;
        QByteArray pixels;
        std::shared_ptr<cybercanvas::MemoryBudget::Lease> lease;
        std::string error;
    };

    PreviewEvents events;
    QObject lifetime;
    QProcess process;
    QTimer timeout;
    std::function<void()> redraw;
    std::filesystem::path root;
    std::map<Key, Entry> entries;
    std::deque<Key> queued;
    std::optional<Key> active;
    bool inflight = false, inspecting = false;
    uint64_t clock = 0;
    QByteArray response;
    std::string metadata, pack, failure;
    std::map<std::string, std::string> known;

    explicit Impl(std::function<void()> callback) : redraw(std::move(callback)) {
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &lifetime, [this] {
            failure = "Preview asset worker timed out";
            process.kill();
        });
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &lifetime, [this] {
            receive();
        });
        QObject::connect(&process, &QProcess::finished, &lifetime, [this](int code, QProcess::ExitStatus status) {
            finish(code == 0 && status == QProcess::NormalExit);
        });
        QObject::connect(&process, &QProcess::errorOccurred, &lifetime, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                finish(false);
            }
        });
    }

    ~Impl() {
        QObject::disconnect(&process, nullptr, &lifetime, nullptr);
        QObject::disconnect(&timeout, nullptr, &lifetime, nullptr);
        timeout.stop();
        process.kill();
        process.waitForFinished(1000);
    }

    void receive() {
        const qint64 limit = inspecting ? 256 * 1024 : 16 * 1024 * 1024 + 12;
        response += process.read(std::max(qint64(0), limit + 1 - response.size()));
        if (response.size() > limit || process.bytesAvailable() > 0) {
            failure = "Preview asset response exceeds budget";
            process.kill();
        }
    }

    void start(bool inspect) {
        inspecting = inspect;
        inflight = true;
        response.clear();
        failure.clear();
        QStringList arguments{QString::fromStdWString(root.wstring()), inspect ? "inspect" : "decode"};
        if (!inspect) {
            active = queued.front();
            queued.pop_front();
            arguments << QString::fromStdString(active->first) << QString::number(active->second)
                      << QString::fromStdString(known.at(active->first)) << QString::fromStdString(pack);
        }
        process.setProgram(QCoreApplication::applicationDirPath() + "/cd-sdk-image-worker.exe");
        process.setArguments(arguments);
        process.setProcessChannelMode(QProcess::SeparateChannels);
        timeout.start(10000);
        process.start();
    }

    void finish(bool success) {
        if (!inflight) {
            return;
        }
        receive();
        timeout.stop();
        inflight = false;
        success = success && failure.empty();
        try {
            if (!success) {
                throw std::runtime_error("Preview asset validation or decode failed");
            }
            if (inspecting) {
                const auto document = characterchat::parseBoundedDocument(response.toStdString(), 256 * 1024);
                pack = document.at("pack").get<std::string>();
                if (!characterchat::validResourcePackId(pack)) {
                    throw std::runtime_error("Invalid preview resource revision");
                }
                for (const auto& asset : document.at("resources").at("assets")) {
                    known.emplace(asset.at("id").get<std::string>(), asset.at("sha256").get<std::string>());
                }
                metadata = response.toStdString();
            } else {
                QDataStream stream(response);
                stream.setByteOrder(QDataStream::LittleEndian);
                quint32 magic = 0, width = 0, height = 0;
                stream >> magic >> width >> height;
                const auto bytes = uint64_t(width) * height * 4;
                if (magic != 0x4d494443 || !width || !height || width > active->second || height > active->second ||
                    response.size() != qint64(bytes + 12)) {
                    throw std::runtime_error("Invalid decoded image response");
                }
                auto& entry = entries.at(*active);
                entry.lease = imageBudget().reserve(bytes);
                if (!entry.lease) {
                    throw std::runtime_error("Preview image memory budget exhausted");
                }
                entry.pixels = response.sliced(12);
                entry.width = width;
                entry.height = height;
                entry.state = CD_CHARACTER_IMAGE_READY;
            }
        } catch (const std::exception& error) {
            failure = error.what();
            if (active) {
                auto& entry = entries.at(*active);
                entry.state = CD_CHARACTER_IMAGE_FAILED;
                entry.error = failure;
            }
        }
        response.clear();
        active.reset();
        if (redraw) {
            redraw();
        }
        if (!queued.empty()) {
            QTimer::singleShot(0, &lifetime, [this] {
                if (!inflight && !queued.empty()) {
                    start(false);
                }
            });
        }
    }
};

PreviewAssets::PreviewAssets(std::function<void()> redraw) : impl_(std::make_unique<Impl>(std::move(redraw))) {}

PreviewAssets::~PreviewAssets() = default;

void PreviewAssets::root(const std::filesystem::path& path) {
    if (impl_->inflight || !impl_->root.empty()) {
        throw std::logic_error("Create a new preview asset scope when replacing resources");
    }
    impl_->root = std::filesystem::absolute(path);
    impl_->start(true);
}

void PreviewAssets::tick() {
    impl_->events.pump();
}

std::string PreviewAssets::metadata() const {
    return impl_->metadata;
}

std::string PreviewAssets::error() const {
    return impl_->failure;
}

CdResult PreviewAssets::request(const char* pack, const char* id, uint32_t edge, CdCharacterImage* output) {
    if (!id || strnlen_s(id, 65) > 64 || !characterchat::validIdentifier(id) || !output ||
        output->struct_size < sizeof(*output) || edge < 64 || edge > 2048 || (pack && strnlen_s(pack, 70) > 69)) {
        return CD_ERROR_ARGUMENT;
    }
    *output = {sizeof(*output), CD_CHARACTER_IMAGE_LOADING};
    if (impl_->metadata.empty()) {
        if (!impl_->inflight && !impl_->failure.empty()) {
            output->state = CD_CHARACTER_IMAGE_FAILED;
            output->error = impl_->failure.c_str();
        }
        return CD_OK;
    }
    if ((pack && *pack && pack != impl_->pack) || !impl_->known.contains(id)) {
        output->state = CD_CHARACTER_IMAGE_FAILED;
        output->error = "Unknown preview image or resource revision";
        return CD_OK;
    }
    const uint32_t bucket = std::min(2048u, ((edge + 63) / 64) * 64);
    const Impl::Key key{id, bucket};
    auto found = impl_->entries.find(key);
    if (found == impl_->entries.end()) {
        while (impl_->entries.size() >= 32) {
            auto oldest = impl_->entries.end();
            for (auto item = impl_->entries.begin(); item != impl_->entries.end(); ++item) {
                if (item->second.state != CD_CHARACTER_IMAGE_LOADING &&
                    (oldest == impl_->entries.end() || item->second.used < oldest->second.used)) {
                    oldest = item;
                }
            }
            if (oldest == impl_->entries.end()) {
                output->state = CD_CHARACTER_IMAGE_FAILED;
                output->error = "Preview image queue is full";
                return CD_OK;
            }
            impl_->entries.erase(oldest);
        }
        found = impl_->entries.try_emplace(key).first;
        impl_->queued.push_back(key);
        if (!impl_->inflight) {
            impl_->start(false);
        }
    }
    auto& entry = found->second;
    entry.used = ++impl_->clock;
    *output = {sizeof(*output),    entry.state, entry.width, entry.height,
               entry.width * 4,    0,           1,           reinterpret_cast<const uint8_t*>(entry.pixels.constData()),
               entry.error.c_str()};
    return CD_OK;
}
}  // namespace cdsdk
