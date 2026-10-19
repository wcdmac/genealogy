#include <genealogy/service/graph_service.hpp>

#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>

namespace genealogy {

GraphService::GraphService(Database& db) : db_(db) {}

Result<lineage::PersonGraph> GraphService::load_full() {
  PersonRepository persons(db_);
  PersonFilter pf;
  pf.limit = 200000;
  auto person_rows = persons.list(pf);
  if (!person_rows) return person_rows.error();

  UnionRepository unions(db_);
  UnionFilter uf;
  uf.limit = 200000;
  auto union_rows = unions.list(uf);
  if (!union_rows) return union_rows.error();

  // 零拷贝：图按值接收后把两个结果向量移进去自持（见 graph.hpp 的
  // 生命周期说明）。曾经的写法是 build(*rows, *urows)——图里存的是
  // 指向两个局部 Result 的裸指针，函数一返回全图悬空，症状是
  // validate 时 SIGSEGV，而 stats/search 靠未覆写内存侥幸正常。
  return lineage::PersonGraph::build(std::move(*person_rows),
                                     std::move(*union_rows));
}

}  // namespace genealogy
