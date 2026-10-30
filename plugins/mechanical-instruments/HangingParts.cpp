#include "plugins/mechanical-instruments/HangingParts.h"

namespace mechanicalart {
physicsmaterials::Registration hangingPart(std::string_view name) {
    if (name == "spring-beam") {
        return {{2172, 724}, {44, 273, 2084, 177}};
    }
    if (name == "spring-card-short") {
        return {{940, 1672}, {66, 84, 807, 1493}};
    }
    if (name == "spring-card-tall") {
        return {{732, 2149}, {45, 200, 641, 1795}};
    }
    if (name == "chime-beam") {
        return {{2172, 724}, {69, 178, 2034, 376}};
    }
    if (name == "chime-moon") {
        return {{1254, 1254}, {199, 23, 901, 1203}};
    }
    if (name == "chime-bell") {
        return {{1254, 1254}, {229, 95, 790, 1030}};
    }
    if (name == "chime-star") {
        return {{1254, 1254}, {133, 89, 988, 999}};
    }
    if (name == "chime-leaf") {
        return {{866, 1817}, {83, 130, 700, 1543}};
    }
    if (name == "chime-tag") {
        return {{948, 1659}, {231, 89, 484, 1456}};
    }
    if (name == "air-2x4") {
        return {{1536, 1024}, {77, 35, 1406, 931}};
    }
    if (name == "air-1x4") {
        return {{2041, 770}, {42, 14, 1957, 721}};
    }
    if (name == "air-1x2") {
        return {{1618, 972}, {62, 122, 1498, 734}};
    }
    return {{1, 1}, {0, 0, 1, 1}};
}
}  // namespace mechanicalart
