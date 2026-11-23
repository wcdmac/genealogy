#pragma once

// 重复人物检测与合并（借鉴 FamilySearch 并排合并与四级建议）。
//
// 合并语义：把 loser 的从属关系迁到 winner，再删除 loser。
// 冲突（如两人父亲不同）不静默覆盖，作为字段级冲突返回，由 GUI 呈现。
// 合并前自动留 VACUUM INTO 快照，调用方可据此短时撤销。

#include <genealogy/error.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/database.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace genealogy {

enum class MergeConfidence { Strong, Possible, Unlikely, Unknown };

struct DuplicatePair {
  std::string winner_id;
  std::string loser_id;
  std::string winner_name;
  std::string loser_name;
  MergeConfidence confidence = MergeConfidence::Unknown;
  std::string reason;
};

struct MergeConflict {
  std::string field;
  std::string winner_value;
  std::string loser_value;
};

struct MergeResult {
  std::string backup_path;          // 合并前快照（撤销用）
  std::vector<MergeConflict> conflicts;
};

class MergeService {
 public:
  explicit MergeService(Database& db);

  // 疑似重复检测：同名（姓+名）且出生年不冲突（或皆空）者成对返回。
  Result<std::vector<DuplicatePair>> detect(int limit = 100) const;

  // 只检查冲突（不写库），供 GUI 并排对照呈现。
  Result<std::vector<MergeConflict>> inspect(const std::string& winner_id,
                                             const std::string& loser_id) const;

  // 合并 loser → winner。dry_run=true 只返回会发生的冲突，不写库。
  // backup_dir 非空则先做 VACUUM INTO 快照（用于撤销）。
  Result<MergeResult> merge(const std::string& winner_id,
                            const std::string& loser_id, bool dry_run,
                            const std::string& backup_dir);

 private:
  Database* db_;
  Result<std::vector<MergeConflict>> inspect_conflicts(
      const Person& winner, const Person& loser) const;
};

const char* to_string(MergeConfidence c);

}  // namespace genealogy
