#pragma once

#include <optional>
#include <string>

#include <genealogy/model/genealogy_date.hpp>
#include <nlohmann/json.hpp>

namespace genealogy {

// 迁徙主体：人（某次迁居）或支派（整支外迁）。
enum class MigrationSubjectType {
  Person,
  Branch,
};

std::string to_string(MigrationSubjectType type);
std::optional<MigrationSubjectType> migration_subject_from_string(
    const std::string& s);

// 迁徙记录（migration 表）：从甲地到乙地的一次迁移。
// date 用 GenealogyDate（与 event 同口径：谱面原文优先，禁止裸串）。
struct Migration {
  std::string id;
  MigrationSubjectType subject_type = MigrationSubjectType::Person;
  std::string subject_id;
  std::optional<std::string> from_place_id;
  std::optional<std::string> to_place_id;
  std::optional<GenealogyDate> date;
  std::optional<std::string> reason;  // 迁因（经商/避乱/仕进/投亲…）
  std::optional<std::string> route;   // 迁经路线（文字描述）
  std::optional<std::string> note;
};

nlohmann::json to_json(const Migration& v);

}  // namespace genealogy
