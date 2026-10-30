#include <cstring>
#include <memory>

#include "sdk/PackageApi.h"
#include "support/Package.h"

namespace {
struct Handle {
    cdsdk::pb::PluginPackage package;
    cdsdk::pb::PluginDescriptor descriptor;
    std::string json;
    bool packaged = false;
};

void copyError(char* output, uint32_t capacity, const std::string& message) {
    if (!output || !capacity)
        return;
    const auto size = std::min<size_t>(message.size(), capacity - 1);
    std::memcpy(output, message.data(), size);
    output[size] = '\0';
}

CdResult open(const char* path, CdPackageHandle* output, char* error, uint32_t capacity, bool packaged) {
    if (output)
        *output = nullptr;
    if (!path || !output)
        return CD_ERROR_ARGUMENT;
    try {
        auto handle = std::make_unique<Handle>();
        cdsdk::Error failure;
        handle->packaged = packaged;
        const bool ok = packaged ? cdsdk::readPackage(cdsdk::utf8Path(path), &handle->package, &failure)
                                 : cdsdk::readDescriptor(cdsdk::utf8Path(path), &handle->descriptor, &failure);
        if (!ok) {
            copyError(error, capacity, cdsdk::errorText(failure));
            return CD_ERROR_ARGUMENT;
        }
        if (packaged)
            handle->descriptor = handle->package.plugin();
        handle->json = cdsdk::descriptorJson(handle->descriptor);
        *output = handle.release();
        copyError(error, capacity, "");
        return CD_OK;
    } catch (const std::exception& failure) {
        copyError(error, capacity, failure.what());
        return CD_ERROR_INTERNAL;
    } catch (...) {
        copyError(error, capacity, "SDK internal failure");
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

CdResult CD_CALL cd_package_open(const char* path, CdPackageHandle* output, char* error, uint32_t capacity) {
    return open(path, output, error, capacity, true);
}

CdResult CD_CALL cd_descriptor_open(const char* path, CdPackageHandle* output, char* error, uint32_t capacity) {
    return open(path, output, error, capacity, false);
}

void CD_CALL cd_package_close(CdPackageHandle handle) {
    const auto owned = std::unique_ptr<Handle>(static_cast<Handle*>(handle));
}

CdResult CD_CALL cd_package_descriptor_json(CdPackageHandle handle, const char** output, uint64_t* size) {
    if (!handle || !output || !size)
        return CD_ERROR_ARGUMENT;
    const auto* value = static_cast<Handle*>(handle);
    *output = value->json.data();
    *size = value->json.size();
    return CD_OK;
}

CdResult CD_CALL cd_package_extract(CdPackageHandle handle, const char* path, char* error, uint32_t capacity) {
    if (!handle || !path || !static_cast<Handle*>(handle)->packaged)
        return CD_ERROR_ARGUMENT;
    try {
        cdsdk::Error failure;
        if (!cdsdk::extractPackage(static_cast<Handle*>(handle)->package, cdsdk::utf8Path(path), &failure)) {
            copyError(error, capacity, cdsdk::errorText(failure));
            return CD_ERROR_ARGUMENT;
        }
        copyError(error, capacity, "");
        return CD_OK;
    } catch (const std::exception& failure) {
        copyError(error, capacity, failure.what());
        return CD_ERROR_INTERNAL;
    } catch (...) {
        copyError(error, capacity, "SDK extraction failed");
        return CD_ERROR_INTERNAL;
    }
}
