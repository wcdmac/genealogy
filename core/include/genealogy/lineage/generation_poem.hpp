#pragma once

// 派语/字辈引擎（设计文档 3.2「GenerationPoem 规则引擎」、ADR-6 规则外置）。
//
// 派语（字辈/排行/昭穆）：宋代已出现的联句形式，同姓不同支派语不同，
// 是区分支派最重要的依据。本引擎做三件事：
//   1. 联句 → 辈字序列（按字拆分，忽略标点数字）；
//   2. 名字 → 辈字定位（这个人的辈字在派语第几句）；
//   3. 辈字 ↔ 世代序号交叉校验（两套口径互查，冲突即报）。
//
// 全部是纯函数：输入（派语序列、姓名、世代表）→ 输出，无 IO、无状态，
// 可快照测试。数据（派语联句）在库里按宗族/支派配置，不在代码里——
// 这正是 ADR-6 说的「规则外置为数据」。
//
// 命名惯例的重要提醒：双名取首字为辈字、单名取本字为辈字是最常见形态，
// 但一支一俗并非铁律（有支派辈字在双名次字；有支派单名不冠辈字）。
// 因此 locate 返回**全部**命中，由谱师定夺；expected_seq 只是按最常见
// 惯例给的参考值，界面必须标明「惯例推断」。

#include <optional>
#include <string>
#include <unordered_set>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/lineage/graph.hpp>

namespace genealogy::lineage {

// 联句 → 辈字序列。只取 CJK 表意文字，标点/空白/数字/外文一概忽略。
// 例：「德宗景文正，邦家万世兴」→ [德,宗,景,文,正,邦,家,万,世,兴]
std::vector<std::string> parse_poem(std::string_view text);

// 名字里的辈字命中：位置（第几个字，0 起）+ 辈序（派语第几句，1 起）。
// 同一个字在派语里出现多次时全部列出（真实存在，谱师自己判断）。
struct NameHit {
  std::size_t position = 0;   // 在名字中的字第（0 起）
  int seq = 0;                // 派语辈序（1 起）
};

std::vector<NameHit> locate_in_name(const std::vector<std::string>& poem,
                                    const std::string& name);

// 惯例推断的辈序：双名取首字命中，单名取本字命中；都没有返回 nullopt。
// 仅作参考——界面必须标明这是惯例推断，不是事实。
std::optional<int> expected_seq(const std::vector<std::string>& poem,
                                const std::string& given_name);

// 辈字 ↔ 世代序号交叉校验的一条结果。
struct PoemConflict {
  std::string person_id;
  std::string person_name;      // 姓 + 名（报告可读性）
  std::optional<int> name_seq;  // 名字推断的辈序
  std::optional<int> generation_no;  // 自始祖起算的世代序号
  std::optional<int> expected_no;    // 按辈序期望的世代序号
  std::string detail;           // 人类可读的差异说明
};

// 对全图做交叉校验。
//   generations: compute_generations() 的输出（id → 世代序号）
//   poem:        parse_poem() 的输出
//   poem_start:  派语第一句对应的世代序号（默认 1 = 始祖辈。
//                不少支派从第 N 世才开始定派语，那时传 N）
// 只报「两边都有值且不一致」的冲突；单边缺失（名字没辈字 / 世代断链）
// 不是冲突——那可能是录入未全，也可能是外亲来归，不能算错。
// member_filter：支级校验用——图覆盖全宗族（世代序号自真始祖起算），
// 但检查只限支内成员；lineage 级传 nullptr 查全部。
std::vector<PoemConflict> check_poem_consistency(
    const PersonGraph& g,
    const std::unordered_map<std::string, int>& generations,
    const std::vector<std::string>& poem, int poem_start = 1,
    const std::unordered_set<std::string>* member_filter = nullptr);

}  // namespace genealogy::lineage
