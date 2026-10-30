#include <CharacterChat/Configuration.h>
#include <CharacterChat/Protocol.h>
#include <CharacterResources/ResourceTree.h>
#include <Windows.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("Invalid preview asset or manifest");
    }
}

void writeOutput(const QByteArray& bytes) {
    const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        require(WriteFile(handle, bytes.constData() + offset, DWORD(bytes.size() - offset), &written, nullptr) &&
                written);
        offset += written;
    }
}
}  // namespace

int main(int argc, char** argv) try {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    require(arguments.size() == 3 || arguments.size() == 7);
    characterresources::ResourceTree package(arguments[1]);
    const auto fixture =
        characterchat::parseBoundedDocument(package.read("sdk-preview.json", 4096).toStdString(), 4096);
    const auto profilesPath = fixture.at("profiles").get<std::string>();
    const auto resourcesPath = fixture.at("resources").get<std::string>();
    const auto resourcesBytes = package.read(QString::fromStdString(resourcesPath), 256 * 1024);
    auto manifest = characterchat::parseConfiguration(resourcesBytes.toStdString(), resourcesPath).at("resources");
    const auto directory = QFileInfo(QString::fromStdString(resourcesPath)).path();
    const auto pack =
        "pack-" + QCryptographicHash::hash(resourcesBytes, QCryptographicHash::Sha256).toHex().toStdString();
    QByteArray response;
    QBuffer output(&response);
    require(output.open(QIODevice::WriteOnly));
    if (arguments[2] == "inspect") {
        auto profilesDocument = characterchat::parseConfiguration(
            package.read(QString::fromStdString(profilesPath), 128 * 1024).toStdString(), profilesPath);
        for (auto& profile : profilesDocument.at("characters")) {
            profile.erase("pack");
            profile.erase("enabled");
        }
        auto profiles = characterchat::parseProfiles(profilesDocument.dump());
        auto& assets = manifest.at("assets");
        require(assets.is_array() && assets.size() <= 64);
        for (auto asset = assets.begin(); asset != assets.end();) {
            if (asset->at("type") != "image") {
                asset = assets.erase(asset);
                continue;
            }
            const auto data =
                package.read(QDir(directory).filePath(QString::fromStdString(asset->at("file").get<std::string>())),
                             32 * 1024 * 1024);
            QBuffer input;
            input.setData(data);
            require(input.open(QIODevice::ReadOnly));
            QImageReader reader(&input);
            reader.setDecideFormatFromContent(true);
            const auto size = reader.size();
            require(size.isValid() && size.width() <= 8192 && size.height() <= 8192 && !reader.supportsAnimation());
            (*asset)["bytes"] = uint64_t(data.size());
            (*asset)["sha256"] = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();
            (*asset)["width"] = size.width();
            (*asset)["height"] = size.height();
            ++asset;
        }
        characterchat::parseResources(manifest.dump());
        nlohmann::json characters = nlohmann::json::array();
        for (const auto& profile : profiles) {
            characters.push_back({{"id", profile.id}, {"name", profile.name}, {"greeting", profile.greeting}});
        }
        const auto metadata =
            nlohmann::json{{"characters", characters}, {"pack", pack}, {"resources", manifest}}.dump();
        require(metadata.size() <= 256 * 1024);
        require(output.write(metadata.data(), qint64(metadata.size())) == qint64(metadata.size()));
        writeOutput(response);
        return 0;
    }
    require(arguments[2] == "decode" && arguments.size() == 7);
    const auto id = arguments[3].toStdString();
    bool valid = false;
    const auto edge = arguments[4].toUInt(&valid);
    require(valid && edge >= 64 && edge <= 2048 && characterchat::validIdentifier(id));
    const auto& assets = manifest.at("assets");
    require(assets.is_array() && assets.size() <= 64);
    const auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) {
        return item.at("id") == id;
    });
    require(asset != assets.end() && asset->at("type") == "image");
    require(arguments[6].toStdString() == pack && characterchat::validSha256(arguments[5].toStdString()));
    const auto bytes = package.read(
        QDir(directory).filePath(QString::fromStdString(asset->at("file").get<std::string>())), 32 * 1024 * 1024);
    require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() == arguments[5].toLatin1());
    QBuffer buffer;
    buffer.setData(bytes);
    require(buffer.open(QIODevice::ReadOnly));
    QImageReader::setAllocationLimit(64);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const auto original = reader.size();
    require(original.isValid() && original.width() <= 8192 && original.height() <= 8192 && !reader.supportsAnimation());
    const auto size = original.scaled(int(edge), int(edge), Qt::KeepAspectRatio);
    reader.setScaledSize(size);
    auto image = reader.read();
    require(!image.isNull());
    if (image.size() != size) {
        image = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    require(image.sizeInBytes() <= 16 * 1024 * 1024);
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x4d494443) << quint32(image.width()) << quint32(image.height());
    for (int row = 0; row < image.height(); ++row) {
        require(output.write(reinterpret_cast<const char*>(image.constScanLine(row)), image.width() * 4) ==
                image.width() * 4);
    }
    writeOutput(response);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
