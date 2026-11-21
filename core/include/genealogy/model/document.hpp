#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace genealogy {

// 文档类型（document.type，schema CHECK 约束同口径）：谱序/凡例/
// 族规家训/艺文/契约/修次说明——设计文档 1.3「谱序/凡例/家训/艺文/
// 契据」四类核心要素的落点。
enum class DocumentType {
  Preface,      // 谱序（自序/他序/跋）
  Rule,         // 凡例
  Regulation,   // 族规家法/家训
  Literature,   // 艺文（族人著述/诗词/奏疏）
  Contract,     // 契约（祀田/义庄/承嗣/分家文约）
  EditionNote,  // 修次说明（版本考据）
};

std::string to_string(DocumentType type);
std::optional<DocumentType> document_type_from_string(const std::string& s);

// 文档（document 表）：谱书自身的篇章正文。区别于 source（证据来源）；
// 契据的双角色用 source.type='document' 表达。citation 的 subject_type
// 不含 document——文档不是证据锚点，故删除无 409 保护。
// document 表无 rev（篇章正文非谱面实体；乐观锁仅覆盖谱面实体）。
struct Document {
  std::string id;
  DocumentType type = DocumentType::Preface;
  std::string title;  // 必填：《嘉庆四年修谱序》 等
  std::optional<std::string> author;
  std::optional<std::string> date_text;  // 年份照录原文（嘉庆四年 等）
  std::optional<std::string> edition;    // 所属修次（嘉庆四年修 等）
  std::optional<std::string> content;    // 正文全文（FTS 索引列）
  std::optional<std::int64_t> seq;       // 篇序（同类型多篇有序）
};

nlohmann::json to_json(const Document& v);

}  // namespace genealogy
