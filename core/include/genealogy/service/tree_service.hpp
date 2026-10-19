#pragma once

// 世系树用例（设计文档 3.2 service 模块的 TreeService）。
//
// 存在的理由：CLI 的 io export tree 与 server 的 /exports/tree 都要
// 「全量装载 → 组树 → 渲染」这条链。两端各写一遍的后果是迟早漂移
// （渲染文本、装载上限、深度语义各变各的），而这类漂移不报错，
// 只是「导出结果偶尔不一样」。收敛到 core 后两端同一份代码。
//
// 装载为什么放 service 而不放 lineage：lineage 是纯函数算法层
// （无副作用、可快照测试），碰数据库就脏了。
//
// 生命周期警告（踩过的 SIGSEGV）：TreeNode::person 是指向
// PersonGraph 快照内人物的**裸指针**。组树的函数一旦把 graph 作
// 局部变量再返回 TreeNode，调用方拿到的就是悬空指针——所以这里
// 不提供「返回 TreeNode 且自持 graph」的入口；要么调用方自己
// 持 graph 走纯函数 descendants_tree，要么直接用 export_tree
// （graph 与渲染同作用域，自洽）。

#include <string>

#include <genealogy/error.hpp>
#include <genealogy/io/tree_render.hpp>
#include <genealogy/lineage/tree.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

class TreeService {
 public:
  explicit TreeService(Database& db);

  // 渲染入口：内部自持全库快照，组树与渲染在同一作用域完成。
  // CLI / server 的树导出都走这个（安全且同一份实现）。
  Result<std::string> export_tree(const std::string& root_id,
                                  io::TreeFormat format, int max_depth);

 private:
  Database& db_;
};

}  // namespace genealogy
