#pragma once
#include <cstdint>
constexpr int LV_IMG_CF_TRUE_COLOR = 4;
struct lv_img_dsc_t {
  struct { unsigned cf = 0, w = 0, h = 0; } header;
  unsigned data_size = 0;
  const uint8_t* data = nullptr;
};
inline void lv_img_cache_invalidate_src(const void*) {}
