#include "plugins/mecha-desk/controllers/toys/Toys.h"

namespace aeris_toys {
Model::Model(const CdCreateContext& context, Kind kind, bool compact, bool mechanical)
    : toys::Model(context, kind, compact, mechanical,
                  {.illustrated = true, .wallInset = .015, .dragMargin = {.3, .4}}) {}

Model::Model(Kind kind, bool compact, bool mechanical)
    : toys::Model(kind, compact, mechanical, {.illustrated = true, .wallInset = .015, .dragMargin = {.3, .4}}) {}
}  // namespace aeris_toys
