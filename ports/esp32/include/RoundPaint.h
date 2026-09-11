#pragma once
#include <lvgl/lvgl.h>
#include <cmath>

namespace Esp32 {
  constexpr float Pi = 3.14159265f;

  inline lv_coord_t coord(int value) {
    return static_cast<lv_coord_t>(value);
  }

  inline lv_point_t polar(float degrees, int radius, int x = 233, int y = 233) {
    const float angle = degrees * Pi / 180;
    return {coord(x + std::lround(std::sin(angle) * radius)), coord(y - std::lround(std::cos(angle) * radius))};
  }

  struct Paint {
    const lv_area_t* clip;

    void box(int x, int y, int w, int h, uint32_t color, int radius = 0) const {
      const lv_area_t area {coord(x), coord(y), coord(x + w - 1), coord(y + h - 1)};
      lv_draw_rect_dsc_t d;
      lv_draw_rect_dsc_init(&d);
      d.bg_color = lv_color_hex(color);
      d.bg_opa = LV_OPA_COVER;
      d.radius = radius;
      lv_draw_rect(&area, clip, &d);
    }

    void dot(int x, int y, int radius, uint32_t color) const {
      box(x - radius, y - radius, radius * 2, radius * 2, color, LV_RADIUS_CIRCLE);
    }

    void line(lv_point_t a, lv_point_t b, int width, uint32_t color) const {
      lv_draw_line_dsc_t d;
      lv_draw_line_dsc_init(&d);
      d.color = lv_color_hex(color);
      d.width = width;
      d.round_start = d.round_end = true;
      lv_draw_line(&a, &b, clip, &d);
    }

    void line(int x1, int y1, int x2, int y2, int width, uint32_t color) const {
      line({coord(x1), coord(y1)}, {coord(x2), coord(y2)}, width, color);
    }

    void arc(int x, int y, int radius, int start, int end, int width, uint32_t color) const {
      lv_draw_line_dsc_t d;
      lv_draw_line_dsc_init(&d);
      d.color = lv_color_hex(color);
      d.width = width;
      d.round_start = d.round_end = true;
      lv_draw_arc(x, y, radius, start, end, clip, &d);
    }

    void text(const char* value, int y, uint32_t color, const lv_font_t* font = &jetbrains_mono_bold_20, int x = 0, int w = 466) const {
      const lv_area_t area {coord(x), coord(y), coord(x + w - 1), coord(y + font->line_height + 4)};
      lv_draw_label_dsc_t d;
      lv_draw_label_dsc_init(&d);
      d.color = lv_color_hex(color);
      d.font = font;
      d.flag = LV_TXT_FLAG_CENTER;
      lv_draw_label(&area, clip, &d, value, nullptr);
    }

    void triangle(lv_point_t a, lv_point_t b, lv_point_t c, uint32_t color) const {
      const lv_point_t points[] {a, b, c};
      lv_draw_rect_dsc_t d;
      lv_draw_rect_dsc_init(&d);
      d.bg_color = lv_color_hex(color);
      lv_draw_triangle(points, clip, &d);
    }

    void star(int x, int y, int size, uint32_t color) const {
      line(x - size, y, x + size, y, 2, color);
      line(x, y - size, x, y + size, 2, color);
    }

    void heart(int x, int y, int size, uint32_t color) const {
      dot(x - size / 2, y, size / 2 + 1, color);
      dot(x + size / 2, y, size / 2 + 1, color);
      triangle({coord(x - size), coord(y)}, {coord(x + size), coord(y)}, {coord(x), coord(y + size + 3)}, color);
    }
  };
}
