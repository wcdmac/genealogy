#pragma once

// 新建人物面板：以焦点人物为上下文的快捷录入。
//
// 设计思想借鉴 Gramps 的关系快捷按钮与 Heredis 的智能预填：
// 打开面板时按当前选中人物预填姓氏、世代、父母；创建后自动成为新焦点。
// 数据装配在 new_person_panel_state.cpp（无 ImGui 依赖，可无头测试），
// 渲染层只画控件并调状态机 API。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <string>

namespace genealogy::gui {

struct NewPersonDraft {
  std::string family_name;
  std::string given_name;
  std::string courtesy_name;
  int gender = 2;              // 0 男 / 1 女 / 2 未知
  std::string birth_date;      // 公历 ISO 或老谱原文（年号自动识别）
  std::string death_date;
  std::string generation_no;   // 世代序号（可空）
  std::string biography;
  std::string note;
  std::string father_id;       // 关系快捷按钮预置
  std::string mother_id;
};

class NewPersonPanel : public Panel {
 public:
  explicit NewPersonPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "新建人物"; }
  void draw(DbSession& session) override;
  // pending 关系与预填锚点都是旧库 ULID，换库不清会在新库建档时误建
  // 关系（第四轮 P2，person_table 的 cancel_edit 同类教训）
  void on_database_closed() override {
    draft_ = NewPersonDraft{};
    prefill_from_id_.clear();
    pending_kind_.clear();
    pending_anchor_.clear();
  }
  bool has_cached_data() const override {
    return !draft_.family_name.empty() || !draft_.given_name.empty() ||
           !draft_.father_id.empty() || !draft_.mother_id.empty() ||
           !pending_kind_.empty() || !pending_anchor_.empty();
  }

  // ---- 无头状态机 API（定义在 new_person_panel_state.cpp）
  // 按焦点人物预填姓氏/世代/父母
  void prefill(DbSession& session, const std::string& focus_id);
  // 创建人物，成功返回 true 并把新 id 写进全局选中；失败写状态栏
  bool create(DbSession& session);
  // 清空表单
  void clear() { draft_ = NewPersonDraft{}; }

  NewPersonDraft& draft() { return draft_; }
  const NewPersonDraft& draft() const { return draft_; }

 private:
  AppState& state_;
  NewPersonDraft draft_;
  std::string prefill_from_id_;  // 上次预填依据的焦点（焦点变化时重预填）
  // 待建关系（prefill 时从 AppState 取走，create 时落地）
  std::string pending_kind_;
  std::string pending_anchor_;
};

}  // namespace genealogy::gui
