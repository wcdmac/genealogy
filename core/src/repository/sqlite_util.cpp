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

Result<void> begin_immediate(sqlite3* db) {
  return exec(db, "BEGIN IMMEDIATE;");
}

Result<void> commit(sqlite3* db) { return exec(db, "COMMIT;"); }

Result<void> rollback(sqlite3* db) { return exec(db, "ROLLBACK;"); }

}  // namespace genealogy::repo_internal
