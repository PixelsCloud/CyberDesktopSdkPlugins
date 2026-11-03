// SPDX-License-Identifier: GPL-3.0-only
#include <Windows.h>
#include <sddl.h>
#include <shlobj.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>
#include <wil/resource.h>
#include <wil/stl.h>
#include <wil/win32_helpers.h>
#include <winrt/base.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "libraries/dwmglass/host/DwmAttachment.h"
#include "libraries/dwmglass/host/DwmSymbols.h"
#include "libraries/dwmglass/host/NativeHost.h"
#include "libraries/dwmglass/src/DwmDiagnosticPacket.h"
#include "libraries/liquidglass/src/NativeServiceProtocol_win.h"

namespace {
namespace host = cyberglass::dwm::host;
using liquidglass::native::kServiceName;

std::filesystem::path executable() {
    return wil::GetModuleFileNameW<std::wstring>(nullptr);
}

std::filesystem::path dataRoot() {
    wil::unique_cotaskmem_string root;
    winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, root.put()));
    const auto directory = std::filesystem::path(root.get()) / L"CyberDesktopNativeGlass";
    PSECURITY_DESCRIPTOR raw = nullptr;
    winrt::check_bool(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;S-1-5-90-0)", SDDL_REVISION_1, &raw, nullptr));
    const wil::unique_hlocal_security_descriptor security(raw);
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), security.get(), FALSE};
    if (!CreateDirectoryW(directory.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS) {
        winrt::throw_last_error();
    }
    const auto flags = GetFileAttributesW(directory.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) ||
        (flags & FILE_ATTRIBUTE_REPARSE_POINT)) {
        throw std::runtime_error("Native glass data directory must be a real directory");
    }
    // Reassert the private cache ACL when an earlier administrator created it.
    winrt::check_bool(SetFileSecurityW(
        directory.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, security.get()));
    return directory;
}

void initializeLogging(const std::filesystem::path& root) {
    const auto name = executable().stem().wstring();
    auto logger = spdlog::rotating_logger_mt("native-host", (root / (name + L".log")).wstring(), 32u * 1024 * 1024, 4);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
}

wil::unique_schandle serviceHandle(DWORD access) {
    wil::unique_schandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    winrt::check_bool(manager != nullptr);
    wil::unique_schandle service(OpenServiceW(manager.get(), kServiceName, access));
    winrt::check_bool(service != nullptr);
    return service;
}

void installService() {
    const auto command = L"\"" + executable().wstring() + L"\" --service";
    wil::unique_schandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE));
    winrt::check_bool(manager != nullptr);
    wil::unique_schandle service(CreateServiceW(
        manager.get(), kServiceName, L"CyberDesktop Native Glass", SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command.c_str(), nullptr, nullptr, L"RpcSs\0\0", nullptr, nullptr));
    if (!service && GetLastError() == ERROR_SERVICE_EXISTS) {
        service = serviceHandle(SERVICE_ALL_ACCESS);
        winrt::check_bool(ChangeServiceConfigW(service.get(), SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_NO_CHANGE,
                                               command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
    winrt::check_bool(service != nullptr);
    SC_ACTION actions[]{{SC_ACTION_RESTART, 1000}, {SC_ACTION_RESTART, 5000}, {SC_ACTION_NONE, 0}};
    SERVICE_FAILURE_ACTIONSW recovery{86400, nullptr, nullptr, DWORD(std::size(actions)), actions};
    winrt::check_bool(ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_FAILURE_ACTIONS, &recovery));
    SERVICE_DESCRIPTIONW description{
        const_cast<wchar_t*>(L"Native GPU glass for explicitly registered CyberGlass surfaces.")};
    winrt::check_bool(ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_DESCRIPTION, &description));
    if (!StartServiceW(service.get(), 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        winrt::throw_last_error();
    }
}

void stopService(bool remove) {
    auto service = serviceHandle(SERVICE_STOP | SERVICE_QUERY_STATUS | (remove ? DELETE : 0));
    SERVICE_STATUS status{};
    if (!ControlService(service.get(), SERVICE_CONTROL_STOP, &status) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
        winrt::throw_last_error();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(150);
    while (true) {
        winrt::check_bool(QueryServiceStatus(service.get(), &status));
        if (status.dwCurrentState == SERVICE_STOPPED) {
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("Native service is still detaching; do not overwrite its DLL");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (remove) {
        winrt::check_bool(DeleteService(service.get()));
    }
}

struct ServiceContext {
    std::unique_ptr<host::NativeHost> engine;
    SERVICE_STATUS_HANDLE handle = nullptr;
    SERVICE_STATUS status{SERVICE_WIN32_OWN_PROCESS, SERVICE_START_PENDING, 0, 0, 0, 0, 120000};

    void publish(DWORD state, DWORD error = 0) {
        status.dwCurrentState = state;
        status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
        status.dwWin32ExitCode = error;
        status.dwCheckPoint = state == SERVICE_STOP_PENDING || state == SERVICE_START_PENDING ? 1 : 0;
        SetServiceStatus(handle, &status);
    }
};

DWORD WINAPI control(DWORD code, DWORD, void*, void* context) {
    auto& service = *static_cast<ServiceContext*>(context);
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
        service.publish(SERVICE_STOP_PENDING);
        service.engine->requestStop();
    }
    return NO_ERROR;
}

void WINAPI serviceMain(DWORD, wchar_t**) {
    ServiceContext service;
    service.handle = RegisterServiceCtrlHandlerExW(kServiceName, &control, &service);
    if (!service.handle) {
        return;
    }
    try {
        service.publish(SERVICE_START_PENDING);
        const auto root = dataRoot();
        initializeLogging(root);
        service.engine =
            std::make_unique<host::NativeHost>(executable().parent_path() / L"cyber_glass_dwm.dll", root / L"symbols");
        service.publish(SERVICE_RUNNING);
        spdlog::info("Native service running; attachment is lazy and restricted to client sessions");
        service.engine->run();
        spdlog::info("Native service stopped");
        service.publish(SERVICE_STOPPED);
    } catch (const winrt::hresult_error& error) {
        spdlog::error("Native service failed: 0x{:08x}", uint32_t(error.code()));
        service.publish(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
    } catch (const std::exception& error) {
        spdlog::error("Native service failed: {}", error.what());
        service.publish(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
    }
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    const std::wstring_view option = argc == 2 ? argv[1] : L"";
    if (option == L"--service") {
        SERVICE_TABLE_ENTRYW table[]{{const_cast<wchar_t*>(kServiceName), &serviceMain}, {nullptr, nullptr}};
        winrt::check_bool(StartServiceCtrlDispatcherW(table));
    } else if (option == L"--install") {
        installService();
    } else if (option == L"--stop" || option == L"--uninstall") {
        stopService(option == L"--uninstall");
    } else if (option == L"--symbols" || option == L"--attach" || option == L"--detach" || option == L"--inspect" ||
               option == L"--diagnose") {
        const auto root = dataRoot();
        initializeLogging(root);
        if (option == L"--symbols") {
            host::prepareSymbols(root / L"symbols");
        } else {
            DWORD session = 0;
            winrt::check_bool(ProcessIdToSessionId(GetCurrentProcessId(), &session));
            host::DwmAttachment attachment(session);
            const auto library = executable().parent_path() / L"cyber_glass_dwm.dll";
            if (option == L"--attach") {
                attachment.attach(library, root / L"symbols");
                std::cout << "process=" << attachment.process() << " generation=" << attachment.generation() << '\n';
            } else if (option == L"--detach") {
                attachment.detach(library);
            } else {
                const auto packet = attachment.diagnostics(library, option == L"--diagnose");
                const auto& values = packet.values;
                const char* names[]{"enabled", "clientBlur", "property", "mappingFailure", "marker",     "brush",
                                    "render",  "lastError",  "command",  "solid",          "rawAlpha",   "surfaces",
                                    "width",   "height",     "dirty",    "parent",         "windowDirty"};
                for (size_t index = 0; index < values.size(); ++index) {
                    std::cout << names[index] << '=' << values[index] << '\n';
                }
                std::cout << "errorText=" << packet.error.data() << '\n';
                std::cout << "residentLiquidMaterials=" << packet.residentLiquidMaterials << '\n';
                for (uint32_t index = 0; index < packet.surfaceCount; ++index) {
                    const auto& surface = packet.surfaces[index];
                    std::cout << "registeredSurface hwnd=" << surface.window << " pid=" << surface.process
                              << " material=" << surface.material << " width=" << surface.width
                              << " height=" << surface.height << '\n';
                }
            }
        }
    } else {
        std::cout << "cyber_glass_host --install | --stop | --uninstall | --symbols | --attach | --detach\n";
        return 2;
    }
    return 0;
} catch (const winrt::hresult_error& error) {
    std::cerr << "Native glass error: 0x" << std::hex << uint32_t(error.code()) << ' '
              << winrt::to_string(error.message()) << '\n';
    return 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
