#include <genealogy/repository/union_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/util/time.hpp>
#include <genealogy/ulid.hpp>

#include "sqlite_util.hpp"

namespace genealogy {
namespace {

using repo_internal::Stmt;
using repo_internal::begin_immediate;
using repo_internal::bind_opt_date;
using repo_internal::bind_opt_int;
using repo_internal::bind_opt_text;
using repo_internal::col_opt_date;
using repo_internal::col_opt_int;
using repo_internal::col_opt_int64;
using repo_internal::col_opt_text;
using repo_internal::col_text;
using repo_internal::commit;
using repo_internal::qualify_cols;
using repo_internal::rollback;

constexpr const char* kCols =
    "id, type, status, sequence, start_date, end_date, place_id, note, rev";

enum Col {
  kId = 0,
  kType,
  kStatus,
  kSequence,
  kStartDate,
  kEndDate,
  kPlaceId,
  kNote,
  kRev,
};

UnionType union_type_from_string(const std::string& s) {
  if (s == "concubinage") return UnionType::Concubinage;
  if (s == "remarriage") return UnionType::Remarriage;
  return UnionType::Marriage;
}

UnionStatus union_status_from_string(const std::string& s) {
  return s == "dissolved" ? UnionStatus::Dissolved : UnionStatus::Active;
}

MemberRole member_role_from_string(const std::string& s) {
  if (s == "husband") return MemberRole::Husband;
  if (s == "concubine") return MemberRole::Concubine;
  return MemberRole::Wife;
}

Union row_to_union(sqlite3_stmt* st) {
  Union u;
  u.id = col_text(st, kId);
  u.type = union_type_from_string(col_text(st, kType));
  u.status = union_status_from_string(col_text(st, kStatus));
  u.sequence = col_opt_int(st, kSequence);
  u.start_date = col_opt_date(st, kStartDate);
  u.end_date = col_opt_date(st, kEndDate);
  u.place_id = col_opt_text(st, kPlaceId);
  u.note = col_opt_text(st, kNote);
  u.rev = col_opt_int64(st, kRev).value_or(1);
  return u;
}

void bind_union(sqlite3_stmt* st, const Union& u) {
  int i = 1;
  sqlite3_bind_text(st, i++, u.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, to_string(u.type).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, to_string(u.status).c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_int(st, i++, u.sequence);
  bind_opt_date(st, i++, u.start_date);
  bind_opt_date(st, i++, u.end_date);
  bind_opt_text(st, i++, u.place_id);
  bind_opt_text(st, i++, u.note);
  sqlite3_bind_int64(st, i++, static_cast<std::int64_t>(u.rev));
}

std::vector<UnionMember> load_members(sqlite3* db, const std::string& union_id) {
  std::vector<UnionMember> out;
  Stmt st(db, "SELECT person_id, role FROM union_member WHERE union_id = ?1"
              " ORDER BY rowid");
  if (!st.ok()) return out;
  sqlite3_bind_text(st.get(), 1, union_id.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(st.get()) == SQLITE_ROW) {
    UnionMember m;
    m.person_id = col_text(st.get(), 0);
    m.role = member_role_from_string(col_text(st.get(), 1));
    out.push_back(std::move(m));
  }
  return out;
}

// 事务内整体替换成员；调用方负责 begin/commit。
Result<void> replace_members(sqlite3* db, const std::string& union_id,
                             const std::vector<UnionMember>& members) {
  {
    Stmt del(db, "DELETE FROM union_member WHERE union_id = ?1");
    if (!del.ok()) return internal("准备 DELETE union_member 失败");
    sqlite3_bind_text(del.get(), 1, union_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(del.get()) != SQLITE_DONE) {
      return Error{ErrorCode::Internal,
                   std::string("删除成员失败: ") + sqlite3_errmsg(db)};
    }
  }
  for (const auto& m : members) {
    Stmt ins(db,
             "INSERT INTO union_member(union_id, person_id, role)"
             " VALUES (?1,?2,?3)");
    if (!ins.ok()) return internal("准备 INSERT union_member 失败");
    sqlite3_bind_text(ins.get(), 1, union_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins.get(), 2, m.person_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins.get(), 3, to_string(m.role).c_str(), -1,
                      SQLITE_TRANSIENT);
    if (sqlite3_step(ins.get()) != SQLITE_DONE) {
      return Error{ErrorCode::Internal,
                   std::string("插入成员失败: ") + sqlite3_errmsg(db)};
    }
  }
  return {};
}

}  // namespace

UnionRepository::UnionRepository(Database& db) : db_(db) {}

Result<std::vector<Union>> UnionRepository::list(
    const UnionFilter& filter) const {
  const std::string sql =
      filter.person_id
          ? "SELECT " + qualify_cols(kCols, "u") +
                " FROM \"union\" u JOIN union_member m ON m.union_id = u.id"
                " WHERE (?1 IS NULL OR m.person_id = ?1)"
                "   AND (?2 IS NULL OR u.id > ?2)"
                " ORDER BY u.id LIMIT ?3"
          : std::string("SELECT ").append(kCols) +
                " FROM \"union\""
                " WHERE (?1 IS NULL OR 1=1)"
                "   AND (?2 IS NULL OR id > ?2)"
                " ORDER BY id LIMIT ?3";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  bind_opt_text(st.get(), 1, filter.person_id);
  bind_opt_text(st.get(), 2, filter.cursor);
  sqlite3_bind_int(st.get(), 3, filter.limit > 0 ? filter.limit : 50);

  std::vector<Union> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_union(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Union> UnionRepository::get(const std::string& id) const {
  const std::string sql =
      std::string("SELECT ").append(kCols) + " FROM \"union\" WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_ROW) return not_found("婚姻结合不存在: " + id);
  Union u = row_to_union(st.get());
  u.members = load_members(db_.raw(), id);
  return u;
}

Result<Union> UnionRepository::insert(const Union& u) {
  if (u.members.empty()) return invalid_argument("婚姻结合至少需要一名成员");

  Union copy = u;
  if (copy.id.empty()) copy.id = generate_ulid();
  copy.rev = 1;

  if (auto tr = begin_immediate(db_.raw()); !tr) return tr.error();
  auto rollback_on_fail = [&](const Error& err) -> Result<Union> {
    rollback(db_.raw());
    return err;
  };

  {
    Stmt st(db_.raw(),
            "INSERT INTO \"union\"(id, type, status, sequence, start_date,"
            " end_date, place_id, note, rev)"
            " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9)");
    if (!st.ok()) return rollback_on_fail(internal("准备 INSERT union 失败"));
    bind_union(st.get(), copy);
    if (sqlite3_step(st.get()) != SQLITE_DONE) {
      return rollback_on_fail(Error{ErrorCode::Internal,
                                    std::string("插入结合失败: ") +
                                        sqlite3_errmsg(db_.raw())});
    }
  }
  if (auto r = replace_members(db_.raw(), copy.id, u.members); !r) {
    return rollback_on_fail(r.error());
  }
  if (auto r = commit(db_.raw()); !r) return rollback_on_fail(r.error());
  return copy;
}

Result<Union> UnionRepository::update(const Union& u) {
  auto current = get(u.id);
  if (!current) return current.error();
  if (current->rev != u.rev) {
    return conflict("版本冲突: 提交 rev=" + std::to_string(u.rev) +
                    "，库中 rev=" + std::to_string(current->rev));
  }
  if (u.members.empty()) return invalid_argument("婚姻结合至少需要一名成员");

  if (auto tr = begin_immediate(db_.raw()); !tr) return tr.error();
  auto rollback_on_fail = [&](const Error& err) -> Result<Union> {
    rollback(db_.raw());
    return err;
  };

  {
    Stmt st(db_.raw(),
            "UPDATE \"union\" SET"
            " type=?1, status=?2, sequence=?3, start_date=?4, end_date=?5,"
            " place_id=?6, note=?7, rev=rev+1 WHERE id=?8 AND rev=?9");
    if (!st.ok()) return rollback_on_fail(internal("准备 UPDATE union 失败"));
    int i = 1;
    sqlite3_bind_text(st.get(), i++, to_string(u.type).c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(st.get(), i++, to_string(u.status).c_str(), -1,
                      SQLITE_TRANSIENT);
    bind_opt_int(st.get(), i++, u.sequence);
    bind_opt_date(st.get(), i++, u.start_date);
    bind_opt_date(st.get(), i++, u.end_date);
    bind_opt_text(st.get(), i++, u.place_id);
    bind_opt_text(st.get(), i++, u.note);
    sqlite3_bind_text(st.get(), i++, u.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st.get(), i++, static_cast<std::int64_t>(u.rev));
    if (sqlite3_step(st.get()) != SQLITE_DONE) {
      return rollback_on_fail(Error{ErrorCode::Internal,
                                    std::string("更新结合失败: ") +
                                        sqlite3_errmsg(db_.raw())});
    }
  }
  if (auto r = replace_members(db_.raw(), u.id, u.members); !r) {
    return rollback_on_fail(r.error());
  }
  if (auto r = commit(db_.raw()); !r) return rollback_on_fail(r.error());

  Union out = u;
  out.rev = u.rev + 1;
  return out;
}

Result<void> UnionRepository::remove(const std::string& id) {
  // union_member 无级联删除，先成员后结合，同一事务。
  if (auto tr = begin_immediate(db_.raw()); !tr) return tr.error();
  auto fail = [&](Error err) -> Result<void> {
    rollback(db_.raw());
    return err;
  };
  {
    Stmt st(db_.raw(), "DELETE FROM union_member WHERE union_id = ?1");
    if (!st.ok()) return fail(internal("准备 DELETE union_member 失败"));
    sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st.get()) != SQLITE_DONE) {
      return fail(Error{ErrorCode::Internal,
                        std::string("删除成员失败: ") + sqlite3_errmsg(db_.raw())});
    }
  }
  {
    Stmt st(db_.raw(), "DELETE FROM \"union\" WHERE id = ?1");
    if (!st.ok()) return fail(internal("准备 DELETE union 失败"));
    sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st.get()) != SQLITE_DONE) {
      return fail(Error{ErrorCode::Internal,
                        std::string("删除结合失败: ") + sqlite3_errmsg(db_.raw())});
    }
    if (sqlite3_changes(db_.raw()) == 0) {
      rollback(db_.raw());
      return not_found("婚姻结合不存在: " + id);
    }
  }
  if (auto r = commit(db_.raw()); !r) return r.error();
  return {};
}

}  // namespace genealogy
