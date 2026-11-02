#include "sdk/services/Background.h"

_Static_assert(sizeof(CdBackgroundApi) == 56, "background service table ABI");
_Static_assert(sizeof(CdBackgroundCommand) == 48, "background command ABI");
_Static_assert(sizeof(CdBackgroundWork) == 72, "background work ABI");
_Static_assert(sizeof(CdBackgroundOutput) == 32, "background output ABI");
_Static_assert(sizeof(CdBackgroundTaskDesc) == 48, "background task ABI");
_Static_assert(sizeof(CdBackgroundControl) == 32, "background control ABI");
_Static_assert(sizeof(CdBackgroundSnapshot) == 64, "background read lease ABI");
_Static_assert(sizeof(CdBackgroundStats) == 192, "independent background statistics ABI");
_Static_assert(offsetof(CdBackgroundOutput, slot) == 28, "output geometry slot ABI");
_Static_assert(offsetof(CdBackgroundSnapshot, slot) == 12, "read geometry slot ABI");

int background_c_abi(void) {
    return CD_BACKGROUND_COMMANDS * CD_BACKGROUND_COMMAND_BYTES == 65536u;
}
