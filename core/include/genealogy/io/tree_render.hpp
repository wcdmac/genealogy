#pragma once

// 世系图文本渲染（设计文档 3.2：io 模块含「世系图导出 DOT/Mermaid/SVG」）。
//
// 为什么在 core 而不在各端手拼：server 的 /exports/tree 与 CLI 的
// io export tree 必须吐出同一份文本——在两端各写一份字符串拼接，
// 迟早漂移（而且这种漂移不报错，只是「导出结果偶尔不一样」）。
// 纯函数、无副作用，可单测快照化。

#include <string>

#include <genealogy/lineage/tree.hpp>

namespace genealogy::io {

enum class TreeFormat {
  Dot,
  Mermaid,
};

// 把后代树渲染成 DOT / Mermaid 文本。
//
// 节点标识一律用人物 ID，标签才是姓名——用姓名当 key 是错的：
// 同名同姓在同族里太常见（行辈 + 派语字决定了重名率），那样会把
// 不同的人并成一个点，且 DOT/Mermaid 语法都会坏。
//
// SVG 不在这里渲染：需要 graphviz 管线，属于展示层的事；
// 调用方拿到 DOT 自行接 graphviz（server 明确对 svvg 返回 501）。
std::string render_tree(const lineage::TreeNode& tree, TreeFormat format);

}  // namespace genealogy::io
