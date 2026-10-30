#include "plugins/mecha-desk/controllers/toys/Toys.h"

namespace aeris_toys {
Model::Model(Kind kind, bool compact, bool mechanical)
    : toys::Model(kind, compact, mechanical, {.illustrated = true, .wallInset = .015, .dragMargin = {.3, .4}}) {}
}  // namespace aeris_toys
