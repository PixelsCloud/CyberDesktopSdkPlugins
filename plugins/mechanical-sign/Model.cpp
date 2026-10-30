#include <Windows.h>
#include <icu.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

#include "plugins/mechanical-sign/Sign.h"

namespace mechanicalsign {
namespace {
constexpr double kFlap = .33, kWarmup = .1, kCoast = .25, kStagger = .055;
constexpr double kAngularSpeed = 6 * std::numbers::pi;

double travel(double time, double duration) {
    time = std::clamp(time, 0., duration);
    if (time < kWarmup) {
        return kAngularSpeed * time * time / (2 * kWarmup);
    }
    const double coast = duration - kCoast;
    if (time <= coast) {
        return kAngularSpeed * (time - kWarmup / 2);
    }
    const double t = time - coast;
    return kAngularSpeed * (coast - kWarmup / 2 + t - t * t / (2 * kCoast));
}
}  // namespace

std::vector<Page> pages(std::string_view text) {
    if (text.size() > 512 || text.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Sign text is too long or contains a null");
    }
    std::wstring wide;
    if (!text.empty()) {
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
        if (!count) {
            throw std::invalid_argument("Invalid UTF-8 sign text");
        }
        wide.resize(count);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), wide.data(), count);
    }
    std::erase(wide, L'\r');
    for (auto& ch : wide) {
        if (ch == L'\t') {
            ch = L' ';
        } else if (ch < 32 && ch != L'\n') {
            throw std::invalid_argument("Unsupported text control");
        }
    }
    UErrorCode error = U_ZERO_ERROR;
    std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> iterator(
        ubrk_open(UBRK_CHARACTER, "root", reinterpret_cast<const UChar*>(wide.data()), int32_t(wide.size()), &error),
        ubrk_close);
    if (U_FAILURE(error) || !iterator) {
        throw std::runtime_error("ICU character segmentation failed");
    }
    std::vector<Page> result(1);
    size_t column = 0;
    int32_t start = ubrk_first(iterator.get());
    for (int32_t end = ubrk_next(iterator.get()); end != UBRK_DONE; start = end, end = ubrk_next(iterator.get())) {
        const auto glyph = wide.substr(start, end - start);
        if (glyph == L"\n") {
            result.emplace_back();
            column = 0;
        } else {
            if (column == kSlots) {
                result.emplace_back();
                column = 0;
            }
            result.back()[column++] = glyph;
        }
    }
    return result;
}

Model::Model() {
    text(pages("CyberDesktop"), true);
}

void Model::text(std::vector<Page> value, bool animate) {
    if (value.empty()) {
        throw std::invalid_argument("Sign requires a text page");
    }
    pages_ = std::move(value);
    page_ = 0;
    if (!animate) {
        finish();
    } else if (active_) {
        pendingText_ = true;
        queued_ = false;
    } else {
        begin(pages_.front(), false);
    }
}

void Model::begin(const Page& target, bool force) {
    before_ = after_;
    after_ = target;
    elapsed_ = 0;
    size_t order = 0;
    for (size_t i = 0; i < kSlots; ++i) {
        changed_[i] = force || before_[i] != after_[i];
        starts_[i] = changed_[i] ? kWarmup + order++ * kStagger : 0;
    }
    duration_ = order ? kWarmup + (order - 1) * kStagger + kFlap + kCoast : 0;
    active_ = order != 0;
}

void Model::request() {
    if (active_) {
        queued_ = true;
        return;
    }
    page_ = (page_ + 1) % pages_.size();
    begin(pages_[page_], true);
}

void Model::finish() {
    baseAngle_ = std::fmod(angle(), 10 * std::numbers::pi);
    active_ = queued_ = pendingText_ = false;
    before_ = after_ = pages_[page_];
    elapsed_ = duration_ = 0;
    changed_.fill(false);
}

bool Model::advance(double delta) {
    if (!active_ || !std::isfinite(delta) || delta <= 0) {
        return false;
    }
    elapsed_ = std::min(duration_, elapsed_ + std::min(delta, .1));
    if (elapsed_ + 1e-9 >= duration_) {
        baseAngle_ = std::fmod(angle(), 10 * std::numbers::pi);
        active_ = false;
        elapsed_ = duration_ = 0;
        before_ = after_;
        changed_.fill(false);
        if (std::exchange(pendingText_, false)) {
            queued_ = false;
            begin(pages_.front(), false);
        } else if (std::exchange(queued_, false)) {
            request();
        }
    }
    return true;
}

bool Model::active() const {
    return active_;
}

bool Model::queued() const {
    return queued_ || pendingText_;
}

double Model::progress(size_t index) const {
    return active_ && changed_.at(index) ? std::clamp((elapsed_ - starts_[index]) / kFlap, 0., 1.) : 1;
}

double Model::angle() const {
    return baseAngle_ + (active_ ? travel(elapsed_, duration_) : 0);
}

double Model::power() const {
    return active_ ? std::min({1., elapsed_ / kWarmup, (duration_ - elapsed_) / kCoast}) : 0;
}

double Model::duration() const {
    return duration_;
}

size_t Model::page() const {
    return page_;
}

size_t Model::pageCount() const {
    return pages_.size();
}

const Page& Model::before() const {
    return before_;
}

const Page& Model::after() const {
    return after_;
}
}  // namespace mechanicalsign
