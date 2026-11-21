#include <genealogy/repository/person_repository.hpp>

#include <sqlite3.h>

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <genealogy/ulid.hpp>
#include <genealogy/util/time.hpp>

#include "sqlite_util.hpp"

namespace genealogy {
namespace {

// 通用助手（Stmt/col_*/bind_*/quote_fts/qualify_cols）见 sqlite_util.hpp
using repo_internal::Stmt;
using repo_internal::bind_date;
using repo_internal::bind_opt_int;
using repo_internal::bind_opt_text;
using repo_internal::col_date;
using repo_internal::col_opt_int;
using repo_internal::col_opt_text;
using repo_internal::col_text;
using repo_internal::bind_person;
using repo_internal::person_insert_sql;
using repo_internal::qualify_cols;
using repo_internal::quote_fts;

// person 表全列（顺序即行映射索引）
constexpr const char* kSelectCols =
    "id, branch_id, family_name, given_name, courtesy_name,"
    " art_name, posthumous_title, alias, gender, birth_order, generation_no,"
    " is_heir, is_living, privacy_level, birth_date, death_date, birth_place_id,"
    " death_place_id, burial_place_id, biography, father_id, mother_id,"
    " portrait_attachment_id, note, rev, created_at, updated_at";

// 列索引
enum Col {
  kId = 0,
  kBranchId,
  kFamilyName,
  kGivenName,
  kCourtesyName,
  kArtName,
  kPosthumousTitle,
  kAlias,
  kGender,
  kBirthOrder,
  kGenerationNo,
  kIsHeir,
  kIsLiving,
  kPrivacy,
  kBirthDate,
  kDeathDate,
  kBirthPlaceId,
  kDeathPlaceId,
  kBurialPlaceId,
  kBiography,
  kFatherId,
  kMotherId,
  kPortraitAttachmentId,
  kNote,
  kRev,
  kCreatedAt,
  kUpdatedAt,
};

Person row_to_person(sqlite3_stmt* st) {
  Person p;
  p.id = col_text(st, kId);
  p.branch_id = col_opt_text(st, kBranchId);
  p.family_name = col_text(st, kFamilyName);
  p.given_name = col_text(st, kGivenName);
  p.courtesy_name = col_opt_text(st, kCourtesyName);
  p.art_name = col_opt_text(st, kArtName);
  p.posthumous_title = col_opt_text(st, kPosthumousTitle);
  p.alias = col_opt_text(st, kAlias);
  const std::string gender = col_text(st, kGender);
  auto g = gender_from_string(gender);
  p.gender = g ? *g : Gender::Unknown;
  p.birth_order = col_opt_int(st, kBirthOrder);
  p.generation_no = col_opt_int(st, kGenerationNo);
  p.is_heir = sqlite3_column_int(st, kIsHeir) != 0;
  p.is_living = sqlite3_column_int(st, kIsLiving) != 0;
  auto privacy = privacy_from_string(col_text(st, kPrivacy));
  p.privacy = privacy ? *privacy : PrivacyLevel::Kin;
  p.birth_date = col_date(st, kBirthDate);
  if (sqlite3_column_type(st, kDeathDate) != SQLITE_NULL) {
    p.death_date = col_date(st, kDeathDate);
  }
  p.birth_place_id = col_opt_text(st, kBirthPlaceId);
  p.death_place_id = col_opt_text(st, kDeathPlaceId);
  p.burial_place_id = col_opt_text(st, kBurialPlaceId);
  p.biography = col_opt_text(st, kBiography);
  p.father_id = col_opt_text(st, kFatherId);
  p.mother_id = col_opt_text(st, kMotherId);
  p.portrait_attachment_id = col_opt_text(st, kPortraitAttachmentId);
  p.note = col_opt_text(st, kNote);
  p.rev = sqlite3_column_int64(st, kRev);
  p.created_at = col_text(st, kCreatedAt);
  p.updated_at = col_text(st, kUpdatedAt);
  return p;
}

}  // namespace

PersonRepository::PersonRepository(Database& db) : db_(db) {}

Result<std::vector<Person>> PersonRepository::list(
    const PersonFilter& filter) const {
  Stmt st(db_.raw(),
          "SELECT id, branch_id, family_name, given_name,"
          " courtesy_name, art_name, posthumous_title, alias, gender,"
          " birth_order, generation_no, is_heir, is_living, privacy_level,"
          " birth_date, death_date, birth_place_id, death_place_id,"
          " burial_place_id, biography, father_id, mother_id,"
          " portrait_attachment_id, note, rev, created_at, updated_at"
          " FROM person"
          " WHERE (?1 IS NULL OR branch_id = ?1)"
          "   AND (?2 IS NULL OR generation_no = ?2)"
          "   AND (?3 IS NULL OR generation_no >= ?3)"
          "   AND (?4 IS NULL OR generation_no <= ?4)"
          "   AND (?5 IS NULL OR id > ?5)"
          "   AND (?6 IS NULL OR father_id = ?6 OR mother_id = ?6)"
          " ORDER BY id LIMIT ?7");
  if (!st.ok()) return internal("准备查询失败");
  bind_opt_text(st.get(), 1, filter.branch_id);
  bind_opt_int(st.get(), 2, filter.generation_no);
  bind_opt_int(st.get(), 3, filter.generation_from);
  bind_opt_int(st.get(), 4, filter.generation_to);
  bind_opt_text(st.get(), 5, filter.cursor);
  bind_opt_text(st.get(), 6, filter.parent_any);
  sqlite3_bind_int(st.get(), 7, filter.limit > 0 ? filter.limit : 50);

  std::vector<Person> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_person(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Person> PersonRepository::get(const std::string& id) const {
  const std::string sql =
      std::string("SELECT ").append(kSelectCols) + " FROM person WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备查询失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(st.get());
  if (rc == SQLITE_ROW) return row_to_person(st.get());
  if (rc != SQLITE_DONE) {
    return internal(std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw()));
  }
  return not_found("人物不存在: " + id);
}

Result<std::vector<Person>> PersonRepository::search(
    std::string_view keyword, int limit, const std::string& after_id) const {
  if (keyword.empty()) return invalid_argument("检索关键词不能为空");
  // after_id 非空时加 keyset 划分（第三轮新-3）：/search 的游标翻页。
  // 排序必须与划分键同源（ORDER BY p.id）：此前 ORDER BY rank 配
  // id 划分，高 rank 低 id 的命中会跨页重复——e2e 游标翻页当场抓出。
  // 相关性排序让位于确定性翻页（本地工具命中集小，id 序足够）。
  std::string sql = "SELECT " + qualify_cols(kSelectCols, "p") +
                    " FROM person p JOIN person_fts"
                    " ON person_fts.rowid = p.rowid"
                    " WHERE person_fts MATCH ?1";
  if (!after_id.empty()) sql += " AND p.id > ?3";
  sql += " ORDER BY p.id LIMIT ?2";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备检索失败: ") + sqlite3_errmsg(db_.raw())};
  }
  const std::string quoted = quote_fts(keyword);
  sqlite3_bind_text(st.get(), 1, quoted.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st.get(), 2, limit > 0 ? limit : 50);
  if (!after_id.empty()) {
    sqlite3_bind_text(st.get(), 3, after_id.c_str(), -1, SQLITE_TRANSIENT);
  }

  std::vector<Person> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_person(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("检索执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Person> PersonRepository::insert(Person p) {
  if (p.id.empty()) p.id = generate_ulid();
  p.rev = 1;
  p.created_at = utc_now_iso();
  p.updated_at = p.created_at;

  Stmt st(db_.raw(), person_insert_sql());
  if (!st.ok()) return internal("准备 INSERT 失败");
  bind_person(st.get(), p);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return p;
}

Result<Person> PersonRepository::update(const Person& p) {
  auto current = get(p.id);
  if (!current) return current.error();
  if (current->rev != p.rev) {
    return conflict("版本冲突: 提交 rev=" + std::to_string(p.rev) +
                    "，库中 rev=" + std::to_string(current->rev));
  }

  // 占位符须与下面 bind 的连续序号一致（?4 曾缺失导致整体错位一位，
  // WHERE rev=?27 实际绑的是 id —— 乐观锁永远冲突，人物无法更新）。
  Stmt st(db_.raw(),
          "UPDATE person SET"
          " branch_id=?1, family_name=?2, given_name=?3,"
          " courtesy_name=?4, art_name=?5, posthumous_title=?6, alias=?7,"
          " gender=?8, birth_order=?9, generation_no=?10, is_heir=?11,"
          " is_living=?12, privacy_level=?13, birth_date=?14, death_date=?15,"
          " birth_place_id=?16, death_place_id=?17, burial_place_id=?18,"
          " biography=?19, father_id=?20, mother_id=?21,"
          " portrait_attachment_id=?22, note=?23, updated_at=?24, rev=rev+1"
          " WHERE id=?25 AND rev=?26");
  if (!st.ok()) return internal("准备 UPDATE 失败");

  int i = 1;
  bind_opt_text(st.get(), i++, p.branch_id);
  sqlite3_bind_text(st.get(), i++, p.family_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, p.given_name.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st.get(), i++, p.courtesy_name);
  bind_opt_text(st.get(), i++, p.art_name);
  bind_opt_text(st.get(), i++, p.posthumous_title);
  bind_opt_text(st.get(), i++, p.alias);
  sqlite3_bind_text(st.get(), i++, to_string(p.gender).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_opt_int(st.get(), i++, p.birth_order);
  bind_opt_int(st.get(), i++, p.generation_no);
  sqlite3_bind_int(st.get(), i++, p.is_heir ? 1 : 0);
  sqlite3_bind_int(st.get(), i++, p.is_living ? 1 : 0);
  sqlite3_bind_text(st.get(), i++, to_string(p.privacy).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_date(st.get(), i++, p.birth_date);
  if (p.death_date) {
    bind_date(st.get(), i++, *p.death_date);
  } else {
    sqlite3_bind_null(st.get(), i++);
  }
  bind_opt_text(st.get(), i++, p.birth_place_id);
  bind_opt_text(st.get(), i++, p.death_place_id);
  bind_opt_text(st.get(), i++, p.burial_place_id);
  bind_opt_text(st.get(), i++, p.biography);
  bind_opt_text(st.get(), i++, p.father_id);
  bind_opt_text(st.get(), i++, p.mother_id);
  bind_opt_text(st.get(), i++, p.portrait_attachment_id);
  bind_opt_text(st.get(), i++, p.note);
  const std::string now = utc_now_iso();
  sqlite3_bind_text(st.get(), i++, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, p.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st.get(), i++, static_cast<std::int64_t>(p.rev));

  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("更新失败: ") + sqlite3_errmsg(db_.raw())};
  }
  // 上面的 get() 预检与这条 UPDATE 之间，rev 可能已被另一个连接改掉
  // （WAL 多连接并发写完全可能）。UPDATE 按 id+rev 匹配 0 行时绝不能
  // 当成功——那是静默丢失更新外加虚报 rev。重读当前 rev 报冲突，
  // 口径与预检一致（place_repository 的 update 一直有这道检查，
  // 这里补齐同一纪律）。
  if (sqlite3_changes(db_.raw()) == 0) {
    auto now_current = get(p.id);
    const std::int64_t cur =
        now_current ? static_cast<std::int64_t>(now_current->rev) : -1;
    return conflict("版本冲突: 提交 rev=" + std::to_string(p.rev) +
                    "，库中 rev=" + std::to_string(cur));
  }
  Person updated = p;
  updated.rev = p.rev + 1;
  updated.updated_at = now;
  return updated;
}

Result<PersonRepository::RelationRefs> PersonRepository::count_relation_references(
    const std::string& id) const {
  RelationRefs out;
  auto count_one = [&](const char* sql, int* dest) -> Result<void> {
    Stmt st(db_.raw(), sql);
    if (!st.ok()) {
      return internal(std::string("引用清查准备失败: ") +
                      sqlite3_errmsg(db_.raw()));
    }
    sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(st.get());
    if (rc != SQLITE_ROW) {
      return internal(std::string("引用清查失败: ") +
                      sqlite3_errmsg(db_.raw()));
    }
    *dest = sqlite3_column_int(st.get(), 0);
    return {};
  };
  if (auto r = count_one(
          "SELECT COUNT(*) FROM parent_child WHERE parent_id = ?1 OR child_id = ?1",
          &out.parent_child_links);
      !r) {
    return r.error();
  }
  if (auto r = count_one("SELECT COUNT(*) FROM union_member WHERE person_id = ?1",
                         &out.union_memberships);
      !r) {
    return r.error();
  }
  if (auto r = count_one(
          "SELECT COUNT(*) FROM branch WHERE founder_person_id = ?1",
          &out.founded_branches);
      !r) {
    return r.error();
  }
  if (auto r = count_one(
          "SELECT COUNT(*) FROM person WHERE father_id = ?1 OR mother_id = ?1",
          &out.child_refs);
      !r) {
    return r.error();
  }
  return out;
}

Result<void> PersonRepository::remove_names(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM person_name WHERE person_id = ?1");
  if (!st.ok()) return internal("名号清理准备失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return internal(std::string("名号清理失败: ") + sqlite3_errmsg(db_.raw()));
  }
  return {};
}

Result<void> PersonRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM person WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("人物不存在: " + id);
  }
  return {};
}

}  // namespace genealogy
