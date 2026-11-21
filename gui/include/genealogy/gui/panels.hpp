#pragma once

// 面板框架：每个面板 = 一份自持状态 + 一组「刷新数据」+ 一组「ImGui 绘制」。
//
// 分层意图：数据层（调 core 用例层）与渲染层（ImGui）分开。
// 现状（2026-11-21 债务偿还）：三个重状态机面板各拆出 *_state.cpp
// （person_card/tree_panel/validation_panel 的装载/决策/异步采纳），
// 无 imgui 依赖、可被 gui-tests 无头驱动；换库清理序列收敛在
// db_switch.cpp。渲染 TU 只画状态机的 API。GUI 的逻辑错误大多出在
// 数据装配——那一半现在有回归网了。
//
// ADR-1：面板只经 DbSession 拿服务，不直连仓储。

#include <genealogy/gui/db_session.hpp>

#include <string>

namespace genealogy::gui {

class Panel {
 public:
  virtual ~Panel() = default;

  // 面板标题（同时作 docking 标签）。
  virtual const char* title() const = 0;

  // 每帧调用：读会话数据 + 绘制。open 为 false 时 app 不调用。
  virtual void draw(DbSession& session) = 0;

  // 关闭数据库时清面板缓存（2026-11 静态审查 L4：换库后曾展示
  // 旧库的树/编辑缓冲）。持缓存的面板各自覆写。
  virtual void on_database_closed() {}

  // 面板是否持有任何缓存/未保存数据（换库清理序列的观测缝，
  // db_switch 的无头断言靠它）。纯虚——新面板漏实现会在编译期
  // 暴露，不再依赖逐个记得写（L4 的教训机制化）。
  virtual bool has_cached_data() const = 0;

  // 面板开关（菜单 / 快捷键控制）。
  bool open = true;
};

}  // namespace genealogy::gui
