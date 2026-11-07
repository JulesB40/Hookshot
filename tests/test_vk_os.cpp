// Synthetic mappings and temporary files only; no game input.
#include "vk_os.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
int main() {
    using namespace mkwvk;
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    auto* src=(unsigned char*)mmap(nullptr,page*3,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(src!=MAP_FAILED);
    for(size_t i=0;i<page*3;++i) src[i]=(unsigned char)i;
    unsigned char dst[32]{};
    assert(os::copy_from_self(dst,src+11,sizeof dst));
    assert(!memcmp(dst,src+11,sizeof dst));
    assert(os::readable_range(src,page*3));
    assert(mprotect(src+page,page,PROT_NONE)==0);
    assert(!os::copy_from_self(dst,src+page,sizeof dst));
    assert(!os::copy_from_self(dst,src+page-16,sizeof dst));
    assert(!os::readable_range(src,page*3));
    assert(os::readable_range(src+page*2,page));
    assert(!os::readable_range((void*)(UINTPTR_MAX-3),8));
    assert(!os::copy_from_self(dst,nullptr,sizeof dst));
    assert(os::copy_from_self(nullptr,nullptr,0));
    assert(munmap(src,page*3)==0);
    assert(!os::copy_from_self(dst,src,sizeof dst));
    assert(!os::readable_range(src,page));
    assert(os::cache_path("cache.bin").empty());
    assert(os::cache_path("../save.dat").empty());
    FILE* f=std::tmpfile(); assert(f);
    assert(os::seek(f,1LL<<33,SEEK_SET)==0 && os::tell(f)==(1LL<<33));
    assert(os::truncate_file(f,64)==0);
    assert(os::seek(f,0,SEEK_END)==0 && os::tell(f)==64);
    assert(os::truncate_file(f,-1)!=0); fclose(f);
    puts("PASS: protected/unmapped copies, overflow, no implicit cache path, 64-bit file offsets");
}
