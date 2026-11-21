#include <genealogy/gui/db_switch.hpp>

namespace genealogy::gui {

void reset_for_database_switch(AppState& state,
                               std::vector<Panel*>& panels) {
  // 全局选中态：人物卡与关系面板的端点都来自它（第三轮新-C5——
  // 换库后拿旧库 id 建图，关系面板算出的是旧库数据）
  state.selected_person_id.clear();
  state.relation_ego.clear();
  state.relation_alter.clear();
  // 逐面板缓存：检索命中/人物表行/人物卡缓冲/树快照/校验结论
  for (auto* p : panels) p->on_database_closed();
}

}  // namespace genealogy::gui
