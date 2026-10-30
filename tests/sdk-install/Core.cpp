#include <sdk/WeatherPresentation.h>
#include <sdk/cpp/Services.h>

#include <iostream>

int main() {
    cdsdk::Services services(nullptr, nullptr);
    CdInputServiceApi input{};
    if (services.query(CD_SERVICE_INPUT, input) != CD_ERROR_UNSUPPORTED || cdsdk::weather::iconName(0, true).empty()) {
        return 1;
    }
    std::cout << "Installed Core without Canvas or private source: PASS\n";
    return 0;
}
