#include "tools/PreviewEvents.h"

#include <QCoreApplication>
#include <QEventLoop>

namespace cdsdk {
struct PreviewEvents::Impl {
    int argc = 1;
    char name[15] = "cd-sdk-preview";
    char* argv[2]{name, nullptr};
    std::unique_ptr<QCoreApplication> application;

    Impl() {
        if (!QCoreApplication::instance()) {
            application = std::make_unique<QCoreApplication>(argc, argv);
        }
    }
};

PreviewEvents::PreviewEvents() {
    // All preview services share the same owner UI thread.
    static std::weak_ptr<Impl> current;
    impl_ = current.lock();
    if (!impl_) {
        impl_ = std::make_shared<Impl>();
        current = impl_;
    }
}

PreviewEvents::~PreviewEvents() = default;

void PreviewEvents::pump() {
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}
}  // namespace cdsdk
