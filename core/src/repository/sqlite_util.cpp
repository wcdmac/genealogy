#include "sqlite_util.hpp"

#include <nlohmann/json.hpp>

namespace genealogy::repo_internal {

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

std::optional<std::int64_t> col_opt_int64(sqlite3_stmt* st, int col) {
  if (sqlite3_column_type(st, col) == SQLITE_NULL) return std::nullopt;
  return static_cast<std::int64_t>(sqlite3_column_int64(st, col));
}

std::optional<double> col_opt_double(sqlite3_stmt* st, int col) {
  if (sqlite3_column_type(st, col) == SQLITE_NULL) return std::nullopt;
  return sqlite3_column_double(st, col);
}

void bind_opt_text(sqlite3_stmt* st, int idx,
                   const std::optional<std::string>& v) {
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

void bind_opt_double(sqlite3_stmt* st, int idx, const std::optional<double>& v) {
  if (v) {
    sqlite3_bind_double(st, idx, *v);
  } else {
    sqlite3_bind_null(st, idx);
  }
}

void bind_date(sqlite3_stmt* st, int idx, const GenealogyDate& d) {
  const std::string s = d.to_json().dump();
  sqlite3_bind_text(st, idx, s.c_str(), -1, SQLITE_TRANSIENT);
}

void bind_opt_date(sqlite3_stmt* st, int idx,
                   const std::optional<GenealogyDate>& d) {
  if (d) {
    bind_date(st, idx, *d);
  } else {
    sqlite3_bind_null(st, idx);
  }
}

GenealogyDate col_date(sqlite3_stmt* st, int col) {
  const auto raw = col_opt_text(st, col);
  if (!raw) return {};
  // 已知取舍：库中日期 JSON 损坏（外部工具改库/历史 bug）时静默返回
  // 空值——「曾登记过日期」这一事实丢失，校验规则（R10/R12）无法感知。
  // 不这样做的代价是读路径全线上抬错误通道：col_date 是所有实体行
  // 映射的公共件，一处损坏会让整个列表 500。仓储层也没有日志设施
  // （spdlog 暂缓，见根 CMakeLists）可供留痕。列入 ROADMAP 暂缓项，
  // 日志落地时改为「返回错误 + 记日志」。
  const auto j = nlohmann::json::parse(*raw, nullptr, false);
  if (j.is_discarded()) return {};
  auto d = GenealogyDate::from_json(j);
  return d ? std::move(*d) : GenealogyDate{};
}

std::optional<GenealogyDate> col_opt_date(sqlite3_stmt* st, int col) {
  if (sqlite3_column_type(st, col) == SQLITE_NULL) return std::nullopt;
  return col_date(st, col);
}

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

Result<void> exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err != nullptr ? err : "unknown error";
    sqlite3_free(err);
    return Error{ErrorCode::Internal, msg};
  }
  return {};
}

// 事务助手是**嵌套感知**的：已在外层事务中时，begin 是空操作，commit 与
// rollback 也不动作（由最外层收尾）。这样仓储内部「多表必须同事务」的写法
// （如 union + union_member）可以原样嵌进调用方开的大事务里，
// 不会撞 "cannot start a transaction within a transaction"。
Result<void> begin_immediate(sqlite3* db) {
  if (sqlite3_get_autocommit(db) == 0) return {};
  return exec(db, "BEGIN IMMEDIATE;");
}

Result<void> commit(sqlite3* db) {
  if (sqlite3_get_autocommit(db) != 0) return {};
  return exec(db, "COMMIT;");
}

Result<void> rollback(sqlite3* db) {
  if (sqlite3_get_autocommit(db) != 0) return {};
  return exec(db, "ROLLBACK;");
}


// ---- 主表 INSERT ----

const char* person_insert_sql() noexcept {
  return "INSERT INTO person(id, lineage_id, branch_id, family_name,"
         " given_name, courtesy_name, art_name, posthumous_title, alias,"
         " gender, birth_order, generation_no, is_heir, is_living, privacy_level,"
         " birth_date, death_date, birth_place_id, death_place_id,"
         " burial_place_id, biography, father_id, mother_id,"
         " portrait_attachment_id, note, rev, created_at, updated_at)"
         " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,"
         "?17,?18,?19,?20,?21,?22,?23,?24,?25,?26,?27,?28)";
}

const char* place_insert_sql() noexcept {
  return "INSERT INTO place(id, name, type, modern_admin, historical_name,"
         " lat, lng, note) VALUES (?1,?2,?3,?4,?5,?6,?7,?8)";
}

void bind_place(sqlite3_stmt* st, const Place& p) {
  int i = 1;
  sqlite3_bind_text(st, i++, p.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, p.name.c_str(), -1, SQLITE_TRANSIENT);
  if (p.type) {
    sqlite3_bind_text(st, i++, to_string(*p.type).c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st, i++);
  }
  bind_opt_text(st, i++, p.modern_admin);
  bind_opt_text(st, i++, p.historical_name);
  bind_opt_double(st, i++, p.lat);
  bind_opt_double(st, i++, p.lng);
  bind_opt_text(st, i++, p.note);
}

const char* union_insert_sql() noexcept {
  return "INSERT INTO \"union\"(id, type, status, sequence, start_date,"
         " end_date, place_id, note, rev)"
         " VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9)";
}

const char* union_member_insert_sql() noexcept {
  return "INSERT INTO union_member(union_id, person_id, role)"
         " VALUES (?1,?2,?3)";
}

const char* event_insert_sql() noexcept {
  return "INSERT INTO event(id, subject_type, subject_id, type, date, place_id,"
         " description, created_at) VALUES (?1,?2,?3,?4,?5,?6,?7,?8)";
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
  sqlite3_bind_text(st, i++, to_string(p.privacy).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_date(st, i++, p.birth_date);
  bind_opt_date(st, i++, p.death_date);
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

void bind_event(sqlite3_stmt* st, const Event& e) {
  int i = 1;
  sqlite3_bind_text(st, i++, e.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, to_string(e.subject_type).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, e.subject_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, to_string(e.type).c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_date(st, i++, e.date);
  bind_opt_text(st, i++, e.place_id);
  bind_opt_text(st, i++, e.description);
  sqlite3_bind_text(st, i++, e.created_at.c_str(), -1, SQLITE_TRANSIENT);
}

void bind_union_member(sqlite3_stmt* st, const std::string& union_id,
                       const UnionMember& m) {
  sqlite3_bind_text(st, 1, union_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, m.person_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, to_string(m.role).c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace genealogy::repo_internal
