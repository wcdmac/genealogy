#include <genealogy/repository/transaction.hpp>
#include <genealogy/service/event_service.hpp>

#include <genealogy/ulid.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>

namespace genealogy {

EventService::EventService(Database& db)
    : db_(&db), repo_(db),
      persons_(db),
      unions_(db),
      branches_(db),
      places_(db) {}

Error EventService::validate(const Event& e) {
  if (e.subject_id.empty()) return invalid_argument("subject_id 必填");
  // 谱本体事件（Book 与历史 Lineage 兼容枚举）的 subject_id 必须是字面量
  // "book"——lineage 表已在 0007 删除，允许任意 ULID 会制造指向不存在
  // 实体的孤儿事件，重新破坏 0008 的修复。
  const bool book_subject = (e.subject_type == EventSubjectType::Book ||
                             e.subject_type == EventSubjectType::Lineage);
  if (book_subject) {
    if (e.subject_id != "book")
      return invalid_argument("谱本体事件的 subject_id 必须是 book");
  } else if (!is_valid_ulid(e.subject_id)) {
    return invalid_argument("subject_id 不是合法 ULID: " + e.subject_id);
  }

  // 主体存在性：按 subject_type 分派
  switch (e.subject_type) {
    case EventSubjectType::Person: {
      auto subject = persons_.get(e.subject_id);
      if (!subject) return not_found("事件主体（人物）不存在: " + e.subject_id);
      break;
    }
    case EventSubjectType::Union: {
      auto subject = unions_.get(e.subject_id);
      if (!subject) return not_found("事件主体（结合）不存在: " + e.subject_id);
      break;
    }
    case EventSubjectType::Branch: {
      auto subject = branches_.get(e.subject_id);
      if (!subject) return not_found("事件主体（支派）不存在: " + e.subject_id);
      break;
    }
    case EventSubjectType::Lineage:
    case EventSubjectType::Book: {
      // 单谱单库：谱本体事件无主体表可查（lineage 历史兼容 / book 即谱本体）
      break;
    }
  }

  if (e.place_id && !e.place_id->empty()) {
    auto place = places_.get(*e.place_id);
    if (!place) return not_found("事件地点不存在: " + *e.place_id);
  }
  return {};
}

Result<Event> EventService::create_event(const Event& draft) {
  Event normalized = draft;
  if (normalized.place_id && normalized.place_id->empty())
    normalized.place_id.reset();
  if (auto err = validate(normalized); err) return err;
  return repo_.insert(normalized);
}

Result<std::vector<Event>> EventService::list_events(
    const EventFilter& filter) {
  return repo_.list(filter);
}

Result<Event> EventService::get_event(const std::string& id) {
  return repo_.get(id);
}

Result<void> EventService::remove_event(const std::string& id) {
  // 预检与删除必须在同一写事务内，否则并发插入引用会留下孤儿（TOCTOU）。
  Transaction tx(*db_);
  if (tx.error()) return tx.error().value();
  // ROADMAP M7.2：删除被引事实时 409——事件被证据引用则拒绝，
  // 引用解除（删 citation）后即可删除。
  CitationRepository citations(*db_);
  auto cn = citations.count_for_subject(CitationSubjectType::Event, id);
  if (!cn) return cn.error();
  const int n = *cn;
  if (n > 0) {
    return conflict("事件被 " + std::to_string(n) +
                   " 条证据引用，先解除引用再删除");
  }
  auto removed = repo_.remove(id);
  if (!removed) return removed.error();
  if (auto c = tx.commit(); !c) return c.error();
  return {};
}

}  // namespace genealogy
