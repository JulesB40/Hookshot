/* GPL-3.0-or-later. Never uses MAP_FIXED, which could overwrite another mapping. */
#define _GNU_SOURCE
#include "guest_arena.h"
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int hos_guest_reserve(HosGuestArena* a, uintptr_t requested, size_t bytes) {
    if (!a || a->base) { errno=EINVAL; return 0; }
    const long page=sysconf(_SC_PAGESIZE);
    if (page<=0 || !bytes || bytes%(size_t)page || requested%(size_t)page || bytes>UINTPTR_MAX-requested) {
        errno=EINVAL; return 0;
    }
    int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags|=MAP_NORESERVE;
#endif
    void* p=MAP_FAILED;
#ifdef MAP_FIXED_NOREPLACE
    if (requested) p=mmap((void*)requested,bytes,PROT_NONE,flags|MAP_FIXED_NOREPLACE,-1,0);
    // On older Android kernels this flag may be unavailable or ignored. A plain
    // hint is safe; a different returned allocation is ours to unmap and refuse.
    if (requested && p==MAP_FAILED && errno!=EINVAL && errno!=EOPNOTSUPP) return 0;
#endif
    if (p==MAP_FAILED) p=mmap((void*)requested,bytes,PROT_NONE,flags,-1,0);
    if (p==MAP_FAILED) return 0;
    if (requested && p!=(void*)requested) { munmap(p,bytes); errno=EEXIST; return 0; }
    a->base=p; a->size=bytes; a->page_size=(size_t)page;
    return 1;
}
static int valid(HosGuestArena* a,size_t offset,size_t bytes) {
    if (!a || !a->base || !bytes || offset>a->size || bytes>a->size-offset ||
        offset%a->page_size || bytes%a->page_size) { errno=EINVAL; return 0; }
    return 1;
}
int hos_guest_reserve_relocated(HosGuestArena* a,uintptr_t preferred,size_t bytes) {
    if (!a || a->base) { errno=EINVAL; return 0; }
    if (preferred && hos_guest_reserve(a,preferred,bytes)) return 1;
    return hos_guest_reserve(a,0,bytes);
}
int hos_guest_commit(HosGuestArena* a,size_t offset,size_t bytes) {
    if (!valid(a,offset,bytes)) return 0;
    return mprotect((char*)a->base+offset,bytes,PROT_READ|PROT_WRITE)==0;
}
int hos_guest_decommit(HosGuestArena* a,size_t offset,size_t bytes) {
    if (!valid(a,offset,bytes)) return 0;
    void* p=(char*)a->base+offset;
    if (mprotect(p,bytes,PROT_NONE)!=0) return 0;
    return madvise(p,bytes,MADV_DONTNEED)==0;
}
int hos_guest_release(HosGuestArena* a) {
    if (!a || !a->base) { errno=EINVAL; return 0; }
    if (munmap(a->base,a->size)!=0) return 0;
    memset(a,0,sizeof *a); return 1;
}
