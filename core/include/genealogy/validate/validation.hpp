#pragma once

// 一致性校验规则引擎（设计文档 3.2 `validate` 模块）。
//
// 设计取舍：
//   · 规则的「阈值 / 文案 / 等级 / 是否启用」全在 shared/rules/validation.json，
//     构建期嵌入二进制，换规则不改代码（ADR-6 规则外置）；
//   · 规则的「判定逻辑」是代码里固定的一组谓词（check 字段的取值即谓词名）。
//     没有做成通用表达式语言——家谱校验要的是可读、可测试、可回溯，
//     一个小型 DSL 只会让规则文件变成没人敢改的第二套程序。
//   · 输出 ValidationIssue[]，字段与 OpenAPI 的 ValidationIssue 一一对应，
//     服务层可直接搬进 problem+json /issues 接口。

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/model/event.hpp>

namespace genealogy {

enum class IssueSeverity { Error, Warning, Info };

inline const char* to_string(IssueSeverity s) {
  switch (s) {
    case IssueSeverity::Error: return "error";
    case IssueSeverity::Warning: return "warning";
    case IssueSeverity::Info: return "info";
  }
  return "info";
}

// 字段与 OpenAPI components/schemas/ValidationIssue 对齐
struct ValidationIssue {
  std::string rule_id;
  IssueSeverity severity = IssueSeverity::Info;
  std::string entity_type;   // person / union / event
  std::string entity_id;
  std::string message;
  std::string suggestion;
};

struct ValidationOptions {
  // 只跑这些规则；空 = 跑全部启用的规则
  std::vector<std::string> only_rule_ids;
};

struct ValidationReport {
  std::string ruleset;
  std::string ruleset_version;
  std::vector<ValidationIssue> issues;
  int error_count = 0;
  int warning_count = 0;
  int info_count = 0;
  int rules_run = 0;    // 实际跑了几条规则
  int rules_total = 0;  // 规则表里一共几条
};

// 跑校验。graph 提供人物与婚姻结合；events 可选（墓葬早于卒年等规则需要）；
// citations 可选（R16 无证据事实 / R17 引用冲突需要；为空时这两条静默跳过，
// 与 events 的既有约定一致）。调用方须保证 graph 指向的数据存活期覆盖本次调用。
Result<ValidationReport> validate(const lineage::PersonGraph& graph,
                                  const std::vector<Event>* events = nullptr,
                                  const std::vector<Citation>* citations = nullptr,
                                  const ValidationOptions& opt = {});

}  // namespace genealogy
