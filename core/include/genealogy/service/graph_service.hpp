#pragma once

// 全库图装载用例（GraphService）。
//
// 「全量人物 + 全量结合 → PersonGraph」这条链 server 有三处调用
// （stats / validate / export_tree），CLI 的 person tree 也要——
// 各自内联的后果是同口径的四份拷贝（装载上限、失败处理迟早漂移）。
// 收敛到这里一份。
//
// 装载上限 20 万 = 灌库验收规模（10 万人物）留一倍余量。更大的库
// 先跑 bench 确认口径，再谈按需子图（设计文档 4.4.1）。

#include <genealogy/error.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

class GraphService {
 public:
  explicit GraphService(Database& db);

  // 全库快照。失败（查询错误）返回 Error。
  Result<lineage::PersonGraph> load_full();

 private:
  Database& db_;
};

}  // namespace genealogy
