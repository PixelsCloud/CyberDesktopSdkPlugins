#include "plugins/mechanical-desk/controllers/toys/Toys.h"

namespace mechanicaldesk_toys {
Model::Model(Kind kind, bool compact, bool mechanical)
    : toys::Model(kind, compact, mechanical, {.illustrated = true, .separateEnclosure = true}) {}
}  // namespace mechanicaldesk_toys
