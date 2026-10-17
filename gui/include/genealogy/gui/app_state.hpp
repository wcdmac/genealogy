#pragma once

// 跨面板共享的应用状态。面板之间不直接互相调用，全部经这一层联动——
// 否则六个面板互相持引用，指向关系很快成一团，且无法单测。

#include <string>

namespace genealogy::gui {

struct AppState {
  // 当前选中的人物（全局联动：表格/检索/树点选 → 人物卡跟随）。
  std::string selected_person_id;

  // 关系面板的两个端点（默认沿用选中者 + 空）。
  std::string relation_ego;
  std::string relation_alter;

  // 数据库路径（打开对话框的结果回写到这里）。
  std::string db_path;

  // 状态栏消息。is_error 控制颜色（红/白）。
  std::string status_message;
  bool status_is_error = false;

  // 在世者详情显式开关（设计文档 6.5：API 层默认脱敏；
  // GUI 本地库允许显式展开）。仅本地形态有意义。
  bool show_living_details = false;

  void set_status(std::string msg, bool is_error = false) {
    status_message = std::move(msg);
    status_is_error = is_error;
  }
};

}  // namespace genealogy::gui
