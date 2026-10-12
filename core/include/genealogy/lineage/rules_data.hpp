#pragma once

// 规则表数据符号声明：由构建期嵌入（cmake/EmbedText.cmake 读取
// shared/rules/*.json），运行时零文件依赖。符号定义在生成产物
// build/core/generated/rules_*.cpp 中，请勿手改。

namespace genealogy::lineage::rules_data {

extern const char* kMourningMingQingJson;
extern const char* kAppellationJson;

}  // namespace genealogy::lineage::rules_data
