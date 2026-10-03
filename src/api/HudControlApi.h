#pragma once
#include <stdint.h>
/* Locally authored optional ABI. Copied POD only, no game/ImGui/STL pointers.
 * Version 1, Win64 __cdecl. Retain enabled loader-owned LHolo module per call.
 * All functions catch exceptions. Zero return means unavailable/rejected.
 * Context bit 1 = gameplay, 2 = GUI/navigation. Modifier bit 8 = any.
 * ABI slots follow the owner's persistent IDs and count; consumers must not
 * assume the parent's new navigation slots have already been implemented. */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct HudControlHotkeyV1 {
    uint32_t size,id,key,modifiers,contexts;
    char label[128];
} HudControlHotkeyV1;
typedef struct HudControlLayoutV1 {
    uint32_t size,id,custom,anchor;
    float x,y,scale;
} HudControlLayoutV1;
typedef struct HudControlApiV1 {
    uint32_t size,version;
    uint32_t (__cdecl *hotkey_count)(void);
    uint32_t (__cdecl *read_hotkey)(uint32_t,HudControlHotkeyV1*);
    uint32_t (__cdecl *write_hotkey)(uint32_t,uint32_t,uint32_t,uint32_t);
    /* write operation 0 set, 1 clear, 2 reset; setting requests persistence. */
    uint32_t (__cdecl *read_layout)(uint32_t,HudControlLayoutV1*);
    uint32_t (__cdecl *write_layout)(HudControlLayoutV1 const*,uint32_t);
    /* layout 0 schematic, 1 materials; operation 1 resets legacy corner. */
} HudControlApiV1;
typedef uint32_t (__cdecl *HudControlGetApiV1)(uint32_t,HudControlApiV1*);
#ifdef __cplusplus
}
#endif
