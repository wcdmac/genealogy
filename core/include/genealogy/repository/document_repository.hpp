#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/document.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct DocumentFilter {
  std::optional<DocumentType> type;
  int limit = 50;
  std::optional<std::string> cursor;
};

class DocumentRepository {
 public:
  explicit DocumentRepository(Database& db);

  Result<std::vector<Document>> list(const DocumentFilter& filter) const;

  // 正文检索（ROADMAP 7.4 验收门「文档可按正文检索命中」）。
  // trigram FTS 对中文子串天然友好；查询不足 3 个 unicode 字符时
  // MATCH 不命中，回退 title/content 的 LIKE 全扫（文档量级几百篇，
  // <50ms 门槛内）。命中按 FTS rank 排序。
  // after_id：keyset 游标（第三轮新-3，语义同 PersonRepository::search）
  Result<std::vector<Document>> search(std::string_view keyword,
                                       int limit,
                                       const std::string& after_id = {}) const;

  Result<Document> get(const std::string& id) const;
  Result<Document> insert(Document doc);
  Result<Document> update(const Document& doc);
  Result<void> remove(const std::string& id);

 private:
  Database& db_;
};

}  // namespace genealogy
