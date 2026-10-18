#pragma once

// 派语辈字仓储（generation_poem 表）。
//
// 与其它仓储的区别：写入语义是「整份替换」而非单行增删——派语联句是
// 一个整体，谱师录入时给的是一整串字，逐条 insert/update 既难用又容易
// 留下半截。replace_all 在一个事务里删旧写新，失败整体回滚。

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/generation_poem.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

class GenerationPoemRepository {
 public:
  explicit GenerationPoemRepository(Database& db);

  // 按归属列出（按 seq 升序）。owner_id 为空 = 列不出任何东西
  // （归属是必填维度，没有「全部派语」这种查询——UI 不需要）。
  Result<std::vector<GenerationPoemEntry>> list(PoemOwner owner,
                                                const std::string& owner_id)
      const;

  // 整份替换：删掉该归属下全部旧条目，按 entries 重写。
  // entries 为空 = 清空该归属的派语。seq 由调用方给（从 1 起连续），
  // 仓储不重排——辈序是谱学事实，不是存储细节。
  Result<void> replace_all(PoemOwner owner, const std::string& owner_id,
                           const std::vector<GenerationPoemEntry>& entries);

 private:
  Database& db_;
};

}  // namespace genealogy
