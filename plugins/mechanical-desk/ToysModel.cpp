#include "plugins/mechanical-desk/controllers/toys/Toys.h"

namespace mechanicaldesk_toys {
Model::Model(const CdCreateContext& context, Kind kind, bool compact, bool mechanical)
    : toys::Model(context, kind, compact, mechanical, {.illustrated = true, .separateEnclosure = true}) {}

Model::Model(Kind kind, bool compact, bool mechanical)
    : toys::Model(kind, compact, mechanical, {.illustrated = true, .separateEnclosure = true}) {}
}  // namespace mechanicaldesk_toys
