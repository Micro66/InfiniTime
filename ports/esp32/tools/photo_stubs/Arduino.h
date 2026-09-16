#pragma once
#include <cstdint>
extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }
