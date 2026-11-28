#include <genealogy/service/merge_service.hpp>

#include <genealogy/io/backup_service.hpp>
#include <genealogy/repository/transaction.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/ulid.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace genealogy {

namespace fs = std::filesystem;

const char* to_string(MergeConfidence c) {
  switch (c) {
    case MergeConfidence::Strong: return "强烈建议";
    case MergeConfidence::Possible: return "可能重复";
    case MergeConfidence::Unlikely: return "不建议";
    case MergeConfidence::Unknown: break;
  }
  return "无法判断";
}

MergeService::MergeService(Database& db) : db_(&db) {}

namespace {
std::optional<int> birth_year(const Person& p) {
  return p.birth_date.gregorian_year();
}

// ancestor 是否为 of 的祖先（父母链向上 BFS，kMaxHops 只防库中已有环）。
// 合并前必须互查：关系迁移 UPDATE 会把所有 father_id=loser 的行改指
// winner——若 winner 自己的父亲就是 loser，winner 将变成自己的父亲
// （自亲环，外键恰好满足）；更深的祖先链同理成环。
bool is_ancestor(const std::string& ancestor_id, const std::string& of_id,
                 PersonRepository& repo) {
  if (ancestor_id == of_id) return true;
  std::vector<std::string> frontier{of_id};
  std::set<std::string> visited{of_id};
  constexpr int kMaxHops = 1000;
  for (int hops = 0; !frontier.empty() && hops < kMaxHops; ++hops) {
    auto p = repo.get(frontier.back());
    frontier.pop_back();
    if (!p) continue;
    for (const auto* next : {&p->father_id, &p->mother_id}) {
      if (!*next || visited.count(**next)) continue;
      if (**next == ancestor_id) return true;
      visited.insert(**next);
      frontier.push_back(**next);
    }
  }
  return false;
}
}  // namespace

Result<std::vector<DuplicatePair>> MergeService::detect(int limit) const {
  // 负数 limit 无意义：回退默认值而非返回空集（与注释口径一致）。
  if (limit < 0) limit = 100;
  PersonRepository repo(*db_);
  PersonFilter f;
  f.limit = 200000;
  auto all = repo.list(f);
  if (!all) return all.error();

  std::map<std::string, std::vector<const Person*>> by_name;
  for (const auto& p : *all)
    by_name[p.family_name + "|" + p.given_name].push_back(&p);

  std::vector<DuplicatePair> out;
  for (const auto& [name, group] : by_name) {
    (void)name;
    for (std::size_t i = 0; i < group.size() && out.size() < limit; ++i) {
      for (std::size_t j = i + 1; j < group.size() && out.size() < limit; ++j) {
        const auto& a = *group[i];
        const auto& b = *group[j];
        DuplicatePair pair;
        pair.winner_id = a.id;
        pair.loser_id = b.id;
        pair.winner_name = a.family_name + a.given_name;
        pair.loser_name = b.family_name + b.given_name;
        const auto ya = birth_year(a), yb = birth_year(b);
        if (ya && yb && *ya != *yb) {
          pair.confidence = MergeConfidence::Unlikely;
          pair.reason = "出生年不同（" + std::to_string(*ya) + " / " +
                        std::to_string(*yb) + "）";
        } else if (ya && yb && *ya == *yb) {
          pair.confidence = MergeConfidence::Strong;
          pair.reason = "同名且出生年一致（" + std::to_string(*ya) + "）";
        } else {
          pair.confidence = MergeConfidence::Possible;
          pair.reason = "同名但出生年缺失，需人工核对";
        }
        out.push_back(std::move(pair));
      }
    }
  }
  return out;
}

Result<std::vector<MergeConflict>> MergeService::inspect_conflicts(
    const Person& winner, const Person& loser) const {
  std::vector<MergeConflict> out;
  auto check = [&](const char* field, const std::string& a,
                   const std::string& b) {
    if (!a.empty() && !b.empty() && a != b)
      out.push_back({field, a, b});
  };
  check("父亲", winner.father_id.value_or(""), loser.father_id.value_or(""));
  check("母亲", winner.mother_id.value_or(""), loser.mother_id.value_or(""));
  check("字", winner.courtesy_name.value_or(""), loser.courtesy_name.value_or(""));
  check("生年",
        winner.birth_date.gregorian ? *winner.birth_date.gregorian : std::string{},
        loser.birth_date.gregorian ? *loser.birth_date.gregorian : std::string{});
  return out;
}

Result<std::vector<MergeConflict>> MergeService::inspect(
    const std::string& winner_id, const std::string& loser_id) const {
  PersonRepository repo(*db_);
  auto winner = repo.get(winner_id);
  if (!winner) return winner.error();
  auto loser = repo.get(loser_id);
  if (!loser) return loser.error();
  return inspect_conflicts(*winner, *loser);
}

Result<MergeResult> MergeService::merge(const std::string& winner_id,
                                        const std::string& loser_id,
                                        bool dry_run,
                                        const std::string& backup_dir) {
  PersonRepository repo(*db_);
  auto winner = repo.get(winner_id);
  if (!winner) return winner.error();
  auto loser = repo.get(loser_id);
  if (!loser) return loser.error();
  if (winner_id == loser_id)
    return Error{ErrorCode::InvalidArgument, "不能与自己合并"};

  // 亲缘互查（见 is_ancestor 注释）：无论谁是谁的祖先，关系迁移都会
  // 制造祖先环。先解除亲子关系再合并。
  if (is_ancestor(loser_id, winner_id, repo) ||
      is_ancestor(winner_id, loser_id, repo))
    return Error{ErrorCode::Conflict,
                 "两人存在直系血缘，不可直接合并；请先解除亲子关系"};

  // 同一结合的两位成员不可直接合并（第四轮 P3）：union_member 主键
  // (union_id, person_id) 会让成员迁移 UPDATE 撞键，以 IoError+SQLite
  // 原文冒出。先给出人话 409。
  {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(
            db_->raw(),
            "SELECT COUNT(*) FROM union_member a JOIN union_member b"
            " ON a.union_id=b.union_id WHERE a.person_id=?1 AND b.person_id=?2",
            -1, &st, nullptr) != SQLITE_OK)
      return Error{ErrorCode::IoError, "结合成员检查准备失败"};
    sqlite3_bind_text(st, 1, winner_id.c_str(),
                      static_cast<int>(winner_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, loser_id.c_str(),
                      static_cast<int>(loser_id.size()), SQLITE_TRANSIENT);
    const bool row = sqlite3_step(st) == SQLITE_ROW;
    const long shared = row ? sqlite3_column_int64(st, 0) : -1;
    const std::string err = row ? "" : sqlite3_errmsg(db_->raw());
    sqlite3_finalize(st);
    if (!row) return Error{ErrorCode::Internal, "结合成员检查失败: " + err};
    if (shared > 0) {
      return Error{ErrorCode::Conflict,
                   "两人在同一结合中互为成员（如夫妻），不可直接合并；"
                   "请先解除其中一方的结合成员身份"};
    }
  }

  MergeResult result;
  auto conflicts = inspect_conflicts(*winner, *loser);
  if (!conflicts) return conflicts.error();
  if (dry_run) {
    result.conflicts = std::move(*conflicts);
    return result;
  }
  // 冲突不静默覆盖：返回冲突清单并拒绝写库；GUI 先呈现由人决定。
  // preview（dry_run）也走同一分支，故 GUI 总能拿到冲突。
  if (!conflicts->empty()) {
    result.conflicts = std::move(*conflicts);
    return Error{ErrorCode::Conflict, "存在字段冲突，请人工核对后再合并"};
  }

  // 合并不不可逆：快照失败必须中止，不能"承诺有撤销点却没留"。
  if (!backup_dir.empty()) {
    fs::path dir(backup_dir);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
      return Error{ErrorCode::IoError,
                   "合并前无法创建备份目录: " + ec.message()};
    const fs::path snap = dir /
        ("genealogy-merge-" + generate_ulid() + ".db");
    if (auto s = io::snapshot_database(*db_, snap); !s)
      return Error{ErrorCode::IoError,
                   "合并前快照失败，已中止合并: " + s.error().message()};
    result.backup_path = snap.string();
  }

  // 全部写操作在同一事务内：任一步失败由析构回滚，不留半套合并。
  // Transaction 内部已持 write_mutex，无需再手动 unique_lock。
  Transaction transaction(*db_);
  if (transaction.error()) return transaction.error().value();
  const char* sqls[] = {
      "UPDATE parent_child SET parent_id=? WHERE parent_id=?",
      "UPDATE union_member SET person_id=? WHERE person_id=?",
      "UPDATE event SET subject_id=? WHERE subject_type='person' AND subject_id=?",
      "UPDATE migration SET subject_id=? WHERE subject_type='person' AND subject_id=?",
      "UPDATE citation SET subject_id=? WHERE subject_type='person' AND subject_id=?",
      "UPDATE attachment SET subject_id=? WHERE subject_type='person' AND subject_id=?",
      "UPDATE person_name SET person_id=? WHERE person_id=?",
      "UPDATE person SET father_id=? WHERE father_id=?",
      "UPDATE person SET mother_id=? WHERE mother_id=?",
      "UPDATE branch SET founder_person_id=? WHERE founder_person_id=?",
  };
  for (const char* sql : sqls) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_->raw(), sql, -1, &st, nullptr) != SQLITE_OK)
      return Error{ErrorCode::IoError, "合并语句准备失败"};
    sqlite3_bind_text(st, 1, winner_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, loser_id.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    const std::string err = ok ? "" : sqlite3_errmsg(db_->raw());
    sqlite3_finalize(st);
    if (!ok) return Error{ErrorCode::IoError, "关系迁移失败: " + err};
  }
  // parent_child 可能出现重复（同一对父母），去重
  {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "DELETE FROM parent_child WHERE id NOT IN "
        "(SELECT MIN(id) FROM parent_child GROUP BY child_id,parent_id,relation)";
    if (sqlite3_prepare_v2(db_->raw(), sql, -1, &st, nullptr) == SQLITE_OK) {
      const int rc = sqlite3_step(st);
      const std::string err = (rc == SQLITE_DONE) ? "" : sqlite3_errmsg(db_->raw());
      sqlite3_finalize(st);
      if (rc != SQLITE_DONE)
        return Error{ErrorCode::IoError, "亲子关系去重失败: " + err};
    }
  }
  auto removed = repo.remove(loser_id);
  if (!removed) return removed.error();
  if (auto c = transaction.commit(); !c) return c.error();
  return result;
}

}  // namespace genealogy
