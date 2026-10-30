#include "plugins/vampire-alice/controllers/toys/Toys.h"

namespace alice_toys {
Model::Model(Kind kind, bool compact, bool mechanical)
    : toys::Model(kind, compact, mechanical, {.illustrated = true}) {}
}  // namespace alice_toys
