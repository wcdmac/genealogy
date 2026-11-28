#pragma once

// 版框几何与字符游标——分页器与渲染器共用的单一事实源。
//
// 为什么要共享：过去分页器按抽象「码点容量」切页，渲染器按自己的列/行
// 几何排版，两者模型不一致，导致续页点落在页面上任意位置（近似值）。
// 现在两者都用 TextCursor 按真实行列推进，分页点就是真实换页点。
//
// 单位统一为「半行」：正文 1 字 = 2 半行（占满一行），
// 夹注双行小字 1 字 = 1 半行（两字占一行）。换列时夹注补齐到整行，
// 保证「两字一格」不被拆到两列。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

namespace genealogy::io {

struct LayoutGeometry {
  int page_w = 720;
  int page_h = 1040;
  int frame_l = 90;
  int frame_r = 630;
  int frame_t = 62;
  int frame_b = 978;
  int center_x = 312;
  int center_w = 96;
  int body_step = 19;   // 正文行高（像素）
  int note_step = 12;   // 夹注小字行高（像素）
  int col_gap = 22;     // 列间距（像素）
  int top_pad = 30;     // 版框顶部留白
  int bottom_pad = 30;  // 版框底部留白
  int body_px = 15;     // 正文字号（像素）——CSS 与几何的单一来源
  int note_px = 10;     // 夹注字号（像素）
  int mark_dx = 14;     // 专名线横向偏移（像素）

  // 从预设 typography 推导全部度量。默认 11pt 时保持原视觉数值
  // （body_step=19 / note_step=12 / col_gap=22 / mark_dx=14），零回归；
  // 改预设字号后 CSS 字号、行距、列距、专名线偏移同步联动。
  static LayoutGeometry from_preset(const nlohmann::json& j) {
    LayoutGeometry g;
    const auto& t = j.value("typography", nlohmann::json::object());
    const std::string size = t.value("body_size", "11pt");
    double pt = 11;
    try { pt = std::stod(size); } catch (...) { pt = 11; }
    g.body_px = static_cast<int>(std::lround(pt * 96.0 / 72.0));  // pt→px @96dpi
    g.body_step = static_cast<int>(std::lround(g.body_px * 1.25)); // 字距 1.25em
    g.col_gap = static_cast<int>(std::lround(g.body_px * 1.47));   // 行距≈1.47em
    g.mark_dx = static_cast<int>(std::lround(g.body_px * 0.93));   // 专名线偏移

    // 夹注字号取正文的 0.67（10/15，与原 .note 10px 一致）。
    // 注意：notes.note_size_ratio 旧字段语义是「每格字数占比」而非字号比，
    // 不用于字号推导，避免预设 0.6 把字号算成 9px。
    constexpr double kNoteSizeRatio = 0.67;
    g.note_px = std::max(6, static_cast<int>(
        std::lround(g.body_px * kNoteSizeRatio)));
    g.note_step = std::max(8, static_cast<int>(std::lround(g.note_px * 1.2)));
    return g;
  }
};

// 每列可容纳的半行数（行高口径 = 正文行）
inline int half_rows_per_column(const LayoutGeometry& g) {
  const int usable = g.frame_b - g.frame_t - g.top_pad - g.bottom_pad;
  return (usable / std::max(1, g.body_step)) * 2;
}

// 每页列数（从右版框到版心左界）
inline int columns_per_page(const LayoutGeometry& g) {
  const int right = g.frame_r - 24;
  const int left_limit = g.center_x + g.center_w + 20;
  if (right <= left_limit) return 1;
  return (right - left_limit) / std::max(1, g.col_gap) + 1;
}

// 第 col 列的 x 坐标（col=0 为最右列）
inline int column_x(const LayoutGeometry& g, int col) {
  return g.frame_r - 24 - col * g.col_gap;
}

// 第 half_row 半行的 y 坐标
inline int row_y(const LayoutGeometry& g, int half_row) {
  return g.frame_t + g.top_pad + (half_row / 2) * g.body_step;
}

// 字符游标：按真实几何推进，供分页与渲染共用。
class TextCursor {
 public:
  struct Placement {
    int col = 0;
    int half_row = 0;
  };

  explicit TextCursor(LayoutGeometry g) : g_(g) {}

  // 尝试放置一个字符。本页放不下返回 false 且状态不变；
  // 否则写入位置并推进游标，返回 true。
  bool place(bool is_note, Placement& out) {
    const int cap = half_rows_per_column(g_);
    int delta = is_note ? 1 : 2;  // 半行数
    // 正文/其他样式必须偶对齐：前一夹注只占半格时，正文从下一格起
    // （奇半行空置），避免与夹注同格叠印。奇半行必 ≤ cap-1（cap 恒
    // 偶数），补一行不会越列；越列由下方通用检查兜住。
    if (!is_note && half_row_ % 2 != 0) {
      ++half_row_;
    }
    // 夹注成对：偶半行起一对（两字一格）；本列装不下这一对时，
    // 整对挪到下列，避免两字一格被拆列。奇半行是上一夹注的下半格，
    // 夹注直接入位成对，不再跳行。
    if (is_note && half_row_ % 2 == 0 && half_row_ + 2 > cap) {
      if (!next_column()) return false;
    }
    if (half_row_ + delta > cap) {
      if (!next_column()) return false;
    }
    out.col = col_;
    out.half_row = half_row_;
    half_row_ += delta;
    return true;
  }

  void reset() {
    col_ = 0;
    half_row_ = 0;
  }

  int col() const { return col_; }
  int half_row() const { return half_row_; }
  const LayoutGeometry& geometry() const { return g_; }

 private:
  bool next_column() {
    if (col_ + 1 >= columns_per_page(g_)) return false;
    ++col_;
    half_row_ = 0;
    return true;
  }

  LayoutGeometry g_;
  int col_ = 0;
  int half_row_ = 0;
};

}  // namespace genealogy::io
