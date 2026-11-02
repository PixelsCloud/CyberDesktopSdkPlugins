#include "plugins/vampire-alice/controllers/toys/Toys.h"

namespace alice_toys {
Model::Model(const CdCreateContext& context, Kind kind, bool compact, bool mechanical)
    : toys::Model(context, kind, compact, mechanical, {.illustrated = true}) {}

Model::Model(Kind kind, bool compact, bool mechanical)
    : toys::Model(kind, compact, mechanical, {.illustrated = true}) {}
}  // namespace alice_toys
