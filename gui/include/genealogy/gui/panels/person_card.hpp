#pragma once

// 人物卡面板：选中人物的全字段展示 + 行内编辑。
//
// 设计文档 3.4「人物卡」：姓名/字/号/生卒/葬地/行传/字号辈/隐私标记，
// 行内编辑。编辑走 PersonService::update_person（带 rev 乐观锁）。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

class PersonCardPanel : public Panel {
 public:
  explicit PersonCardPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "人物卡"; }
  void draw(DbSession& session) override;

 private:
  // 数据层：把选中人物读进编辑缓冲。返回 false 表示没选中或读失败。
  bool load(DbSession& session);
  // 数据层：保存编辑缓冲。成功返回 true，失败把原因写进 state 状态栏。
  bool save(DbSession& session);
  void on_database_closed() override {
    loaded_id_.clear();
    loaded_rev_ = 0;
    dirty_ = false;
  }

  AppState& state_;
  std::string loaded_id_;      // 编辑缓冲对应的人物 id
  bool dirty_ = false;        // 缓冲有未保存修改
  int loaded_rev_ = 0;        // 载入时的 rev（ optimistic lock 用）

  // 编辑缓冲（ImGui InputText 要持久化缓冲，故持成员）
  std::string buf_family_;
  std::string buf_given_;
  std::string buf_courtesy_;   // 字
  std::string buf_art_;        // 号
  std::string buf_posthumous_; // 谥 / 赠
  std::string buf_alias_;      // 别称
  std::string buf_biography_;  // 行传
  std::string buf_note_;
  std::string buf_birth_;      // 公历 ISO
  // 名号变更史（M7.1）：只读展示，编辑走 genea person name / REST
  std::vector<std::string> name_rows_;
  std::string buf_death_;      // 公历 ISO
  int buf_gender_ = 2;         // 0 男 / 1 女 / 2 未知
  int buf_privacy_ = 1;        // 0 公开 / 1 亲缘 / 2 私密
  bool buf_heir_ = false;
  bool buf_living_ = false;
  std::string buf_birth_order_;
  std::string buf_generation_no_;
  // 行传/备注用 std::string 缓冲：这两个字段是长文本（可达数千字），
  // widgets.hpp 的 CallbackResize 桥接直接在 string 上编辑——不再有
  // 固定缓冲截断（第三轮新-C14 把 8192/4096 定长数组换掉）
  std::string bio_buf_;
  std::string note_buf_;

  // 关系链展示（父母/配偶/子女，点选跳转）
  std::string father_id_;
  std::string mother_id_;
  std::vector<std::string> spouse_ids_;
  std::vector<std::string> child_ids_;
};

}  // namespace genealogy::gui
