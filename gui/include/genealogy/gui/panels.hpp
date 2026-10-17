#pragma once

// 面板框架：每个面板 = 一份自持状态 + 一组「刷新数据」+ 一组「ImGui 绘制」。
//
// 分层理由：数据层（调 core 用例层）与渲染层（ImGui）分开，前者不依赖
// 任何 UI 头文件，可以在无显示环境（CI、SSH）里单测——GUI 的逻辑错误
// 大多出在数据装配（筛选、翻页、选中态），不在画像素。渲染层只保证
// 编译通过 + 运行时报错路径。
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

  // 面板开关（菜单 / 快捷键控制）。
  bool open = true;
};

}  // namespace genealogy::gui
