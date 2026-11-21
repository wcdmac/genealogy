#!/usr/bin/env python3
"""依赖许可证白名单 CI 卡点（设计文档 2.1 / 6.2）。

规则：
1. vcpkg.json 的全部依赖（含 features）必须在 shared/licenses/allowlist.txt；
2. 全仓文本文件不得出现 Qt 引用（Qt 的 CMake 包查询、Qt5/Qt6 命名空间目标、
   Qt 头文件包含）。匹配模式定义见本文件 QT_PATTERN。

退出码：0 通过；1 存在违规。
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOWLIST = ROOT / "shared" / "licenses" / "allowlist.txt"
MANIFEST = ROOT / "vcpkg.json"

# 「vcpkg」必须跳过：CI 在工作区里同时检出 pinned vcpkg 工具链克隆
# （ci.yml「检出 vcpkg」步骤），ports 树内有海量 Qt 引用，不是项目内容；
# 本地开发机 vcpkg 在仓库外（D:\vcpkg），所以本机跑不出来这个问题。
# 「.data」是 gitignore 的本地资料工作区（下载的原始转换表等）；
# 「dist」是前端构建产物（vite 打包的压缩 JS）——都是第三方/生成内容，
# 不是本仓项目源码。目录豁免一律精确目录名匹配（见扫描循环处）。
SKIP_DIR_PARTS = {
    ".git", "vcpkg", "vcpkg_installed", "out", "__pycache__", "node_modules",
    "build", ".data", "dist",
}
TEXT_SUFFIXES = {
    ".json", ".cmake", ".txt", ".cpp", ".hpp", ".cxx", ".cc", ".h",
    ".py", ".yml", ".yaml", ".md", ".sql",
    # 前端源码与平台脚本（复核 2.2：此前缺失，clients/web 与脚本目录
    # 完全不扫，许可与「禁 Qt」卡点对半个仓库假绿）
    ".ts", ".tsx", ".js", ".mjs", ".sh", ".ps1", ".bat",
}
QT_PATTERN = re.compile(r"Qt[56]?::|find_package\s*\(\s*Qt|#include\s*<Q[tT]")


def load_allowlist() -> set[str]:
    names: set[str] = set()
    for line in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            names.add(line)
    return names


def manifest_deps() -> set[str]:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    deps: set[str] = set()
    for dep in manifest.get("dependencies", []):
        deps.add(dep if isinstance(dep, str) else dep["name"])
    for feature in manifest.get("features", {}).values():
        for dep in feature.get("dependencies", []):
            deps.add(dep if isinstance(dep, str) else dep["name"])
    return deps


def main() -> int:
    violations: list[str] = []

    allowed = load_allowlist()
    for dep in sorted(manifest_deps()):
        if dep not in allowed:
            violations.append(f"依赖不在白名单: {dep}")

    scan_counts: dict[str, int] = {}
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or path.suffix not in TEXT_SUFFIXES:
            continue
        if path.resolve() == Path(__file__).resolve():
            continue  # 豁免自身（QT_PATTERN 的定义文本）
        rel = path.relative_to(ROOT)
        # 只对目录段做精确名豁免（复核 2.2：此前 part.startswith("build")
        # 连文件名也匹配——build-notes.md 这类文件被静默跳过，且任何以
        # build 开头的目录整树豁免，为「禁 Qt」扫描留了隐形口子）。
        if any(part in SKIP_DIR_PARTS for part in rel.parts[:-1]):
            continue
        scan_counts[path.suffix] = scan_counts.get(path.suffix, 0) + 1
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            if QT_PATTERN.search(line):
                violations.append(
                    f"{rel}:{lineno} 出现 Qt 引用: {line.strip()[:80]}"
                )

    if violations:
        print("许可证白名单检查失败：")
        for item in violations:
            print("  -", item)
        return 1

    print(f"许可证白名单检查通过（{len(allowed)} 项登记依赖，全仓无 Qt 引用）")
    # 扫描覆盖行：CI 断言（ci.yml 许可证步骤）以此验证前端后缀真的进了
    # 扫描集——「tsx=非零」证明 clients/web 的 .tsx 文件被扫描过，
    # TEXT_SUFFIXES 再被删改时卡点当天红。
    covered = ", ".join(f"{k.lstrip('.')}={scan_counts[k]}"
                        for k in sorted(scan_counts))
    print(f"扫描覆盖: {covered}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
