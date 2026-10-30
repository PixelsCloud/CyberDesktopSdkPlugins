#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "plugins/koi-suite/Pond.h"

namespace koiplugin {
double Vec::length() const {
    return std::hypot(x, y);
}

Vec direction(double angle) {
    return {std::cos(angle), std::sin(angle)};
}

double angleDifference(double angle, double anchor) {
    return std::remainder(angle - anchor, 2 * kPi);
}

double Blessing::opacity() const {
    if (word < 0) {
        return 0;
    }
    const double fade = std::min(std::clamp(age / .8, 0.0, 1.0), std::clamp((kBlessingDuration - age) / 1.3, 0.0, 1.0));
    return fade * fade * (3 - 2 * fade);
}

float blessingFontSize(double width, double height, size_t characters) {
    const double size = std::clamp(std::sqrt(width * height) * .12, 20.0, 28.0) * (characters == 1   ? 1.35
                                                                                   : characters == 4 ? .86
                                                                                                     : 1);
    const double available = (width - 32) / std::max(size_t(1), characters) - 2;
    return float(std::min(size, std::max(1.0, available)));
}

void Fish::resolve(Vec head) {
    const Vec velocity = head - joints[0];
    if (velocity.length() > .000001) {
        angles[0] = std::atan2(velocity.y, velocity.x);
    }
    joints[0] = head;

    for (size_t i = 1; i < joints.size(); ++i) {
        const auto relative = joints[i - 1] - joints[i];
        const double proposed = std::atan2(relative.y, relative.x);
        angles[i] = angles[i - 1] + std::clamp(angleDifference(proposed, angles[i - 1]), -kPi / 8, kPi / 8);
        joints[i] = joints[i - 1] - direction(angles[i]) * link;
    }
}

Vec Fish::edge(int joint, double offset, double extra) const {
    return joints[joint] + direction(angles[joint] + offset) * ((widths[joint] + extra) * link / 64);
}

FinPose finPose(const Fish& fish, int joint, int side) {
    const bool front = joint == 3;
    // Exact Fish.pde attachment, previous-vertebra orientation and ellipse sizes.
    // The body masks each ellipse's inner half. Opposite +/-PI/4 signs are
    // essential; the relative spine angles provide the reference's articulation.
    return {fish.edge(joint, side * (front ? kPi / 3 : kPi / 2)), fish.angles[joint - 1] - side * kPi / 4,
            (front ? 80 : 48) * fish.link / 64, (front ? 32 : 16) * fish.link / 64};
}

std::vector<Vec> bodyOutline(const Fish& fish) {
    std::vector<Vec> edges;
    edges.reserve(24);
    for (int i = 0; i < 10; ++i) {
        edges.push_back(fish.edge(i, kPi / 2));
    }
    edges.push_back(fish.edge(9, kPi));
    for (int i = 9; i >= 0; --i) {
        edges.push_back(fish.edge(i, -kPi / 2));
    }
    edges.push_back(fish.edge(0, -kPi / 6));
    edges.push_back(fish.edge(0, 0, 4));
    edges.push_back(fish.edge(0, kPi / 6));
    return edges;
}

std::array<Vec, 8> tailOutline(const Fish& fish) {
    // A resting tilt exposes the upstream tapered fin even during straight swimming.
    // Curvature adds roll without cancelling this projection at a particular phase.
    const double idleTilt = 1.30 + .20 * std::sin(fish.finPhase);
    const double bend =
        angleDifference(fish.angles[0], fish.angles[6]) + angleDifference(fish.angles[6], fish.angles[11]);
    const double projection = std::hypot(bend, idleTilt);
    std::array<Vec, 8> points;
    for (int i = 8; i < 12; ++i) {
        const double width = 1.5 * projection * (i - 8) * (i - 8) * fish.link / 64;
        const double lean = std::clamp(bend * 2, -4.0, 4.0) * (i - 8) / 3 * fish.link / 64;
        points[i - 8] = fish.joints[i] + direction(fish.angles[i] - kPi / 2) * (width + lean);
    }
    // Keep the upstream continuous edge instead of adding a fork or fan.
    for (int i = 11; i >= 8; --i) {
        const double width = std::min(projection * 6, 13.0) * fish.link / 64;
        const double lean = std::clamp(bend * 2, -4.0, 4.0) * (i - 8) / 3 * fish.link / 64;
        points[4 + 11 - i] = fish.joints[i] + direction(fish.angles[i] + kPi / 2) * (width - lean);
    }
    return points;
}

CdRect fishBounds(const Fish& fish) {
    double left = std::numeric_limits<double>::max(), top = left;
    double right = -left, bottom = -left;
    const auto include = [&](Vec center, double radius) {
        left = std::min(left, center.x - radius);
        right = std::max(right, center.x + radius);
        top = std::min(top, center.y - radius);
        bottom = std::max(bottom, center.y + radius);
    };
    for (size_t i = 0; i < fish.joints.size(); ++i) {
        // Also covers spline overshoot, dorsal/caudal fins, shadows and antialiasing.
        include(fish.joints[i], fish.link * 2.5 + 8);
    }
    for (int joint : {3, 7}) {
        for (int side : {-1, 1}) {
            const auto pose = finPose(fish, joint, side);
            include(pose.center, pose.radiusX + 8);
        }
    }
    for (const auto point : tailOutline(fish)) {
        include(point, fish.link * .5 + 8);
    }
    return {left, top, right - left, bottom - top};
}

std::vector<LilyPad> lilyPads(double width, double height) {
    const double unit = std::min(std::sqrt(width * height) * .092, std::min(width, height) * .19);
    std::vector<LilyPad> pads{{{width * .13, height * .16}, unit * 1.18, -.4},
                              {{width * .27, height * .11}, unit * .83, 1.5},
                              {{width * .09, height * .36}, unit * .64, 2.1},
                              {{width * .86, height * .79}, unit * 1.08, .7},
                              {{width * .95, height * .66}, unit * .69, 3.2}};
    if (height > width * .7) {
        pads.push_back({{width * .83, height * .91}, unit * .82, 2.7});
        pads.push_back({{width * .08, height * .22}, unit * .65, -.9});
    }
    for (int i = 0; i < 2; ++i) {
        auto smallest = std::min_element(pads.begin(), pads.end(), [](const auto& a, const auto& b) {
            return (a.notched ? std::numeric_limits<double>::max() : a.radius) <
                   (b.notched ? std::numeric_limits<double>::max() : b.radius);
        });
        smallest->notched = true;
    }
    return pads;
}

double fishLengthForGrid(double width, double height, int rows, int columns) {
    // Normalize every footprint to the same 2x4 canvas at the current cell size.
    // The host removes four logical pixels per side from the content surface.
    const double referenceWidth = std::max(24.0, (width + 8) * 4 / std::max(1, columns) - 8);
    const double referenceHeight = std::max(24.0, (height + 8) * 2 / std::max(1, rows) - 8);
    return std::min(std::max(std::sqrt(referenceWidth * referenceHeight) * .26, 10.0),
                    std::min(referenceWidth, referenceHeight) * .45);
}

double Scene::random(double low, double high) {
    return std::uniform_real_distribution<double>(low, high)(random_);
}

void Scene::setBoundary(std::function<bool(Vec, double)> boundary) {
    if (!fish_.empty()) {
        throw std::logic_error("Set the pond boundary before configuring fish");
    }
    boundary_ = std::move(boundary);
}

Vec Scene::destination(double margin) {
    margin = std::min(margin, std::min(width_, height_) * .28);
    for (unsigned attempt = 0; attempt < 1024; ++attempt) {
        const Vec point{random(margin, width_ - margin), random(margin, height_ - margin)};
        if (!boundary_ || boundary_(point, std::max(0., margin))) {
            return point;
        }
    }
    throw std::runtime_error("No destination fits the closed pond boundary");
}

bool Scene::fits(const Fish& fish) const {
    // Conservative disks cover the spine, both fins, the flared tail and shadow.
    // Check the entire animal, not just its head or the water bounding rectangle.
    return !boundary_ || std::all_of(fish.joints.begin(), fish.joints.end(), [&](Vec joint) {
        return boundary_(joint, fish.link * 2.6 + 1);
    });
}

void Scene::moveWithinBoundary(Fish& fish, Vec next, double turn) {
    auto candidate = fish;
    candidate.resolve(next);
    if (fish.boundaryRecovery <= 0 && fits(candidate)) {
        fish = candidate;
        return;
    }

    if (fish.boundaryRecovery <= 0) {
        // Keep one escape destination and turn direction. Replacing the waypoint
        // every blocked frame made opposite turns cancel forever at a bank corner.
        fish.target = destination(fish.link * 4);
        fish.boundaryRecovery = 1.5;
        const auto desired = fish.target - fish.joints[0];
        fish.boundaryTurn = angleDifference(std::atan2(desired.y, desired.x), fish.angles[0]) < 0 ? -1 : 1;
        fish.age = 0;
    }

    // Make room for the complete articulated pose before turning. Translation is
    // limited to one swim step and every candidate retains fin/tail clearance.
    const double step = std::max(.01, (next - fish.joints[0]).length());
    const Vec toward = fish.target - fish.joints[5];
    const double heading = std::atan2(toward.y, toward.x);
    bool translated = false;
    for (double scale : {1., .5, .25}) {
        for (double angle : {0., kPi / 4, -kPi / 4, kPi / 2, -kPi / 2, kPi * .75, -kPi * .75, kPi}) {
            candidate = fish;
            const auto offset = direction(heading + angle) * (step * scale);
            for (auto& joint : candidate.joints) {
                joint = joint + offset;
            }
            if (fits(candidate)) {
                fish = candidate;
                translated = true;
                break;
            }
        }
        if (translated) {
            break;
        }
    }
    const auto pivot = fish.joints[5];
    const double rotation = fish.boundaryTurn * turn;
    candidate = fish;
    for (size_t i = 0; i < fish.joints.size(); ++i) {
        const auto relative = fish.joints[i] - pivot;
        candidate.joints[i] = pivot + Vec{relative.x * std::cos(rotation) - relative.y * std::sin(rotation),
                                          relative.x * std::sin(rotation) + relative.y * std::cos(rotation)};
        candidate.angles[i] += rotation;
    }
    if (fits(candidate)) {
        fish = candidate;
    }
    fish.heading = fish.angles[0];
}

void Scene::configure(double width, double height, int count, uint32_t seed, double fishLength) {
    width = std::max(24.0, width);
    height = std::max(24.0, height);
    count = std::clamp(count, 1, 12);
    if (!std::isfinite(fishLength) || fishLength <= 0) {
        fishLength = fishLengthForGrid(376, 184, 2, 4);
    }
    if (width == width_ && height == height_ && count == int(fish_.size()) && seed == seed_ &&
        fishLength == fishLength_) {
        return;
    }

    width_ = width;
    height_ = height;
    seed_ = seed;
    fishLength_ = fishLength;
    random_.seed(seed);
    time_ = 0;
    accumulator_ = 0;
    fish_.clear();
    ripples_.clear();
    hasPointer_ = false;
    scareUntil_ = 0;
    reentries_ = 0;
    lastExitBounds_ = {};
    const double length = fishLength_;
    for (int i = 0; i < count; ++i) {
        Fish fish;
        fish.link = length * random(.80, 1.16) / 11;
        const double margin = std::min(fish.link * 3, std::min(width, height) * .18);
        for (int attempt = 0; attempt < 24; ++attempt) {
            fish.joints[0] = destination(margin);
            const bool spaced = std::all_of(fish_.begin(), fish_.end(), [&](const auto& other) {
                return (fish.joints[0] - other.joints[0]).length() >= (fish.link + other.link) * 4;
            });
            if (spaced) {
                break;
            }
        }
        fish.heading = random(0, 2 * kPi);
        fish.phase = random(0, 2 * kPi);
        fish.finPhase = fish.phase;
        fish.depth = random(.1, .9);
        fish.pattern = i % 5;
        fish.age = random(0, 3);
        fish.target = destination(-fish.link * 16);
        for (size_t j = 0; j < fish.joints.size(); ++j) {
            fish.angles[j] = fish.heading;
            fish.joints[j] = fish.joints[0] - direction(fish.heading) * (fish.link * j);
        }
        if (boundary_) {
            for (unsigned attempt = 0; !fits(fish); ++attempt) {
                if (attempt == 1024) {
                    throw std::runtime_error("Fish does not fit the closed pond boundary");
                }
                fish.heading = random(0, 2 * kPi);
                const auto head = destination(margin);
                for (size_t j = 0; j < fish.joints.size(); ++j) {
                    fish.angles[j] = fish.heading;
                    fish.joints[j] = head - direction(fish.heading) * (fish.link * j);
                }
            }
            fish.target = destination(fish.link * 4);
        }
        fish_.push_back(fish);
    }
    blessingRandom_.seed(seed ^ 0xA17C93D5u);
    for (size_t i = 0; i < blessingOrder_.size(); ++i) {
        blessingOrder_[i] = int(i);
    }
    blessingCursor_ = blessingOrder_.size();
    lastBlessing_ = -1;
    blessing_ = {};
    nextBlessingAt_ = 0;
    nextBlessing();
    // The current preview cadence starts with a readable word immediately.
    blessing_.age = .8;
}

void Scene::nextBlessing() {
    if (std::min(width_, height_) < 70) {
        return;
    }
    if (blessingCursor_ == blessingOrder_.size()) {
        std::shuffle(blessingOrder_.begin(), blessingOrder_.end(), blessingRandom_);
        if (blessingOrder_.front() == lastBlessing_) {
            std::swap(blessingOrder_[0], blessingOrder_[1]);
        }
        blessingCursor_ = 0;
    }
    blessing_.word = blessingOrder_[blessingCursor_++];
    lastBlessing_ = blessing_.word;
    blessing_.age = 0;
    const auto word = kBlessingWords[blessing_.word];
    const double font = blessingFontSize(width_, height_, word.size());
    const double halfWidth = (font + 2) * word.size() * .5;
    const auto pads = lilyPads(width_, height_);
    double bestScore = -std::numeric_limits<double>::max();
    for (int attempt = 0; attempt < 12; ++attempt) {
        const Vec center{
            std::uniform_real_distribution<double>(halfWidth + 12, width_ - halfWidth - 12)(blessingRandom_),
            std::uniform_real_distribution<double>(height_ * .36, height_ * .68)(blessingRandom_)};
        double score = std::numeric_limits<double>::max();
        for (const auto& pad : pads) {
            const double dx = std::max(0.0, std::abs(center.x - pad.center.x) - halfWidth);
            const double dy = std::max(0.0, std::abs(center.y - pad.center.y) - font * .7);
            score = std::min(score, std::hypot(dx, dy) - pad.radius);
        }
        for (const auto& fish : fish_) {
            for (int joint : {0, 3, 6, 9}) {
                const double dx = std::max(0.0, std::abs(center.x - fish.joints[joint].x) - halfWidth);
                const double dy = std::max(0.0, std::abs(center.y - fish.joints[joint].y) - font * .7);
                score = std::min(score, std::hypot(dx, dy) - fish.link);
            }
        }
        if (score > bestScore) {
            bestScore = score;
            blessing_.center = center;
        }
    }
}

void Scene::ripple(Vec center) {
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || center.x < 0 || center.y < 0 || center.x > width_ ||
        center.y > height_) {
        return;
    }
    if (ripples_.size() == 8) {
        ripples_.erase(ripples_.begin());
    }
    ripples_.push_back({center, 0});
}

void Scene::setMouseBehavior(MouseBehavior behavior) {
    if (mouseBehavior_ == behavior) {
        return;
    }
    mouseBehavior_ = behavior;
    hasPointer_ = false;
    scareUntil_ = 0;
    for (auto& fish : fish_) {
        fish.alarm = 0;
    }
}

void Scene::pointer(Vec center) {
    if (mouseBehavior_ != MouseBehavior::Follow || !std::isfinite(center.x) || !std::isfinite(center.y) ||
        center.x < 0 || center.y < 0 || center.x > width_ || center.y > height_) {
        return;
    }
    pointer_ = center;
    hasPointer_ = true;
}

void Scene::clearPointer() {
    hasPointer_ = false;
}

void Scene::click(Vec center) {
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || center.x < 0 || center.y < 0 || center.x > width_ ||
        center.y > height_) {
        return;
    }
    ripple(center);
    if (mouseBehavior_ == MouseBehavior::Follow) {
        pointer(center);
    } else {
        threat_ = center;
        scareUntil_ = time_ + 2.6;
    }
}

void Scene::advance(double seconds, double speed) {
    if (!std::isfinite(seconds) || !std::isfinite(speed) || seconds <= 0 || fish_.empty()) {
        return;
    }
    // No catch-up after a blocked UI, resume or sleep. Work per tick stays bounded.
    accumulator_ += std::min(seconds, .15);
    // Apply the livelier baseline to existing instances as well as new ponds.
    speed = std::clamp(speed, .3, 2.0) * 1.5;
    constexpr double timestep = 1.0 / 60;
    while (accumulator_ + .000000001 >= timestep) {
        step(timestep, speed);
        accumulator_ -= timestep;
    }
}

void Scene::reenter(Fish& fish, int exitEdge) {
    lastExitBounds_ = fishBounds(fish);
    const double margin = fish.link * 3 + 9;
    // Edges: left, right, top, bottom. Re-enter at a random point on the opposite
    // edge, entirely outside the frame, already facing inward with a gentle angle.
    fish.entryEdge = exitEdge ^ 1;
    Vec head;
    double heading = 0;
    if (fish.entryEdge < 2) {
        head = {fish.entryEdge == 0 ? -margin : width_ + margin, random(height_ * .12, height_ * .88)};
        heading = fish.entryEdge == 0 ? 0 : kPi;
    } else {
        head = {random(width_ * .12, width_ * .88), fish.entryEdge == 2 ? -margin : height_ + margin};
        heading = fish.entryEdge == 2 ? kPi / 2 : -kPi / 2;
    }
    fish.heading = heading + random(-.25, .25);
    fish.travelHeading = fish.heading;
    for (size_t joint = 0; joint < fish.joints.size(); ++joint) {
        fish.joints[joint] = head - direction(fish.heading) * (joint * fish.link);
        fish.angles[joint] = fish.heading;
    }
    fish.target = destination(-fish.link * 16);
    fish.age = 0;
    fish.alarm = 0;
    fish.turnRate = 0;
    fish.entering = true;
    fish.leaving = false;
    ++reentries_;
}

void Scene::step(double seconds, double speed) {
    time_ += seconds;
    if (blessing_.word >= 0) {
        blessing_.age += seconds;
        if (blessing_.age >= kBlessingDuration) {
            blessing_.word = -1;
            nextBlessingAt_ = time_ + std::uniform_real_distribution<double>(1.0, 2.0)(blessingRandom_);
        }
    } else if (time_ >= nextBlessingAt_) {
        nextBlessing();
    }
    for (auto& ripple : ripples_) {
        ripple.age += seconds;
    }
    std::erase_if(ripples_, [](const auto& value) {
        return value.age > 2.6;
    });

    for (auto& fish : fish_) {
        fish.age += seconds * speed;
        fish.boundaryRecovery = std::max(0., fish.boundaryRecovery - seconds * speed);
        const auto head = fish.joints[0];
        const bool headOutside = head.x < 0 || head.y < 0 || head.x > width_ || head.y > height_;
        if (!boundary_ && !fish.entering && !fish.leaving && headOutside) {
            fish.leaving = true;
            const Vec outward{head.x < 0        ? -1.0
                              : head.x > width_ ? 1.0
                                                : 0.0,
                              head.y < 0         ? -1.0
                              : head.y > height_ ? 1.0
                                                 : 0.0};
            const Vec escape = direction(fish.heading) + outward * .6;
            fish.travelHeading = std::atan2(escape.y, escape.x);
        }
        if (fish.entering) {
            const double progress = fish.entryEdge == 0   ? head.x
                                    : fish.entryEdge == 1 ? width_ - head.x
                                    : fish.entryEdge == 2 ? head.y
                                                          : height_ - head.y;
            const double span = fish.entryEdge < 2 ? width_ : height_;
            if (progress > std::min(fish.link * 12, span * .60)) {
                fish.entering = false;
            }
        }
        const bool crossing = fish.entering || fish.leaving;
        if (!crossing && fish.boundaryRecovery <= 0 &&
            ((fish.target - head).length() < fish.link * 5 || fish.age > 12)) {
            fish.target = destination(boundary_ ? fish.link * 4 : -fish.link * 16);
            fish.age = 0;
        }
        const bool following =
            !crossing && fish.boundaryRecovery <= 0 && mouseBehavior_ == MouseBehavior::Follow && hasPointer_;
        Vec desired = (following ? pointer_ : fish.target) - head;
        const double targetDistance = desired.length();
        desired = desired * (1 / std::max(.001, desired.length()));
        const double remaining = crossing || fish.boundaryRecovery > 0 ? 0 : std::max(0.0, (scareUntil_ - time_) / 2.6);
        fish.alarm = remaining * std::clamp(1 - (head - threat_).length() / std::hypot(width_, height_), .20, 1.0);
        if (remaining > 0) {
            Vec away = head - threat_;
            if (away.length() < .01) {
                away = direction(fish.heading + kPi / 2);
            }
            desired = desired * (1 - remaining) + away * (remaining * 4 / away.length());
        }
        for (const auto& other : fish_) {
            if (&other == &fish) {
                continue;
            }
            for (int joint : {0, 4, 8}) {
                const Vec away = head - other.joints[joint];
                const double distance = away.length();
                const double clearance = (fish.link + other.link) * 3.5;
                if (distance > .01 && distance < clearance) {
                    desired = desired + away * ((clearance - distance) * 1.8 / (clearance * distance));
                }
            }
        }
        if (boundary_) {
            // Anticipate the bank before contact, including the concave waist of the
            // wide pond. Direct2D supplies the actual authored geometry predicate.
            const double lookahead = fish.link * 6;
            for (unsigned i = 0; i < 8; ++i) {
                const auto toward = direction(i * kPi / 4);
                if (!boundary_(head + toward * lookahead, fish.link * 2.6 + 1)) {
                    desired = desired - toward * 1.4;
                }
            }
        }

        const double activity = 1 + fish.alarm * 2.2;
        const double wave = std::sin(fish.finPhase) * .40;
        const double wanted = crossing ? fish.travelHeading + wave * .4 : std::atan2(desired.y, desired.x) + wave;
        const double turn = seconds * speed * (1.15 + fish.alarm * 3.2);
        const double rotation = std::clamp(angleDifference(wanted, fish.heading), -turn, turn);
        fish.heading += rotation;
        fish.turnRate = std::lerp(fish.turnRate, rotation / seconds, std::min(1.0, seconds * 9));
        const double approach = following ? std::clamp(targetDistance / (fish.link * 7), .22, 1.0) : 1;
        fish.finPhase += seconds * speed * activity * (3.5 + std::abs(fish.turnRate) * .65);
        const Vec next = head + direction(fish.heading) * (fish.link * 4.6 * seconds * speed * activity * approach);
        if (boundary_) {
            moveWithinBoundary(fish, next, turn * 1.5);
        } else {
            fish.resolve(next);
        }
        if (!boundary_ && !fish.entering) {
            const auto bounds = fishBounds(fish);
            if (bounds.x + bounds.width < 0) {
                reenter(fish, 0);
            } else if (bounds.x > width_) {
                reenter(fish, 1);
            } else if (bounds.y + bounds.height < 0) {
                reenter(fish, 2);
            } else if (bounds.y > height_) {
                reenter(fish, 3);
            }
        }
    }
}
}  // namespace koiplugin
