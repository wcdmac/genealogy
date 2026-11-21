#pragma once

// note/biography 页码出处 → Source/Citation 的迁移用例
// （7.6 第三道验收门「证据链覆盖关键事实」的执行体）。
//
// 背景：转录工作台在 citation 域建成前把出处写进自由文本（note 开头
// 的「谱pNN」，TranscribeView 的 noteBits 组装），真实评测库累计
// 385 条 note + 2 条 biography。本用例把它们迁入 M7.2 的
// Source/Citation 域——既是数据迁移，也是该域第一次被真实数据
// 驱动验证。
//
// 策略（2026-11-21 定稿）：
//   · 出处 token = 「谱pNN[-NN]」（必须带 谱 前缀）。裸 pNN（「据
//     p100」「p55 待终核」）语义歧义——可能是另一份文献的页码或
//     待办标记，不迁移，留在原文里；
//   · 来源按 lineage 自动派生（一谱一 Source，type=genealogy_book，
//     title = lineage 名去括号 + 「家谱」，year_text = 括号内原文）；
//     显式 bindings 可覆盖（同一 lineage_id 只派生一次）；
//   · confidence：note/bio 含「存疑」或「待核」→ low，否则 high
//     （录入纪律：页码出自原卷直读）；
//   · 幂等：与库内已有 citation（subject+source+locator 三键）去重，
//     重复执行收敛为 0 新增——转录工作台仍在往 note 写新出处，
//     本命令可对同一库反复执行；
//   · note 只剥离「开头连续的出处 token + 紧随分隔符」，正文里的
//     引用（墓志铭见谱p55-60 之类）原样保留；biography 一律不剥离；
//   · 剥离后为空的 note 清成 NULL（person.note 无空串口径）；
//   · 主体来源不明的 lineage：跳过并记 warning，不猜。

#include <genealogy/error.hpp>
#include <genealogy/repository/database.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace genealogy {

struct ProvenanceSourceBinding {
  std::string lineage_id;                 // 该谱人物所属 lineage
  std::string title;                      // Source::title（谱名）
  std::string year_text;                  // 年份照录（清嘉庆四年钞本）
  std::optional<std::string> repository;  // 收藏机构；未知不填
  std::optional<std::string> author;      // 修谱者；未知不填
};

struct ProvenanceMigrationReport {
  int sources_created = 0;
  int sources_reused = 0;
  int citations_created = 0;
  int citations_deduped = 0;  // 库内已有而跳过
  int persons_scanned = 0;
  int persons_cited = 0;      // 本次至少新增一条 citation 的人物
  int notes_stripped = 0;
  int low_confidence = 0;
  std::vector<std::string> warnings;
};

// dry_run = true 时只扫描与计数，不写库（报告字段语义相同）。
Result<ProvenanceMigrationReport> migrate_note_provenance(
    Database& db, const std::vector<ProvenanceSourceBinding>& bindings,
    bool strip_note_tokens = true, bool dry_run = false);

nlohmann::json to_json(const ProvenanceMigrationReport& r);

}  // namespace genealogy
