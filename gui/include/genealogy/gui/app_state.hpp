#pragma once

// 跨面板共享的应用状态。面板之间不直接互相调用，全部经这一层联动——
// 否则六个面板互相持引用，指向关系很快成一团，且无法单测。

#include <functional>
#include <string>

namespace genealogy::gui {

struct AppState {
  // 当前选中的人物（全局联动：表格/检索/树点选 → 人物卡跟随）。
  std::string selected_person_id;

  // 关系快捷录入：人物卡按钮设置锚点与关系类型，新建面板读取并预填。
  // kind: "child" | "father" | "mother" | "spouse"；空表示无待建关系。
  std::string new_relation_kind;
  std::string new_relation_anchor;

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

  // 换库生命周期回调：GUI 主程序注入，供"恢复数据库"这类
  // 面板内直接开关会话的路径使用——必须先停后台 worker 并清缓存，
  // 否则 worker 持有的 DbSession* 会 use-after-free。
  std::function<void()> before_db_close;
  std::function<void(const std::string&)> after_db_open;

  void set_status(std::string msg, bool is_error = false) {
    status_message = std::move(msg);
    status_is_error = is_error;
  }
};

}  // namespace genealogy::gui
