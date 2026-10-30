#include "plugins/redline-desk/controllers/toys/Toys.h"

namespace redline_toys {
Model::Model(Kind kind, bool compact, bool mechanical)
    : toys::Model(kind, compact, mechanical, {.illustrated = true}) {}
}  // namespace redline_toys
