#include <stddef.h>
#include <stdio.h>

#pragma pack(push, 1)
#include <sdk/services/CharacterChat.h>
#include <sdk/services/CharacterImages.h>
#include <sdk/services/Composition.h>
#include <sdk/services/Live2DActions.h>
#include <sdk/services/TextInput.h>

struct CallerPacking {
    char byte;
    uint64_t value;
};

#pragma pack(pop)

_Static_assert(sizeof(struct CallerPacking) == 9, "service headers preserve caller packing");
_Static_assert(sizeof(CdCharacterChatApi) == 24 && offsetof(CdCharacterChatApi, connect) == 8, "chat ABI");
_Static_assert(sizeof(CdTextInputApi) == 40 && offsetof(CdTextInputApi, configure) == 16, "native text ABI");
_Static_assert(sizeof(CdCharacterImage) == 48 && offsetof(CdCharacterImage, bgra) == 32, "image ABI");
_Static_assert(sizeof(CdCharacterImagesApi) == 16 && sizeof(CdCharacterVersionedImagesApi) == 16, "image APIs");
_Static_assert(sizeof(CdCompositionApi) == 32 && offsetof(CdCompositionApi, invalidate) == 24, "composition ABI");
_Static_assert(sizeof(CdLive2DActionsApi) == 16, "Live2D actions ABI");

int main(void) {
    puts("C11 service ABI sizes, offsets and caller packing: PASS");
    return 0;
}
