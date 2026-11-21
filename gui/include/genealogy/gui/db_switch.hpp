#pragma once

// 换库/关库的统一清理序列（复核 2.13 + 第三轮新-C5）。
//
// 为什么抽成自由函数：open() 会先 close() 旧库——新库打开失败旧库
// 也已不在，因此「打开」与「关闭」两个入口必须做同一套面板/选中态
// 清理，散在 app.cpp 两处就会再漂移。抽到这里后 gui-tests 直接驱动
// 断言：任意填充的面板缓存与全局选中态，一次调用全部清空。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <vector>

namespace genealogy::gui {

// 清全局选中态（人物卡/关系端点）+ 逐面板 on_database_closed。
void reset_for_database_switch(AppState& state,
                               std::vector<Panel*>& panels);

}  // namespace genealogy::gui
