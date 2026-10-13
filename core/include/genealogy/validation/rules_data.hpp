#pragma once

// 校验规则表数据符号声明：由构建期嵌入（cmake/EmbedText.cmake 读取
// shared/rules/validation.json），运行时零文件依赖。
// 符号定义在生成产物 build/core/generated/rules_validation.cpp 中。

namespace genealogy::validation::rules_data {

extern const char* kValidationJson;

}  // namespace genealogy::validation::rules_data
