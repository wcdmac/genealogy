#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <genealogy/model/person.hpp>
#include <genealogy/model/union.hpp>

namespace genealogy::lineage {

// 世系图快照（设计文档 4.4.1）：节点 = Person，边 = parent_child + union_member。
// 世系算法层纯函数式 API 的输入；不持有数据库连接、无副作用、可重入。
//
// 生命周期：图**自持数据**。build 按值接收后移入内部成员，索引建在
// 自己的数据上——曾经的 borrowing 契约（索引指向调用方向量、要求调用方
// 保活）是颗雷：服务端两个路由文件各踩一次，load_graph 把仓储结果放在
// 局部 Result 里就返回图，函数一返回全图悬空。症状极具迷惑性——
// stats/search 靠尚未被覆写的内存「正常」工作，validate 多分配几次
// 才 SIGSEGV，日志里只剩进程消失，几乎无法定位。契约靠口头约束
// 是靠不住的，干脆让类型自己保活。
class PersonGraph {
 public:
  // persons：人物集合（通常为同一 lineage）；unions：婚姻结合（配偶边来源）。
  // 按值接收：传入左值会拷贝一份，传入右值（std::move）零拷贝。
  static PersonGraph build(std::vector<Person> persons,
                           std::vector<Union> unions = {});

  const Person* find(const std::string& id) const;
  bool contains(const std::string& id) const;
  std::size_t size() const noexcept { return nodes_.size(); }

  // 全部节点（批量遍历用；返回内部索引，调用方勿长期持有）。
  const std::unordered_map<std::string, const Person*>& nodes() const noexcept {
    return nodes_;
  }

  const Person* father_of(const std::string& id) const;
  const Person* mother_of(const std::string& id) const;

  // 子女排序：birth_order 升序（行几小者在前）→ 出生日期 → id。
  // 排序确定，保证算法输出可复现（快照测试的前提）。
  // 返回常量引用：树展开会逐节点调用，返回副本等于白扔分配。
  const std::vector<const Person*>& children_of(const std::string& id) const;

  // 兄弟姐妹：同父或同母（不含自己）；半亲计入（M1 不区分全/半）。
  const std::vector<const Person*>& siblings_of(const std::string& id) const;

  // 配偶：union 成员中除自己外的各方（含妾/继配）。
  const std::vector<const Person*>& spouses_of(const std::string& id) const;

  // 是否已婚（图中存在配偶边）。
  bool is_married(const std::string& id) const;

  // 婚姻结合快照。校验引擎需要按结合整体看，而 spouses_of() 只给单侧视角。
  // 无结合时返回 nullptr（与「有空结合向量」区分：前者是没建边，后者是建了空边）。
  const std::vector<Union>* unions() const noexcept {
    return unions_.empty() ? nullptr : &unions_;
  }

  // 默认构造：build 内部要用（先造空图、再移入数据、再建索引）。
  // 声明了移动构造会抑制隐式默认构造，故显式要回来。
  PersonGraph() = default;

  // 拷贝被禁：索引指向内部数据，拷贝一份会让副本的指针留在原对象上，
  //  silently broken。移动安全——vector 移动只转移缓冲区，
  // 元素不住址不变，索引随缓冲区一起过户，仍旧有效。
  PersonGraph(const PersonGraph&) = delete;
  PersonGraph& operator=(const PersonGraph&) = delete;
  PersonGraph(PersonGraph&&) noexcept = default;
  PersonGraph& operator=(PersonGraph&&) noexcept = default;

 private:
  // 数据属主。索引（nodes_/children_/siblings_/spouses_）里的指针
  // 一律指向这两个成员，故二者的住址一旦定下就不能再动——
  // 这就是禁拷贝、且 build 必须先移入再建索引的原因。
  std::vector<Person> persons_;
  std::vector<Union> unions_;

  std::unordered_map<std::string, const Person*> nodes_;
  std::unordered_map<std::string, std::vector<const Person*>> children_;
  std::unordered_map<std::string, std::vector<const Person*>> siblings_;
  std::unordered_map<std::string, std::vector<const Person*>> spouses_;
};

}  // namespace genealogy::lineage
