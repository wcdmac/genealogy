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
// 生命周期：graph 持有指向调用方数据的指针，调用方须保证 persons/unions
// 的存活期覆盖 graph 使用期（仓储查询结果需保持在场）。
class PersonGraph {
 public:
  // persons：人物集合（通常为同一 lineage）；unions：婚姻结合（配偶边来源）。
  static PersonGraph build(const std::vector<Person>& persons,
                           const std::vector<Union>& unions = {});

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
  std::vector<const Person*> children_of(const std::string& id) const;

  // 兄弟姐妹：同父或同母（不含自己）；半亲计入（M1 不区分全/半）。
  std::vector<const Person*> siblings_of(const std::string& id) const;

  // 配偶：union 成员中除自己外的各方（含妾/继配）。
  std::vector<const Person*> spouses_of(const std::string& id) const;

  // 是否已婚（图中存在配偶边）。
  bool is_married(const std::string& id) const;

  // 婚姻结合快照（build 时传入的那份）。校验引擎需要按结合整体看，
  // 而 spouses_of() 只给单侧视角。
  const std::vector<Union>* unions() const noexcept { return unions_; }

 private:
  std::unordered_map<std::string, const Person*> nodes_;
  std::unordered_map<std::string, std::vector<const Person*>> children_;
  std::unordered_map<std::string, std::vector<const Person*>> siblings_;
  std::unordered_map<std::string, std::vector<const Person*>> spouses_;
  const std::vector<Union>* unions_ = nullptr;
};

}  // namespace genealogy::lineage
