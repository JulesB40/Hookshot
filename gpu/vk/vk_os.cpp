// GPL-3.0-or-later.
#include "vk_os.h"
#include <chrono>
#include <cstring>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
extern "C" char** environ;
#endif
namespace mkwvk::os {
bool replace_file(const char* source, const char* destination) {
    if (!source || !*source || !destination || !*destination) return false;
#if defined(_WIN32)
    return MoveFileExA(source, destination, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(source, destination) == 0;
#endif
}
int seek(FILE* file, int64_t offset, int origin) {
#if defined(_WIN32)
    return _fseeki64(file, offset, origin);
#else
    return fseeko(file, (off_t)offset, origin);
#endif
}
int64_t tell(FILE* file) {
#if defined(_WIN32)
    return _ftelli64(file);
#else
    return ftello(file);
#endif
}
int truncate_file(FILE* file, int64_t size) {
    if (!file || size < 0) return -1;
#if defined(_WIN32)
    return (int)_chsize_s(_fileno(file), size);
#else
    return ftruncate(fileno(file), (off_t)size);
#endif
}
char** environment() {
#if defined(_WIN32)
    return _environ;
#else
    return ::environ;
#endif
}
void name_thread(const char* name) {
    if (!name) return;
#if defined(_WIN32)
    wchar_t wide[64]{};
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 64);
    SetThreadDescription(GetCurrentThread(), wide);
#else
    char short_name[16]{};
    std::snprintf(short_name, sizeof short_name, "%s", name);
    pthread_setname_np(pthread_self(), short_name);
#endif
}
void priority_hint(int direction) {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), direction > 0 ? THREAD_PRIORITY_ABOVE_NORMAL :
        direction < 0 ? THREAD_PRIORITY_BELOW_NORMAL : THREAD_PRIORITY_NORMAL);
#else
    (void)direction; // The Android app scheduler owns priorities; no privilege changes.
#endif
}
bool affinity(uint64_t mask) {
#if defined(_WIN32)
    return SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)mask) != 0;
#else
    if (!mask) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    for (unsigned i = 0; i < 64 && i < CPU_SETSIZE; ++i) if (mask >> i & 1) CPU_SET(i, &set);
    return sched_setaffinity(0, sizeof set, &set) == 0;
#endif
}
bool exists(const char* path) {
    if (!path || !*path) return false;
#if defined(_WIN32)
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat status{};
    return stat(path, &status) == 0;
#endif
}
uint64_t monotonic_ms() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string cache_path(const char* name) {
    if (!name || !*name || std::strchr(name, '/') || std::strchr(name, '\\')) return {};
#if defined(_WIN32)
    char exe[MAX_PATH];
    const DWORD count = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (!count || count == MAX_PATH) return {};
    std::string directory(exe, count);
    return directory.substr(0, directory.find_last_of("\\/") + 1) + name;
#else
    // No implicit CWD path: Android caching waits for confined app-private storage.
    return {};
#endif
}
bool copy_from_self(void* dst, const void* src, size_t size) {
    if (!size) return true;
    if (!src || !dst || size > (size_t)PTRDIFF_MAX) return false;
#if defined(_WIN32)
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), src, dst, size, &copied) && copied == size;
#else
    iovec local{dst, size}, remote{const_cast<void*>(src), size};
    const ssize_t copied = syscall(SYS_process_vm_readv, getpid(), &local, 1UL, &remote, 1UL, 0UL);
    if (copied == (ssize_t)size) return true;
    if (copied >= 0 || (errno != EPERM && errno != EACCES && errno != ENOSYS)) return false;
    // Kernel-validated fallback for self copies; no raw memcpy or signal handlers.
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) return false;
    bool ok = true;
    for (size_t at = 0; at < size && ok;) {
        const size_t bytes = size - at < 4096 ? size - at : 4096;
        ssize_t written;
        do { written = write(fds[1], (const char*)src + at, bytes); } while (written < 0 && errno == EINTR);
        if (written != (ssize_t)bytes) { ok = false; break; }
        for (size_t consumed = 0; consumed < bytes;) {
            ssize_t n;
            do { n = read(fds[0], (char*)dst + at + consumed, bytes - consumed); } while (n < 0 && errno == EINTR);
            if (n <= 0) { ok = false; break; }
            consumed += (size_t)n;
        }
        at += bytes;
    }
    close(fds[0]); close(fds[1]);
    return ok;
#endif
}
bool readable_range(const void* ptr, size_t size) {
    const uintptr_t start = (uintptr_t)ptr;
    if (!ptr || !size || size > UINTPTR_MAX - start) return false;
    const uintptr_t end = start + size;
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(ptr, &info, sizeof info) == sizeof info && info.State == MEM_COMMIT &&
        (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY)) && !(info.Protect & PAGE_GUARD) &&
        end <= (uintptr_t)info.BaseAddress + info.RegionSize;
#else
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps) return false;
    uintptr_t cursor = start;
    char line[1024], permission[5];
    unsigned long long lo, hi;
    while (cursor < end && std::fgets(line, sizeof line, maps)) {
        if (std::sscanf(line, "%llx-%llx %4s", &lo, &hi, permission) != 3 || hi <= cursor) continue;
        if (lo > cursor || permission[0] != 'r') break;
        cursor = (uintptr_t)hi;
    }
    std::fclose(maps);
    return cursor >= end;
#endif
}
}
