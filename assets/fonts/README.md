# 随包字体（全库自带）

本目录随软件分发三款**完整**思源（Source Han）CJK 字体，不做子集化——
子集化会导致生僻字缺字（tofu），与家谱软件"老谱原文照录"的需求冲突。

| 文件 | 用途 | 覆盖 |
|---|---|---|
| `SourceHanSansSC-Regular.otf` | GUI 界面 / 正文 | 44,853 字形：基本区汉字 20,992 + 扩展区约 2,594（含 Ext B 约 2,109） |
| `SourceHanSansSC-Bold.otf` | GUI 标题 / 强调 | 同上 |
| `SourceHanSerifSC-Regular.otf` | 印刷出版 / 谱书正文 | 44,779 字形：基本区汉字 20,992 + 扩展区约 2,531 |

## 许可

三款字体均为 **SIL Open Font License 1.1**，全文见：
- `OFL-SourceHanSans.txt`
- `OFL-SourceHanSerif.txt`

OFL 1.1 允许闭源商用、随软件捆绑分发、嵌入 PDF、子集化；本的软件部分
不因使用这些字体而需要开源。约束（已遵守）：
1. 字体不单独售卖，仅随软件分发；
2. 每份字体随附版权声明与 OFL 全文（本目录两份 txt）；
3. 字体文件**未经修改**（直接从 Adobe 官方 release 提取），故保留原字体名
   "Source Han Sans/Serif SC"；RFN 仅约束修改版，原版可正常使用该名称。

下载来源：`github.com/adobe-fonts/source-han-sans` release 2.005R、
`github.com/adobe-fonts/source-han-serif` release 2.003R（SC 地区子集 OTF）。

## 覆盖边界（诚实说明）

单款思源 SC 覆盖约 4.5 万字形，不是 Unicode 16.0 的全部 98,682 个汉字——
Ext C 以后（约 1.6 万字）与部分 ext B 生僻字不在其中。运行时对缺失字形
走平台回退链（Windows DirectWrite / macOS CoreText / Linux fontconfig），
并保留缺字提示；出版 HTML 内嵌的字体子集即本目录完整字体，遇极生僻字时
由系统字体兜底。这是当前开源宽松许可字体的实际天花板。
