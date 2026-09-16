#pragma once
#include <cstdlib>
constexpr int MALLOC_CAP_SPIRAM = 1;
inline void* heap_caps_malloc(size_t count, int) { return malloc(count); }
