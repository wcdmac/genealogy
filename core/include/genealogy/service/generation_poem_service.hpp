#pragma once

// 派语用例层。设计文档 3.2「GenerationPoem 规则引擎」的用例出口。
//
// 三件事：录入/更换派语（联句原文 → 结构化辈字 + 原文留档）、
// 查询派语、全库辈字与世代交叉校验。

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/lineage/generation_poem.hpp>
#include <genealogy/model/generation_poem.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/branch_repository.hpp>
#include <genealogy/repository/generation_poem_repository.hpp>
#include <genealogy/repository/book_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>

namespace genealogy {

class GenerationPoemService {
 public:
  explicit GenerationPoemService(Database& db);

  // 录入/更换派语。text 是联句原文（含标点亦可，引擎只取汉字）。
  // 归属（lineage/branch）必须已存在，否则 NotFound。
  // 同时把原文写回 lineage.generation_poem / 留档（结构化条目是权威）。
  Result<std::vector<GenerationPoemEntry>> set_poem(
      PoemOwner owner, const std::string& owner_id, const std::string& text);

  // 查派语（结构化条目，按辈序）。空 vector = 该归属没有派语。
  Result<std::vector<GenerationPoemEntry>> get_poem(
      PoemOwner owner, const std::string& owner_id);

  // 辈字 ↔ 世代交叉校验。poem_start 是派语第一句对应的世代序号
  // （默认 1 = 始祖辈；支派从第 N 世起定派语时传 N）。
  Result<std::vector<lineage::PoemConflict>> check_consistency(
      PoemOwner owner, const std::string& owner_id, int poem_start = 1);

 private:
  GenerationPoemRepository poems_;
  BookRepository books_;
  BranchRepository branches_;
  PersonRepository persons_;
  UnionRepository unions_;
};

}  // namespace genealogy
