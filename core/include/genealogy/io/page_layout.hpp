#pragma once

// 页布局引擎：把标记节点流按版框容量分配到页，夹注超长自动续排到下一页。
//
// 关键设计：先布局后渲染。过去「解析→按〔页〕切→渲染」两段式无法处理
// 夹注超长——本引擎增加中间的「页分配」阶段，溢出内容进入下一页并
// 标注「（续）」，不再静默截断。

#include <cstddef>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/io/layout_geometry.hpp>
#include <genealogy/io/markup.hpp>

namespace genealogy::io {

struct LaidOutPage {
  std::vector<MarkupNode> nodes;
  bool continuation = false;   // 本页承接上页未完夹注
  std::size_t carried_chars = 0;
};

struct PageLayoutOptions {
  std::vector<MarkupNode> source;
  // 共享版框几何：分页与渲染用同一套行列模型，续页点即真实换页点。
  LayoutGeometry geometry;
  // use_geometry=false 时只按〔页〕标记分页，不按容量自动续页。
  bool use_geometry = true;
};

struct PageLayout {
  std::vector<LaidOutPage> pages;
  std::size_t total_chars = 0;
  std::size_t continued_count = 0;  // 发生续排的夹注段数
};

Result<PageLayout> paginate_content(const PageLayoutOptions& opt);

}  // namespace genealogy::io
