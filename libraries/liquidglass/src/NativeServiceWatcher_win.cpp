#include "libraries/liquidglass/src/NativeServiceWatcher_win.h"

#include <Windows.h>
#include <wil/resource.h>

#include <type_traits>
#include <utility>

#include "libraries/liquidglass/src/NativeServiceProtocol_win.h"

namespace liquidglass::native {
namespace {
struct NativeApi {
    wil::unique_hmodule module{LoadLibraryExW(L"sechost.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    decltype(&SubscribeServiceChangeNotifications) subscribe =
        module
            ? reinterpret_cast<decltype(subscribe)>(GetProcAddress(module.get(), "SubscribeServiceChangeNotifications"))
            : nullptr;
    decltype(&UnsubscribeServiceChangeNotifications) unsubscribe =
        module ? reinterpret_cast<decltype(unsubscribe)>(
                     GetProcAddress(module.get(), "UnsubscribeServiceChangeNotifications"))
               : nullptr;
};

struct SubscriptionCloser {
    decltype(&UnsubscribeServiceChangeNotifications) unsubscribe = nullptr;

    void operator()(PSC_NOTIFICATION_REGISTRATION value) const {
        if (unsubscribe) {
            unsubscribe(value);
        }
    }
};

using Subscription = std::unique_ptr<std::remove_pointer_t<PSC_NOTIFICATION_REGISTRATION>, SubscriptionCloser>;

struct CallbackContext {
    std::function<void(bool)> changed;
    bool database = false;
};

void CALLBACK changed(DWORD, void* context) {
    const auto& value = *static_cast<CallbackContext*>(context);
    try {
        value.changed(value.database);
    } catch (...) {
        // Never unwind through SCM's native callback. Connection retry remains
        // bounded independently if the caller cannot queue a notification.
    }
}

Subscription subscribe(const NativeApi& api, SC_HANDLE handle, SC_EVENT_TYPE type, CallbackContext& context) {
    PSC_NOTIFICATION_REGISTRATION value = nullptr;
    if (handle && api.subscribe && api.unsubscribe &&
        api.subscribe(handle, type, &changed, &context, &value) == ERROR_SUCCESS) {
        return Subscription(value, {api.unsubscribe});
    }
    return {};
}
}  // namespace

struct ServiceWatcher::Impl {
    NativeApi api;
    wil::unique_schandle manager;
    wil::unique_schandle service;
    CallbackContext databaseContext, statusContext;
    // Unsubscribe waits for callbacks. It must precede context/handle release
    // and must never be called from SCM's own callback.
    Subscription database, status;
};

ServiceWatcher::ServiceWatcher(std::function<void(bool)> notify) : m_impl(std::make_unique<Impl>()) {
    auto& value = *m_impl;
    value.databaseContext = {notify, true};
    value.statusContext = {std::move(notify), false};
    value.manager.reset(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE));
    value.database = subscribe(value.api, value.manager.get(), SC_EVENT_DATABASE_CHANGE, value.databaseContext);
}

ServiceWatcher::~ServiceWatcher() = default;

bool ServiceWatcher::refresh(bool reopen) {
    auto& value = *m_impl;
    if (reopen) {
        value.status.reset();
        value.service.reset();
    }
    if (!value.service && value.manager) {
        value.service.reset(OpenServiceW(value.manager.get(), kServiceName, SERVICE_QUERY_STATUS));
        value.status = subscribe(value.api, value.service.get(), SC_EVENT_STATUS_CHANGE, value.statusContext);
    }
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes = 0;
    return value.service &&
           QueryServiceStatusEx(value.service.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status),
                                sizeof(status), &bytes) &&
           status.dwCurrentState == SERVICE_RUNNING;
}

}  // namespace liquidglass::native
