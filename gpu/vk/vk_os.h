// GPL-3.0-or-later. Portable renderer host operations.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
namespace mkwvk::os {
void name_thread(const char* name);
void priority_hint(int direction);
bool affinity(uint64_t mask);
bool exists(const char* path);
uint64_t monotonic_ms();
std::string cache_path(const char* basename);
bool copy_from_self(void* dst, const void* src, size_t size);
bool readable_range(const void* ptr, size_t size);
int seek(FILE* file, int64_t offset, int origin);
int64_t tell(FILE* file);
int truncate_file(FILE* file, int64_t size);
char** environment();
bool replace_file(const char* source, const char* destination);
}
