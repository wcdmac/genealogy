#pragma once

// GEDCOM 5.5.1 导入导出（设计文档 3.2 `io` 模块）。
//
// 分三层，各自可独立测试：
//   1. 语法层 parse_gedcom：字节 → 记录树。只管 level/xref/tag/value、
//      CONT/CONC 合并、字符集识别，不认识任何谱意。
//   2. 映射层 import_gedcom：记录树 → 领域模型。认不出的东西一律进
//      MappingLoss，绝不静默丢弃——「损失显式化」是本模块的硬要求。
//   3. 写出层 export_gedcom：领域模型 → GEDCOM 文本。按 255 字节切行，
//      保证自家读取器能无损往返。
//
// 字符集：UTF-8（带/不带 BOM）、UTF-16、CP1252、Latin-1。ANSEL 与
// MacRoman 等明确拒绝并报 6001——宁可让用户先转码，也不把字节猜成乱码。

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/union.hpp>

namespace genealogy::io {

// ---------------------------------------------------------------- 语法层

struct GedcomNode {
  int level = 0;
  std::string xref;   // 不含 @ 的记录号，如 "I1"；空 = 结构节点
  std::string tag;    // 原始标签，如 "NAME" / "_CUSTOM"
  std::string value;  // 已合并 CONT/CONC 后的值
  std::vector<GedcomNode> children;

  const GedcomNode* child(std::string_view tag) const noexcept;
  std::vector<const GedcomNode*> children_all(std::string_view tag) const;
  // 直接子节点的值；无则空串。
  std::string child_value(std::string_view tag) const;
};

struct GedcomDocument {
  std::string encoding;        // 识别出的字符集名，如 "UTF-8"
  std::string source_name;     // HEAD.SOUR.NAME
  std::string source_version;  // HEAD.SOUR.VERS
  std::string gedcom_version;  // HEAD.GEDC.VERS
  std::string form;            // HEAD.GEDC.FORM
  std::vector<GedcomNode> records;  // level 0 节点

  const GedcomNode* header() const noexcept;
};

// 解析 GEDCOM 文本（UTF-8 字节串，自动识别 BOM 与 HEAD.CHAR）。
// 失败统一返回 ErrorCode::IoError(6001)：结构残缺、字符集不支持等。
Result<GedcomDocument> parse_gedcom(std::string_view bytes);

// ---------------------------------------------------------------- 映射层

enum class LossSeverity { Warning, Info };

// 映射损失：一条 = 一处「原文里有、我们模型里没有」的信息。
// severity = Warning 表示可能影响谱意（如 OBJE 附件、SOUR 证据链）；
// Info 表示只是没地方放（如 _UID、ASSO）。
struct MappingLoss {
  LossSeverity severity = LossSeverity::Info;
  std::string xref;    // 相关记录，如 "@I1@"；无则空
  std::string tag;     // 触发损失的标签
  std::string reason;  // 人话说明（面向谱师，不面向程序员）
};

struct ImportOptions {
  std::string lineage_id;             // 导入落到哪个宗族（必填）
  std::string default_surname;        // NAME 无 /姓/ 结构时的兜底姓（可空）
  bool create_places = true;          // 按名称自动建地点
};

struct ImportResult {
  std::vector<Person> persons;
  std::vector<Union> unions;
  std::vector<Event> events;
  std::vector<Place> places;
  std::vector<MappingLoss> losses;
  // GEDCOM xref（"@I1@"）→ 新分配的 id，供调用方追溯与报告
  std::unordered_map<std::string, std::string> id_map;
};

Result<ImportResult> import_gedcom(const GedcomDocument& doc,
                                  const ImportOptions& opt);

// 建档顺序：父母必须先于子女——用例层会校验父母存在性，而 GEDCOM 文件里
// 子记录完全可以排在父记录前面。多轮「消化」做拓扑排序；一轮消化不掉
// 说明有环或自指，此时按 id 硬灌，让用例层把具体的错报出来。
std::vector<Person> order_by_dependency(const std::vector<Person>& persons);

// ---------------------------------------------------------------- 写出层

struct ExportOptions {
  const std::vector<Person>* persons = nullptr;
  const std::vector<Union>* unions = nullptr;
  const std::vector<Event>* events = nullptr;
  const std::vector<Place>* places = nullptr;  // place_id → 名称 解析用
  std::string submitter_name = "genealogy";
  std::string source_version = "1.0";
  // 导出侧损失台账（镜像导入侧的 add_loss）：凡「库里有、GEDCOM
  // 表达不了/不在本次导出集内」的信息逐条记账——导出与导入同样
  // 不许静默丢（高阳李氏试修实证：成员缺集、亲子链断裂都曾无声）。
  // 传 nullptr = 不收集（CLI/REST 应始终传入）。
  std::vector<MappingLoss>* losses = nullptr;
};

std::string export_gedcom(const ExportOptions& opt);

}  // namespace genealogy::io
