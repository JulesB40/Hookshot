/* GPL-3.0-or-later. POSIX guest address-space ownership; no file access. */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct HosGuestArena { void* base; size_t size, page_size; } HosGuestArena;
#ifdef __cplusplus
extern "C" {
#endif
int hos_guest_reserve(HosGuestArena* arena, uintptr_t requested, size_t bytes);
int hos_guest_commit(HosGuestArena* arena, size_t offset, size_t bytes);
int hos_guest_decommit(HosGuestArena* arena, size_t offset, size_t bytes);
int hos_guest_release(HosGuestArena* arena);
#ifdef __cplusplus
}
#endif
