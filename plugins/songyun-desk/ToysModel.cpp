#include "plugins/songyun-desk/controllers/toys/Toys.h"

namespace songyun_toys {
Model::Model(Kind kind, bool compact, bool mechanical)
    : toys::Model(kind, compact, mechanical, {.illustrated = true}) {}
}  // namespace songyun_toys
