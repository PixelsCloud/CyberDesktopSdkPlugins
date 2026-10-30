#pragma once
#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace lunarplugin {
struct Date {
    int year = 2026, month = 2, day = 17;
    bool operator==(const Date&) const = default;
};

bool valid(Date date);
Date today();
Date shiftDay(Date date, int days);
Date shiftMonth(Date date, int months);
int weekday(Date date);
int dayCount(Date date);

struct Lunar {
    int year = 0, month = 0, day = 0;
    bool leap = false;
    std::wstring monthName, dayName, yearName, festival, term;
    std::wstring label() const;
};

Lunar convert(Date date);

struct Month {
    std::array<Date, 42> dates;
    int rows = 6;
};

Month monthGrid(Date date, bool monday);

struct Spec {
    const char* id;
    int rows, columns;
    bool mechanical = false;
};

struct View {
    Date current, selected;
    bool dark = false;
    Date previous;
    double progress = 1;
    int direction = 1;
};

int hit(Spec spec, double w, double h, double x, double y);
cdsdk::Settings defaults(bool mechanical = false);

class Renderer {
public:
    Renderer();
    ~Renderer();
    void bind(const CdCreateContext&, std::function<void(uint32_t)>);
    void input(const CdInput&);
    void cancelInput();
    void pause(bool);
    bool pendingImages() const;
    void syncInput(Spec, const cdsdk::Settings&, View, CdRect);
    bool render(Spec spec, const cdsdk::Settings& settings, View view, const CdFrame& frame, CdRect safe);
    bool save(const std::wstring& filename, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* output, uint32_t size);
}  // namespace lunarplugin
