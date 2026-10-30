#include "runtime/live2d/ShaderCache.h"
#undef D3DCompile

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <cstring>

namespace {
bool healthy = true;
}

bool live2dShaderCacheHealthy() {
    return healthy;
}

HRESULT WINAPI CdLive2DCompile(const void* source, SIZE_T length, const char* name, const D3D_SHADER_MACRO* defines,
                               ID3DInclude* includes, const char* entry, const char* target, UINT flags,
                               UINT effectFlags, ID3DBlob** code, ID3DBlob** errors) {
    if (errors) {
        *errors = nullptr;
    }
    if (!source || length > 2 * 1024 * 1024 || !code || !entry || !target || defines || includes) {
        healthy = false;
        return E_INVALIDARG;
    }
    *code = nullptr;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(static_cast<const char*>(source), qsizetype(length)));
    hash.addData(QByteArray(entry) + '\0' + target + '\0' + QByteArray::number(flags) + ':' +
                 QByteArray::number(effectFlags));
    const auto directory = QCoreApplication::applicationDirPath() + "/live2d/shader-cache";
    const auto path = directory + '/' + hash.result().toHex() + ".cso";
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) && file.size() > 0 && file.size() < 8 * 1024 * 1024) {
        const auto bytes = file.readAll();
        if (SUCCEEDED(D3DCreateBlob(size_t(bytes.size()), code))) {
            std::memcpy((*code)->GetBufferPointer(), bytes.constData(), size_t(bytes.size()));
            return S_OK;
        }
    }
    if (!qEnvironmentVariableIsSet("CD_LIVE2D_COMPILE_SHADERS")) {
        healthy = false;
        return E_FAIL;
    }
    const auto result =
        D3DCompile(source, length, name, defines, includes, entry, target, flags, effectFlags, code, errors);
    if (SUCCEEDED(result)) {
        QDir().mkpath(directory);
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(static_cast<const char*>((*code)->GetBufferPointer()), (*code)->GetBufferSize()) < 0 ||
            !output.commit()) {
            healthy = false;
            return E_FAIL;
        }
    } else {
        healthy = false;
    }
    return result;
}
