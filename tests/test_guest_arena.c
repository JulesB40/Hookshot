/* Only private synthetic mappings; never opens or modifies a file. */
#include "guest_arena.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    HosGuestArena a={0}, occupied={0};
    assert(hos_guest_reserve(&a,0,page*4));
    assert(hos_guest_commit(&a,0,page*4));
    memset(a.base,0x5a,page*4);
    // Refuse address collisions rather than replace the existing mapping.
    assert(!hos_guest_reserve(&occupied,(uintptr_t)a.base,page));
    assert(((unsigned char*)a.base)[0]==0x5a);
    // Relocation must preserve the occupied mapping and choose independent space.
    assert(hos_guest_reserve_relocated(&occupied,(uintptr_t)a.base,page));
    assert(occupied.base!=a.base);
    assert(hos_guest_commit(&occupied,0,page));
    memset(occupied.base,0xa5,page);
    assert(((unsigned char*)a.base)[0]==0x5a);
    assert(hos_guest_release(&occupied));
    assert(!hos_guest_commit(&a,1,page));
    assert(!hos_guest_decommit(&a,0,page+1));
    assert(!hos_guest_commit(&a,a.size,page));
    assert(!hos_guest_commit(&a,SIZE_MAX,page));
    assert(hos_guest_decommit(&a,page,page));
    assert(hos_guest_commit(&a,page,page));
    for (size_t i=0;i<page;++i) assert(((unsigned char*)a.base)[page+i]==0);
    assert(((unsigned char*)a.base)[0]==0x5a);
    assert(((unsigned char*)a.base)[page*2]==0x5a);
    assert(hos_guest_release(&a));
    assert(!hos_guest_release(&a));
    assert(!hos_guest_reserve(&a,UINTPTR_MAX-page+1,page*2));
    puts("PASS: guest arena collision refusal, alignment/bounds, decommit zeroing and neighboring data preservation");
}
