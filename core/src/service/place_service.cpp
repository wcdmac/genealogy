#include <genealogy/service/place_service.hpp>

#include <genealogy/repository/transaction.hpp>

#include <sqlite3.h>

#include <string>

namespace genealogy {

PlaceService::PlaceService(Database& db) : db_(&db), repo_(db) {}

Result<Place> PlaceService::create_place(const Place& draft) {
  if (draft.name.empty()) return invalid_argument("地名必填");
  return repo_.insert(draft);
}

Result<Place> PlaceService::get_place(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Place>> PlaceService::list_places(
    const PlaceFilter& filter) {
  return repo_.list(filter);
}

Result<Place> PlaceService::update_place(const Place& place) {
  if (place.id.empty()) return invalid_argument("id 必填");
  if (place.name.empty()) return invalid_argument("地名必填");
  return repo_.update(place);
}

namespace {

// 统计指向该地点的引用总数。place_id 在 person/event/union/migration
// 四处被引用且都有 DDL 外键——不做预检时删除会以 500（FK 原文）冒出，
// 与其余服务的 409 口径不符（第四轮 P2-3）。
Result<long> count_place_refs(Database& db, const std::string& place_id) {
  static const char* const kSqls[] = {
      "SELECT COUNT(*) FROM person WHERE birth_place_id=?1"
      " OR death_place_id=?1 OR burial_place_id=?1",
      "SELECT COUNT(*) FROM event WHERE place_id=?1",
      "SELECT COUNT(*) FROM \"union\" WHERE place_id=?1",
      "SELECT COUNT(*) FROM migration WHERE from_place_id=?1"
      " OR to_place_id=?1",
  };
  long total = 0;
  for (const char* sql : kSqls) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db.raw(), sql, -1, &st, nullptr) != SQLITE_OK) {
      return internal("引用计数查询准备失败");
    }
    sqlite3_bind_text(st, 1, place_id.c_str(),
                      static_cast<int>(place_id.size()), SQLITE_TRANSIENT);
    const int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
      total += sqlite3_column_int64(st, 0);
    } else if (rc != SQLITE_DONE) {
      const std::string why = sqlite3_errmsg(db.raw());
      sqlite3_finalize(st);
      return internal("引用计数查询失败: " + why);
    }
    sqlite3_finalize(st);
  }
  return total;
}

}  // namespace

Result<void> PlaceService::remove_place(const std::string& id) {
  // 预检与删除同一写事务，避免并发插入引用造成 TOCTOU 孤儿。
  Transaction tx(*db_);
  if (tx.error()) return tx.error().value();
  auto refs = count_place_refs(*db_, id);
  if (!refs) return refs.error();
  if (*refs > 0) {
    return conflict("地点被 " + std::to_string(*refs) +
                    " 条记录引用（人物生卒葬地/事件/结合/迁徙），"
                    "先解除引用再删除");
  }
  auto removed = repo_.remove(id);
  if (!removed) return removed.error();
  if (auto c = tx.commit(); !c) return c.error();
  return {};
}

}  // namespace genealogy
