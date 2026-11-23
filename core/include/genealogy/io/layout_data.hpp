#pragma once

// 版式预设数据（由 cmake/EmbedText.cmake 从 shared/layout/*.json 嵌入）。
// 参数化的是公版古籍形制（版心/鱼尾/象鼻/栏线/书耳等），
// 不来自任何商业软件的私有表达。

namespace genealogy::io::layout_data {

extern const char* kGujiClassicJson;
extern const char* kPlainModernJson;

}  // namespace genealogy::io::layout_data
