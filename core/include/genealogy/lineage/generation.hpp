#pragma once

#include <string>
#include <unordered_map>

#include <genealogy/error.hpp>
#include <genealogy/lineage/graph.hpp>

namespace genealogy::lineage {

// 世代计算（大宗谱法：自始祖起算，始祖 = 1）。
// 多父母世代不一致时取 max(父, 母) + 1；无法从始祖到达者
// （关系环、父母双缺且非始祖的孤儿）不入表。
std::unordered_map<std::string, int> compute_generations(const PersonGraph& g);

// 单查。NotFound = 人物不在图中，或无法从始祖到达（疑关系环/断代）。
Result<int> generation_of(const PersonGraph& g, const std::string& id);

// 世代「序号」之差（alter 相对 ego，正 = 晚辈、负 = 尊长）。
//
// 注意与 Relationship::generation_gap() 的区别：本函数比的是二人各自
// 自始祖起算的世代序号，属「字辈/世代编号」口径；只有双方都能上溯到同一
// 始祖时才有意义。外亲与姻亲常因父母未录而断链，序号会塌到始祖辈，
// 此时应改用关系路径推导的辈分差（Relationship::generation_gap()）。
Result<int> generation_no_gap(const PersonGraph& g, const std::string& ego,
                              const std::string& alter);

}  // namespace genealogy::lineage
