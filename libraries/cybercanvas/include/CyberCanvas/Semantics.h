#pragma once

#include <string>
#include <vector>

#include "CyberCanvas/Types.h"

namespace cybercanvas {
enum class SemanticRole { Group, Text, Image, Button, Toggle, Slider, List, ListItem };
enum class Key { Tab, Left, Right, Up, Down, Enter, Space, Escape, Home, End };

struct KeyEvent {
    Key key = Key::Tab;
    bool shift = false;
};

// IDs are stable scene handles, not business keys. Hidden nodes are omitted;
// disabled nodes remain readable but cannot acquire focus or perform actions.
struct SemanticNode {
    uint32_t id = 0, parent = 0;
    SemanticRole role = SemanticRole::Group;
    Rect bounds;
    std::wstring name, value;
    double number = 0, minimum = 0, maximum = 1, step = 0;
    bool enabled = true, focusable = false, focused = false, selected = false, checked = false, busy = false,
         error = false;
};
}  // namespace cybercanvas
