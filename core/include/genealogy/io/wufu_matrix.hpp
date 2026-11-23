#pragma once

// 九族五服严格矩阵：纵轴九族（上四世～下四世），横轴五服（斩衰～缌麻）。
// 服等逐对由 lineage::compute_mourning_degree 按外置规则表计算，
// 世代差由关系路径 generation_gap() 推出（不用世代序号差）。

#include <genealogy/error.hpp>
#include <genealogy/io/chart_svg.hpp>

namespace genealogy::io {

Result<ChartResult> render_wufu_matrix(const ChartOptions& opt);

}  // namespace genealogy::io
