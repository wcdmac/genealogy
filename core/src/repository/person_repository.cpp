#include <genealogy/repository/person_repository.hpp>

#include <sqlite3.h>

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <genealogy/ulid.hpp>
#include <genealogy/util/time.hpp>

namespace genealogy {
namespace {

// person 表全列（顺序即行映射索引）
constexpr const char* kSelectCols =
    "id, lineage_id, branch_id, family_name, given_name, courtesy_name,"
    " art_name, posthumous_title, alias, gender, birth_order, generation_no,"
    " is_heir, is_living, privacy_level, birth_date, death_date, birth_place_id,"
    " death_place_id, burial_place_id, biography, father_id, mother_id,"
    " portrait_attachment_id, note, rev, created_at, updated_at";

// 列索引
enum Col {
  kId = 0,
  kLineageId,
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

class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) {
    st_ = sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) == SQLITE_OK ? st_
                                                                     : nullptr;
  }
  ~Stmt() {
    if (st_ != nullptr) sqlite3_finalize(st_);
  }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  sqlite3_stmt* get() const noexcept { return st_; }
  bool ok() const noexcept { return st_ != nullptr; }

 private:
  sqlite3_stmt* st_ = nullptr;
};

std::string col_text(sqlite3_stmt* st, int col) {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(st, col));
  return text != nullptr ? std::string(text) : std::string();
}

std::optional<std::string> col_opt_text(sqlite3_stmt* st, int col) {
  if (sqlite3_column_type(st, col) == SQLITE_NULL) return std::nullopt;
  return col_text(st, col);
}

std::optional<int> col_opt_int(sqlite3_stmt* st, int col) {
  if (sqlite3_column_type(st, col) == SQLITE_NULL) return std::nullopt;
  return sqlite3_column_int(st, col);
}

GenealogyDate col_date(sqlite3_stmt* st, int col) {
  const auto raw = col_opt_text(st, col);
  if (!raw) return {};
  const auto j = nlohmann::json::parse(*raw, nullptr, false);
  if (j.is_discarded()) return {};
  auto d = GenealogyDate::from_json(j);
  return d ? std::move(*d) : GenealogyDate{};
}

Person row_to_person(sqlite3_stmt* st) {
  Person p;
  p.id = col_text(st, kId);
  p.lineage_id = col_text(st, kLineageId);
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

void bind_opt_text(sqlite3_stmt* st, int idx, const std::optional<std::string>& v) {
  if (v) {
    sqlite3_bind_text(st, idx, v->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st, idx);
  }
}

void bind_opt_int(sqlite3_stmt* st, int idx, const std::optional<int>& v) {
  if (v) {
    sqlite3_bind_int(st, idx, *v);
  } else {
    sqlite3_bind_null(st, idx);
  }
}

void bind_date(sqlite3_stmt* st, int idx, const GenealogyDate& d) {
  const std::string s = d.to_json().dump();
  sqlite3_bind_text(st, idx, s.c_str(), -1, SQLITE_TRANSIENT);
}

void bind_person(sqlite3_stmt* st, const Person& p) {
  int i = 1;
  sqlite3_bind_text(st, i++, p.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, p.lineage_id.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st, i++, p.branch_id);
  sqlite3_bind_text(st, i++, p.family_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, p.given_name.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st, i++, p.courtesy_name);
  bind_opt_text(st, i++, p.art_name);
  bind_opt_text(st, i++, p.posthumous_title);
  bind_opt_text(st, i++, p.alias);
  sqlite3_bind_text(st, i++, to_string(p.gender).c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_int(st, i++, p.birth_order);
  bind_opt_int(st, i++, p.generation_no);
  sqlite3_bind_int(st, i++, p.is_heir ? 1 : 0);
  sqlite3_bind_int(st, i++, p.is_living ? 1 : 0);
  sqlite3_bind_text(st, i++, to_string(p.privacy).c_str(), -1, SQLITE_TRANSIENT);
  bind_date(st, i++, p.birth_date);
  if (p.death_date) {
    bind_date(st, i++, *p.death_date);
  } else {
    sqlite3_bind_null(st, i++);
  }
  bind_opt_text(st, i++, p.birth_place_id);
  bind_opt_text(st, i++, p.death_place_id);
  bind_opt_text(st, i++, p.burial_place_id);
  bind_opt_text(st, i++, p.biography);
  bind_opt_text(st, i++, p.father_id);
  bind_opt_text(st, i++, p.mother_id);
  bind_opt_text(st, i++, p.portrait_attachment_id);
  bind_opt_text(st, i++, p.note);
  sqlite3_bind_int64(st, i++, static_cast<std::int64_t>(p.rev));
  sqlite3_bind_text(st, i++, p.created_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, p.updated_at.c_str(), -1, SQLITE_TRANSIENT);
}

// FTS5 MATCH 语法对原文关键词做双引号转义（字符串字面量，无语法歧义）
std::string quote_fts(std::string_view keyword) {
  std::string out = "\"";
  for (const char c : keyword) {
    if (c == '"') {
      out += "\"\"";
    } else {
      out += c;
    }
  }
  out += "\"";
  return out;
}

// 把 "a, b, c" 形式的列清单转换为 "alias.a, alias.b, alias.c"。
// person_fts 与 person 存在同名列，JOIN 检索时必须限定，否则 SQLite
// 报 ambiguous column name（prepare 即失败）。
std::string qualify_cols(const char* cols, const char* alias) {
  std::string out = alias;
  out += '.';
  for (const char* p = cols; *p != '\0'; ++p) {
    out += *p;
    if (*p == ',' && *(p + 1) == ' ') {
      out += ' ';
      out += alias;
      out += '.';
      ++p;
    }
  }
  return out;
}

}  // namespace

PersonRepository::PersonRepository(Database& db) : db_(db) {}

Result<std::vector<Person>> PersonRepository::list(
    const PersonFilter& filter) const {
  Stmt st(db_.raw(),
          "SELECT id, lineage_id, branch_id, family_name, given_name,"
          " courtesy_name, art_name, posthumous_title, alias, gender,"
          " birth_order, generation_no, is_heir, is_living, privacy_level,"
          " birth_date, death_date, birth_place_id, death_place_id,"
          " burial_place_id, biography, father_id, mother_id,"
          " portrait_attachment_id, note, rev, created_at, updated_at"
          " FROM person"
          " WHERE (?1 IS NULL OR branch_id = ?1)"
          "   AND (?2 IS NULL OR generation_no = ?2)"
          "   AND (?3 IS NULL OR id > ?3)"
          " ORDER BY id LIMIT ?4");
  if (!st.ok()) return internal("准备查询失败");
  bind_opt_text(st.get(), 1, filter.branch_id);
  bind_opt_int(st.get(), 2, filter.generation_no);
  bind_opt_text(st.get(), 3, filter.cursor);
  sqlite3_bind_int(st.get(), 4, filter.limit > 0 ? filter.limit : 50);

  std::vector<Person> out;
  while (sqlite3_step(st.get()) == SQLITE_ROW) {
    out.push_back(row_to_person(st.get()));
  }
  return out;
}

Result<Person> PersonRepository::get(const std::string& id) const {
  const std::string sql =
      std::string("SELECT ").append(kSelectCols) + " FROM person WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备查询失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) return row_to_person(st.get());
  return not_found("人物不存在: " + id);
}

Result<std::vector<Person>> PersonRepository::search(std::string_view keyword,
                                                     int limit) const {
  if (keyword.empty()) return invalid_argument("检索关键词不能为空");
  const std::string sql = "SELECT " + qualify_cols(kSelectCols, "p") +
                          " FROM person p JOIN person_fts"
                          " ON person_fts.rowid = p.rowid"
                          " WHERE person_fts MATCH ?1 ORDER BY rank LIMIT ?2";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备检索失败: ") + sqlite3_errmsg(db_.raw())};
  }
  const std::string quoted = quote_fts(keyword);
  sqlite3_bind_text(st.get(), 1, quoted.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st.get(), 2, limit > 0 ? limit : 50);

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
  p.rev = 1;
  p.created_at = utc_now_iso();
  p.updated_at = p.created_at;

  Stmt st(db_.raw(),
          "INSERT INTO person(id, lineage_id, branch_id, family_name,"
          " given_name, courtesy_name, art_name, posthumous_title, alias,"
          " gender, birth_order, generation_no, is_heir, is_living, privacy_level,"
          " birth_date, death_date, birth_place_id, death_place_id,"
          " burial_place_id, biography, father_id, mother_id,"
          " portrait_attachment_id, note, rev, created_at, updated_at)"
          " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,"
          "?17,?18,?19,?20,?21,?22,?23,?24,?25,?26,?27,?28)");
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

  Stmt st(db_.raw(),
          "UPDATE person SET"
          " lineage_id=?1, branch_id=?2, family_name=?3, given_name=?4,"
          " courtesy_name=?5, art_name=?6, posthumous_title=?7, alias=?8,"
          " gender=?9, birth_order=?10, generation_no=?11, is_heir=?12,"
          " is_living=?13, privacy_level=?14, birth_date=?15, death_date=?16,"
          " birth_place_id=?17, death_place_id=?18, burial_place_id=?19,"
          " biography=?20, father_id=?21, mother_id=?22,"
          " portrait_attachment_id=?23, note=?24, updated_at=?25, rev=rev+1"
          " WHERE id=?26 AND rev=?27");
  if (!st.ok()) return internal("准备 UPDATE 失败");

  int i = 1;
  sqlite3_bind_text(st.get(), i++, p.lineage_id.c_str(), -1, SQLITE_TRANSIENT);
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
  Person updated = p;
  updated.rev = p.rev + 1;
  updated.updated_at = now;
  return updated;
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
