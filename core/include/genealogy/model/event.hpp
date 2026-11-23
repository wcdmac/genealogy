#pragma once

#include <optional>
#include <string>

#include <genealogy/model/genealogy_date.hpp>

#include <nlohmann/json.hpp>

namespace genealogy {

enum class EventSubjectType { Person, Union, Branch, Lineage, Book };

// 事件类型与 DDL CHECK 约束一一对应（0001_init.sql）。
enum class EventType {
  Birth,
  Death,
  Burial,
  Marriage,
  Migration,
  Examination,
  Office,
  Honor,
  Residence,
  Annotation,
};

std::string to_string(EventSubjectType type);
std::string to_string(EventType type);
std::optional<EventSubjectType> event_subject_from_string(const std::string& s);
std::optional<EventType> event_type_from_string(const std::string& s);

// 事件（event 表）：事实 = 时间 + 地点 + 主体 + 描述。
struct Event {
  std::string id;
  EventSubjectType subject_type = EventSubjectType::Person;
  std::string subject_id;
  EventType type = EventType::Annotation;
  std::optional<GenealogyDate> date;
  std::optional<std::string> place_id;
  std::optional<std::string> description;
  std::string created_at;
};

nlohmann::json to_json(const Event& v);

}  // namespace genealogy
